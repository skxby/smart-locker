#include "control.h"
#include "inputs.h"
#include "timebase.h"
#include <string.h>

#define PENDING_COUNT 16u
enum { EVENT=1, RECOVER, SNAPSHOT, INTENT, FINALIZE, RELEASE, RESUME };
typedef struct {
    uint32_t token;
    uint8_t kind;
    mcu_request_context_t request;
    locker_unlock_intent_t intent;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_resume_ready_request_t resume;
} pending_t;
struct mcu_control {
    mcu_control_config_t config;
    mcu_journal_t *journal;
    mcu_control_sink_t sink;
    mcu_journal_state_t state;
    locker_board_inputs_t inputs;
    input_debounce_t door, item;
    locker_uuid_t session, host;
    uint64_t revision, now, last_sample, progress, progress_time, barrier_revision;
    locker_wire_cursor_t barrier_cursor;
    uint32_t next_token, last_event_token, health_age;
    uint8_t mode;
    bool recovered, bound, failed, session_valid, health_known, barrier_valid;
    bool executing, command_pending;
    pending_t pending[PENDING_COUNT];
};
static mcu_control_t singleton;
static bool eq(const locker_uuid_t *a, const locker_uuid_t *b)
{ return memcmp(a->bytes,b->bytes,16u)==0; }
static bool nil(const locker_uuid_t *a)
{ static const locker_uuid_t zero={{0}}; return eq(a,&zero); }
static locker_wire_cursor_t cursor(const locker_event_key_t *k)
{ locker_wire_cursor_t c={k->boot_id,k->event_seq}; return c; }
static int order(locker_wire_cursor_t a, locker_wire_cursor_t b)
{ return a.boot_id!=b.boot_id ? (a.boot_id>b.boot_id?1:-1) : (a.event_seq>b.event_seq?1:a.event_seq<b.event_seq?-1:0); }
void mcu_control_fault(mcu_control_t *c)
{
    if(c==NULL) return;
    c->failed=true; c->mode=LOCKER_WIRE_WIRE_DEVICE_MODE_DEGRADED;
    mcu_board_force_off();
}
void mcu_control_session_lost(mcu_control_t *c)
{
    c->session_valid=false; c->health_known=false; c->barrier_valid=false;
    if(!c->failed) c->mode=LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING;
    /* Revocation also prevents an in-flight commit from starting a pulse. */
    mcu_board_force_off();
}
static bool healthy(const mcu_control_t *c)
{ return c->recovered && !c->failed && c->state.continuity_ok && !c->state.dropped_events &&
    c->state.journal_state==LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY; }
static bool fresh(const mcu_control_t *c)
{ return c->health_known && c->progress!=0 && c->now>=c->progress_time &&
    c->now-c->progress_time<LOCKER_HOST_TIMEOUT_MS &&
    c->health_age<LOCKER_HOST_TIMEOUT_MS-(c->now-c->progress_time); }
static bool physical(const mcu_control_t *c)
{ return c->inputs.door==LOCKER_DOOR_STATE_CLOSED && c->inputs.item!=LOCKER_ITEM_STATE_UNKNOWN &&
    c->inputs.supply_valid && c->now>=c->last_sample && c->now-c->last_sample<=100u; }
