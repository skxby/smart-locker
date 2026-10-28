#define _GNU_SOURCE
#include "locker_contracts.h"
#include "domain_json.h"
#include "locker/common/uuid.h"
#include "locker/common/clock.h"
#include <time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <poll.h>
#include <sodium.h>

struct locker_ipc {
    int listener;
    pthread_t thread;
    atomic_bool stopping;
    locker_core_t *core;
    locker_ipc_config_t config;
    ino_t inode;
    bool joined;
    struct { uint64_t since; unsigned attempts; } limits[2];
};
struct binding { const char *method, *kind, *path; bool command, admin, body, idem; int success; };
static const struct binding methods[] = {
    {"login",LOCKER_CORE_SESSION_CREATE,NULL,true,false,true,false,200},
    {"getCurrentSession",LOCKER_CORE_SESSION_CURRENT,NULL,false,false,false,false,200},
    {"logout",LOCKER_CORE_SESSION_REVOKE,NULL,true,false,false,false,204},
    {"getDevice",LOCKER_CORE_DEVICE_GET,NULL,false,false,false,false,200},
    {"getSnapshot",LOCKER_CORE_SNAPSHOT_GET,NULL,false,false,false,false,200},
    {"listTools",LOCKER_CORE_TOOLS_LIST,NULL,false,false,false,false,200},
    {"getTool",LOCKER_CORE_TOOL_GET,"tool_id",false,false,false,false,200},
    {"createOperation",LOCKER_CORE_OPERATION_CREATE,NULL,true,false,true,true,201},
    {"getOperation",LOCKER_CORE_OPERATION_GET,"operation_id",false,false,false,false,200},
    {"confirmOperation",LOCKER_CORE_OPERATION_CONFIRM,"operation_id",true,false,true,true,200},
    {"cancelOperation",LOCKER_CORE_OPERATION_CANCEL,"operation_id",true,false,true,true,200},
    {"presence.submit","LOCAL_PRESENCE",NULL,true,false,true,true,200},
    {"maintenance.reconcile","LOCAL_MAINTENANCE",NULL,true,true,true,true,200},
    {"listLoans",LOCKER_CORE_LOANS_LIST,NULL,false,false,false,false,200},
    {"listEvents",LOCKER_CORE_EVENTS_LIST,NULL,false,false,false,false,200},
    {"listReviews",LOCKER_CORE_REVIEWS_LIST,NULL,false,true,false,false,200},
    {"resolveOperation",LOCKER_CORE_OPERATION_RESOLVE,"operation_id",true,true,true,true,200},
    {"listUsers",LOCKER_CORE_USERS_LIST,NULL,false,true,false,false,200},
    {"createUser",LOCKER_CORE_USER_CREATE,NULL,true,true,true,true,201},
    {"updateUser",LOCKER_CORE_USER_UPDATE,"user_id",true,true,true,true,200},
    {"listCardObservations",LOCKER_CORE_CARD_OBSERVATIONS_LIST,NULL,false,true,false,false,200},
    {"bindCard",LOCKER_CORE_CARD_BIND,"user_id",true,true,true,true,200},
    {"unbindCard",LOCKER_CORE_CARD_UNBIND,"user_id",true,true,false,true,200}
};

static bool string(const locker_json_document_t *d, const locker_json_node_t *o, const char *key, char *out, size_t cap)
{
    return locker_domain_json_text(d, locker_domain_json_get(d,o,key),out,cap,NULL);
}

static bool number(const locker_json_document_t *d,const locker_json_node_t *o,const char *key,uint64_t *value)
{
    char text[21]; if (!string(d,o,key,text,sizeof(text)) || !text[0] || (text[0]=='0' && text[1])) return false;
    *value=0;
    for (size_t i=0;text[i];++i) { if (text[i]<'0' || text[i]>'9' || *value>(UINT64_MAX-(unsigned)(text[i]-'0'))/10) return false; *value=*value*10+(unsigned)(text[i]-'0'); }
    return true;
}

