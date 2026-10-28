#include "domain_internal.h"
#include <string.h>
#include <stdio.h>
#ifdef LOCKER_NETWORK_AUTH
#include <sodium.h>
#endif

static void finish(locker_core_t *core, const domain_job_t *job, locker_status_t status, size_t length)
{
    domain_completion_accept(core, job->completion, &job->completion_token, status, NULL, false);
    domain_completion_finish(core, job->completion, &job->completion_token, status,
        length ? core->response_buffer : NULL, length, core->revision);
}

#ifdef LOCKER_NETWORK_AUTH
static bool text(const locker_json_document_t *d, const locker_json_node_t *o,
                 const char *key, char *out, size_t capacity, size_t minimum, size_t maximum)
{
    size_t bytes = 0, chars = 0;
    if (!locker_domain_json_text(d, domain_json_member(d, o, key), out, capacity, &bytes)) return false;
    for (size_t i = 0; i < bytes; ++i) if (((unsigned char)out[i] & 0xc0) != 0x80) ++chars;
    return chars >= minimum && chars <= maximum;
}

#endif
static locker_status_t audit_user(locker_core_t *core,domain_change_t *change,const locker_command_t *c,const char *id,const char *type)
{
    locker_uuid_t audit; char key[37],stamp[32],reason[128]; uint64_t wall=0;
    if (locker_clock_port_new_uuid(&core->clock,&audit)!=LOCKER_STATUS_OK || !domain_uuid_to_text(&audit,key,sizeof(key)) ||
        locker_clock_port_wall_utc_ms(&core->clock,&wall)!=LOCKER_STATUS_OK || !locker_domain_time_format_utc(wall,stamp,sizeof(stamp))) return LOCKER_STATUS_NOT_READY;
    snprintf(reason,sizeof(reason),"%s for user %s",type,id);
    if (!domain_change_put_begin(change,"audits",key)) return LOCKER_STATUS_NO_SPACE;
    domain_write_audit(&change->writer,&audit,&c->principal.user_id,NULL,type,reason,stamp,true,NULL,LOCKER_TIME_QUALITY_HOST_SYNCED);
    domain_change_put_end(change); return LOCKER_STATUS_OK;
}
#ifdef LOCKER_NETWORK_AUTH
static locker_status_t user_scan(locker_core_t *core,const char *name,const char *exclude,unsigned *total,bool *duplicate,bool *other_admin)
{
    char after[193]=""; *total=0; *duplicate=false; *other_admin=false;
    for (unsigned page_no=0;page_no<3;++page_no) {
        domain_doc_t page; locker_status_t status=domain_repo_page(core,"users",after,50,&page);
        if (status!=LOCKER_STATUS_OK) return status;
        const locker_json_node_t *docs=domain_json_member(&page.doc,page.value,"documents");
        if (!docs || docs->type!=LOCKER_JSON_ARRAY) return LOCKER_STATUS_IO_ERROR;
        for (uint32_t i=0;i<docs->count;++i) {
            const locker_json_node_t *entry=locker_domain_json_at(&page.doc,docs,i);
            const locker_json_node_t *value=domain_json_member(&page.doc,entry,"value"), *u=domain_json_member(&page.doc,value,"user");
            char username[33],role[16]; bool enabled=false;
            if (!domain_json_read_text(&page.doc,domain_json_member(&page.doc,entry,"key"),after,sizeof(after)) ||
                !text(&page.doc,u,"username",username,sizeof(username),3,32) || !text(&page.doc,u,"role",role,sizeof(role),1,15) ||
                !domain_json_read_bool(&page.doc,domain_json_member(&page.doc,u,"enabled"),&enabled)) return LOCKER_STATUS_IO_ERROR;
            if (++*total>100) return LOCKER_STATUS_NO_SPACE;
            if (name && !strcmp(name,username)) *duplicate=true;
            if (exclude && strcmp(exclude,after) && enabled && !strcmp(role,"ADMIN")) *other_admin=true;
        }
        if (docs->count<50) return LOCKER_STATUS_OK;
    }
    return LOCKER_STATUS_NO_SPACE;
}
#endif

