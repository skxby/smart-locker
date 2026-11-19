#define _GNU_SOURCE
#include "locker_contracts.h"
#include "locker/common/clock.h"
#include "locker/common/uuid.h"
#include <arpa/inet.h>
#include "civetweb.h"
#include "domain_json.h"
#include "ws.h"
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sodium.h>
#include <openssl/ssl.h>

struct locker_http {
    struct mg_context *context;
    locker_ws_t *ws;
    pthread_t stop_thread;
    bool stop_started, stop_joined;
    locker_core_t *core;
    locker_http_config_t config;
    pthread_mutex_t limits_mutex;
    struct { char address[48]; uint64_t expires; unsigned attempts; } limits[64];
};

static int failure(struct mg_connection *c, int status, const char *code);

static int reply(struct mg_connection *c, int status, const char *body)
{
    size_t n = body ? strlen(body) : 0;
    char location[96] = "";
    if (status == 201) {
        locker_json_node_t nodes[128]; locker_json_member_t members[128];
        locker_json_document_t d = {0}; d.nodes = nodes; d.node_capacity = 128;
        d.members = members; d.member_capacity = 128;
        char id[37]; locker_uuid_t uuid;
        if (!body || !locker_domain_json_parse(&d, body, n)) return failure(c,503,"DEVICE_UNAVAILABLE");
        const locker_json_node_t *data = locker_domain_json_get(&d, locker_domain_json_root(&d), "data");
        if (!locker_domain_json_text(&d, locker_domain_json_get(&d, data, "id"), id, sizeof(id), NULL) ||
            locker_uuid_parse(id, strlen(id), &uuid) != LOCKER_STATUS_OK) return failure(c,503,"DEVICE_UNAVAILABLE");
        snprintf(location, sizeof(location), "Location: /api/v1/%s/%s\r\n", strstr(mg_get_request_info(c)->local_uri, "/users") ? "users" : "operations", id);
    }
    if (status==429) strcpy(location,"Retry-After: 1\r\n");
    mg_printf(c, "HTTP/1.1 %d Response\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\n%sContent-Length: %zu\r\n\r\n", status, location, n);
    if (n) mg_write(c, body, n);
    return status;
}

static int failure(struct mg_connection *c, int status, const char *code)
{
    char body[512], id[37];
    locker_uuid_t request_id={0};
    const char *correlation = mg_get_header(c, "X-Request-ID");
    if (!correlation || locker_uuid_parse(correlation, strlen(correlation), &request_id) != LOCKER_STATUS_OK)
        (void)locker_clock_real_new_uuid(&request_id);
    if (locker_uuid_format(&request_id, id, sizeof(id)) != LOCKER_STATUS_OK)
        return reply(c, 503, NULL);
    snprintf(body, sizeof(body), "{\"error\":{\"code\":\"%s\",\"message\":\"Request rejected\",\"retryable\":%s,\"operation_id\":null},\"request_id\":\"%s\"}", code, (!strcmp(code,"RATE_LIMITED") || !strcmp(code,"DEVICE_UNAVAILABLE") || !strcmp(code,"QUEUE_FULL") || !strcmp(code,"RESULT_PENDING")) ? "true" : "false", id);
    return reply(c, status, body);
}

/* Bounded per-source work admission; forwarding headers never confer identity. */
static bool admit(locker_http_t *h, const char *address)
{
    uint64_t now = locker_clock_real_monotonic_ms();
    size_t slot = 64, free_slot = 64;
    pthread_mutex_lock(&h->limits_mutex);
    for (size_t i = 0; i < 64; ++i) {
        if (h->limits[i].expires <= now) free_slot = i;
        else if (!strcmp(h->limits[i].address, address)) { slot = i; break; }
    }
    if (slot == 64 && free_slot != 64) {
        slot = free_slot;
        snprintf(h->limits[slot].address, sizeof(h->limits[slot].address), "%s", address);
        h->limits[slot].expires = now + 60000;
        h->limits[slot].attempts = 0;
    }
    bool allowed = slot < 64 && h->limits[slot].attempts < 5;
    if (allowed) ++h->limits[slot].attempts;
    pthread_mutex_unlock(&h->limits_mutex);
    if (getenv("LOCKER_DEVICE_TRACE") != NULL) {
        fprintf(stderr, "admit addr=%s slot=%zu attempts=%u now=%llu expires=%llu allowed=%d\n",
                address ? address : "(null)", slot,
                slot < 64 ? h->limits[slot].attempts : 0u,
                (unsigned long long)now,
                slot < 64 ? (unsigned long long)h->limits[slot].expires : 0ull,
                allowed ? 1 : 0);
        fflush(stderr);
    }
    return allowed;
}

