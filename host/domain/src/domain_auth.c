#include "domain_internal.h"
#include <string.h>
#include <stdio.h>
/* Bounded per-account session storage keeps disable/revoke atomic within the
 * 32-mutation repository budget. Login removes that account's obsolete rows. */
locker_status_t domain_user_sessions(locker_core_t *core,const char *user_id,bool stale_only,
    char keys[LOCKER_DOMAIN_MAX_USER_SESSIONS][37],size_t *count,size_t *active)
{
    char after[193]=""; size_t total=0; uint64_t wall=0; *count=0; *active=0;
    if (locker_clock_port_wall_utc_ms(&core->clock,&wall)!=LOCKER_STATUS_OK) return LOCKER_STATUS_NOT_READY;
    for (unsigned page_no=0;page_no<64;++page_no) {
        domain_doc_t page; locker_status_t status=domain_repo_page(core,"sessions",after,50,&page);
        if (status!=LOCKER_STATUS_OK) return status;
        const locker_json_node_t *docs=domain_json_member(&page.doc,page.value,"documents");
        if (!docs || docs->type!=LOCKER_JSON_ARRAY) return LOCKER_STATUS_IO_ERROR;
        for (uint32_t i=0;i<docs->count;++i) {
            const locker_json_node_t *entry=locker_domain_json_at(&page.doc,docs,i);
            const locker_json_node_t *value=domain_json_member(&page.doc,entry,"value");
            char owner[37],expires[32]; bool revoked=false; uint64_t until=0;
            if (!domain_json_read_text(&page.doc,domain_json_member(&page.doc,entry,"key"),after,sizeof(after)) ||
                !domain_json_read_text(&page.doc,domain_json_member(&page.doc,value,"user_id"),owner,sizeof(owner))) return LOCKER_STATUS_IO_ERROR;
            if (strcmp(owner,user_id)) continue;
            if (++total>LOCKER_DOMAIN_MAX_USER_SESSIONS) return LOCKER_STATUS_NO_SPACE;
            if (!domain_json_read_text(&page.doc,domain_json_member(&page.doc,value,"expires_at"),expires,sizeof(expires)) ||
                !locker_domain_time_parse_utc(expires,strlen(expires),&until) ||
                !domain_json_read_bool(&page.doc,domain_json_member(&page.doc,value,"revoked"),&revoked)) return LOCKER_STATUS_IO_ERROR;
            bool current=!revoked && until>wall;
            if (current) ++*active;
            if (!stale_only || !current) { if (strlen(after)!=36) return LOCKER_STATUS_IO_ERROR; strcpy(keys[(*count)++],after); }
        }
        if (docs->count<50) return LOCKER_STATUS_OK;
    }
    return LOCKER_STATUS_NO_SPACE;
}

#ifdef LOCKER_NETWORK_AUTH
#include <sodium.h>

