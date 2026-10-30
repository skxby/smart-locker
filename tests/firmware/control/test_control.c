/* Actual Control + locked littlefs/Journal, software NOR and deterministic
 * task delivery. This proves protocol/guards, never electrical pulse width. */
#include "control.h"
#include "journal.h"
#include "nor_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static nor_fixture_t nor;
static mcu_control_t *control;
static mcu_journal_t *journal;
static uint64_t mono;
static unsigned starts,offs,reply_count,result_count;
static bool active,reply_full;
static locker_uuid_t epoch={{0x42}}, session={{0x11}}, host={{0x22}};
static locker_device_reply_t replies[256];
typedef struct { uint32_t token; locker_status_t result; bool has_state; mcu_journal_state_t state; } result_t;
static result_t results[128];
static unsigned rh,rc;
static const mcu_journal_config_t geometry={true,0xef4018u,16777216u,4096u,256u,0u};
static locker_wire_get_snapshot_response_t snap;
static locker_board_inputs_t inputs;
uint64_t mcu_board_monotonic_ms(void) { return mono; }
void mcu_board_force_off(void) { ++offs; active=false; }
locker_status_t mcu_board_start_bounded_pulse(uint16_t ms)
{ assert(ms>0 && ms<=1000); assert(!active); ++starts; active=true; return LOCKER_STATUS_OK; }
bool board_lock_pulse_active(void) { return active; }
static bool owner(void *u) { (void)u; return true; }
static void no_lock(void *u) { (void)u; }
static void pump(void *u) { (void)u; (void)mcu_journal_process_one(journal); }
static locker_status_t committed(void *u,uint32_t t,locker_status_t s)
{
    result_t *r; (void)u; assert(rc<128); r=&results[(rh+rc++)%128];
    memset(r,0,sizeof(*r)); r->token=t; r->result=s; return LOCKER_STATUS_QUEUED;
}
static locker_status_t management(void *u,uint32_t t,locker_status_t s,const mcu_journal_state_t *v)
{
    locker_status_t result=committed(u,t,s); result_t *r=&results[(rh+rc-1u)%128];
    assert(v); r->state=*v; r->has_state=true; return result;
}
static locker_status_t event(void *u,const locker_device_event_t *e)
{ (void)u; assert(e->key.event_seq && e->key.boot_id); assert(!memcmp(e->key.journal_epoch.bytes,epoch.bytes,16)); return LOCKER_STATUS_OK; }
static locker_status_t reply(void *u,const locker_device_reply_t *r)
{
    (void)u; if(reply_full) return LOCKER_STATUS_BUSY;
    assert(reply_count<256); replies[reply_count++]=*r;
    assert(!r->received_host_mono_ms && !r->request_started_host_mono_ms); return LOCKER_STATUS_QUEUED;
}
static void sample(void)
{ inputs.observed_mono_ms=mono; assert(mcu_control_observe_inputs(control,&inputs)==LOCKER_STATUS_OK); }
static void deliver(void)
{
    while(rc) {
        result_t r=results[rh]; rh=(rh+1u)%128; --rc; sample();
        (void)mcu_control_on_journal_result(control,r.token,r.result,r.has_state?&r.state:NULL); ++result_count;
    }
}
static void drain(void)
{
    unsigned n=0;
    for(;;) {
        locker_status_t s=mcu_journal_process_one(journal); bool pending=rc!=0;
        deliver(); assert(++n<256);
        if(s==LOCKER_STATUS_NOT_READY && !pending) break;
    }
}
static mcu_request_context_t context(uint8_t type,uint32_t no)
{ mcu_request_context_t r={0}; r.session_id=session; r.message_type=type; r.request_no=no; r.received_mono_ms=mono; return r; }
static void dispatch(uint8_t type,uint32_t no,locker_wire_request_union_t *q)
{ mcu_request_context_t r=context(type,no); assert(mcu_control_dispatch(control,&r,q)==LOCKER_STATUS_QUEUED); }
static locker_device_reply_t *last(uint8_t type,uint32_t no)
{ locker_device_reply_t *r; assert(reply_count); r=&replies[reply_count-1u]; assert(r->message_type==type && r->request_no==no); return r; }
static void hello(uint32_t no)
{
    locker_wire_request_union_t q={0}; q.hello.new_session_id=session; q.hello.host_instance_id=host;
    dispatch(1,no,&q); assert(last(1,no)->body.hello.result==0);
}
static void health(uint64_t progress,uint32_t age)
{
    locker_wire_request_union_t q={0}; q.host_health.host_instance_id=host;
    q.host_health.core_progress=progress; q.host_health.core_observed_age_ms=age;
    dispatch(9,90,&q); assert(last(9,90)->body.host_health.result==0);
}
static void snapshot(uint32_t no)
{
    mcu_request_context_t r=context(2,no); unsigned before=reply_count;
    assert(mcu_control_make_snapshot(control,&r)==LOCKER_STATUS_QUEUED); assert(reply_count==before);
    drain(); snap=last(2,no)->body.get_snapshot; assert(!snap.result);
}
static void ready(void)
{
    locker_wire_request_union_t q={0}; locker_event_key_t key={0};
    health(1,0); snapshot(20);
    strcpy(key.device_id,"control-test"); key.journal_epoch=epoch;
    key.boot_id=snap.last_committed.boot_id; key.event_seq=snap.last_committed.event_seq;
    assert(mcu_journal_ack(journal,&key)==LOCKER_STATUS_OK);
    q.resume_ready.session_id=session; q.resume_ready.journal_epoch=epoch;
    q.resume_ready.expected_boot_id=snap.boot_id; q.resume_ready.expected_physical_revision=snap.physical_revision;
    q.resume_ready.persisted_through=snap.last_committed;
    dispatch(5,50,&q); drain(); assert(last(5,50)->body.resume_ready.result==0);
    assert(last(5,50)->body.resume_ready.device_mode==1);
}
static void fresh(bool verified)
{
    mcu_journal_port_t port={0}; mcu_journal_sink_t sink={NULL,committed,event,management};
    mcu_control_sink_t cs={NULL,reply}; mcu_control_config_t cfg={verified,1000,2000};
    nor_fixture_init(&nor,4096); port.fs=&nor.config; port.lock=no_lock; port.unlock=no_lock; port.wait=pump; port.is_owner=owner;
    assert(mcu_journal_attach(&port)==LOCKER_STATUS_OK);
    assert(mcu_journal_provision(&geometry,&epoch,"control-test")==LOCKER_STATUS_OK);
    assert(mcu_journal_init(&geometry,&journal)==LOCKER_STATUS_OK);
    assert(mcu_journal_bind_sink(journal,&sink)==LOCKER_STATUS_OK);
    assert(mcu_control_init(&cfg,journal,&control)==LOCKER_STATUS_OK);
    assert(mcu_control_bind_sink(control,&cs)==LOCKER_STATUS_OK);
    mono=100; starts=offs=reply_count=result_count=rh=rc=0; active=reply_full=false;
    assert(mcu_control_recover(control)==LOCKER_STATUS_QUEUED); drain();
    memset(&inputs,0,sizeof(inputs)); inputs.door=LOCKER_DOOR_STATE_CLOSED; inputs.item=LOCKER_ITEM_STATE_PRESENT;
    inputs.supply_valid=true; inputs.supply_mv=12000; sample(); mono+=31; sample(); drain();
    hello(1);
}
static void stop(void)
{ active=false; drain(); assert(mcu_journal_shutdown(journal)==LOCKER_STATUS_OK); nor_fixture_destroy(&nor); }
static locker_unlock_intent_t intent(void)
{
    locker_unlock_intent_t i={0}; i.operation_id.bytes[0]=3; i.command_id.bytes[0]=4;
    i.session_id=session; i.journal_epoch=epoch; i.expected_boot_id=snap.boot_id;
    i.expected_physical_revision=snap.physical_revision; i.action_no=snap.action_watermark+1;
    i.not_after_mcu_ms=mono+15000; i.slot_id=1; i.kind=LOCKER_OPERATION_KIND_BORROW; return i;
}
static void submit(const locker_unlock_intent_t *i,uint32_t no)
{ mcu_request_context_t r=context(7,no); assert(mcu_control_submit_unlock(control,&r,i)==LOCKER_STATUS_QUEUED); }
static void release(const locker_unlock_intent_t *i,uint32_t no)
{
    locker_context_guard_t g={0}; mcu_request_context_t r=context(11,no);
    g.session_id=session; g.journal_epoch=epoch; g.expected_boot_id=snap.boot_id;
    g.expected_physical_revision=snap.physical_revision; g.operation_id=i->operation_id; g.command_id=i->command_id;
    assert(mcu_control_release_context(control,&r,&g)==LOCKER_STATUS_QUEUED);
}
static void success_and_duplicate(void)
{
    locker_unlock_intent_t i;
    fresh(true); ready(); i=intent(); submit(&i,70); assert(!starts);
    submit(&i,71); assert(last(7,71)->body.execute_unlock.result==LOCKER_WIRE_RESULT_BUSY);
    drain(); assert(starts==1 && active && last(7,70)->body.execute_unlock.command_state==LOCKER_COMMAND_STATE_ACCEPTED);
    submit(&i,72); assert(starts==1 && last(7,72)->body.execute_unlock.result==0);
    active=false; mcu_control_tick(control); drain(); snapshot(21); assert(snap.active_command_state==3 && snap.action_watermark==1);
    release(&i,110); assert(last(2,21)); drain(); assert(!last(11,110)->body.release_context.already_released);
    release(&i,111); assert(last(11,111)->body.release_context.already_released);
    submit(&i,73); assert(last(7,73)->body.execute_unlock.result==LOCKER_WIRE_RESULT_REPLAY_BLOCKED && starts==1);
    stop();
}
static void invalidation(unsigned reason)
{
    locker_unlock_intent_t i;
    fresh(true); ready(); i=intent(); if(reason==4) i.not_after_mcu_ms=mono+1; submit(&i,70);
    assert(mcu_journal_process_one(journal)==LOCKER_STATUS_OK); assert(!starts && rc);
    switch(reason) {
    case 0: mono+=2000; break;
    case 1: inputs.item=LOCKER_ITEM_STATE_ABSENT; sample(); mono+=31; sample(); break;
    case 2: session.bytes[1]++; hello(2); break;
    case 3: inputs.supply_valid=false; sample(); break;
    case 4: mono=i.not_after_mcu_ms; break;
    case 5: mcu_control_session_lost(control); break;
    case 6: reply_full=true; break;
    default: assert(0);
    }
    deliver(); drain(); assert(!starts); reply_full=false;
    if(reason!=6) { snapshot(23); assert(snap.action_watermark==1 && snap.active_command_state==LOCKER_COMMAND_STATE_REJECTED); }
    stop();
}
static void deadlines_and_failure(void)
{
    locker_unlock_intent_t i;
    fresh(true); ready(); i=intent(); i.not_after_mcu_ms=mono;
    submit(&i,70); assert(last(7,70)->body.execute_unlock.result==LOCKER_WIRE_RESULT_BAD_PRECONDITION && !starts);
    i.not_after_mcu_ms=mono+15001; submit(&i,71); assert(last(7,71)->body.execute_unlock.result==4 && !starts);
    i=intent(); i.action_no++; submit(&i,72); assert(last(7,72)->body.execute_unlock.result==6);
    i=intent(); submit(&i,73); nor_fixture_cut_at(&nor,1); drain(); assert(!starts);
    assert(last(7,73)->body.execute_unlock.result==LOCKER_WIRE_RESULT_LOG_UNAVAILABLE);
    nor_fixture_restart(&nor); (void)mcu_journal_shutdown(journal); nor_fixture_destroy(&nor);
}
static void snapshot_boundary(void)
{
    mcu_request_context_t a,b; unsigned before;
    fresh(true); a=context(2,201); b=context(2,202); before=reply_count;
    assert(mcu_control_make_snapshot(control,&a)==LOCKER_STATUS_QUEUED);
    inputs.item=LOCKER_ITEM_STATE_ABSENT; sample(); mono+=31; sample();
    assert(mcu_control_make_snapshot(control,&b)==LOCKER_STATUS_QUEUED);
    assert(reply_count==before); drain();
    assert(replies[before].request_no==201 && replies[before+1].request_no==202);
    assert(replies[before].body.get_snapshot.item==2 && replies[before+1].body.get_snapshot.item==1);
    assert(replies[before].body.get_snapshot.last_committed.event_seq<replies[before+1].body.get_snapshot.last_committed.event_seq);
    assert(replies[before].body.get_snapshot.physical_revision<replies[before+1].body.get_snapshot.physical_revision);
    stop();
}
static void health_and_unverified(void)
{
    locker_unlock_intent_t i;
    fresh(true); ready(); i=intent(); mono+=5000; sample(); health(1,0);
    submit(&i,70); assert(last(7,70)->body.execute_unlock.result==3 && !starts);
    stop(); fresh(false); health(1,0); snapshot(20); i=intent(); submit(&i,70);
    assert(last(7,70)->body.execute_unlock.result==3 && !starts); stop();
}
static void prepare_just_in_time(void)
{
    locker_unlock_intent_t i;
    fresh(true); ready(); i=intent(); submit(&i,70);
    assert(mcu_journal_process_one(journal)==LOCKER_STATUS_OK); mono+=1999; deliver(); drain();
    assert(starts==1); active=false; mcu_control_tick(control); drain(); stop();
}
static void card_is_only_a_fact(void)
{
    uint8_t uid[10]={1,2,3,4,5,6,7,8,9,10}; unsigned n;
    fresh(true);
    for(n=4;n<=10;n+=3) assert(mcu_control_observe_card(control,uid,(uint8_t)n)==LOCKER_STATUS_QUEUED);
    assert(mcu_control_observe_card(control,uid,5)==LOCKER_STATUS_INVALID); drain(); assert(!starts); stop();
}
int main(void)
{
    unsigned n; success_and_duplicate();
    for(n=0;n<7;n++) invalidation(n);
    deadlines_and_failure(); snapshot_boundary(); health_and_unverified(); prepare_just_in_time(); card_is_only_a_fact();
    puts("Control/real Journal guards PASS (software NOR, no electrical evidence)"); return 0;
}