static void send_reply(int fd, const char *id, int status, const char *body, const char *error)
{
    char *packet = malloc(65536);
    if (!packet) return;
    char failure[512];
    if (error) {
        snprintf(failure,sizeof(failure),"{\"error\":{\"code\":\"%s\",\"message\":\"Request rejected\",\"retryable\":%s,\"operation_id\":null},\"request_id\":\"%s\"}",error,(!strcmp(error,"RATE_LIMITED") || !strcmp(error,"DEVICE_UNAVAILABLE") || !strcmp(error,"QUEUE_FULL") || !strcmp(error,"RESULT_PENDING")) ? "true" : "false",id);
        body = failure;
    }
    int n = snprintf(packet,65536,"{\"v\":1,\"request_id\":\"%s\",\"status\":%d,\"body\":%s}",id,status,body ? body : "null");
    if (n >= 65536) {
        free(packet); send_reply(fd,id,503,NULL,"QUEUE_FULL"); return;
    }
    if (n > 0 && !error && body && strcmp(body,"null")) {
        char *last=NULL, *at=packet;
        const char *marker="\"request_id\":\"";
        while ((at=strstr(at,marker))!=NULL) { last=at+strlen(marker); at=last; }
        if (last && strlen(last)>=37 && last[36]=='"') memcpy(last,id,36);
    }
    if (n > 0) (void)send(fd,packet,(size_t)n,MSG_NOSIGNAL);
    sodium_memzero(packet,65536); free(packet);
}