static locker_status_t domain_login(locker_core_t *core, const locker_command_t *command,
                                     size_t *response_length)
{
    locker_json_document_t args = {0};
    args.nodes = core->arg_nodes;
    args.node_capacity = LOCKER_DOMAIN_DOC_NODES;
    args.members = core->arg_members;
    args.member_capacity = LOCKER_DOMAIN_DOC_MEMBERS;
    size_t n = domain_strnlen(command->arguments_json, sizeof(command->arguments_json));
    if (n == sizeof(command->arguments_json) ||
        !locker_domain_json_parse(&args, command->arguments_json, n)) return LOCKER_STATUS_INVALID;
    const locker_json_node_t *root = locker_domain_json_root(&args);
    char username[33], password[513] = "";
    if (root == NULL || root->type != LOCKER_JSON_OBJECT || root->count != 2 ||
        !domain_json_read_text(&args, domain_json_member(&args, root, "username"), username, sizeof(username)) ||
        !domain_json_read_text(&args, domain_json_member(&args, root, "password"), password, sizeof(password)))
    {
        sodium_memzero(password, sizeof(password));
        return LOCKER_STATUS_INVALID;
    }
    n = strlen(username);
    if (n < 3) { sodium_memzero(password, sizeof(password)); return LOCKER_STATUS_INVALID; }
    for (size_t i = 0; i < n; ++i) {
        char c = username[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) {
            sodium_memzero(password, sizeof(password)); return LOCKER_STATUS_INVALID;
        }
    }
    /* Count UTF-8 characters rather than bytes; parser has validated UTF-8. */
    size_t chars = 0;
    for (const unsigned char *p = (const unsigned char *)password; *p; ++p)
        if ((*p & 0xc0) != 0x80) ++chars;
    if (chars < 12 || chars > 128) {
        sodium_memzero(password, sizeof(password)); return LOCKER_STATUS_INVALID;
    }
    uint64_t mono = 0;
    (void)locker_clock_port_monotonic_ms(&core->clock, &mono);
    size_t rate = 64, available = 64;
    for (size_t i = 0; i < 64; ++i) {
        if (core->login_limits[i].expires_ms <= mono) available = i;
        else if (strcmp(core->login_limits[i].username, username) == 0) { rate = i; break; }
    }
    if (rate == 64) {
        if (available == 64) { sodium_memzero(password, sizeof(password)); return LOCKER_STATUS_BUSY; }
        rate = available;
        strcpy(core->login_limits[rate].username, username);
        core->login_limits[rate].failures = 0;
        core->login_limits[rate].expires_ms = mono > UINT64_MAX - 60000 ? UINT64_MAX : mono + 60000;
    }
    if (core->login_limits[rate].failures >= 5) {
        sodium_memzero(password, sizeof(password)); return LOCKER_STATUS_BUSY;
    }
    char after[193] = "", user_json[2048] = "", user_id[37] = "";
    locker_status_t status = LOCKER_STATUS_INVALID;
    for (;;) {
        domain_doc_t page;
        status = domain_repo_page(core, "users", after, 50, &page);
        if (status != LOCKER_STATUS_OK) break;
        const locker_json_node_t *docs = domain_json_member(&page.doc, page.value, "documents");
        if (docs == NULL || docs->type != LOCKER_JSON_ARRAY) { status = LOCKER_STATUS_IO_ERROR; break; }
        if (docs->count == 0) { status = LOCKER_STATUS_INVALID; break; }
        bool matched = false;
        for (uint32_t i = 0; i < docs->count; ++i) {
            const locker_json_node_t *entry = locker_domain_json_at(&page.doc, docs, i);
            const locker_json_node_t *value = domain_json_member(&page.doc, entry, "value");
            const locker_json_node_t *user = domain_json_member(&page.doc, value, "user");
            char name[33], hash[257];
            if (!domain_json_read_text(&page.doc, domain_json_member(&page.doc, entry, "key"), after, sizeof(after))) {
                status = LOCKER_STATUS_IO_ERROR; matched = true; break;
            }
            if (!domain_json_read_text(&page.doc, domain_json_member(&page.doc, user, "username"), name, sizeof(name)) ||
                strcmp(name, username) != 0) continue;
            matched = true;
            bool enabled = false;
            status = LOCKER_STATUS_INVALID;
            if (!domain_json_read_bool(&page.doc, domain_json_member(&page.doc, user, "enabled"), &enabled) || !enabled ||
                !domain_json_read_text(&page.doc, domain_json_member(&page.doc, value, "password_hash"), hash, sizeof(hash)) ||
                strncmp(hash, "$argon2id$", 10) != 0 ||
                crypto_pwhash_str_verify(hash, password, strlen(password)) != 0) break;
            if (!domain_json_read_text(&page.doc, domain_json_member(&page.doc, user, "id"), user_id, sizeof(user_id)) ||
                !locker_domain_json_render(&page.doc, user, user_json, sizeof(user_json), NULL)) {
                status = LOCKER_STATUS_IO_ERROR; break;
            }
            status = LOCKER_STATUS_OK;
            break;
        }
        if (matched) break;
        if (docs->count < 50) { status = LOCKER_STATUS_INVALID; break; }
    }
    sodium_memzero(password, sizeof(password));
    if (status == LOCKER_STATUS_INVALID) ++core->login_limits[rate].failures;
    if (status != LOCKER_STATUS_OK) return status;
    uint64_t wall;
    char expires[32], id[37], token[65], hash[65];
    unsigned char entropy[32], digest[32];
    locker_uuid_t session;
    if (sodium_init() < 0 || locker_clock_port_wall_utc_ms(&core->clock, &wall) != LOCKER_STATUS_OK ||
        wall > UINT64_MAX - 28800000 ||
        !locker_domain_time_format_utc(wall + 28800000, expires, sizeof(expires)) ||
        locker_clock_port_new_uuid(&core->clock, &session) != LOCKER_STATUS_OK ||
        !domain_uuid_to_text(&session, id, sizeof(id))) return LOCKER_STATUS_NOT_READY;
    char obsolete[LOCKER_DOMAIN_MAX_USER_SESSIONS][37]; size_t obsolete_count=0,active_count=0;
    status=domain_user_sessions(core,user_id,true,obsolete,&obsolete_count,&active_count);
    if (status!=LOCKER_STATUS_OK) return status;
    if (active_count>=LOCKER_DOMAIN_MAX_USER_SESSIONS) return LOCKER_STATUS_BUSY;
    randombytes_buf(entropy, sizeof(entropy));
    sodium_bin2hex(token, sizeof(token), entropy, sizeof(entropy));
    sodium_memzero(entropy, sizeof(entropy));
    crypto_hash_sha256(digest, (const unsigned char *)token, strlen(token));
    sodium_bin2hex(hash, sizeof(hash), digest, sizeof(digest));
    sodium_memzero(digest, sizeof(digest));
    domain_change_t change;
    domain_change_init(core, &change, core->revision, core->revision + 1u);
    if (!domain_change_check(&change, "sessions", id, "ABSENT") ||
        !domain_change_put_begin(&change, "sessions", id)) {
        sodium_memzero(token, sizeof(token)); return LOCKER_STATUS_NO_SPACE;
    }
    char record[512];
    snprintf(record, sizeof(record), "{\"id\":\"%s\",\"user_id\":\"%s\",\"token_hash\":\"%s\",\"expires_at\":\"%s\",\"revoked\":false}",
             id, user_id, hash, expires);
    locker_domain_json_literal(&change.writer, record);
    domain_change_put_end(&change);
    for (size_t i=0;i<obsolete_count;++i) domain_change_delete(&change,"sessions",obsolete[i]);
    status = domain_change_commit(core, &change);
    if (status == LOCKER_STATUS_OK) {
        locker_json_writer_t w;
        domain_response_begin(&w, core->response_buffer, sizeof(core->response_buffer));
        domain_response_data_object(&w);
        locker_domain_json_key(&w, "token"); locker_domain_json_string(&w, token);
        locker_domain_json_literal(&w, ",\"expires_at\":"); locker_domain_json_string(&w, expires);
        locker_domain_json_literal(&w, ",\"user\":"); locker_domain_json_literal(&w, user_json);
        domain_response_end(&w, core->revision, &command->request_id);
        if (locker_domain_json_writer_ok(&w)) *response_length = w.length;
        else status = LOCKER_STATUS_NO_SPACE;
    }
    sodium_memzero(token, sizeof(token));
    return status;
}