bool domain_handle_user_query(locker_core_t *core, const domain_job_t *job)
{
    const locker_query_t *q = &job->body.query;
    bool observations = !strcmp(q->kind, LOCKER_CORE_CARD_OBSERVATIONS_LIST);
    if (!observations && strcmp(q->kind, LOCKER_CORE_USERS_LIST)) return false;
    locker_status_t status = domain_principal_check(core, &q->principal);
    if (status == LOCKER_STATUS_OK && q->principal.role != LOCKER_ROLE_ADMIN) status = LOCKER_STATUS_INVALID;
    locker_json_writer_t w;
    domain_response_begin(&w, core->response_buffer, sizeof(core->response_buffer));
    domain_response_data_object(&w);
    locker_domain_json_literal(&w, "\"items\":[");
    if (status == LOCKER_STATUS_OK && observations) {
        uint64_t now = domain_now(core);
        if (core->last_card.valid && now >= core->last_card.mono_ms && now - core->last_card.mono_ms <= LOCKER_CARD_OBSERVATION_MS) {
            char id[37], masked[16]; domain_uuid_to_text(&core->last_card.id, id, sizeof(id));
            snprintf(masked,sizeof(masked),"****%02x%02x",core->last_card.uid[core->last_card.uid_len-2],core->last_card.uid[core->last_card.uid_len-1]);
            locker_domain_json_literal(&w,"{\"id\":"); locker_domain_json_string(&w,id);
            locker_domain_json_literal(&w,",\"masked_uid\":"); locker_domain_json_string(&w,masked);
            locker_domain_json_literal(&w,",\"observed_age_ms\":"); locker_domain_json_u64(&w,now-core->last_card.mono_ms);
            locker_domain_json_literal(&w,",\"consumed\":"); locker_domain_json_bool(&w,core->last_card.consumed); locker_domain_json_literal(&w,"}");
        }
    } else if (status == LOCKER_STATUS_OK) {
        char after[193]=""; unsigned written=0;
        for (unsigned page_no=0;page_no<3 && status==LOCKER_STATUS_OK;++page_no) {
            domain_doc_t page; status=domain_repo_page(core,"users",after,50,&page);
            if (status!=LOCKER_STATUS_OK) break;
            const locker_json_node_t *docs=domain_json_member(&page.doc,page.value,"documents");
            if (!docs || docs->type!=LOCKER_JSON_ARRAY) { status=LOCKER_STATUS_IO_ERROR; break; }
            for (uint32_t i=0;i<docs->count;++i) {
                const locker_json_node_t *entry=locker_domain_json_at(&page.doc,docs,i);
                const locker_json_node_t *value=domain_json_member(&page.doc,entry,"value");
                const locker_json_node_t *user=domain_json_member(&page.doc,value,"user"); char json[2048];
                if (!domain_json_read_text(&page.doc,domain_json_member(&page.doc,entry,"key"),after,sizeof(after)) ||
                    !locker_domain_json_render(&page.doc,user,json,sizeof(json),NULL)) { status=LOCKER_STATUS_IO_ERROR; break; }
                if (written>=100) { status=LOCKER_STATUS_NO_SPACE; break; }
                if (written) locker_domain_json_literal(&w,",");
                locker_domain_json_literal(&w,json); ++written;
            }
            if (docs->count<50) break;
        }
    }
    locker_domain_json_literal(&w, "]"); domain_response_end(&w, core->revision, &q->request_id);
    if (!locker_domain_json_writer_ok(&w)) status = LOCKER_STATUS_NO_SPACE;
    finish(core, job, status, status == LOCKER_STATUS_OK ? w.length : 0);
    return true;
}

