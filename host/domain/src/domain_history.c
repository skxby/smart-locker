#include "domain_internal.h"
#include <string.h>
#include <stdio.h>

static bool revision_read(const char *text, size_t n, uint64_t *out)
{
    *out=0;
    if (!n || n>20 || (n>1 && text[0]=='0')) return false;
    for (size_t i=0;i<n;++i) {
        if (text[i]<'0' || text[i]>'9' || *out>(UINT64_MAX-(unsigned)(text[i]-'0'))/10) return false;
        *out=*out*10+(unsigned)(text[i]-'0');
    }
    return true;
}

bool domain_handle_history_query(locker_core_t *core, const domain_job_t *job)
{
    const locker_query_t *q=&job->body.query;
    bool loans=!strcmp(q->kind,LOCKER_CORE_LOANS_LIST);
    if (!loans && strcmp(q->kind,LOCKER_CORE_EVENTS_LIST)) return false;
    locker_status_t status=domain_principal_check(core,&q->principal);
    uint16_t limit=q->limit ? q->limit : 20;
    char after[193]=""; char phase=loans ? 'L' : 'D';
    if (limit>100) status=LOCKER_STATUS_INVALID;
    if (status==LOCKER_STATUS_OK && q->cursor[0]) {
        const char *colon=strchr(q->cursor,':'); uint64_t revision=0;
        if (!colon || (loans ? q->cursor[0]!='L' : q->cursor[0]!='D' && q->cursor[0]!='A') ||
            !revision_read(q->cursor+1,(size_t)(colon-q->cursor-1),&revision) || strlen(colon+1)>=sizeof(after)) status=LOCKER_STATUS_INVALID;
        else if (revision!=core->revision) status=LOCKER_STATUS_CONFLICT;
        else { phase=q->cursor[0]; strcpy(after,colon+1); }
    }
    locker_json_writer_t w;
    domain_response_begin(&w,core->response_buffer,sizeof(core->response_buffer)); domain_response_data_object(&w);
    locker_domain_json_literal(&w,"\"items\":[");
    unsigned written=0, scanned=0; bool more=false;
    while (status==LOCKER_STATUS_OK && scanned<4096) {
        domain_doc_t page;
        const char *collection=phase=='L' ? "loans" : phase=='D' ? "device_events" : "audits";
        status=domain_repo_page(core,collection,after,1,&page);
        if (status!=LOCKER_STATUS_OK) break;
        const locker_json_node_t *docs=domain_json_member(&page.doc,page.value,"documents");
        if (!docs || docs->type!=LOCKER_JSON_ARRAY) { status=LOCKER_STATUS_IO_ERROR; break; }
        if (!docs->count) {
            if (phase=='D') { phase='A'; after[0]=0; continue; }
            more=false; break;
        }
        const locker_json_node_t *entry=locker_domain_json_at(&page.doc,docs,0);
        const locker_json_node_t *value=domain_json_member(&page.doc,entry,"value");
        char key[193], object[4096]; locker_uuid_t owner={0}, operation={0};
        if (!domain_json_read_text(&page.doc,domain_json_member(&page.doc,entry,"key"),key,sizeof(key))) { status=LOCKER_STATUS_IO_ERROR; break; }
        ++scanned;
        bool include=true;
        if (loans) {
            if (!domain_json_read_uuid(&page.doc,domain_json_member(&page.doc,value,"borrower_id"),&owner) ||
                !locker_domain_json_render(&page.doc,value,object,sizeof(object),NULL)) { status=LOCKER_STATUS_IO_ERROR; break; }
            include=q->principal.role==LOCKER_ROLE_ADMIN || locker_uuid_equal(&owner,&q->principal.user_id);
        } else {
            const locker_json_node_t *op=domain_json_member(&page.doc,value,"operation_id");
            if (!op || (op->type!=LOCKER_JSON_NULL && !domain_json_read_uuid(&page.doc,op,&operation))) { status=LOCKER_STATUS_IO_ERROR; break; }
            if (!locker_uuid_is_nil(&q->resource_id) && !locker_uuid_equal(&q->resource_id,&operation)) include=false;
            locker_json_writer_t e; locker_domain_json_writer_init(&e,object,sizeof(object));
            locker_domain_json_literal(&e,"{");
            if (phase=='A') {
                if (q->principal.role!=LOCKER_ROLE_ADMIN && locker_uuid_is_nil(&operation)) {
                    locker_uuid_t actor;
                    include=include && domain_json_read_uuid(&page.doc,domain_json_member(&page.doc,value,"actor_id"),&actor) && locker_uuid_equal(&actor,&q->principal.user_id);
                }
                const char *fields[]={"id","operation_id","type","reason","occurred_at","time_quality","source_event"};
                const char *names[]={"id","operation_id","type","message","occurred_at","time_quality","source_event"};
                for (size_t i=0;i<7;++i) {
                    char item[2048];
                    if (!locker_domain_json_render(&page.doc,domain_json_member(&page.doc,value,fields[i]),item,sizeof(item),NULL)) { status=LOCKER_STATUS_IO_ERROR; break; }
                    if (i) locker_domain_json_literal(&e,",");
                    locker_domain_json_key(&e,names[i]); locker_domain_json_literal(&e,item);
                }
                locker_domain_json_literal(&e,",\"manual\":true}");
            } else {
                locker_domain_sha256_t hash; uint8_t digest[32]; locker_uuid_t id;
                locker_domain_sha256_init(&hash); locker_domain_sha256_update(&hash,key,strlen(key)); locker_domain_sha256_final(&hash,digest);
                memcpy(id.bytes,digest,16); id.bytes[6]=(uint8_t)((id.bytes[6]&15)|0x50); id.bytes[8]=(uint8_t)((id.bytes[8]&63)|0x80);
                char id_text[37], source[1024], op_json[64]; int64_t event_type=0;
                static const char *types[]={"BOOT","PULSE_STARTED","PULSE_FINISHED","DOOR_OPENED","DOOR_CLOSED","ITEM_REMOVED","ITEM_PRESENT","CARD_OBSERVED","SUPPLY_CHANGED","HEALTH_CHANGED","JOURNAL_FAULT","COMMAND_ACCEPTED","CONTEXT_RELEASED","CONTINUITY_LOST","UI_RESTART_REQUEST"};
                if (!domain_json_read_int(&page.doc,domain_json_member(&page.doc,value,"event_type"),&event_type) || event_type<1 || event_type>15 ||
                    !locker_domain_json_render(&page.doc,domain_json_member(&page.doc,value,"key"),source,sizeof(source),NULL) ||
                    !locker_domain_json_render(&page.doc,op,op_json,sizeof(op_json),NULL)) { status=LOCKER_STATUS_IO_ERROR; break; }
                domain_uuid_to_text(&id,id_text,sizeof(id_text));
                locker_domain_json_key(&e,"id"); locker_domain_json_string(&e,id_text);
                locker_domain_json_literal(&e,",\"operation_id\":"); locker_domain_json_literal(&e,op_json);
                locker_domain_json_literal(&e,",\"type\":"); locker_domain_json_string(&e,types[event_type-1]);
                locker_domain_json_literal(&e,",\"message\":\"Device event\",\"occurred_at\":null,\"time_quality\":\"UNSYNCED\",\"source_event\":");
                locker_domain_json_literal(&e,source); locker_domain_json_literal(&e,",\"manual\":false}");
            }
            if (status!=LOCKER_STATUS_OK || !locker_domain_json_writer_ok(&e)) { status=LOCKER_STATUS_IO_ERROR; break; }
            if (include && q->principal.role!=LOCKER_ROLE_ADMIN && !locker_uuid_is_nil(&operation)) {
                domain_operation_t owned;
                locker_status_t loaded=domain_operation_load(core,&operation,0,&owned);
                include=loaded==LOCKER_STATUS_OK && locker_uuid_equal(&owned.actor_id,&q->principal.user_id);
            }
        }
        /* Leave this whole record for the next page if either item or byte budget is full. */
        if (include && (written>=limit || w.length+strlen(object)+1024>60000)) { more=true; break; }
        strcpy(after,key);
        if (include) { if (written) locker_domain_json_literal(&w,","); locker_domain_json_literal(&w,object); ++written; }
        more=true;
    }
    locker_domain_json_literal(&w,"],\"next_cursor\":");
    if (status==LOCKER_STATUS_OK && more) {
        char cursor[256]; snprintf(cursor,sizeof(cursor),"%c%llu:%s",phase,(unsigned long long)core->revision,after);
        locker_domain_json_string(&w,cursor);
    } else locker_domain_json_null(&w);
    domain_response_end(&w,core->revision,&q->request_id);
    if (!locker_domain_json_writer_ok(&w)) status=LOCKER_STATUS_NO_SPACE;
    domain_completion_finish(core,job->completion,&job->completion_token,status,
        status==LOCKER_STATUS_OK ? w.buffer : NULL,status==LOCKER_STATUS_OK ? w.length : 0,core->revision);
    return true;
}