static bool history_query(const char *query, bool events, uint16_t *limit, char *cursor, locker_uuid_t *operation)
{
    if (!query || !*query) return true;
    if (strlen(query)>2048) return false;
    char copy[2049]; strcpy(copy,query); char *save=NULL; unsigned seen=0;
    for (char *part=strtok_r(copy,"&",&save); part; part=strtok_r(NULL,"&",&save)) {
        char *equal=strchr(part,'='); if (!equal) return false; *equal++=0;
        unsigned bit=!strcmp(part,"limit") ? 1u : !strcmp(part,"cursor") ? 2u : events && !strcmp(part,"operation_id") ? 4u : 0u;
        if (!bit || (seen&bit)) return false;
        seen|=bit;
        char value[513]; int n=mg_url_decode(equal,(int)strlen(equal),value,sizeof(value),1);
        if (n<=0 || (size_t)n!=strlen(value)) return false;
        if (bit==1) {
            unsigned v=0; if (n>3 || (n>1 && value[0]=='0')) return false;
            for (int i=0;i<n;++i) { if (value[i]<'0' || value[i]>'9') return false; v=v*10+(unsigned)(value[i]-'0'); }
            if (!v || v>100) return false;
            *limit=(uint16_t)v;
        } else if (bit==2) strcpy(cursor,value);
        else if (locker_uuid_parse(value,(size_t)n,operation)!=LOCKER_STATUS_OK) return false;
    }
    return true;
}