static void handle_packet(locker_ipc_t *ipc,int fd,struct ucred cred,char *packet,ssize_t n)
{
    locker_json_node_t nodes[640]; locker_json_member_t members[640];
    locker_json_document_t d = {0}; d.nodes=nodes; d.node_capacity=640; d.members=members; d.member_capacity=640;
    char id[37], method[64]; locker_uuid_t request_id;
    if (!locker_domain_json_parse(&d,packet,(size_t)n)) return;
    const locker_json_node_t *root = locker_domain_json_root(&d);
    if (!root || root->type != LOCKER_JSON_OBJECT || !string(&d,root,"request_id",id,sizeof(id)) ||
        locker_uuid_parse(id,strlen(id),&request_id) != LOCKER_STATUS_OK) return;
    int64_t version = 0;
    const locker_json_node_t *paths=locker_domain_json_get(&d,root,"path_ids"), *query=locker_domain_json_get(&d,root,"query");
    const locker_json_node_t *params=locker_domain_json_get(&d,root,"params"), *key=locker_domain_json_get(&d,root,"idempotency_key");
    const locker_json_node_t *session=locker_domain_json_get(&d,root,"session_token");
    if (root->count != 8 || !locker_domain_json_int64(&d,locker_domain_json_get(&d,root,"v"),&version) || version != 1 ||
        !string(&d,root,"method",method,sizeof(method)) || !paths || paths->type != LOCKER_JSON_OBJECT ||
        !query || query->type != LOCKER_JSON_OBJECT || !params || !key || !session) {
        send_reply(fd,id,400,NULL,"VALIDATION_ERROR"); return;
    }
    const struct binding *b = NULL;
    for (size_t i=0;i<sizeof(methods)/sizeof(methods[0]);++i) if (!strcmp(method,methods[i].method)) b=&methods[i];
    if (!b) { send_reply(fd,id,404,NULL,"NOT_FOUND"); return; }
    bool unbind=!strcmp(method,"unbindCard");
    bool history=!strcmp(method,"listLoans") || !strcmp(method,"listEvents");
    uint16_t page_limit=20; char cursor[513]=""; locker_uuid_t filter={0};
    if (history) {
        unsigned fields=0;
        const locker_json_node_t *l=locker_domain_json_get(&d,query,"limit");
        if (l) { int64_t v=0; ++fields; if (!locker_domain_json_int64(&d,l,&v) || v<1 || v>100) { send_reply(fd,id,400,NULL,"VALIDATION_ERROR"); return; } page_limit=(uint16_t)v; }
        if (locker_domain_json_get(&d,query,"cursor")) { ++fields; if (!string(&d,query,"cursor",cursor,sizeof(cursor)) || !cursor[0]) { send_reply(fd,id,400,NULL,"VALIDATION_ERROR"); return; } }
        if (!strcmp(method,"listEvents") && locker_domain_json_get(&d,query,"operation_id")) {
            char op[37]; ++fields;
            if (!string(&d,query,"operation_id",op,sizeof(op)) || locker_uuid_parse(op,strlen(op),&filter)!=LOCKER_STATUS_OK) { send_reply(fd,id,400,NULL,"VALIDATION_ERROR"); return; }
        }
        if (fields!=query->count) { send_reply(fd,id,400,NULL,"VALIDATION_ERROR"); return; }
    } else if (query->count != (unbind ? 1u : 0u)) { send_reply(fd,id,400,NULL,"VALIDATION_ERROR"); return; }
    char unbind_revision[21]="";
    if (unbind && !string(&d,query,"user_revision",unbind_revision,sizeof(unbind_revision))) { send_reply(fd,id,400,NULL,"VALIDATION_ERROR"); return; }
    locker_uuid_t resource={0}, idem={0}; char text[65];
    if (paths->count != (b->path ? 1u : 0u) || (b->path && (!string(&d,paths,b->path,text,sizeof(text)) ||
        locker_uuid_parse(text,strlen(text),&resource) != LOCKER_STATUS_OK)) ||
        (b->body ? params->type != LOCKER_JSON_OBJECT : params->type != LOCKER_JSON_NULL) ||
        (b->idem ? (!locker_domain_json_text(&d,key,text,sizeof(text),NULL) ||
        locker_uuid_parse(text,strlen(text),&idem) != LOCKER_STATUS_OK || locker_uuid_is_nil(&idem)) : key->type != LOCKER_JSON_NULL)) {
        send_reply(fd,id,400,NULL,"VALIDATION_ERROR"); return;
    }
    locker_principal_t principal={0}; bool login=!strcmp(method,"login"), logout=!strcmp(method,"logout");
    if (login && session->type != LOCKER_JSON_NULL) { send_reply(fd,id,400,NULL,"VALIDATION_ERROR"); return; }
    if (login) {
        size_t slot=(uint32_t)cred.uid==ipc->config.allowed_admin_uid ? 1u : 0u;
        uint64_t now=locker_clock_real_monotonic_ms();
        if (now-ipc->limits[slot].since>=60000) { ipc->limits[slot].since=now; ipc->limits[slot].attempts=0; }
        if (ipc->limits[slot].attempts>=5) { send_reply(fd,id,429,NULL,"RATE_LIMITED"); return; }
        ++ipc->limits[slot].attempts;
    }
    if (!login) {
        locker_auth_request_t auth={0};
        if (logout && session->type == LOCKER_JSON_NULL) { send_reply(fd,id,204,NULL,NULL); return; }
        if (!locker_domain_json_text(&d,session,auth.token,sizeof(auth.token),&auth.token_length) || auth.token_length != 64) {
            send_reply(fd,id,401,NULL,"UNAUTHENTICATED"); return;
        }
        locker_status_t status=locker_core_authenticate(ipc->core,&auth,&principal); sodium_memzero(&auth,sizeof(auth));
        if (status != LOCKER_STATUS_OK) { send_reply(fd,id,logout && status==LOCKER_STATUS_INVALID ? 204 : status==LOCKER_STATUS_INVALID ? 401 : 503,NULL,logout && status==LOCKER_STATUS_INVALID ? NULL : status==LOCKER_STATUS_INVALID ? "UNAUTHENTICATED" : "DEVICE_UNAVAILABLE"); return; }
        if (b->admin && (principal.role != LOCKER_ROLE_ADMIN || (uint32_t)cred.uid != ipc->config.allowed_admin_uid)) {
            send_reply(fd,id,403,NULL,"FORBIDDEN"); return;
        }
    }
    locker_query_result_t *result=calloc(1,sizeof(*result));
    if (!result) { send_reply(fd,id,503,NULL,"QUEUE_FULL"); return; }
    locker_status_t status;
    if (!strcmp(method,"presence.submit") || !strcmp(method,"maintenance.reconcile")) {
        locker_peer_t peer={0}; peer.uid=(uint32_t)cred.uid; peer.gid=(uint32_t)cred.gid; peer.pid=(uint32_t)cred.pid;
        peer.verified_ui_peer=peer.uid==ipc->config.allowed_ui_uid; peer.verified_admin_peer=peer.uid==ipc->config.allowed_admin_uid;
        locker_receipt_t receipt;
        if (!strcmp(method,"presence.submit")) {
            locker_presence_t p={0}; p.principal=principal; p.request_id=request_id; p.idempotency_key=idem;
            if (params->count!=2 || !string(&d,params,"operation_id",text,sizeof(text)) ||
                locker_uuid_parse(text,strlen(text),&p.operation_id)!=LOCKER_STATUS_OK || !number(&d,params,"operation_revision",&p.operation_revision)) {
                free(result); send_reply(fd,id,400,NULL,"VALIDATION_ERROR"); return;
            }
            locker_query_t visible={0}; visible.principal=principal; visible.request_id=request_id; visible.resource_id=p.operation_id;
            strcpy(visible.kind,LOCKER_CORE_OPERATION_GET);
            status=locker_core_query(ipc->core,&visible,result);
            if (status==LOCKER_STATUS_OK) status=result->status;
            if (status!=LOCKER_STATUS_OK) {
                free(result); send_reply(fd,id,status==LOCKER_STATUS_INVALID ? 404 : 503,NULL,status==LOCKER_STATUS_INVALID ? "NOT_FOUND" : "DEVICE_UNAVAILABLE"); return;
            }
            memset(result,0,sizeof(*result));
            status=locker_local_presence_submit(ipc->core,&peer,&p,&receipt);
        } else {
            locker_maintenance_t m={0}; m.principal=principal; m.request_id=request_id; m.idempotency_key=idem;
            if (params->count!=5 || !string(&d,params,"journal_epoch",text,sizeof(text)) ||
                locker_uuid_parse(text,strlen(text),&m.journal_epoch)!=LOCKER_STATUS_OK ||
                !number(&d,params,"boot_id",&m.expected_boot_id) || !number(&d,params,"physical_revision",&m.physical_revision) ||
                !number(&d,params,"snapshot_revision",&m.snapshot_revision) || !string(&d,params,"reason",m.reason,sizeof(m.reason))) {
                free(result); send_reply(fd,id,400,NULL,"VALIDATION_ERROR"); return;
            }
            status=locker_local_maintenance_reconcile(ipc->core,&peer,&m,&receipt);
        }
        if (status==LOCKER_STATUS_QUEUED) status=locker_core_wait_result(ipc->core,&receipt.submission_token,1000,result);
    } else if (b->command) {
        locker_command_t c={0}; c.principal=principal; c.request_id=request_id; c.resource_id=resource; c.idempotency_key=idem;
        strcpy(c.kind,b->kind);
        if (b->body && !locker_domain_json_render(&d,params,c.arguments_json,sizeof(c.arguments_json),NULL)) {
            free(result); send_reply(fd,id,400,NULL,"VALIDATION_ERROR"); return;
        }
        if (unbind) snprintf(c.arguments_json,sizeof(c.arguments_json),"{\"user_revision\":\"%s\"}",unbind_revision);
        if (strcmp(method,"login") && strcmp(method,"logout") && !locker_domain_json_validate_command(c.arguments_json,c.kind)) {
            sodium_memzero(&c,sizeof(c)); free(result); send_reply(fd,id,400,NULL,"VALIDATION_ERROR"); return;
        }
        if (b->path && !strncmp(b->kind,"OPERATION_",10)) {
            locker_query_t visible={0}; visible.principal=principal; visible.request_id=request_id; visible.resource_id=resource;
            strcpy(visible.kind,LOCKER_CORE_OPERATION_GET);
            status=locker_core_query(ipc->core,&visible,result);
            if (status==LOCKER_STATUS_OK) status=result->status;
            if (status!=LOCKER_STATUS_OK) {
                sodium_memzero(&c,sizeof(c)); free(result);
                send_reply(fd,id,status==LOCKER_STATUS_INVALID ? 404 : 503,NULL,status==LOCKER_STATUS_INVALID ? "NOT_FOUND" : "DEVICE_UNAVAILABLE"); return;
            }
            memset(result,0,sizeof(*result));
        }
        locker_receipt_t receipt; status=locker_core_submit(ipc->core,&c,&receipt); sodium_memzero(&c,sizeof(c));
        if (status==LOCKER_STATUS_QUEUED) status=locker_core_wait_result(ipc->core,&receipt.submission_token,1000,result);
    } else {
        locker_query_t q={0}; q.principal=principal; q.request_id=request_id; q.resource_id=history ? filter : resource; q.limit=page_limit; strcpy(q.cursor,cursor); strcpy(q.kind,b->kind);
        status=locker_core_query(ipc->core,&q,result);
    }
    if (status==LOCKER_STATUS_OK) status=result->status;
    if (status==LOCKER_STATUS_OK) send_reply(fd,id,b->success,b->success==204 ? NULL : result->json,NULL);
    else if (status==LOCKER_STATUS_INVALID) send_reply(fd,id,login ? 401 : b->path || !strcmp(method,"presence.submit") ? 404 : 400,NULL,login ? "UNAUTHENTICATED" : b->path || !strcmp(method,"presence.submit") ? "NOT_FOUND" : "VALIDATION_ERROR");
    else if (status==LOCKER_STATUS_BUSY) send_reply(fd,id,login ? 429 : !strcmp(method,"createOperation") ? 409 : 503,NULL,
        login ? "RATE_LIMITED" : !strcmp(method,"createOperation") ? "SLOT_BUSY" : "QUEUE_FULL");
    else if (status==LOCKER_STATUS_CONFLICT && result->json[0]) send_reply(fd,id,409,result->json,NULL);
    else send_reply(fd,id,status==LOCKER_STATUS_CONFLICT ? 409 : 503,NULL,status==LOCKER_STATUS_CONFLICT ? "REVISION_CONFLICT" : "DEVICE_UNAVAILABLE");
    sodium_memzero(result,sizeof(*result)); free(result);
}

