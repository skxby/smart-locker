#include "journal.h"
#include <string.h>
#include <limits.h>

#define HEADER 384u
#define IMAGE_BYTES (HEADER + 96u * MCU_JOURNAL_MAX_EVENTS + 4u)
typedef struct {
    char device[25];
    mcu_journal_state_t state;
    locker_device_event_t events[MCU_JOURNAL_MAX_EVENTS];
    locker_unlock_intent_t released_intent;
    uint64_t released_boot;
    bool boot_open;
    uint16_t count;
} checkpoint_t;
typedef struct { bool done; locker_status_t status; locker_event_batch_t page; } completion_t;
typedef struct {
    unsigned kind; uint32_t token;
    union { locker_device_event_t event; locker_unlock_intent_t intent;
        mcu_journal_control_request_t control; locker_event_key_t cursor; } data;
    uint8_t limit;
    completion_t *completion;
} job_t;
struct mcu_journal {
    lfs_t fs; mcu_journal_port_t port; mcu_journal_sink_t sink;
    checkpoint_t current, next;
    job_t queue[MCU_JOURNAL_QUEUE_CAPACITY];
    unsigned head, count; uint32_t inflight, last_event_token;
    bool mounted, running, fault, bound;
    uint8_t image[IMAGE_BYTES], file_cache[256];
};
static struct mcu_journal singleton;
static bool same_uuid(const locker_uuid_t *a, const locker_uuid_t *b) { return memcmp(a,b,16u)==0; }
static bool zero_uuid(const locker_uuid_t *a) { static const locker_uuid_t z={{0}}; return same_uuid(a,&z); }
static int order(const locker_event_key_t *a,const locker_event_key_t *b) {
    if(a->boot_id!=b->boot_id) return a->boot_id<b->boot_id?-1:1;
    return a->event_seq==b->event_seq?0:a->event_seq<b->event_seq?-1:1;
}
static bool empty_cursor(const locker_event_key_t *k) { return k->boot_id==0 && k->event_seq==0; }
static void put(uint8_t **p,uint64_t v,unsigned n) { while(n--) { *(*p)++=(uint8_t)v; v>>=8; } }
static uint64_t get(const uint8_t **p,unsigned n) { uint64_t v=0; unsigned i; for(i=0;i<n;i++) v|=(uint64_t)*(*p)++<<(8u*i); return v; }
static void uuid_put(uint8_t **p,const locker_uuid_t *u) { memcpy(*p,u,16); *p+=16; }
static void uuid_get(const uint8_t **p,locker_uuid_t *u) { memcpy(u,*p,16); *p+=16; }
static uint32_t crc32(const uint8_t *p,size_t n) { uint32_t c=UINT32_MAX; unsigned i; while(n--) { c^=*p++; for(i=0;i<8;i++) c=(c>>1)^((0u-(c&1u))&0xedb88320u); } return ~c; }
static void intent_put(uint8_t **p,const locker_unlock_intent_t *i) {
    uuid_put(p,&i->operation_id); uuid_put(p,&i->command_id); uuid_put(p,&i->session_id); uuid_put(p,&i->journal_epoch);
    put(p,i->expected_boot_id,8); put(p,i->expected_physical_revision,8); put(p,i->action_no,8); put(p,i->not_after_mcu_ms,8); put(p,i->slot_id,2); put(p,i->kind,1);
}
static void intent_get(const uint8_t **p,locker_unlock_intent_t *i) {
    uuid_get(p,&i->operation_id); uuid_get(p,&i->command_id); uuid_get(p,&i->session_id); uuid_get(p,&i->journal_epoch);
    i->expected_boot_id=get(p,8); i->expected_physical_revision=get(p,8); i->action_no=get(p,8); i->not_after_mcu_ms=get(p,8); i->slot_id=(uint16_t)get(p,2); i->kind=(locker_operation_kind_t)get(p,1);
}
static void event_put(uint8_t **p,const locker_device_event_t *e) {
    put(p,e->key.boot_id,8); put(p,e->key.event_seq,8); put(p,e->mono_ms,8); put(p,e->event_type,2); put(p,e->reason,2);
    uuid_put(p,&e->operation_id); uuid_put(p,&e->command_id); put(p,e->door,1); put(p,e->item,1); put(p,e->flags,2);
    put(p,e->supply_mv,4); put(p,e->physical_revision,8); put(p,e->detail,8); put(p,e->card_uid_len,1); memcpy(*p,e->card_uid,10); *p+=10; put(p,0,1);
}
static void key_identity(checkpoint_t *c,locker_event_key_t *k) { memcpy(k->device_id,c->device,25); k->journal_epoch=c->state.journal_epoch; }
static void event_get(const uint8_t **p,locker_device_event_t *e,checkpoint_t *c) {
    memset(e,0,sizeof(*e)); key_identity(c,&e->key);
    e->key.boot_id=get(p,8); e->key.event_seq=get(p,8); e->mono_ms=get(p,8); e->event_type=(uint16_t)get(p,2); e->reason=(uint16_t)get(p,2);
    uuid_get(p,&e->operation_id); uuid_get(p,&e->command_id); e->door=(locker_door_state_t)get(p,1); e->item=(locker_item_state_t)get(p,1); e->flags=(uint16_t)get(p,2);
    e->supply_mv=(uint32_t)get(p,4); e->physical_revision=get(p,8); e->detail=get(p,8); e->card_uid_len=(uint8_t)get(p,1); memcpy(e->card_uid,*p,10); *p+=11;
}
static void cursors(checkpoint_t *c) {
    key_identity(c,&c->state.first_available); key_identity(c,&c->state.last_committed); key_identity(c,&c->state.ack_persisted);
    if(c->count) { c->state.first_available=c->events[0].key; c->state.last_committed=c->events[c->count-1u].key; }
}
static void encode(mcu_journal_t *j,checkpoint_t *c) {
    uint8_t *p=j->image; unsigned i; memset(p,0,IMAGE_BYTES);
    memcpy(p,"LKRJNL01",8); p+=8; put(&p,1,4); memcpy(p,c->device,25); p+=25;
    uuid_put(&p,&c->state.journal_epoch); put(&p,c->state.boot_id,8); put(&p,c->state.action_watermark,8); put(&p,c->state.dropped_events,8);
    put(&p,c->state.ack_persisted.boot_id,8); put(&p,c->state.ack_persisted.event_seq,8);
    put(&p,c->state.has_active,1); put(&p,c->state.command_state,1); put(&p,c->count,2); intent_put(&p,&c->state.active_intent);
    intent_put(&p,&c->released_intent); put(&p,c->released_boot,8);
    put(&p,c->state.continuity_ok,1); put(&p,c->boot_open,1);
    p=j->image+HEADER; for(i=0;i<c->count;i++) event_put(&p,&c->events[i]);
    p=j->image+IMAGE_BYTES-4; put(&p,crc32(j->image,IMAGE_BYTES-4),4);
}
static bool valid_event(const locker_device_event_t *e) {
    return e->event_type>=1 && e->event_type<=15 && (unsigned)e->door<=LOCKER_DOOR_STATE_OPEN && (unsigned)e->item<=LOCKER_ITEM_STATE_PRESENT &&
        (e->flags&~31u)==0 && (e->card_uid_len==0 || e->card_uid_len==4 || e->card_uid_len==7 || e->card_uid_len==10);
}
static bool stored_intent_valid(const locker_unlock_intent_t *i,const checkpoint_t *c) {
    return !zero_uuid(&i->operation_id) && !zero_uuid(&i->command_id) && !zero_uuid(&i->session_id) &&
        same_uuid(&i->journal_epoch,&c->state.journal_epoch) && i->expected_boot_id>0 && i->expected_boot_id<=c->state.boot_id &&
        i->action_no>0 && i->action_no<=c->state.action_watermark && i->slot_id==1 &&
        (i->kind==LOCKER_OPERATION_KIND_BORROW || i->kind==LOCKER_OPERATION_KIND_RETURN);
}
static bool decode(mcu_journal_t *j,checkpoint_t *c) {
    const uint8_t *p=j->image+IMAGE_BYTES-4; unsigned i; bool ack_found=false, accepted_found=false; uint64_t crc=get(&p,4); p=j->image;
    if(crc!=crc32(p,IMAGE_BYTES-4) || memcmp(p,"LKRJNL01",8)) return false;
    p+=8; if(get(&p,4)!=1) return false; memset(c,0,sizeof(*c)); memcpy(c->device,p,25); p+=25;
    if(!c->device[0] || c->device[24]) return false;
    uuid_get(&p,&c->state.journal_epoch); if(zero_uuid(&c->state.journal_epoch)) return false;
    c->state.boot_id=get(&p,8); c->state.action_watermark=get(&p,8); c->state.dropped_events=get(&p,8);
    c->state.ack_persisted.boot_id=get(&p,8); c->state.ack_persisted.event_seq=get(&p,8);
    { uint64_t active=get(&p,1); if(active>1) return false; c->state.has_active=active!=0; }
    c->state.command_state=(locker_command_state_t)get(&p,1); c->count=(uint16_t)get(&p,2); intent_get(&p,&c->state.active_intent);
    intent_get(&p,&c->released_intent); c->released_boot=get(&p,8);
    { uint64_t continuity=get(&p,1), open=get(&p,1);
      if(continuity>1 || open>1) return false;
      c->state.continuity_ok=continuity!=0; c->boot_open=open!=0; }
    if(c->count>MCU_JOURNAL_MAX_EVENTS || c->state.command_state>LOCKER_COMMAND_STATE_UNKNOWN_RESULT || (c->state.dropped_events && c->state.continuity_ok)) return false;
    if((c->state.boot_id==0)!=(c->count==0) || (c->state.ack_persisted.boot_id==0)!=(c->state.ack_persisted.event_seq==0)) return false;
    ack_found=empty_cursor(&c->state.ack_persisted);
    p=j->image+HEADER;
    for(i=0;i<c->count;i++) {
        event_get(&p,&c->events[i],c);
        if(!valid_event(&c->events[i]) || !c->events[i].key.boot_id || !c->events[i].key.event_seq || c->events[i].key.boot_id>c->state.boot_id) return false;
        if(i && (order(&c->events[i-1].key,&c->events[i].key)>=0 ||
            (c->events[i-1].key.boot_id==c->events[i].key.boot_id && c->events[i].key.event_seq!=c->events[i-1].key.event_seq+1))) return false;
        if((!i || c->events[i-1].key.boot_id!=c->events[i].key.boot_id) &&
           (c->events[i].key.event_seq!=1 || c->events[i].event_type!=LOCKER_WIRE_DEVICE_EVENT_TYPE_BOOT)) return false;
        if(i && c->events[i-1].key.boot_id!=c->events[i].key.boot_id && c->events[i].key.boot_id!=c->events[i-1].key.boot_id+1) return false;
        if(order(&c->events[i].key,&c->state.ack_persisted)==0) ack_found=true;
        if(c->events[i].event_type==LOCKER_WIRE_DEVICE_EVENT_TYPE_COMMAND_ACCEPTED &&
            same_uuid(&c->events[i].command_id,&c->state.active_intent.command_id) && same_uuid(&c->events[i].operation_id,&c->state.active_intent.operation_id) &&
            c->events[i].detail==c->state.action_watermark) accepted_found=true;
    }
    if(c->count && (c->events[0].key.boot_id!=1 || c->events[c->count-1].key.boot_id!=c->state.boot_id)) return false;
    cursors(c);
    if(!ack_found) return false;
    if(c->state.has_active) {
        if(!stored_intent_valid(&c->state.active_intent,c) || c->state.active_intent.action_no!=c->state.action_watermark || c->state.command_state==LOCKER_COMMAND_STATE_NONE || !accepted_found) return false;
    } else {
        uint8_t active[99],*ap=active; unsigned k;
        intent_put(&ap,&c->state.active_intent);
        for(k=0;k<sizeof(active);k++) if(active[k]) return false;
        if(c->state.command_state!=LOCKER_COMMAND_STATE_NONE) return false;
    }
    if(!zero_uuid(&c->released_intent.command_id) && (!stored_intent_valid(&c->released_intent,c) || !c->released_boot || c->released_boot>c->state.boot_id)) return false;
    c->state.journal_state=c->state.continuity_ok?LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY:LOCKER_WIRE_WIRE_JOURNAL_STATE_DEGRADED; return true;
}
static locker_status_t io_status(int rc) { return rc==LFS_ERR_NOSPC?LOCKER_STATUS_NO_SPACE:LOCKER_STATUS_IO_ERROR; }
static locker_status_t persist(mcu_journal_t *j) {
    lfs_file_t f; struct lfs_file_config cfg={0}; int rc,close_rc;
    cfg.buffer=j->file_cache; encode(j,&j->next);
    rc=lfs_file_opencfg(&j->fs,&f,"staging",LFS_O_WRONLY|LFS_O_CREAT|LFS_O_TRUNC,&cfg);
    if(rc<0) return io_status(rc);
    rc=(int)lfs_file_write(&j->fs,&f,j->image,IMAGE_BYTES);
    if(rc==(int)IMAGE_BYTES) rc=lfs_file_sync(&j->fs,&f); else if(rc>=0) rc=LFS_ERR_IO;
    close_rc=lfs_file_close(&j->fs,&f); if(rc>=0) rc=close_rc;
    if(rc>=0) rc=lfs_rename(&j->fs,"staging","journal");
    if(rc<0) return io_status(rc);
    j->current=j->next; return LOCKER_STATUS_OK;
}
static void fault(mcu_journal_t *j,locker_status_t s) {
    j->fault=true; j->current.state.continuity_ok=false;
    j->current.state.journal_state=s==LOCKER_STATUS_NO_SPACE?LOCKER_WIRE_WIRE_JOURNAL_STATE_FULL:LOCKER_WIRE_WIRE_JOURNAL_STATE_FAULT;
}
static bool geometry(const mcu_journal_config_t *c) { return c && c->geometry_verified && c->jedec_id==0xef4018u && c->capacity_bytes==16777216u && c->sector_bytes==4096u && c->page_bytes==256 && c->chip_select==0; }
locker_status_t mcu_journal_attach(const mcu_journal_port_t *p) {
    if(singleton.mounted || !p || !p->fs || !p->lock || !p->unlock || !p->wait || !p->is_owner ||
        !p->fs->read || !p->fs->prog || !p->fs->erase || !p->fs->sync ||
        p->fs->cache_size!=256 || p->fs->prog_size!=256 || p->fs->read_size!=16 ||
        p->fs->block_size!=4096 || p->fs->block_count!=4096 ||
        !p->fs->read_buffer || !p->fs->prog_buffer || !p->fs->lookahead_buffer) return LOCKER_STATUS_INVALID;
    memset(&singleton,0,sizeof(singleton)); singleton.port=*p; return LOCKER_STATUS_OK;
}
locker_status_t mcu_journal_provision(const mcu_journal_config_t *config,const locker_uuid_t *epoch,const char *device) {
    mcu_journal_t *j=&singleton; locker_status_t s; size_t n;
    if(!geometry(config) || !epoch || zero_uuid(epoch) || !device || !j->port.fs || j->mounted || !j->port.is_owner(j->port.user)) return LOCKER_STATUS_INVALID;
    n=strlen(device); if(n==0 || n>24) return LOCKER_STATUS_INVALID;
    if(lfs_format(&j->fs,j->port.fs)<0 || lfs_mount(&j->fs,j->port.fs)<0) return LOCKER_STATUS_IO_ERROR;
    j->mounted=true; memset(&j->next,0,sizeof(j->next)); memcpy(j->next.device,device,n+1); j->next.state.journal_epoch=*epoch;
    j->next.state.continuity_ok=true; j->next.state.journal_state=LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
    s=persist(j); (void)lfs_unmount(&j->fs); j->mounted=false; return s;
}
static locker_status_t add_event(mcu_journal_t *j, const locker_device_event_t *event) {
    checkpoint_t *c=&j->next; locker_device_event_t e=*event; uint64_t seq=1;
    if(c->count==MCU_JOURNAL_MAX_EVENTS) return LOCKER_STATUS_NO_SPACE;
    if(c->count && c->events[c->count-1].key.boot_id==c->state.boot_id) {
        seq=c->events[c->count-1].key.event_seq; if(seq==UINT64_MAX) return LOCKER_STATUS_NO_SPACE; ++seq;
    }
    key_identity(c,&e.key); e.key.boot_id=c->state.boot_id; e.key.event_seq=seq; e.live_transport=false;
    c->events[c->count++]=e; cursors(c); return LOCKER_STATUS_OK;
}
locker_status_t mcu_journal_init(const mcu_journal_config_t *config,mcu_journal_t **out) {
    mcu_journal_t *j=&singleton; lfs_file_t f; struct lfs_file_config cfg={0}; int rc,cl; locker_status_t s; locker_device_event_t boot={0};
    if(out) *out=NULL;
    if(!out || !geometry(config) || !j->port.fs || j->mounted || !j->port.is_owner(j->port.user)) return LOCKER_STATUS_INVALID;
    if(lfs_mount(&j->fs,j->port.fs)<0) return LOCKER_STATUS_IO_ERROR;
    j->mounted=true; cfg.buffer=j->file_cache;
    rc=lfs_file_opencfg(&j->fs,&f,"journal",LFS_O_RDONLY,&cfg);
    if(rc<0) { (void)mcu_journal_shutdown(j); return LOCKER_STATUS_IO_ERROR; }
    rc=lfs_file_size(&j->fs,&f)==IMAGE_BYTES?(int)lfs_file_read(&j->fs,&f,j->image,IMAGE_BYTES):LFS_ERR_CORRUPT;
    cl=lfs_file_close(&j->fs,&f);
    if(rc!=(int)IMAGE_BYTES || cl<0 || !decode(j,&j->current)) { (void)mcu_journal_shutdown(j); return LOCKER_STATUS_IO_ERROR; }
    j->next=j->current;
    if(j->next.state.boot_id==UINT64_MAX) { (void)mcu_journal_shutdown(j); return LOCKER_STATUS_NO_SPACE; }
    ++j->next.state.boot_id;
    if(j->next.boot_open) {
        /* A sudden reset cannot prove that no observed fact was in flight.
           This epoch-wide latch survives every later clean mount/session. */
        j->next.state.continuity_ok=false;
        j->next.state.dropped_events=UINT64_MAX;
        j->next.state.journal_state=LOCKER_WIRE_WIRE_JOURNAL_STATE_DEGRADED;
    }
    j->next.boot_open=true;
    if(j->next.state.has_active && (j->next.state.command_state==LOCKER_COMMAND_STATE_ACCEPTED || j->next.state.command_state==LOCKER_COMMAND_STATE_PULSE_STARTED))
        j->next.state.command_state=LOCKER_COMMAND_STATE_UNKNOWN_RESULT;
    boot.event_type=LOCKER_WIRE_DEVICE_EVENT_TYPE_BOOT;
    s=add_event(j,&boot); if(s==LOCKER_STATUS_OK) s=persist(j);
    if(s!=LOCKER_STATUS_OK) { (void)mcu_journal_shutdown(j); return s; }
    j->running=true; j->fault=false; j->head=j->count=0; j->inflight=j->last_event_token=0; j->bound=false;
    if(j->current.count>=MCU_JOURNAL_MAX_EVENTS-MCU_JOURNAL_RESERVE_EVENTS)
        j->current.state.journal_state=LOCKER_WIRE_WIRE_JOURNAL_STATE_FULL;
    *out=j; return LOCKER_STATUS_OK;
}
locker_status_t mcu_journal_shutdown(mcu_journal_t *j) {
    locker_status_t result=LOCKER_STATUS_OK; int rc;
    if(j!=&singleton || !j->port.is_owner(j->port.user) || j->count) return LOCKER_STATUS_BUSY;
    if(j->running && !j->fault) {
        j->next=j->current; j->next.boot_open=false;
        result=persist(j); if(result!=LOCKER_STATUS_OK) fault(j,result);
    }
    if(j->mounted) { rc=lfs_unmount(&j->fs); if(rc<0 && result==LOCKER_STATUS_OK) result=io_status(rc); }
    j->mounted=j->running=false; return result;
}
locker_status_t mcu_journal_bind_sink(mcu_journal_t *j,const mcu_journal_sink_t *s) {
    if(j!=&singleton || !j->running || j->bound || !s || !s->committed || !s->event_committed || !s->control_result) return LOCKER_STATUS_INVALID;
    j->sink=*s; j->bound=true; return LOCKER_STATUS_OK;
}
static locker_status_t enqueue(mcu_journal_t *j,const job_t *job) {
    unsigned i; locker_status_t s=LOCKER_STATUS_QUEUED;
    if(j!=&singleton || !j->running || !j->bound) return LOCKER_STATUS_NOT_READY;
    j->port.lock(j->port.user);
    if(j->count==MCU_JOURNAL_QUEUE_CAPACITY) s=LOCKER_STATUS_BUSY;
    if(job->token && j->inflight==job->token) s=LOCKER_STATUS_CONFLICT;
    for(i=0;i<j->count;i++) if(job->token && j->queue[(j->head+i)%MCU_JOURNAL_QUEUE_CAPACITY].token==job->token) s=LOCKER_STATUS_CONFLICT;
    if(s==LOCKER_STATUS_QUEUED) { j->queue[(j->head+j->count)%MCU_JOURNAL_QUEUE_CAPACITY]=*job; ++j->count; }
    j->port.unlock(j->port.user); return s;
}
locker_status_t mcu_journal_append(mcu_journal_t *j,const locker_device_event_t *e,uint32_t token) {
    job_t job={0}; if(!e || !token || !valid_event(e)) return LOCKER_STATUS_INVALID;
    job.kind=0; job.token=token; job.data.event=*e; return enqueue(j,&job);
}
locker_status_t mcu_journal_commit_intent(mcu_journal_t *j,const locker_unlock_intent_t *i,uint32_t token) {
    job_t job={0}; if(!i || !token) return LOCKER_STATUS_INVALID;
    job.kind=1; job.token=token; job.data.intent=*i; return enqueue(j,&job);
}
locker_status_t mcu_journal_control(mcu_journal_t *j,const mcu_journal_control_request_t *r) {
    job_t job={0}; if(!r || !r->token || r->kind>MCU_JOURNAL_RELEASE) return LOCKER_STATUS_INVALID;
    job.kind=2; job.token=r->token; job.data.control=*r; return enqueue(j,&job);
}
static bool cursor_identity(checkpoint_t *c,const locker_event_key_t *k) { return same_uuid(&k->journal_epoch,&c->state.journal_epoch) && memcmp(k->device_id,c->device,25)==0; }
static locker_status_t read_owner(mcu_journal_t *j,const locker_event_key_t *after,uint8_t limit,locker_event_batch_t *r) {
    unsigned i; checkpoint_t *c=&j->current; bool found=empty_cursor(after);
    memset(r,0,sizeof(*r));
    if(!cursor_identity(c,after) || (after->boot_id==0)!=(after->event_seq==0)) return LOCKER_STATUS_CONFLICT;
    r->first_available=c->state.first_available; r->last_committed=c->state.last_committed; r->next_after=*after;
    for(i=0;i<c->count;i++) if(order(&c->events[i].key,after)==0) found=true;
    r->gap_before_page=!found;
    for(i=0;i<c->count;i++) if(order(&c->events[i].key,after)>0) {
        if(r->count==limit) { r->has_more=true; break; }
        r->events[r->count++]=c->events[i]; r->next_after=c->events[i].key;
    }
    return LOCKER_STATUS_OK;
}
static locker_status_t ack_owner(mcu_journal_t *j,const locker_event_key_t *k) {
    checkpoint_t *c=&j->current; unsigned i; bool found=false;
    if(!cursor_identity(c,k) || !k->boot_id || !k->event_seq) return LOCKER_STATUS_CONFLICT;
    if(order(k,&c->state.ack_persisted)<=0) return LOCKER_STATUS_OK;
    for(i=0;i<c->count;i++) if(order(k,&c->events[i].key)==0) found=true;
    if(!found) return LOCKER_STATUS_CONFLICT;
    j->next=*c; j->next.state.ack_persisted=*k;
    /* Minimal conservative policy: no event deletion in S2-04. Stage4 owns
       bounded ACK-only reclamation; full media must fail closed. */
    return persist(j);
}
static locker_status_t sync_job(mcu_journal_t *j,job_t *job,locker_event_batch_t *r) {
    completion_t completion={0}; locker_status_t s; bool done;
    if(j!=&singleton || !j->running || !j->bound) return LOCKER_STATUS_NOT_READY;
    job->completion=&completion; s=enqueue(j,job); if(s!=LOCKER_STATUS_QUEUED) return s;
    do {
        if(j->port.is_owner(j->port.user)) (void)mcu_journal_process_one(j); else j->port.wait(j->port.user);
        j->port.lock(j->port.user); done=completion.done; j->port.unlock(j->port.user);
    } while(!done);
    if(r) *r=completion.page;
    return completion.status;
}
locker_status_t mcu_journal_read_page(mcu_journal_t *j,const locker_event_key_t *after,uint8_t limit,locker_event_batch_t *r) {
    job_t job={0}; if(!after || !r || !limit || limit>4) return LOCKER_STATUS_INVALID;
    job.kind=3; job.data.cursor=*after; job.limit=limit; return sync_job(j,&job,r);
}
locker_status_t mcu_journal_ack(mcu_journal_t *j,const locker_event_key_t *k) {
    job_t job={0}; if(!k) return LOCKER_STATUS_INVALID; job.kind=4; job.data.cursor=*k; return sync_job(j,&job,NULL);
}
static bool matches(const mcu_journal_control_request_t *r,const checkpoint_t *c) {
    const locker_unlock_intent_t *i=&c->state.active_intent;
    return same_uuid(&r->journal_epoch,&c->state.journal_epoch) && r->expected_boot_id==c->state.boot_id &&
        same_uuid(&r->session_id,&i->session_id) && same_uuid(&r->operation_id,&i->operation_id) && same_uuid(&r->command_id,&i->command_id) && r->action_no==i->action_no;
}
static locker_status_t apply(mcu_journal_t *j,const job_t *job) {
    locker_status_t s; checkpoint_t *c=&j->next; locker_device_event_t e={0}; const locker_unlock_intent_t *i;
    j->next=j->current;
    if(job->kind==3) return read_owner(j,&job->data.cursor,job->limit,&job->completion->page);
    if(job->kind==2 && job->data.control.kind==MCU_JOURNAL_RECOVER) return LOCKER_STATUS_OK;
    if(j->fault) return LOCKER_STATUS_NOT_READY;
    if(job->kind==4) return ack_owner(j,&job->data.cursor);
    if(job->kind==0) {
        const locker_device_event_t *fact=&job->data.event;
        if(fact->event_type==LOCKER_WIRE_DEVICE_EVENT_TYPE_BOOT || fact->event_type==LOCKER_WIRE_DEVICE_EVENT_TYPE_COMMAND_ACCEPTED || fact->event_type==LOCKER_WIRE_DEVICE_EVENT_TYPE_CONTEXT_RELEASED)
            return LOCKER_STATUS_INVALID;
        if(fact->event_type==LOCKER_WIRE_DEVICE_EVENT_TYPE_PULSE_STARTED || fact->event_type==LOCKER_WIRE_DEVICE_EVENT_TYPE_PULSE_FINISHED) {
            if(!c->state.has_active || !same_uuid(&fact->command_id,&c->state.active_intent.command_id) || !same_uuid(&fact->operation_id,&c->state.active_intent.operation_id)) return LOCKER_STATUS_CONFLICT;
            if(fact->event_type==LOCKER_WIRE_DEVICE_EVENT_TYPE_PULSE_STARTED) {
                if(c->state.command_state!=LOCKER_COMMAND_STATE_ACCEPTED) return LOCKER_STATUS_CONFLICT;
                c->state.command_state=LOCKER_COMMAND_STATE_PULSE_STARTED;
            } else {
                if(c->state.command_state!=LOCKER_COMMAND_STATE_PULSE_STARTED) return LOCKER_STATUS_CONFLICT;
                c->state.command_state=LOCKER_COMMAND_STATE_PULSE_FINISHED;
            }
        }
        s=add_event(j,fact); if(s!=LOCKER_STATUS_OK) return s;
    }
    if(job->kind==1) {
        i=&job->data.intent;
        if(!c->state.continuity_ok || c->state.dropped_events) return LOCKER_STATUS_NOT_READY;
        if(!same_uuid(&i->journal_epoch,&c->state.journal_epoch) || i->expected_boot_id!=c->state.boot_id || zero_uuid(&i->session_id) || zero_uuid(&i->operation_id) || zero_uuid(&i->command_id) || i->slot_id!=1 || (i->kind!=LOCKER_OPERATION_KIND_BORROW && i->kind!=LOCKER_OPERATION_KIND_RETURN)) return LOCKER_STATUS_CONFLICT;
        if(c->state.has_active) {
            uint8_t a[99],b[99],*ap=a,*bp=b; intent_put(&ap,i); intent_put(&bp,&c->state.active_intent);
            return memcmp(a,b,99)==0?LOCKER_STATUS_OK:LOCKER_STATUS_BUSY;
        }
        if(i->action_no<=c->state.action_watermark) return LOCKER_STATUS_REPLAY_BLOCKED;
        if(c->state.action_watermark==UINT64_MAX || i->action_no!=c->state.action_watermark+1) return LOCKER_STATUS_CONFLICT;
        if(c->count>=MCU_JOURNAL_MAX_EVENTS-MCU_JOURNAL_RESERVE_EVENTS) return LOCKER_STATUS_NO_SPACE;
        c->state.active_intent=*i; c->state.action_watermark=i->action_no; c->state.has_active=true; c->state.command_state=LOCKER_COMMAND_STATE_ACCEPTED;
        e.event_type=LOCKER_WIRE_DEVICE_EVENT_TYPE_COMMAND_ACCEPTED; e.operation_id=i->operation_id; e.command_id=i->command_id; e.detail=i->action_no;
        s=add_event(j,&e); if(s!=LOCKER_STATUS_OK) return s;
    }
    if(job->kind==2) {
        const mcu_journal_control_request_t *r=&job->data.control;
        if(r->kind==MCU_JOURNAL_BARRIER) return (!r->barrier_after_token || r->barrier_after_token==j->last_event_token)?LOCKER_STATUS_OK:LOCKER_STATUS_CONFLICT;
        if(!c->state.has_active) {
            const locker_unlock_intent_t *released=&c->released_intent;
            if(r->kind==MCU_JOURNAL_RELEASE && !zero_uuid(&released->command_id) &&
                same_uuid(&r->journal_epoch,&c->state.journal_epoch) && r->expected_boot_id==c->released_boot &&
                same_uuid(&r->session_id,&released->session_id) && same_uuid(&r->operation_id,&released->operation_id) &&
                same_uuid(&r->command_id,&released->command_id) && r->action_no==released->action_no) return LOCKER_STATUS_OK;
            return LOCKER_STATUS_CONFLICT;
        }
        if(!matches(r,c)) return LOCKER_STATUS_CONFLICT;
        if(r->kind==MCU_JOURNAL_FINALIZE) {
            if(r->terminal_state!=LOCKER_COMMAND_STATE_REJECTED && r->terminal_state!=LOCKER_COMMAND_STATE_PULSE_FINISHED && r->terminal_state!=LOCKER_COMMAND_STATE_UNKNOWN_RESULT) return LOCKER_STATUS_INVALID;
            if(c->state.command_state==LOCKER_COMMAND_STATE_PULSE_STARTED) {
                if(r->terminal_state!=LOCKER_COMMAND_STATE_UNKNOWN_RESULT) return LOCKER_STATUS_CONFLICT;
            } else if(c->state.command_state!=LOCKER_COMMAND_STATE_ACCEPTED && c->state.command_state!=LOCKER_COMMAND_STATE_UNKNOWN_RESULT && c->state.command_state!=r->terminal_state) return LOCKER_STATUS_CONFLICT;
            c->state.command_state=r->terminal_state;
        } else {
            if(c->state.command_state==LOCKER_COMMAND_STATE_ACCEPTED || c->state.command_state==LOCKER_COMMAND_STATE_PULSE_STARTED) return LOCKER_STATUS_CONFLICT;
            e.event_type=LOCKER_WIRE_DEVICE_EVENT_TYPE_CONTEXT_RELEASED; e.operation_id=c->state.active_intent.operation_id; e.command_id=c->state.active_intent.command_id;
            s=add_event(j,&e); if(s!=LOCKER_STATUS_OK) return s;
            c->released_intent=c->state.active_intent; c->released_boot=c->state.boot_id;
            c->state.has_active=false; memset(&c->state.active_intent,0,sizeof(c->state.active_intent)); c->state.command_state=LOCKER_COMMAND_STATE_NONE;
        }
    }
    return persist(j);
}
locker_status_t mcu_journal_process_one(mcu_journal_t *j) {
    job_t job; locker_status_t s,cb=LOCKER_STATUS_OK; bool event_added; uint16_t previous_count;
    if(j!=&singleton || !j->running || !j->port.is_owner(j->port.user)) return LOCKER_STATUS_NOT_READY;
    j->port.lock(j->port.user);
    if(!j->count) { j->port.unlock(j->port.user); return LOCKER_STATUS_NOT_READY; }
    job=j->queue[j->head]; j->head=(j->head+1)%MCU_JOURNAL_QUEUE_CAPACITY; --j->count; j->inflight=job.token;
    j->port.unlock(j->port.user);
    previous_count=j->current.count;
    s=apply(j,&job);
    if(job.kind==0 && (s==LOCKER_STATUS_IO_ERROR || s==LOCKER_STATUS_NO_SPACE || s==LOCKER_STATUS_NOT_READY)) {
        if(j->current.state.dropped_events!=UINT64_MAX) ++j->current.state.dropped_events;
    }
    if(s==LOCKER_STATUS_IO_ERROR || (s==LOCKER_STATUS_NO_SPACE && !(job.kind==1 && previous_count>=MCU_JOURNAL_MAX_EVENTS-MCU_JOURNAL_RESERVE_EVENTS))) fault(j,s);
    event_added=s==LOCKER_STATUS_OK && j->current.count>previous_count;
    if(j->current.count>=MCU_JOURNAL_MAX_EVENTS-MCU_JOURNAL_RESERVE_EVENTS && !j->fault)
        j->current.state.journal_state=LOCKER_WIRE_WIRE_JOURNAL_STATE_FULL;
    if(job.kind==0 && s==LOCKER_STATUS_OK) j->last_event_token=job.token;
    if(job.completion) {
        j->port.lock(j->port.user); job.completion->status=s; job.completion->done=true; j->port.unlock(j->port.user);
    } else {
        if(job.kind==2) cb=j->sink.control_result(j->sink.user,job.token,s,&j->current.state);
        else cb=j->sink.committed(j->sink.user,job.token,s);
        if(event_added && j->current.count) {
            locker_device_event_t event=j->current.events[j->current.count-1]; event.live_transport=true;
            if(j->sink.event_committed(j->sink.user,&event)!=LOCKER_STATUS_OK) cb=LOCKER_STATUS_BUSY;
        }
        if(cb!=LOCKER_STATUS_OK && cb!=LOCKER_STATUS_QUEUED && cb!=LOCKER_STATUS_ACCEPTED) fault(j,LOCKER_STATUS_IO_ERROR);
    }
    j->port.lock(j->port.user); j->inflight=0; j->port.unlock(j->port.user); return s;
}