static int request(struct mg_connection *c)
{
    const struct mg_request_info *r = mg_get_request_info(c);
    locker_http_t *h = mg_get_user_data(mg_get_context(c));
    const char *path = r->local_uri;
    if (!path || strncmp(path, "/api/v1/", 8)) return failure(c, 404, "NOT_FOUND");
    path += 7;
    if (!strcmp(path, "/ws") && !strcmp(r->request_method, "GET")) return 0;
    bool login = !strcmp(path, "/sessions") && !strcmp(r->request_method, "POST");
    bool logout = !strcmp(path, "/sessions/current") && !strcmp(r->request_method, "DELETE");
    const char *query_kind = NULL;
    const char *resource = NULL;
    const char *command_kind = NULL;
    char operation_id[37] = {0};
    if (!strcmp(r->request_method, "POST")) {
        if (!strcmp(path, "/operations")) command_kind = LOCKER_CORE_OPERATION_CREATE;
        else if (!strncmp(path, "/operations/", 12) && strlen(path) >= 36 + 12) {
            const char *suffix = path + 12 + 36;
            if (!strcmp(suffix, "/confirm")) command_kind = LOCKER_CORE_OPERATION_CONFIRM;
            else if (!strcmp(suffix, "/cancel")) command_kind = LOCKER_CORE_OPERATION_CANCEL;
            else if (!strcmp(suffix, "/resolve")) command_kind = LOCKER_CORE_OPERATION_RESOLVE;
            if (command_kind) { memcpy(operation_id, path + 12, 36); resource = operation_id; }
        }
    }
    if (!strcmp(r->request_method, "POST") && !strcmp(path, "/users")) command_kind = LOCKER_CORE_USER_CREATE;
    if (!strcmp(r->request_method, "PATCH") && !strncmp(path, "/users/", 7)) {
        command_kind = LOCKER_CORE_USER_UPDATE; resource = path + 7;
    }
    bool unbind = false;
    if ((!strcmp(r->request_method,"PUT") || !strcmp(r->request_method,"DELETE")) &&
        !strncmp(path,"/users/",7) && strlen(path)==7+36+5 && !strcmp(path+7+36,"/card")) {
        memcpy(operation_id,path+7,36); resource=operation_id;
        unbind=!strcmp(r->request_method,"DELETE");
        command_kind=unbind ? LOCKER_CORE_CARD_UNBIND : LOCKER_CORE_CARD_BIND;
    }
    bool user_command = command_kind && (!strcmp(command_kind, LOCKER_CORE_USER_CREATE) || !strcmp(command_kind, LOCKER_CORE_USER_UPDATE) || !strcmp(command_kind, LOCKER_CORE_CARD_BIND) || !strcmp(command_kind, LOCKER_CORE_CARD_UNBIND));
    bool body_required = login || (command_kind && !unbind);
    if (!strcmp(r->request_method, "GET")) {
        if (!strcmp(path, "/sessions/current")) query_kind = LOCKER_CORE_SESSION_CURRENT;
        else if (!strcmp(path, "/device")) query_kind = LOCKER_CORE_DEVICE_GET;
        else if (!strcmp(path, "/snapshot")) query_kind = LOCKER_CORE_SNAPSHOT_GET;
        else if (!strcmp(path, "/card-observations")) query_kind = LOCKER_CORE_CARD_OBSERVATIONS_LIST;
        else if (!strcmp(path,"/loans")) query_kind=LOCKER_CORE_LOANS_LIST;
        else if (!strcmp(path,"/events")) query_kind=LOCKER_CORE_EVENTS_LIST;
        else if (!strcmp(path, "/reviews")) query_kind = LOCKER_CORE_REVIEWS_LIST;
        else if (!strcmp(path, "/users")) query_kind = LOCKER_CORE_USERS_LIST;
        else if (!strcmp(path, "/tools")) query_kind = LOCKER_CORE_TOOLS_LIST;
        else if (!strncmp(path, "/tools/", 7)) {
            query_kind = LOCKER_CORE_TOOL_GET; resource = path + 7;
        } else if (!strncmp(path, "/operations/", 12)) {
            query_kind = LOCKER_CORE_OPERATION_GET; resource = path + 12;
        }
    }
    if (!login && !logout && !query_kind && !command_kind) return failure(c, 404, "NOT_FOUND");
        uint16_t page_limit=20; char page_cursor[513]=""; locker_uuid_t filter_operation={0};
    bool history=query_kind && (!strcmp(query_kind,LOCKER_CORE_LOANS_LIST) || !strcmp(query_kind,LOCKER_CORE_EVENTS_LIST));
    const char *unbind_revision = NULL;
    if (history) {
        if (!history_query(r->query_string,!strcmp(query_kind,LOCKER_CORE_EVENTS_LIST),&page_limit,page_cursor,&filter_operation)) return failure(c,400,"VALIDATION_ERROR");
    } else if (r->query_string && *r->query_string) {
        if (!unbind || strncmp(r->query_string,"user_revision=",14)) return failure(c,400,"VALIDATION_ERROR");
        unbind_revision=r->query_string+14;
        size_t n=strlen(unbind_revision);
        if (!n || n>20) return failure(c,400,"VALIDATION_ERROR");
        for (size_t i=0;i<n;++i) if (unbind_revision[i]<'0' || unbind_revision[i]>'9') return failure(c,400,"VALIDATION_ERROR");
    }
    if (unbind && !unbind_revision) return failure(c,400,"VALIDATION_ERROR");
    if (mg_get_header(c, "Transfer-Encoding") || r->content_length > 4096 ||
        (body_required && r->content_length < 0)) return failure(c, 400, "VALIDATION_ERROR");
    if (!body_required && r->content_length > 0) return failure(c, 400, "VALIDATION_ERROR");
    locker_uuid_t resource_id = {0}, request_id;
    const char *correlation = mg_get_header(c, "X-Request-ID");
    if (correlation) {
        if (locker_uuid_parse(correlation, strlen(correlation), &request_id) != LOCKER_STATUS_OK)
            return failure(c, 400, "VALIDATION_ERROR");
    } else if (locker_clock_real_new_uuid(&request_id) != LOCKER_STATUS_OK) {
        return failure(c, 503, "DEVICE_UNAVAILABLE");
    }
    if (resource && locker_uuid_parse(resource, strlen(resource), &resource_id) != LOCKER_STATUS_OK)
        return failure(c, 400, "VALIDATION_ERROR");
    locker_principal_t principal = {0};
    if (!login) {
        const char *bearer = mg_get_header(c, "Authorization");
        locker_auth_request_t auth = {0};
        if (!bearer || strncmp(bearer, "Bearer ", 7) || strlen(bearer + 7) != 64)
            return failure(c, 401, "UNAUTHENTICATED");
        memcpy(auth.token, bearer + 7, 64); auth.token_length = 64;
        locker_status_t authenticated = locker_core_authenticate(h->core, &auth, &principal);
        sodium_memzero(&auth, sizeof(auth));
        if (authenticated != LOCKER_STATUS_OK) {
            if (logout && authenticated == LOCKER_STATUS_INVALID) return reply(c, 204, NULL);
            return failure(c, 401, "UNAUTHENTICATED");
        }
    }
    if ((user_command || (command_kind && !strcmp(command_kind,LOCKER_CORE_OPERATION_RESOLVE)) || (query_kind && (!strcmp(query_kind, LOCKER_CORE_USERS_LIST) || !strcmp(query_kind,LOCKER_CORE_CARD_OBSERVATIONS_LIST) || !strcmp(query_kind,LOCKER_CORE_REVIEWS_LIST)))) && principal.role != LOCKER_ROLE_ADMIN)
        return failure(c, 403, "FORBIDDEN");
    locker_query_result_t *result = calloc(1, sizeof(*result));
    if (!result) return failure(c, 503, "QUEUE_FULL");
    locker_status_t status;
    if (command_kind && resource && !user_command) {
        locker_query_t visible = {0}; visible.principal = principal;
        visible.request_id = request_id; visible.resource_id = resource_id;
        strcpy(visible.kind, LOCKER_CORE_OPERATION_GET);
        status = locker_core_query(h->core, &visible, result);
        if (status == LOCKER_STATUS_OK) status = result->status;
        if (status != LOCKER_STATUS_OK) {
            free(result);
            return failure(c, status == LOCKER_STATUS_INVALID ? 404 : 503,
                status == LOCKER_STATUS_INVALID ? "NOT_FOUND" : "DEVICE_UNAVAILABLE");
        }
    }
    if (query_kind) {
        locker_query_t q = {0}; q.principal = principal;
        q.request_id = request_id; q.resource_id = history ? filter_operation : resource_id;
        q.limit=page_limit; strcpy(q.cursor,page_cursor);
        strcpy(q.kind, query_kind);
        status = locker_core_query(h->core, &q, result);
    } else {
        locker_command_t command = {0}; command.principal = principal;
        strcpy(command.kind, command_kind ? command_kind : login ? LOCKER_CORE_SESSION_CREATE : LOCKER_CORE_SESSION_REVOKE);
        command.resource_id = resource_id;
        if (command_kind) {
            const char *key = mg_get_header(c, "Idempotency-Key");
            if (!key || locker_uuid_parse(key, strlen(key), &command.idempotency_key) != LOCKER_STATUS_OK ||
                locker_uuid_is_nil(&command.idempotency_key)) { free(result); return failure(c, 400, "VALIDATION_ERROR"); }
        }
        if (unbind_revision) snprintf(command.arguments_json,sizeof(command.arguments_json),"{\"user_revision\":\"%s\"}",unbind_revision);
        if (body_required) {
            const char *type = mg_get_header(c, "Content-Type");
            if (!type || strcmp(type, "application/json") || !r->content_length) {
                free(result); return failure(c, 400, "VALIDATION_ERROR");
            }
            if (login && !admit(h, r->remote_addr)) { free(result); return failure(c, 429, "RATE_LIMITED"); }
            size_t count = 0;
            while (count < (size_t)r->content_length) {
                int n = mg_read(c, command.arguments_json + count, (size_t)r->content_length - count);
                if (n <= 0) { sodium_memzero(&command, sizeof(command)); free(result); return failure(c, 400, "VALIDATION_ERROR"); }
                count += (size_t)n;
            }
            if (memchr(command.arguments_json, 0, count)) {
                sodium_memzero(&command, sizeof(command)); free(result); return failure(c, 400, "VALIDATION_ERROR");
            }
        }
        if (command_kind && !locker_domain_json_validate_command(command.arguments_json, command_kind)) {
            sodium_memzero(&command, sizeof(command)); free(result); return failure(c, 400, "VALIDATION_ERROR");
        }
        locker_receipt_t receipt;
        command.request_id = request_id;
        status = locker_core_submit(h->core, &command, &receipt);
        sodium_memzero(&command, sizeof(command));
        if (status == LOCKER_STATUS_QUEUED)
            status = locker_core_wait_result(h->core, &receipt.submission_token, 1000, result);
    }
    if (status == LOCKER_STATUS_OK) status = result->status;
    int response;
    if (status == LOCKER_STATUS_OK) response = reply(c, logout ? 204 : command_kind && (!strcmp(command_kind, LOCKER_CORE_OPERATION_CREATE) || !strcmp(command_kind, LOCKER_CORE_USER_CREATE)) ? 201 : 200, logout ? NULL : result->json);
    else if (status == LOCKER_STATUS_INVALID) response = failure(c, login ? 401 : resource ? 404 : 400,
        login ? "UNAUTHENTICATED" : resource ? "NOT_FOUND" : "VALIDATION_ERROR");
    else if (status == LOCKER_STATUS_CONFLICT) response = result->json[0] ? reply(c, 409, result->json) : failure(c, 409, "REVISION_CONFLICT");
    else if (status == LOCKER_STATUS_BUSY) response = failure(c, command_kind ? 409 : 429, command_kind ? "SLOT_BUSY" : "RATE_LIMITED");
    else response = failure(c, 503, "DEVICE_UNAVAILABLE");
    sodium_memzero(result, sizeof(*result)); free(result);
    return response;
}