/* Core-thread only. Pages stay bounded and keys advance even when records do
 * not match. No repository pointer or raw token escapes to HTTP workers. */
static locker_status_t authenticate_token(locker_core_t *core,
                                           const locker_auth_request_t *request,
                                           locker_principal_t *principal)
{
    unsigned char digest[crypto_hash_sha256_BYTES];
    char wanted[65], after[193] = "";
    if (sodium_init() < 0) return LOCKER_STATUS_IO_ERROR;
    if (crypto_hash_sha256(digest, (const unsigned char *)request->token,
                           request->token_length) != 0) return LOCKER_STATUS_IO_ERROR;
    sodium_bin2hex(wanted, sizeof(wanted), digest, sizeof(digest));
    sodium_memzero(digest, sizeof(digest));
    for (unsigned page_no=0;page_no<64;++page_no) {
        domain_doc_t page;
        locker_status_t status = domain_repo_page(core, "sessions", after, 50, &page);
        if (status != LOCKER_STATUS_OK) return status;
        const locker_json_node_t *docs = domain_json_member(&page.doc, page.value, "documents");
        if (docs == NULL || docs->type != LOCKER_JSON_ARRAY) return LOCKER_STATUS_IO_ERROR;
        if (docs->count == 0) return LOCKER_STATUS_INVALID;
        for (uint32_t i = 0; i < docs->count; ++i) {
            const locker_json_node_t *entry = locker_domain_json_at(&page.doc, docs, i);
            const locker_json_node_t *value = domain_json_member(&page.doc, entry, "value");
            char hash[65];
            if (!domain_json_read_text(&page.doc, domain_json_member(&page.doc, entry, "key"),
                                       after, sizeof(after)) || value == NULL)
                return LOCKER_STATUS_IO_ERROR;
            if (!domain_json_read_text(&page.doc, domain_json_member(&page.doc, value, "token_hash"),
                                       hash, sizeof(hash))) return LOCKER_STATUS_IO_ERROR;
            if (strlen(hash) != 64 || sodium_memcmp(wanted, hash, 64) != 0) continue;
            if (!domain_json_read_uuid(&page.doc, domain_json_member(&page.doc, value, "id"),
                                       &principal->session_id) ||
                !domain_json_read_uuid(&page.doc, domain_json_member(&page.doc, value, "user_id"),
                                       &principal->user_id)) return LOCKER_STATUS_IO_ERROR;
            char key[37];
            domain_doc_t user;
            if (!domain_uuid_to_text(&principal->user_id, key, sizeof(key)))
                return LOCKER_STATUS_IO_ERROR;
            status = domain_repo_doc(core, 0, "users", key, &user);
            if (status != LOCKER_STATUS_OK) return status;
            if (!user.found) return LOCKER_STATUS_INVALID;
            const locker_json_node_t *u = domain_json_member(&user.doc, user.value, "user");
            char role[16];
            if (!domain_json_read_text(&user.doc, domain_json_member(&user.doc, u, "role"),
                                       role, sizeof(role))) return LOCKER_STATUS_INVALID;
            if (strcmp(role, "ADMIN") == 0) principal->role = LOCKER_ROLE_ADMIN;
            else if (strcmp(role, "USER") == 0) principal->role = LOCKER_ROLE_USER;
            else return LOCKER_STATUS_INVALID;
            principal->authenticated = true;
            return domain_principal_check(core, principal);
        }
        if (docs->count < 50) return LOCKER_STATUS_INVALID;
    }
    return LOCKER_STATUS_NO_SPACE;
}
#endif