bool domain_handle_user_command(locker_core_t *core, const domain_job_t *job, const char *hash)
{
    const locker_command_t *c = &job->body.command;
    bool create = !strcmp(c->kind, LOCKER_CORE_USER_CREATE);
    if (!create && strcmp(c->kind, LOCKER_CORE_USER_UPDATE)) return false;
    locker_status_t status = LOCKER_STATUS_INVALID;
    size_t length = 0;
    char password[513] = "", password_hash[257] = "";
#ifdef LOCKER_NETWORK_AUTH
    locker_json_document_t d = {0}; d.nodes = core->arg_nodes; d.node_capacity = LOCKER_DOMAIN_DOC_NODES;
    d.members = core->arg_members; d.member_capacity = LOCKER_DOMAIN_DOC_MEMBERS;
    char id[37], username[33] = "", display[257] = "", role[16] = "", user_json[2048];
    bool enabled = true,protected_admin=false; char old_role[16]=""; uint64_t revision = 1;
    if (c->principal.role != LOCKER_ROLE_ADMIN || locker_uuid_is_nil(&c->idempotency_key) ||
        !locker_domain_json_parse(&d, c->arguments_json, strlen(c->arguments_json))) goto done;
    const locker_json_node_t *root = locker_domain_json_root(&d);
    if (!root || root->type != LOCKER_JSON_OBJECT || (create ? root->count != 4 : root->count < 2 || root->count > 4)) goto done;
    domain_doc_t existing;
    if (create) {
        if (!text(&d, root, "username", username, sizeof(username), 3, 32) ||
            !text(&d, root, "display_name", display, sizeof(display), 1, 64) ||
            !text(&d, root, "password", password, sizeof(password), 12, 128) ||
            !text(&d, root, "role", role, sizeof(role), 1, 15)) goto done;
        for (size_t i = 0; username[i]; ++i) {
            char ch = username[i];
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '-')) goto done;
        }
        unsigned total=0; bool duplicate=false,other_admin=false;
        status=user_scan(core,username,NULL,&total,&duplicate,&other_admin);
        if (status!=LOCKER_STATUS_OK) goto done;
        if (duplicate) { status=LOCKER_STATUS_CONFLICT; goto done; }
        if (total>=100) { status=LOCKER_STATUS_NO_SPACE; goto done; }
        /* Login uses exact case; the same canonical spelling indexes uniqueness. */
        status = domain_repo_doc(core, 0, "username_index", username, &existing);
        if (status != LOCKER_STATUS_OK) goto done;
        if (existing.found) { status = LOCKER_STATUS_CONFLICT; goto done; }
        status = LOCKER_STATUS_INVALID;
        locker_uuid_t uuid;
        if (locker_clock_port_new_uuid(&core->clock, &uuid) != LOCKER_STATUS_OK ||
            !domain_uuid_to_text(&uuid, id, sizeof(id))) goto done;
    } else {
        if (!domain_uuid_to_text(&c->resource_id, id, sizeof(id))) goto done;
        status = domain_repo_doc(core, 0, "users", id, &existing);
        if (status != LOCKER_STATUS_OK) goto done;
        status = LOCKER_STATUS_INVALID;
        if (!existing.found) goto done;
        const locker_json_node_t *u = domain_json_member(&existing.doc, existing.value, "user");
        uint64_t expected = 0;
        if (!domain_json_read_u64(&existing.doc, domain_json_member(&existing.doc, u, "user_revision"), &revision) ||
            !domain_json_read_u64(&d, domain_json_member(&d, root, "user_revision"), &expected)) goto done;
        if (expected != revision) { status = LOCKER_STATUS_CONFLICT; goto done; }
        if (revision == UINT64_MAX || !text(&existing.doc, u, "username", username, sizeof(username), 3, 32) ||
            !text(&existing.doc, u, "display_name", display, sizeof(display), 1, 64) ||
            !text(&existing.doc, u, "role", role, sizeof(role), 1, 15) ||
            !domain_json_read_bool(&existing.doc, domain_json_member(&existing.doc, u, "enabled"), &enabled) ||
            !text(&existing.doc, existing.value, "password_hash", password_hash, sizeof(password_hash), 1, 256)) goto done;
        protected_admin=enabled && !strcmp(role,"ADMIN"); strcpy(old_role,role);
        ++revision;
        unsigned fields = 1;
        if (domain_json_member(&d, root, "display_name")) { ++fields; if (!text(&d, root, "display_name", display, sizeof(display), 1, 64)) goto done; }
        if (domain_json_member(&d, root, "role")) { ++fields; if (!text(&d, root, "role", role, sizeof(role), 1, 15)) goto done; }
        if (domain_json_member(&d, root, "enabled")) { ++fields; if (!domain_json_read_bool(&d, domain_json_member(&d, root, "enabled"), &enabled)) goto done; }
        if (fields != root->count) goto done;
    }
    if (strcmp(role, "ADMIN") && strcmp(role, "USER")) { status = LOCKER_STATUS_INVALID; goto done; }
    if (!create && protected_admin && (!enabled || strcmp(role,"ADMIN"))) {
        unsigned total=0; bool duplicate=false,other_admin=false;
        status=user_scan(core,NULL,id,&total,&duplicate,&other_admin);
        if (status!=LOCKER_STATUS_OK) goto done;
        if (!other_admin) { status=LOCKER_STATUS_CONFLICT; goto done; }
    }
    char revoked[LOCKER_DOMAIN_MAX_USER_SESSIONS][37]; size_t revoked_count=0,active_count=0;
    if (!create && (!enabled || strcmp(old_role,role))) {
        status=domain_user_sessions(core,id,false,revoked,&revoked_count,&active_count);
        if (status!=LOCKER_STATUS_OK) goto done;
    }
    if (create && crypto_pwhash_str_alg(password_hash, password, strlen(password),
        crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE, crypto_pwhash_ALG_ARGON2ID13)) {
        status = LOCKER_STATUS_NO_SPACE; goto done;
    }
    locker_json_writer_t user;
    locker_domain_json_writer_init(&user, user_json, sizeof(user_json));
    locker_domain_json_literal(&user, "{");
    locker_domain_json_key(&user, "id"); locker_domain_json_string(&user, id);
    locker_domain_json_literal(&user, ",\"username\":"); locker_domain_json_string(&user, username);
    locker_domain_json_literal(&user, ",\"display_name\":"); locker_domain_json_string(&user, display);
    locker_domain_json_literal(&user, ",\"role\":"); locker_domain_json_string(&user, role);
    locker_domain_json_literal(&user, ",\"enabled\":"); locker_domain_json_bool(&user, enabled);
    locker_domain_json_literal(&user, ",\"user_revision\":"); char revision_text[21]; snprintf(revision_text, sizeof(revision_text), "%llu", (unsigned long long)revision); locker_domain_json_string(&user, revision_text);
    locker_domain_json_literal(&user, "}");
    locker_json_writer_t response;
    domain_response_begin(&response, core->response_buffer, sizeof(core->response_buffer));
    locker_domain_json_literal(&response, user_json);
    domain_response_end_value(&response, core->revision + 1, &c->request_id);
    if (!locker_domain_json_writer_ok(&user) || !locker_domain_json_writer_ok(&response)) { status = LOCKER_STATUS_NO_SPACE; goto done; }
    domain_change_t change; domain_change_init(core, &change, core->revision, core->revision + 1);
    if (create) domain_change_check(&change, "username_index", username, "ABSENT");
    domain_change_check(&change, "users", id, create ? "ABSENT" : "EXISTS");
    domain_change_put_begin(&change, "users", id);
    locker_domain_json_literal(&change.writer, "{\"user\":"); locker_domain_json_literal(&change.writer, user_json);
    locker_domain_json_literal(&change.writer, ",\"password_hash\":"); locker_domain_json_string(&change.writer, password_hash);
    locker_domain_json_literal(&change.writer, "}"); domain_change_put_end(&change);
    if (create) {
        domain_change_put_begin(&change, "username_index", username);
        locker_domain_json_literal(&change.writer, "{\"value\":"); locker_domain_json_string(&change.writer, username);
        locker_domain_json_literal(&change.writer, ",\"resource_id\":"); locker_domain_json_string(&change.writer, id);
        locker_domain_json_literal(&change.writer, "}"); domain_change_put_end(&change);
    }
    for (size_t i=0;i<revoked_count;++i) domain_change_delete(&change,"sessions",revoked[i]);
    status=audit_user(core,&change,c,id,create ? "USER_CREATED" : "USER_UPDATED");
    if (status==LOCKER_STATUS_OK) status = domain_idempotency_append(core, &change, c, hash, core->response_buffer);
    if (status == LOCKER_STATUS_OK) status = domain_change_commit(core, &change);
    if (status == LOCKER_STATUS_OK) { length = response.length; locker_uuid_t changed; if (locker_uuid_parse(id, strlen(id), &changed) == LOCKER_STATUS_OK) domain_publish(core, &changed, "user", core->revision); }