static int initialize_tls(void *context, void *user)
{
    locker_http_t *h = user;
    SSL_CTX *ssl = context;
    if (SSL_CTX_set_min_proto_version(ssl, TLS1_2_VERSION) != 1 ||
        SSL_CTX_use_certificate_chain_file(ssl, h->config.certificate_path) != 1 ||
        SSL_CTX_use_PrivateKey_file(ssl, h->config.private_key_path, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(ssl) != 1) return -1;
    return 1;
}

locker_status_t locker_http_start(const locker_http_config_t *config, locker_core_t *core,
                                  locker_publisher_t *publisher, locker_http_t **out)
{
    if (out) *out = NULL;
    if (!config || !core || !publisher || !out || !config->port || config->worker_count < 1 || config->worker_count > 4 ||
        !memchr(config->bind_address, 0, sizeof(config->bind_address)) ||
        !memchr(config->certificate_path, 0, sizeof(config->certificate_path)) ||
        !memchr(config->private_key_path, 0, sizeof(config->private_key_path)) ||
        !config->bind_address[0] || !config->certificate_path[0] || !config->private_key_path[0])
        return LOCKER_STATUS_INVALID;
    struct in_addr address;
    if (inet_pton(AF_INET, config->bind_address, &address) != 1) return LOCKER_STATUS_INVALID;
    locker_http_t *h = calloc(1, sizeof(*h));
    if (!h) return LOCKER_STATUS_NO_SPACE;
    if (pthread_mutex_init(&h->limits_mutex, NULL)) { free(h); return LOCKER_STATUS_IO_ERROR; }
    h->core = core; h->config = *config;
    char port[96], workers[8];
    snprintf(port, sizeof(port), "%s:%us", config->bind_address, config->port);
    snprintf(workers, sizeof(workers), "%u", config->worker_count);
    const char *options[] = {"listening_ports", port, "ssl_certificate", config->certificate_path,
        "ssl_protocol_version", "4",
        "num_threads", workers, "request_timeout_ms", "2000", "websocket_timeout_ms", "2000", "enable_keep_alive", "no", NULL};
    struct mg_callbacks callbacks = {0}; callbacks.begin_request = request; callbacks.init_ssl = initialize_tls;
    h->context = mg_start(&callbacks, h, options);
    if (!h->context) { pthread_mutex_destroy(&h->limits_mutex); free(h); return LOCKER_STATUS_IO_ERROR; }
    h->ws = locker_ws_start(h->context, core, publisher, failure);
    if (!h->ws) { mg_stop(h->context); pthread_mutex_destroy(&h->limits_mutex); free(h); return LOCKER_STATUS_IO_ERROR; }
    *out = h; return LOCKER_STATUS_OK;
}

static void *stop_http(void *user)
{
    locker_http_t *http=user;
    locker_ws_stop(http->ws);
    mg_stop(http->context); http->context=NULL;
    return NULL;
}

locker_status_t locker_http_stop(locker_http_t *http, uint32_t timeout_ms)
{
    if (!http) return LOCKER_STATUS_INVALID;
    if (!http->stop_started) {
        if (pthread_create(&http->stop_thread,NULL,stop_http,http)) return LOCKER_STATUS_IO_ERROR;
        http->stop_started=true;
    }
    if (!http->stop_joined) {
        int result;
        if (!timeout_ms) result=pthread_join(http->stop_thread,NULL);
        else {
            struct timespec deadline; clock_gettime(CLOCK_REALTIME,&deadline);
            deadline.tv_sec+=timeout_ms/1000; deadline.tv_nsec+=(long)(timeout_ms%1000)*1000000;
            if (deadline.tv_nsec>=1000000000) { ++deadline.tv_sec; deadline.tv_nsec-=1000000000; }
            result=pthread_timedjoin_np(http->stop_thread,NULL,&deadline);
        }
        if (result) return LOCKER_STATUS_BUSY;
        http->stop_joined=true;
    }
    return LOCKER_STATUS_OK;
}

void locker_http_destroy(locker_http_t *http)
{
    if (!http) return;
    if (locker_http_stop(http,0)!=LOCKER_STATUS_OK) return;
    locker_ws_destroy(http->ws);
    pthread_mutex_destroy(&http->limits_mutex);
    free(http);
}