bool domain_handle_session_command(locker_core_t *core, const domain_job_t *job)
{
    const locker_command_t *command = &job->body.command;
    if (strcmp(command->kind, LOCKER_CORE_SESSION_REVOKE) == 0) {
        locker_status_t status = domain_principal_check(core, &command->principal);
        if (status == LOCKER_STATUS_OK) {
            char key[37], json[1024];
            domain_doc_t doc;
            domain_uuid_to_text(&command->principal.session_id, key, sizeof(key));
            status = domain_repo_doc(core, 0, "sessions", key, &doc);
            char id[37], uid[37], hash[65], expires[32];
            if (status == LOCKER_STATUS_OK && (!doc.found ||
                !domain_json_read_text(&doc.doc, domain_json_member(&doc.doc, doc.value, "id"), id, sizeof(id)) ||
                !domain_json_read_text(&doc.doc, domain_json_member(&doc.doc, doc.value, "user_id"), uid, sizeof(uid)) ||
                !domain_json_read_text(&doc.doc, domain_json_member(&doc.doc, doc.value, "token_hash"), hash, sizeof(hash)) ||
                !domain_json_read_text(&doc.doc, domain_json_member(&doc.doc, doc.value, "expires_at"), expires, sizeof(expires))))
                status = LOCKER_STATUS_IO_ERROR;
            if (status == LOCKER_STATUS_OK) {
                snprintf(json, sizeof(json), "{\"id\":\"%s\",\"user_id\":\"%s\",\"token_hash\":\"%s\",\"expires_at\":\"%s\",\"revoked\":true}", id, uid, hash, expires);
                domain_change_t change;
                domain_change_init(core, &change, core->revision, core->revision + 1u);
                if (!domain_change_put_begin(&change, "sessions", key)) status = LOCKER_STATUS_NO_SPACE;
                else {
                    locker_domain_json_literal(&change.writer, json);
                    domain_change_put_end(&change);
                    status = domain_change_commit(core, &change);
                }
            }
        }
        domain_completion_accept(core, job->completion, &job->completion_token, status, NULL, false);
        domain_completion_finish(core, job->completion, &job->completion_token, status, NULL, 0, core->revision);
        return true;
    }
    if (strcmp(command->kind, LOCKER_CORE_SESSION_CREATE) != 0) return false;
    locker_status_t status = LOCKER_STATUS_NOT_READY;
    size_t response_length = 0;
#ifdef LOCKER_NETWORK_AUTH
    uint64_t now = 0;
    (void)locker_clock_port_monotonic_ms(&core->clock, &now);
    if (now < core->login_not_before_ms) status = LOCKER_STATUS_BUSY;
    else {
        core->login_not_before_ms = now > UINT64_MAX - 1000 ? UINT64_MAX : now + 1000;
        status = domain_login(core, command, &response_length);
    }
#endif
    domain_completion_accept(core, job->completion, &job->completion_token, status, NULL, false);
    domain_completion_finish(core, job->completion, &job->completion_token, status,
        response_length ? core->response_buffer : NULL, response_length, core->revision);
    return true;
}