static pending_t *allocate(mcu_control_t *c, uint8_t kind, const mcu_request_context_t *r)
{
    unsigned i;
    if(c->next_token==UINT32_MAX) { mcu_control_fault(c); return NULL; }
    for(i=0;i<PENDING_COUNT;i++) if(!c->pending[i].token) {
        pending_t *p=&c->pending[i]; memset(p,0,sizeof(*p));
        p->token=++c->next_token; p->kind=kind; if(r) p->request=*r; return p;
    }
    mcu_control_fault(c); return NULL;
}
static void base_reply(locker_device_reply_t *reply,const mcu_request_context_t *r,uint16_t result)
{
    memset(reply,0,sizeof(*reply)); reply->request_no=r->request_no; reply->message_type=r->message_type;
    /* Every response starts with these two fields in the generated native DTO. */
    reply->body.hello.session_id=r->session_id; reply->body.hello.result=result;
}
static locker_status_t send(mcu_control_t *c, const locker_device_reply_t *r)
{
    locker_status_t s=c->sink.reply(c->sink.user,r);
    if(s!=LOCKER_STATUS_OK && s!=LOCKER_STATUS_QUEUED && s!=LOCKER_STATUS_ACCEPTED) mcu_control_fault(c);
    return LOCKER_STATUS_QUEUED;
}
static locker_status_t error(mcu_control_t *c,const mcu_request_context_t *r,uint16_t result)
{ locker_device_reply_t reply; base_reply(&reply,r,result); return send(c,&reply); }
static uint16_t flags(const mcu_control_t *c)
{
    uint16_t f=0;
    if(c->inputs.supply_valid) f|=LOCKER_WIRE_FLAG_SUPPLY_VALID;
    if(c->inputs.door && c->inputs.item) f|=LOCKER_WIRE_FLAG_PHYSICAL_STABLE;
    if(!c->state.continuity_ok || c->state.dropped_events) f|=LOCKER_WIRE_FLAG_HISTORY_INCOMPLETE;
    if(!healthy(c)) f|=LOCKER_WIRE_FLAG_JOURNAL_DEGRADED;
    if(board_lock_pulse_active()) f|=LOCKER_WIRE_FLAG_PULSE_ACTIVE;
    return f;
}
static locker_status_t append(mcu_control_t *c,uint16_t type,uint64_t detail)
{
    locker_device_event_t e={0}; pending_t *p=allocate(c,EVENT,NULL); locker_status_t s;
    if(!p) return LOCKER_STATUS_NO_SPACE;
    e.mono_ms=c->now; e.physical_revision=c->revision; e.event_type=type; e.detail=detail;
    e.door=c->inputs.door; e.item=c->inputs.item; e.supply_mv=c->inputs.supply_valid?c->inputs.supply_mv:UINT32_MAX;
    e.flags=flags(c);
    if(c->state.has_active) { e.operation_id=c->state.active_intent.operation_id; e.command_id=c->state.active_intent.command_id; }
    s=mcu_journal_append(c->journal,&e,p->token);
    if(s!=LOCKER_STATUS_QUEUED) { p->token=0; mcu_control_fault(c); }
    else c->last_event_token=p->token;
    return s;
}
locker_status_t mcu_control_observe_card(mcu_control_t *c,const uint8_t *uid,uint8_t length)
{
    pending_t *p; locker_device_event_t e={0}; locker_status_t s;
    if(!uid || (length!=4u && length!=7u && length!=10u)) return LOCKER_STATUS_INVALID;
    if(!c->recovered) return LOCKER_STATUS_NOT_READY;
    p=allocate(c,EVENT,NULL); if(!p) return LOCKER_STATUS_NO_SPACE;
    c->now=mcu_board_monotonic_ms(); e.mono_ms=c->now; e.physical_revision=c->revision;
    e.event_type=LOCKER_WIRE_DEVICE_EVENT_TYPE_CARD_OBSERVED; e.card_uid_len=length; memcpy(e.card_uid,uid,length);
    e.door=c->inputs.door; e.item=c->inputs.item; e.flags=flags(c);
    e.supply_mv=c->inputs.supply_valid?c->inputs.supply_mv:UINT32_MAX;
    if(c->state.has_active) { e.operation_id=c->state.active_intent.operation_id; e.command_id=c->state.active_intent.command_id; }
    s=mcu_journal_append(c->journal,&e,p->token);
    if(s==LOCKER_STATUS_QUEUED) c->last_event_token=p->token;
    else { p->token=0; mcu_control_fault(c); }
    return s;
}
static locker_status_t management(mcu_control_t *c,pending_t *p,uint8_t kind,locker_command_state_t terminal)
{
    mcu_journal_control_request_t r={0}; locker_status_t s;
    r.kind=kind; r.token=p->token; r.barrier_after_token=c->last_event_token;
    r.journal_epoch=c->state.journal_epoch; r.expected_boot_id=c->state.boot_id;
    r.session_id=c->state.active_intent.session_id; r.operation_id=c->state.active_intent.operation_id;
    r.command_id=c->state.active_intent.command_id; r.action_no=c->state.active_intent.action_no;
    r.terminal_state=terminal;
    s=mcu_journal_control(c->journal,&r);
    if(s!=LOCKER_STATUS_QUEUED) { p->token=0; mcu_control_fault(c); }
    return s;
}
locker_status_t mcu_control_init(const mcu_control_config_t *config,mcu_journal_t *journal,mcu_control_t **out)
{
    if(out) *out=NULL;
    if(!out || !config || !journal || !config->pulse_ms || config->pulse_ms>1000u ||
        !config->prepare_deadline_ms || config->prepare_deadline_ms>2000u) return LOCKER_STATUS_INVALID;
    memset(&singleton,0,sizeof(singleton)); singleton.config=*config; singleton.journal=journal;
    singleton.mode=LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING; *out=&singleton; return LOCKER_STATUS_OK;
}
locker_status_t mcu_control_bind_sink(mcu_control_t *c,const mcu_control_sink_t *s)
{
    if(c!=&singleton || c->bound || !s || !s->reply) return LOCKER_STATUS_INVALID;
    c->sink=*s; c->bound=true; return LOCKER_STATUS_OK;
}
locker_status_t mcu_control_recover(mcu_control_t *c)
{
    pending_t *p;
    if(!c->bound || c->recovered) return LOCKER_STATUS_NOT_READY;
    p=allocate(c,RECOVER,NULL); return p?management(c,p,MCU_JOURNAL_RECOVER,0):LOCKER_STATUS_NO_SPACE;
}
locker_status_t mcu_control_observe_inputs(mcu_control_t *c,const locker_board_inputs_t *in)
{
    locker_board_inputs_t next; bool door,item,supply;
    c->now=mcu_board_monotonic_ms();
    if(!in || in->observed_mono_ms>c->now || c->now-in->observed_mono_ms>100u ||
       in->observed_mono_ms<c->last_sample || in->door>2 || in->item>2) {
        next=(locker_board_inputs_t){0}; next.observed_mono_ms=c->now;
    } else next=*in;
    next.door=(locker_door_state_t)input_debounce(&c->door,(uint8_t)next.door,30u,next.observed_mono_ms);
    next.item=(locker_item_state_t)input_debounce(&c->item,(uint8_t)next.item,30u,next.observed_mono_ms);
    door=next.door!=c->inputs.door; item=next.item!=c->inputs.item;
    /* S4-02 owns calibrated categories; validity transitions are safety facts.
       Do not create a revision for every ADC quantization step. */
    supply=next.supply_valid!=c->inputs.supply_valid;
    c->inputs=next; c->last_sample=next.observed_mono_ms;
    if(door || item || supply) {
        if(c->revision==UINT64_MAX) { mcu_control_fault(c); return LOCKER_STATUS_NO_SPACE; }
        ++c->revision;
        if(!physical(c)) { mcu_board_force_off(); if(!c->failed) c->mode=0; }
        if(c->recovered) {
            if(door) (void)append(c,next.door==1?LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_CLOSED:
                next.door==2?LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_OPENED:LOCKER_WIRE_DEVICE_EVENT_TYPE_HEALTH_CHANGED,0);
            if(item) (void)append(c,next.item==1?LOCKER_WIRE_DEVICE_EVENT_TYPE_ITEM_REMOVED:
                next.item==2?LOCKER_WIRE_DEVICE_EVENT_TYPE_ITEM_PRESENT:LOCKER_WIRE_DEVICE_EVENT_TYPE_HEALTH_CHANGED,0);
            if(supply) (void)append(c,LOCKER_WIRE_DEVICE_EVENT_TYPE_SUPPLY_CHANGED,0);
        }
    }
    return LOCKER_STATUS_OK;
}
static uint16_t guard(mcu_control_t *c,const locker_uuid_t *epoch,uint64_t boot,uint64_t revision)
{
    if(!eq(epoch,&c->state.journal_epoch)) return LOCKER_WIRE_RESULT_JOURNAL_CHANGED;
    if(boot!=c->state.boot_id || revision!=c->revision) return LOCKER_WIRE_RESULT_PHYSICAL_CHANGED;
    return LOCKER_WIRE_RESULT_OK;
}
static uint16_t eligible(mcu_control_t *c,const mcu_request_context_t *r,const locker_unlock_intent_t *i)
{
    uint16_t g=guard(c,&i->journal_epoch,i->expected_boot_id,i->expected_physical_revision);
    if(!c->session_valid || !eq(&r->session_id,&c->session) || !eq(&i->session_id,&c->session)) return LOCKER_WIRE_RESULT_BAD_SESSION;
    if(g) return g;
    if(!healthy(c)) return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
    if(!c->config.board_verified || c->mode!=1 || !fresh(c)) return LOCKER_WIRE_RESULT_NOT_READY;
    if(!physical(c) || (i->kind==LOCKER_OPERATION_KIND_BORROW?c->inputs.item!=2:c->inputs.item!=1)) return LOCKER_WIRE_RESULT_BAD_PRECONDITION;
    if(c->now<r->received_mono_ms || c->now-r->received_mono_ms>=c->config.prepare_deadline_ms ||
       c->now>=i->not_after_mcu_ms || i->not_after_mcu_ms-r->received_mono_ms>15000u) return LOCKER_WIRE_RESULT_BAD_PRECONDITION;
    return 0;
}
static bool same_intent(const locker_unlock_intent_t *a,const locker_unlock_intent_t *b)
{
    return eq(&a->operation_id,&b->operation_id)&&eq(&a->command_id,&b->command_id)&&
        eq(&a->session_id,&b->session_id)&&eq(&a->journal_epoch,&b->journal_epoch)&&
        a->expected_boot_id==b->expected_boot_id&&a->expected_physical_revision==b->expected_physical_revision&&
        a->action_no==b->action_no&&a->not_after_mcu_ms==b->not_after_mcu_ms&&a->slot_id==b->slot_id&&a->kind==b->kind;
}
static locker_status_t command_reply(mcu_control_t *c,const mcu_request_context_t *r,uint16_t result)
{
    locker_device_reply_t reply; locker_wire_execute_unlock_response_t *v;
    base_reply(&reply,r,result); v=&reply.body.execute_unlock;
    v->operation_id=c->state.active_intent.operation_id; v->command_id=c->state.active_intent.command_id;
    v->command_state=(uint8_t)c->state.command_state; v->action_no=c->state.active_intent.action_no;
    v->command_session_id=c->state.active_intent.session_id; v->command_boot_id=c->state.boot_id;
    v->physical_revision=c->revision;
    if(r->message_type==8) {
        /* The two generated response types have identical fields, but assign
           explicitly rather than reading another union member. */
        locker_wire_query_command_response_t q={0};
        q.session_id=v->session_id; q.result=v->result; q.operation_id=v->operation_id;
        q.command_id=v->command_id; q.command_state=v->command_state; q.action_no=v->action_no;
        q.command_session_id=v->command_session_id; q.command_boot_id=v->command_boot_id;
        q.physical_revision=v->physical_revision; q.reason=v->reason; reply.body.query_command=q;
    }
    return send(c,&reply);
}
locker_status_t mcu_control_submit_unlock(mcu_control_t *c,const mcu_request_context_t *r,const locker_unlock_intent_t *i)
{
    pending_t *p; uint16_t result; locker_status_t s;
    c->now=mcu_board_monotonic_ms();
    if(!c->bound || !r || !i || r->message_type!=7) return LOCKER_STATUS_INVALID;
    if(!c->session_valid || !eq(&r->session_id,&c->session)) return error(c,r,LOCKER_WIRE_RESULT_BAD_SESSION);
    if(c->command_pending) return error(c,r,LOCKER_WIRE_RESULT_BUSY);
    if(c->state.has_active && same_intent(i,&c->state.active_intent)) return command_reply(c,r,0);
    if(i->action_no<=c->state.action_watermark) return error(c,r,LOCKER_WIRE_RESULT_REPLAY_BLOCKED);
    if(c->state.action_watermark==UINT64_MAX || i->action_no!=c->state.action_watermark+1u) return error(c,r,LOCKER_WIRE_RESULT_ACTION_GAP);
    if(c->state.has_active || board_lock_pulse_active()) return error(c,r,LOCKER_WIRE_RESULT_BUSY);
    if(i->slot_id!=1 || nil(&i->operation_id) || nil(&i->command_id) || (i->kind!=1 && i->kind!=2)) return error(c,r,LOCKER_WIRE_RESULT_BAD_PAYLOAD);
    result=eligible(c,r,i); if(result) return error(c,r,result);
    p=allocate(c,INTENT,r); if(!p) return error(c,r,LOCKER_WIRE_RESULT_BUSY);
    p->intent=*i; s=mcu_journal_commit_intent(c->journal,i,p->token);
    if(s!=LOCKER_STATUS_QUEUED) { p->token=0; mcu_control_fault(c); return error(c,r,LOCKER_WIRE_RESULT_LOG_UNAVAILABLE); }
    c->command_pending=true; return LOCKER_STATUS_QUEUED;
}
static void capture(mcu_control_t *c,locker_wire_get_snapshot_response_t *v)
{
    memset(v,0,sizeof(*v)); v->sampled_mono_ms=c->now; v->physical_revision=c->revision;
    v->door=(uint8_t)c->inputs.door; v->item=(uint8_t)c->inputs.item;
    v->supply_mv=c->inputs.supply_valid?c->inputs.supply_mv:UINT32_MAX; v->flags=flags(c); v->device_mode=c->mode;
}
locker_status_t mcu_control_make_snapshot(mcu_control_t *c,const mcu_request_context_t *r)
{
    pending_t *p; c->now=mcu_board_monotonic_ms();
    if(!c->recovered) return error(c,r,LOCKER_WIRE_RESULT_LOG_UNAVAILABLE);
    p=allocate(c,SNAPSHOT,r); if(!p) return error(c,r,LOCKER_WIRE_RESULT_BUSY);
    capture(c,&p->snapshot);
    if(management(c,p,MCU_JOURNAL_BARRIER,0)!=LOCKER_STATUS_QUEUED) return error(c,r,LOCKER_WIRE_RESULT_LOG_UNAVAILABLE);
    return LOCKER_STATUS_QUEUED;
}
locker_status_t mcu_control_release_context(mcu_control_t *c,const mcu_request_context_t *r,const locker_context_guard_t *g)
{
    pending_t *p; uint16_t result=guard(c,&g->journal_epoch,g->expected_boot_id,g->expected_physical_revision);
    if(result) return error(c,r,result);
    if(!eq(&r->session_id,&c->session) || !eq(&g->session_id,&c->session)) return error(c,r,LOCKER_WIRE_RESULT_BAD_SESSION);
    if(c->inputs.door!=1 || board_lock_pulse_active() || c->executing || c->command_pending) return error(c,r,LOCKER_WIRE_RESULT_BAD_PRECONDITION);
    if(c->state.has_active && (!eq(&g->operation_id,&c->state.active_intent.operation_id) ||
        !eq(&g->command_id,&c->state.active_intent.command_id) || c->state.command_state==1 || c->state.command_state==2))
        return error(c,r,LOCKER_WIRE_RESULT_BAD_PRECONDITION);
    if(!c->state.has_active) {
        locker_device_reply_t reply; base_reply(&reply,r,0); reply.body.release_context.already_released=1;
        reply.body.release_context.physical_revision=c->revision; return send(c,&reply);
    }
    p=allocate(c,RELEASE,r); if(!p) return error(c,r,LOCKER_WIRE_RESULT_BUSY);
    if(management(c,p,MCU_JOURNAL_RELEASE,0)!=LOCKER_STATUS_QUEUED) return error(c,r,LOCKER_WIRE_RESULT_LOG_UNAVAILABLE);
    c->command_pending=true; return LOCKER_STATUS_QUEUED;
}
locker_status_t mcu_control_on_journal_result(mcu_control_t *c,uint32_t token,locker_status_t result,const mcu_journal_state_t *state)
{
    pending_t *p=NULL; unsigned n; uint8_t kind; locker_device_reply_t reply;
    c->now=mcu_board_monotonic_ms();
    for(n=0;n<PENDING_COUNT;n++) if(c->pending[n].token==token && token) { p=&c->pending[n]; break; }
    if(!p) { mcu_control_fault(c); return LOCKER_STATUS_CONFLICT; }
    kind=p->kind;
    if(result!=LOCKER_STATUS_OK || (kind!=EVENT && kind!=INTENT && !state)) {
        if(state) c->state=*state;
        mcu_control_fault(c);
        if(p->request.request_no) (void)error(c,&p->request,LOCKER_WIRE_RESULT_LOG_UNAVAILABLE);
        if(kind==INTENT || kind==FINALIZE || kind==RELEASE) c->command_pending=false;
        p->token=0; return result==LOCKER_STATUS_OK?LOCKER_STATUS_INVALID:result;
    }
    if(state) c->state=*state;
    if(kind==RECOVER) {
        c->recovered=true;
        if(!healthy(c)) c->mode=2;
    } else if(kind==INTENT) {
        uint16_t check;
        c->state.has_active=true; c->state.active_intent=p->intent;
        c->state.action_watermark=p->intent.action_no; c->state.command_state=LOCKER_COMMAND_STATE_ACCEPTED;
        check=eligible(c,&p->request,&p->intent);
        if(check) {
            /* Watermark is already consumed. Reply only after a durable
               not-driven checkpoint; never describe a failed write as rejection. */
            p->kind=FINALIZE;
            if(c->next_token==UINT32_MAX) { mcu_control_fault(c); p->token=0; return LOCKER_STATUS_NO_SPACE; }
            p->token=++c->next_token;
            return management(c,p,MCU_JOURNAL_FINALIZE,LOCKER_COMMAND_STATE_REJECTED);
        }
        /* Copy ACCEPTED into Communication before starting, then recheck the
           sticky fault flag (a full reply queue must close admission). */
        (void)command_reply(c,&p->request,0);
        c->now=mcu_board_monotonic_ms();
        if(!c->failed && !eligible(c,&p->request,&p->intent) &&
           mcu_board_start_bounded_pulse(c->config.pulse_ms)==LOCKER_STATUS_OK) {
            c->executing=true; c->state.command_state=LOCKER_COMMAND_STATE_PULSE_STARTED;
            (void)append(c,LOCKER_WIRE_DEVICE_EVENT_TYPE_PULSE_STARTED,c->config.pulse_ms);
        } else {
            mcu_board_force_off(); c->state.command_state=LOCKER_COMMAND_STATE_UNKNOWN_RESULT;
            p->kind=FINALIZE; p->request.request_no=0;
            if(c->next_token==UINT32_MAX) { mcu_control_fault(c); p->token=0; return LOCKER_STATUS_NO_SPACE; }
            p->token=++c->next_token;
            return management(c,p,MCU_JOURNAL_FINALIZE,LOCKER_COMMAND_STATE_UNKNOWN_RESULT);
        }
        c->command_pending=false;
    } else if(kind==FINALIZE) {
        c->command_pending=false;
        if(p->request.request_no) (void)command_reply(c,&p->request,0);
        if(!c->failed) c->mode=0;
    } else if(kind==SNAPSHOT) {
        locker_wire_get_snapshot_response_t *v=&reply.body.get_snapshot;
        base_reply(&reply,&p->request,0); *v=p->snapshot; v->session_id=p->request.session_id;
        v->journal_epoch=state->journal_epoch; v->boot_id=state->boot_id;
        v->first_available=cursor(&state->first_available); v->last_committed=cursor(&state->last_committed);
        v->ack_persisted=cursor(&state->ack_persisted); v->action_watermark=state->action_watermark;
        v->dropped_events=state->dropped_events; v->journal_state=state->journal_state;
        if(!state->continuity_ok || state->dropped_events) v->flags|=LOCKER_WIRE_FLAG_HISTORY_INCOMPLETE;
        if(!healthy(c)) v->flags|=LOCKER_WIRE_FLAG_JOURNAL_DEGRADED;
        if(state->has_active) {
            v->active_operation_id=state->active_intent.operation_id; v->active_command_id=state->active_intent.command_id;
            v->active_session_id=state->active_intent.session_id; v->active_action_no=state->active_intent.action_no;
            v->active_command_state=(uint8_t)state->command_state;
        }
        if(eq(&p->request.session_id,&c->session) && c->session_valid) {
            c->barrier_valid=true; c->barrier_revision=v->physical_revision; c->barrier_cursor=v->last_committed;
        }
        (void)send(c,&reply);
    } else if(kind==RELEASE) {
        c->command_pending=false; if(!c->failed) c->mode=0;
        base_reply(&reply,&p->request,0); reply.body.release_context.physical_revision=c->revision;
        (void)send(c,&reply);
    } else if(kind==RESUME) {
        const locker_wire_resume_ready_request_t *g=&p->resume;
        uint16_t check=guard(c,&g->journal_epoch,g->expected_boot_id,g->expected_physical_revision);
        if(!c->session_valid || !eq(&p->request.session_id,&c->session)) check=LOCKER_WIRE_RESULT_BAD_SESSION;
        else if(!check && (!healthy(c) || !c->config.board_verified || !fresh(c) || !physical(c) ||
            c->state.has_active || c->command_pending || c->executing || board_lock_pulse_active())) check=LOCKER_WIRE_RESULT_NOT_READY;
        else if(!check && (!c->barrier_valid || c->barrier_revision!=c->revision ||
            order(g->persisted_through,c->barrier_cursor)<0 || order(g->persisted_through,cursor(&state->ack_persisted))>0)) check=LOCKER_WIRE_RESULT_BAD_PRECONDITION;
        if(!check) c->mode=1;
        base_reply(&reply,&p->request,check); reply.body.resume_ready.device_mode=c->mode;
        reply.body.resume_ready.physical_revision=c->revision; (void)send(c,&reply);
    }
    p->token=0; return LOCKER_STATUS_OK;
}
void mcu_control_tick(mcu_control_t *c)
{
    c->now=mcu_board_monotonic_ms();
    if(!fresh(c) && c->mode==1) { c->mode=0; mcu_board_force_off(); }
    if(c->executing && !board_lock_pulse_active()) {
        c->executing=false; c->state.command_state=LOCKER_COMMAND_STATE_PULSE_FINISHED;
        /* Software polling is not a current/pulse-width measurement. */
        (void)append(c,LOCKER_WIRE_DEVICE_EVENT_TYPE_PULSE_FINISHED,UINT64_MAX);
        if(!c->failed) c->mode=0;
    }
}
locker_status_t mcu_control_dispatch(mcu_control_t *c,const mcu_request_context_t *r,const locker_wire_request_union_t *q)
{
    locker_device_reply_t reply; pending_t *p;
    c->now=mcu_board_monotonic_ms();
    if(!c->bound || !r || !q || !r->request_no) return LOCKER_STATUS_INVALID;
    if(r->message_type==1) {
        if(!c->session_valid || !eq(&q->hello.new_session_id,&c->session)) {
            mcu_control_session_lost(c); c->session=q->hello.new_session_id; c->host=q->hello.host_instance_id;
            c->session_valid=true;
        }
        base_reply(&reply,r,0); reply.body.hello.session_id=q->hello.new_session_id;
        reply.body.hello.journal_epoch=c->state.journal_epoch; reply.body.hello.boot_id=c->state.boot_id;
        reply.body.hello.firmware_minor=2; reply.body.hello.firmware_patch=1;
        reply.body.hello.device_mode=c->mode; reply.body.hello.journal_state=c->state.journal_state;
        reply.body.hello.action_watermark=c->state.action_watermark;
        return send(c,&reply);
    }
    if(!c->session_valid || !eq(&r->session_id,&c->session)) return error(c,r,LOCKER_WIRE_RESULT_BAD_SESSION);
    switch(r->message_type) {
    case 2: return mcu_control_make_snapshot(c,r);
    case 5:
        p=allocate(c,RESUME,r); if(!p) return error(c,r,LOCKER_WIRE_RESULT_BUSY);
        p->resume=q->resume_ready;
        if(management(c,p,MCU_JOURNAL_BARRIER,0)!=LOCKER_STATUS_QUEUED) return error(c,r,LOCKER_WIRE_RESULT_LOG_UNAVAILABLE);
        return LOCKER_STATUS_QUEUED;
    case 6:
        if(!c->failed) c->mode=0;
        mcu_board_force_off(); base_reply(&reply,r,0); reply.body.disarm.device_mode=c->mode;
        reply.body.disarm.physical_revision=c->revision; return send(c,&reply);
    case 7: {
        locker_unlock_intent_t i={0}; const locker_wire_execute_unlock_request_t *w=&q->execute_unlock;
        i.session_id=w->session_id; i.journal_epoch=w->journal_epoch; i.operation_id=w->operation_id; i.command_id=w->command_id;
        i.expected_boot_id=w->expected_boot_id; i.expected_physical_revision=w->expected_physical_revision;
        i.action_no=w->action_no; i.not_after_mcu_ms=w->not_after_mcu_ms; i.slot_id=w->slot_id; i.kind=(locker_operation_kind_t)w->kind;
        return mcu_control_submit_unlock(c,r,&i);
    }
    case 8:
        if(!eq(&q->query_command.journal_epoch,&c->state.journal_epoch)) return error(c,r,LOCKER_WIRE_RESULT_JOURNAL_CHANGED);
        if(!c->state.has_active || !eq(&q->query_command.command_id,&c->state.active_intent.command_id)) return error(c,r,LOCKER_WIRE_RESULT_UNKNOWN_RESULT);
        return command_reply(c,r,0);
    case 9: {
        const locker_wire_host_health_request_t *h=&q->host_health;
        if(!eq(&h->host_instance_id,&c->host)) return error(c,r,LOCKER_WIRE_RESULT_BAD_SESSION);
        if(!c->health_known || h->core_progress>c->progress) {
            c->progress=h->core_progress; c->progress_time=r->received_mono_ms; c->health_age=h->core_observed_age_ms;
            c->health_known=true;
            if(r->received_mono_ms>c->now) c->health_age=UINT32_MAX;
        } else if(h->core_progress<c->progress) { c->health_age=UINT32_MAX; if(!c->failed) c->mode=0; }
        base_reply(&reply,r,0); reply.body.host_health.received_mono_ms=c->now; reply.body.host_health.device_mode=c->mode;
        return send(c,&reply);
    }
    case 10:
        base_reply(&reply,r,0); reply.body.time_anchor.anchor_id=q->time_anchor.anchor_id;
        reply.body.time_anchor.boot_id=c->state.boot_id; reply.body.time_anchor.mcu_receive_mono_ms=r->received_mono_ms;
        reply.body.time_anchor.mcu_send_mono_ms=c->now; return send(c,&reply);
    case 11: {
        locker_context_guard_t g={0}; const locker_wire_release_context_request_t *w=&q->release_context;
        g.session_id=w->session_id; g.journal_epoch=w->journal_epoch; g.operation_id=w->operation_id; g.command_id=w->command_id;
        g.expected_boot_id=w->expected_boot_id; g.expected_physical_revision=w->expected_physical_revision;
        return mcu_control_release_context(c,r,&g);
    }
    default: return error(c,r,LOCKER_WIRE_RESULT_UNSUPPORTED);
    }
}