static void handle(locker_ipc_t *ipc, int fd)
{
    struct ucred cred; socklen_t length = sizeof(cred); struct stat info;
    if (getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&cred,&length) || length != sizeof(cred) ||
        ((uint32_t)cred.uid != ipc->config.allowed_ui_uid && (uint32_t)cred.uid != ipc->config.allowed_admin_uid) ||
        lstat(ipc->config.socket_path,&info) || !S_ISSOCK(info.st_mode) || info.st_ino != ipc->inode ||
        info.st_uid != geteuid() || (info.st_mode & 0777) != ipc->config.file_mode) return;
    char packet[8193]; struct iovec iov = {packet,8192}; struct msghdr msg = {0}; msg.msg_iov = &iov; msg.msg_iovlen = 1;
    ssize_t n = recvmsg(fd,&msg,0);
    if (n <= 0 || (msg.msg_flags & MSG_TRUNC) || memchr(packet,0,(size_t)n)) return;
    packet[n] = 0;
    handle_packet(ipc,fd,cred,packet,n);
    sodium_memzero(packet,sizeof(packet));
}

static void *serve(void *user)
{
    locker_ipc_t *ipc=user;
    while (!atomic_load(&ipc->stopping)) {
        struct pollfd p={ipc->listener,POLLIN,0};
        if (poll(&p,1,100)<=0 || !(p.revents&POLLIN)) continue;
        int fd=accept4(ipc->listener,NULL,NULL,SOCK_CLOEXEC);
        if (fd<0) continue;
        struct timeval timeout={1,0}; setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
        handle(ipc,fd); close(fd);
    }
    return NULL;
}
locker_status_t locker_ipc_start(const locker_ipc_config_t *config, locker_core_t *core, locker_ipc_t **out)
{
    if (out) *out=NULL;
    if (!config || !core || !out || !memchr(config->socket_path,0,sizeof(config->socket_path)) ||
        config->socket_path[0]!='/' || (config->file_mode!=0600 && config->file_mode!=0660)) return LOCKER_STATUS_INVALID;
    char parent[108]; strcpy(parent,config->socket_path); char *slash=strrchr(parent,'/');
    if (!slash || slash==parent) return LOCKER_STATUS_INVALID;
    *slash=0; struct stat info;
    if (lstat(parent,&info) || !S_ISDIR(info.st_mode) || info.st_uid!=geteuid() || (info.st_mode&0022)) return LOCKER_STATUS_INVALID;
    if (!lstat(config->socket_path,&info)) return LOCKER_STATUS_CONFLICT;
    locker_ipc_t *ipc=calloc(1,sizeof(*ipc)); if (!ipc) return LOCKER_STATUS_NO_SPACE;
    ipc->config=*config; ipc->core=core; atomic_init(&ipc->stopping,false);
    ipc->listener=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);
    if (ipc->listener<0) { free(ipc); return LOCKER_STATUS_IO_ERROR; }
    struct sockaddr_un address={0}; address.sun_family=AF_UNIX; strcpy(address.sun_path,config->socket_path);
    if (bind(ipc->listener,(struct sockaddr *)&address,sizeof(address))) { close(ipc->listener); free(ipc); return LOCKER_STATUS_IO_ERROR; }
    if (chmod(config->socket_path,config->file_mode) || lstat(config->socket_path,&info) || listen(ipc->listener,8)) {
        close(ipc->listener); unlink(config->socket_path); free(ipc); return LOCKER_STATUS_IO_ERROR;
    }
    ipc->inode=info.st_ino;
    if (pthread_create(&ipc->thread,NULL,serve,ipc)) { close(ipc->listener); unlink(config->socket_path); free(ipc); return LOCKER_STATUS_IO_ERROR; }
    *out=ipc; return LOCKER_STATUS_OK;
}
locker_status_t locker_ipc_stop(locker_ipc_t *ipc, uint32_t timeout_ms)
{
    if (!ipc) return LOCKER_STATUS_INVALID;
    atomic_store(&ipc->stopping,true);
    if (!ipc->joined) {
        int result;
        if (!timeout_ms) result=pthread_join(ipc->thread,NULL);
        else {
            struct timespec deadline; clock_gettime(CLOCK_REALTIME,&deadline);
            deadline.tv_sec+=timeout_ms/1000; deadline.tv_nsec+=(long)(timeout_ms%1000)*1000000;
            if (deadline.tv_nsec>=1000000000) { ++deadline.tv_sec; deadline.tv_nsec-=1000000000; }
            result=pthread_timedjoin_np(ipc->thread,NULL,&deadline);
        }
        if (result) return LOCKER_STATUS_BUSY;
        ipc->joined=true; close(ipc->listener);
    }
    return LOCKER_STATUS_OK;
}
void locker_ipc_destroy(locker_ipc_t *ipc)
{
    if (!ipc) return;
    if (locker_ipc_stop(ipc,0)!=LOCKER_STATUS_OK) return;
    struct stat info;
    if (!lstat(ipc->config.socket_path,&info) && S_ISSOCK(info.st_mode) && info.st_ino==ipc->inode) unlink(ipc->config.socket_path);
    free(ipc);
}