bool domain_handle_session_query(locker_core_t *core, const domain_job_t *job)
{
    const locker_query_t *query = &job->body.query;
    if (strcmp(query->kind, LOCKER_CORE_SESSION_CURRENT) != 0) return false;
    locker_status_t status = domain_principal_check(core, &query->principal);
    size_t length = 0;
    if (status == LOCKER_STATUS_OK) {
        char key[37], json[2048]; domain_doc_t doc;
        domain_uuid_to_text(&query->principal.user_id, key, sizeof(key));
        status = domain_repo_doc(core, 0, "users", key, &doc);
        if (status == LOCKER_STATUS_OK) {
            const locker_json_node_t *user = domain_json_member(&doc.doc, doc.value, "user");
            if (!doc.found || !locker_domain_json_render(&doc.doc, user, json, sizeof(json), NULL))
                status = LOCKER_STATUS_IO_ERROR;
            else {
                locker_json_writer_t w;
                domain_response_begin(&w, core->response_buffer, sizeof(core->response_buffer));
                locker_domain_json_literal(&w, json);
                domain_response_end_value(&w, core->revision, &query->request_id);
                if (locker_domain_json_writer_ok(&w)) length = w.length;
                else status = LOCKER_STATUS_NO_SPACE;
            }
        }
    }
    domain_completion_finish(core, job->completion, &job->completion_token, status,
        length ? core->response_buffer : NULL, length, core->revision);
    return true;
}

void domain_handle_auth(locker_core_t *core, const domain_job_t *job)
{
    locker_principal_t principal;
    memset(&principal, 0, sizeof(principal));
    locker_status_t status = LOCKER_STATUS_NOT_READY;
#ifdef LOCKER_NETWORK_AUTH
    status = authenticate_token(core, &job->body.auth, &principal);
#endif
    if (status == LOCKER_STATUS_OK)
        domain_completion_set_principal(core, job->completion, &job->completion_token, &principal);
    domain_completion_accept(core, job->completion, &job->completion_token, status, NULL, false);
    domain_completion_finish(core, job->completion, &job->completion_token, status, NULL, 0,
                              core->revision);
}