done:
    sodium_memzero(password, sizeof(password)); sodium_memzero(password_hash, sizeof(password_hash));
#else
    (void)hash; (void)password; (void)password_hash;
    status = LOCKER_STATUS_NOT_READY;
#endif
    finish(core, job, status, length);
    return true;
}

bool domain_handle_card_command(locker_core_t *core, const domain_job_t *job, const char *hash)
{
    const locker_command_t *c=&job->body.command;
    bool bind=!strcmp(c->kind,LOCKER_CORE_CARD_BIND);
    if (!bind && strcmp(c->kind,LOCKER_CORE_CARD_UNBIND)) return false;
    locker_status_t status=LOCKER_STATUS_INVALID; size_t length=0; const char *error=NULL;
    if (c->principal.role!=LOCKER_ROLE_ADMIN || locker_uuid_is_nil(&c->idempotency_key)) goto done;
    locker_json_document_t d={0}; d.nodes=core->arg_nodes; d.node_capacity=LOCKER_DOMAIN_DOC_NODES;
    d.members=core->arg_members; d.member_capacity=LOCKER_DOMAIN_DOC_MEMBERS;
    if (!locker_domain_json_parse(&d,c->arguments_json,strlen(c->arguments_json))) goto done;
    const locker_json_node_t *root=locker_domain_json_root(&d);
    uint64_t expected=0, revision=0;
    if (!root || root->type!=LOCKER_JSON_OBJECT || root->count!=(bind ? 2u : 1u) ||
        !domain_json_read_u64(&d,domain_json_member(&d,root,"user_revision"),&expected)) goto done;
    char id[37]; domain_uuid_to_text(&c->resource_id,id,sizeof(id));
    domain_doc_t user;
    status=domain_repo_doc(core,0,"users",id,&user);
    if (status!=LOCKER_STATUS_OK) goto done;
    status=LOCKER_STATUS_INVALID;
    if (!user.found) goto done;
    const locker_json_node_t *u=domain_json_member(&user.doc,user.value,"user");
    if (!domain_json_read_u64(&user.doc,domain_json_member(&user.doc,u,"user_revision"),&revision)) goto done;
    if (revision!=expected || revision==UINT64_MAX) { status=LOCKER_STATUS_CONFLICT; goto done; }
    char user_record[2048];
    if (!locker_domain_json_render(&user.doc,user.value,user_record,sizeof(user_record),NULL)) { status=LOCKER_STATUS_IO_ERROR; goto done; }
    char uid[21]="", old_uid[21]="", masked[16]="";
    if (bind) {
        locker_uuid_t observation; uint64_t now=domain_now(core);
        if (!domain_json_read_uuid(&d,domain_json_member(&d,root,"observation_id"),&observation) ||
            !core->last_card.valid || core->last_card.consumed || !locker_uuid_equal(&observation,&core->last_card.id) ||
            now<core->last_card.mono_ms || now-core->last_card.mono_ms>LOCKER_CARD_OBSERVATION_MS) goto done;
        static const char hex[]="0123456789abcdef";
        for (size_t i=0;i<core->last_card.uid_len;++i) { uid[i*2]=hex[core->last_card.uid[i]>>4]; uid[i*2+1]=hex[core->last_card.uid[i]&15]; }
        domain_doc_t index; status=domain_repo_doc(core,0,"card_uid_index",uid,&index);
        if (status!=LOCKER_STATUS_OK) goto done;
        if (index.found) { status=LOCKER_STATUS_CONFLICT; error="CARD_ALREADY_BOUND"; goto done; }
        snprintf(masked,sizeof(masked),"****%s",uid+strlen(uid)-4);
    }
    domain_doc_t old_card;
    status=domain_repo_doc(core,0,"cards",id,&old_card);
    if (status!=LOCKER_STATUS_OK) goto done;
    if (old_card.found && !domain_json_read_text(&old_card.doc,domain_json_member(&old_card.doc,old_card.value,"uid_hex"),old_uid,sizeof(old_uid))) {
        status=LOCKER_STATUS_IO_ERROR; goto done;
    }
    if (!locker_domain_json_parse(&d,user_record,strlen(user_record))) { status=LOCKER_STATUS_IO_ERROR; goto done; }
    user.doc=d; user.value=locker_domain_json_root(&d); u=domain_json_member(&d,user.value,"user");
    locker_json_writer_t response;
    domain_response_begin(&response,core->response_buffer,sizeof(core->response_buffer)); domain_response_data_object(&response);
    locker_domain_json_literal(&response,"\"user_id\":"); locker_domain_json_string(&response,id);
    locker_domain_json_literal(&response,",\"bound\":"); locker_domain_json_bool(&response,bind);
    locker_domain_json_literal(&response,",\"masked_uid\":");
    if (bind) locker_domain_json_string(&response,masked); else locker_domain_json_null(&response);
    char revision_text[21]; snprintf(revision_text,sizeof(revision_text),"%llu",(unsigned long long)(revision+1));
    locker_domain_json_literal(&response,",\"user_revision\":"); locker_domain_json_string(&response,revision_text);
    domain_response_end(&response,core->revision+1,&c->request_id);
    domain_change_t change; domain_change_init(core,&change,core->revision,core->revision+1);
    domain_change_check(&change,"users",id,"EXISTS");
    if (bind) domain_change_check(&change,"card_uid_index",uid,"ABSENT");
    domain_change_put_begin(&change,"users",id);
    locker_domain_json_literal(&change.writer,"{\"user\":{");
    const char *fields[]={"id","username","display_name","role","enabled"};
    for (size_t i=0;i<5;++i) {
        char json[2048]; const locker_json_node_t *field=domain_json_member(&user.doc,u,fields[i]);
        if (!locker_domain_json_render(&user.doc,field,json,sizeof(json),NULL)) { status=LOCKER_STATUS_IO_ERROR; goto done; }
        if (i) locker_domain_json_literal(&change.writer,",");
        locker_domain_json_key(&change.writer,fields[i]); locker_domain_json_literal(&change.writer,json);
    }
    locker_domain_json_literal(&change.writer,",\"user_revision\":"); locker_domain_json_string(&change.writer,revision_text);
    locker_domain_json_literal(&change.writer,"},\"password_hash\":");
    char password_hash[257];
    if (!domain_json_read_text(&user.doc,domain_json_member(&user.doc,user.value,"password_hash"),password_hash,sizeof(password_hash))) { status=LOCKER_STATUS_IO_ERROR; goto done; }
    locker_domain_json_string(&change.writer,password_hash); locker_domain_json_literal(&change.writer,"}"); domain_change_put_end(&change);
    if (old_uid[0]) { if (!bind) domain_change_delete(&change,"cards",id); domain_change_delete(&change,"card_uid_index",old_uid); }
    if (bind) {
        uint64_t wall=0; char stamp[32];
        if (locker_clock_port_wall_utc_ms(&core->clock,&wall)!=LOCKER_STATUS_OK || !locker_domain_time_format_utc(wall,stamp,sizeof(stamp))) { status=LOCKER_STATUS_NOT_READY; goto done; }
        domain_change_put_begin(&change,"cards",id);
        locker_domain_json_literal(&change.writer,"{\"user_id\":"); locker_domain_json_string(&change.writer,id);
        locker_domain_json_literal(&change.writer,",\"uid_hex\":"); locker_domain_json_string(&change.writer,uid);
        locker_domain_json_literal(&change.writer,",\"bound_at\":"); locker_domain_json_string(&change.writer,stamp);
        locker_domain_json_literal(&change.writer,"}"); domain_change_put_end(&change);
        domain_change_put_begin(&change,"card_uid_index",uid);
        locker_domain_json_literal(&change.writer,"{\"value\":"); locker_domain_json_string(&change.writer,uid);
        locker_domain_json_literal(&change.writer,",\"resource_id\":"); locker_domain_json_string(&change.writer,id);
        locker_domain_json_literal(&change.writer,"}"); domain_change_put_end(&change);
    }
    status=audit_user(core,&change,c,id,bind ? "CARD_BOUND" : "CARD_UNBOUND");
    if (status==LOCKER_STATUS_OK) status=domain_idempotency_append(core,&change,c,hash,core->response_buffer);
    if (status==LOCKER_STATUS_OK) status=domain_change_commit(core,&change);
    if (status==LOCKER_STATUS_OK) {
        length=response.length;
        if (bind) core->last_card.consumed=true;
        domain_publish(core,&c->resource_id,"user",core->revision);
    }
done:
    if (error) {
        char request_id[37]; domain_uuid_to_text(&c->request_id,request_id,sizeof(request_id));
        int n=snprintf(core->response_buffer,sizeof(core->response_buffer),"{\"error\":{\"code\":\"%s\",\"message\":\"Request rejected\",\"retryable\":false,\"operation_id\":null},\"request_id\":\"%s\"}",error,request_id);
        length=(size_t)n;
    }
    finish(core,job,status,length); return true;
}
