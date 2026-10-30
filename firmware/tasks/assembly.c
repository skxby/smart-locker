/* S2-05: static bounded mailboxes. One owner each for Control, Storage and TX.
 * The only shared writes are mailbox bookkeeping under short critical sections.
 * The Health role reports loop progress; watchdog policy belongs to S4-03. */
#include "assembly.h"
#include "control.h"
#include "journal.h"
#include "w25q128.h"
#include "peripherals.h"
#include "timebase.h"
#include "lock_output.h"
#include "rc522.h"
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

typedef struct { mcu_request_context_t context; locker_wire_request_union_t body; uint32_t generation; } request_t;
typedef struct {
    uint8_t kind; uint32_t token; locker_status_t status;
    union { request_t request; mcu_journal_state_t state; rc522_uid_t card; } value;
} control_message_t;
typedef struct { bool event; union { locker_device_reply_t reply; locker_device_event_t fact; } body; } outgoing_t;
static control_message_t inbox[16]; static unsigned ih,ic;
static request_t storage_inbox[4]; static unsigned sh,sc;
static outgoing_t outgoing[8]; static unsigned oh,oc;
static bool started,ready_flag,failed;
static uint32_t session_generation;
static locker_uuid_t announced_session;
static bool announced_valid;
static TaskHandle_t storage_owner;
static mcu_journal_t *journal;
static mcu_control_t *control;
static mcu_health_t *health;
static w25q128_t flash;
static const mcu_journal_config_t geometry={true,0xef4018u,16777216u,4096u,256u,0u};
volatile uint32_t s205_diag[16];

static void lock(void *u) { (void)u; taskENTER_CRITICAL(); }
static void unlock(void *u) { (void)u; taskEXIT_CRITICAL(); }
static void wait(void *u) { (void)u; vTaskDelay(pdMS_TO_TICKS(1u)); }
static bool owner(void *u) { (void)u; return xTaskGetCurrentTaskHandle()==storage_owner && __get_IPSR()==0u; }
static uint64_t now(void *u) { (void)u; return mcu_board_monotonic_ms(); }
static void wait_ms(void *u,uint32_t ms) { (void)u; vTaskDelay(pdMS_TO_TICKS(ms)); }
static locker_status_t spi(void *u,const uint8_t *tx,uint8_t *rx,uint16_t n)
{ (void)u; return mcu_board_spi_transfer(BOARD_SPI_FLASH,tx,rx,n,50000u); }
static locker_status_t card_spi(void *u,const uint8_t *tx,uint8_t *rx,uint16_t n)
{ (void)u; return mcu_board_spi_transfer(BOARD_SPI_RC522,tx,rx,n,5000u); }
static void fail(void)
{ taskENTER_CRITICAL(); failed=true; taskEXIT_CRITICAL(); mcu_board_force_off(); }
void assembly_progress(uint8_t id,uint64_t progress)
{ if(mcu_health_report_progress(health,id,progress)!=LOCKER_STATUS_OK) fail(); }
bool assembly_ready(void)
{ bool value; taskENTER_CRITICAL(); value=ready_flag; taskEXIT_CRITICAL(); return value; }
static locker_status_t post(const control_message_t *m)
{
    locker_status_t status=LOCKER_STATUS_QUEUED;
    taskENTER_CRITICAL();
    if(ic==16u) { failed=true; status=LOCKER_STATUS_BUSY; }
    else { inbox[(ih+ic)%16u]=*m; ++ic; }
    taskEXIT_CRITICAL();
    if(status==LOCKER_STATUS_BUSY) mcu_board_force_off();
    return status;
}
static locker_status_t reply(void *u,const locker_device_reply_t *r)
{
    locker_status_t status=LOCKER_STATUS_QUEUED; (void)u;
    taskENTER_CRITICAL();
    if(oc==8u) { failed=true; status=LOCKER_STATUS_BUSY; }
    else { outgoing_t *o=&outgoing[(oh+oc)%8u]; o->event=false; o->body.reply=*r; ++oc; }
    taskEXIT_CRITICAL();
    if(status==LOCKER_STATUS_BUSY) mcu_board_force_off();
    return status;
}
static locker_status_t event(void *u,const locker_device_event_t *e)
{
    (void)u;
    taskENTER_CRITICAL();
    /* Dropped transport notification leaves its durable event available to
       FETCH; no journal continuity loss is invented for absent listeners. */
    if(oc==8u) { failed=true; taskEXIT_CRITICAL(); mcu_board_force_off(); return LOCKER_STATUS_OK; }
    outgoing[(oh+oc)%8u].event=true; outgoing[(oh+oc)%8u].body.fact=*e; ++oc;
    taskEXIT_CRITICAL(); return LOCKER_STATUS_OK;
}
static locker_status_t committed(void *u,uint32_t t,locker_status_t s)
{ control_message_t m={0}; (void)u; m.kind=1; m.token=t; m.status=s; return post(&m); }
static locker_status_t control_result(void *u,uint32_t t,locker_status_t s,const mcu_journal_state_t *state)
{ control_message_t m={0}; (void)u; m.kind=2; m.token=t; m.status=s; if(state) m.value.state=*state; else m.kind=1; return post(&m); }
void assembly_session_lost(void)
{ taskENTER_CRITICAL(); ++session_generation; announced_valid=false; taskEXIT_CRITICAL(); mcu_board_force_off(); }
locker_status_t assembly_dispatch(void *u,const mcu_request_context_t *r,const locker_wire_request_union_t *q)
{
    control_message_t m={0}; locker_status_t result=LOCKER_STATUS_QUEUED; (void)u;
    if(!assembly_ready()) return LOCKER_STATUS_NOT_READY;
    if(r->message_type==1) {
        bool changed;
        taskENTER_CRITICAL();
        changed=!announced_valid || memcmp(announced_session.bytes,q->hello.new_session_id.bytes,16u)!=0;
        if(changed) { ++session_generation; announced_session=q->hello.new_session_id; announced_valid=true; }
        taskEXIT_CRITICAL(); if(changed) mcu_board_force_off();
    }
    if(r->message_type==3 || r->message_type==4) {
        taskENTER_CRITICAL();
        if(sc==4u) { failed=true; result=LOCKER_STATUS_BUSY; }
        else { storage_inbox[(sh+sc)%4u].context=*r; storage_inbox[(sh+sc)%4u].body=*q; ++sc; }
        taskEXIT_CRITICAL(); if(result==LOCKER_STATUS_BUSY) mcu_board_force_off(); return result;
    }
    m.value.request.context=*r; m.value.request.body=*q;
    taskENTER_CRITICAL(); m.value.request.generation=session_generation; taskEXIT_CRITICAL();
    return post(&m);
}
void assembly_transmit(mcu_transport_t *t)
{
    outgoing_t o; bool available;
    for(;;) {
        taskENTER_CRITICAL(); available=oc!=0u;
        if(available) { o=outgoing[oh]; oh=(oh+1u)%8u; --oc; }
        taskEXIT_CRITICAL(); if(!available) break;
        if(!o.event && o.body.reply.message_type==1) {
            memcpy(o.body.reply.body.hello.device_uid,t->device_uid,12u);
            o.body.reply.body.hello.capabilities=LOCKER_WIRE_CAP_DOOR_AND_ITEM|LOCKER_WIRE_CAP_PERSISTENT_JOURNAL;
        }
        if(o.event) {
            if(t->session_valid && mcu_transport_queue_event(t,&o.body.fact)!=LOCKER_STATUS_OK) fail();
        } else {
            locker_status_t s=mcu_transport_queue_reply(t,&o.body.reply);
            if(s!=LOCKER_STATUS_OK && s!=LOCKER_STATUS_CONFLICT) fail();
        }
    }
    if(t->fault_latched) fail();
}
static void control_task(void *u)
{
    uint32_t generation=0; bool did_recover=false; (void)u;
    uint64_t progress=0;
    for(;;) {
        if(assembly_ready()) {
            control_message_t m; bool available,broken; uint32_t current;
            locker_board_inputs_t in={0};
            board_inputs_sample();
            (void)mcu_board_read_inputs(&in); (void)mcu_control_observe_inputs(control,&in);
            taskENTER_CRITICAL(); broken=failed; current=session_generation;
            available=ic!=0u; if(available) { m=inbox[ih]; ih=(ih+1u)%16u; --ic; }
            /* Keep the final session/fault check and nonblocking HAL start
               atomic relative to Communication revocation. Nested mailbox/
               Journal inbox critical sections contain no Flash IO or waits. */
            if(current!=generation) { mcu_control_session_lost(control); generation=current; }
            if(broken) mcu_control_fault(control);
            if(!did_recover) { (void)mcu_control_recover(control); did_recover=true; }
            if(available) {
                if(m.kind==3) (void)mcu_control_observe_card(control,m.value.card.bytes,m.value.card.length);
                else if(m.kind) (void)mcu_control_on_journal_result(control,m.token,m.status,m.kind==2?&m.value.state:NULL);
                else if(m.value.request.generation==current)
                    (void)mcu_control_dispatch(control,&m.value.request.context,&m.value.request.body);
            }
            mcu_control_tick(control); ++s205_diag[0];
            taskEXIT_CRITICAL();
            assembly_progress(LOCKER_MCU_TASK_ID_CONTROL,++progress);
        }
        vTaskDelay(pdMS_TO_TICKS(1u));
    }
}
static void key_identity(locker_event_key_t *k,const locker_uuid_t *epoch,locker_wire_cursor_t cursor)
{
    static const char hex[]="0123456789abcdef"; const uint8_t *uid=(const uint8_t *)UID_BASE; unsigned n;
    memset(k,0,sizeof(*k)); k->journal_epoch=*epoch; k->boot_id=cursor.boot_id; k->event_seq=cursor.event_seq;
    for(n=0;n<12u;n++) { k->device_id[2u*n]=hex[uid[n]>>4]; k->device_id[2u*n+1u]=hex[uid[n]&15u]; }
}
static locker_wire_cursor_t wire_cursor(locker_event_key_t k)
{ locker_wire_cursor_t v={k.boot_id,k.event_seq}; return v; }
static void storage_request(const request_t *r)
{
    locker_device_reply_t out={0}; locker_event_key_t key; locker_status_t s;
    out.request_no=r->context.request_no; out.message_type=r->context.message_type;
    if(out.message_type==3) {
        locker_event_batch_t page; unsigned i;
        locker_wire_fetch_events_response_t *v=&out.body.fetch_events;
        const locker_wire_fetch_events_request_t *q=&r->body.fetch_events;
        v->session_id=r->context.session_id; v->journal_epoch=q->journal_epoch;
        key_identity(&key,&q->journal_epoch,q->after);
        s=mcu_journal_read_page(journal,&key,q->max_count,&page);
        v->result=s==LOCKER_STATUS_OK?0:s==LOCKER_STATUS_CONFLICT?LOCKER_WIRE_RESULT_JOURNAL_CHANGED:LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
        if(!v->result) {
            v->first_available=wire_cursor(page.first_available); v->last_committed=wire_cursor(page.last_committed);
            v->next_after=wire_cursor(page.next_after); v->event_count=page.count; v->has_more=page.has_more; v->gap_before_page=page.gap_before_page;
            for(i=0;i<page.count;i++) {
                locker_wire_event96_t *w=&v->events[i]; const locker_device_event_t *e=&page.events[i];
                w->boot_id=e->key.boot_id; w->event_seq=e->key.event_seq; w->mono_ms=e->mono_ms;
                w->physical_revision=e->physical_revision; w->event_type=e->event_type; w->reason=e->reason;
                w->flags=e->flags; w->operation_id=e->operation_id; w->command_id=e->command_id;
                w->door=(uint8_t)e->door; w->item=(uint8_t)e->item; w->supply_mv=e->supply_mv;
                w->detail=e->detail; w->card_uid_len=e->card_uid_len; memcpy(w->card_uid,e->card_uid,10u);
            }
        }
    } else {
        const locker_wire_ack_persisted_request_t *q=&r->body.ack_persisted;
        out.body.ack_persisted.session_id=r->context.session_id; key_identity(&key,&q->journal_epoch,q->through);
        s=mcu_journal_ack(journal,&key);
        out.body.ack_persisted.result=s==LOCKER_STATUS_OK?0:LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
        if(s==LOCKER_STATUS_OK) out.body.ack_persisted.ack_persisted=q->through;
    }
    (void)reply(NULL,&out);
}
static void storage_task(void *u)
{
    mcu_journal_port_t port={0}; mcu_journal_sink_t sink={NULL,committed,event,control_result};
    mcu_control_sink_t cs={NULL,reply}; mcu_control_config_t config={false,1000u,2000u};
    locker_status_t s; (void)u;
    uint64_t progress=0;
    rc522_t reader={0}; rc522_uid_t previous={0}; uint64_t next_card=0; unsigned absent=0;
    storage_owner=xTaskGetCurrentTaskHandle(); flash.transfer=spi; flash.now_ms=now; flash.wait_ms=wait_ms;
    s=w25q128_init(&flash,&geometry); s205_diag[4]=(uint32_t)s;
    if(s==LOCKER_STATUS_OK) {
        port.fs=w25q128_lfs_config(&flash); port.lock=lock; port.unlock=unlock; port.wait=wait; port.is_owner=owner;
        s=mcu_journal_attach(&port);
    }
    if(s==LOCKER_STATUS_OK) s=mcu_journal_init(&geometry,&journal);
    if(s==LOCKER_STATUS_OK) s=mcu_journal_bind_sink(journal,&sink);
    if(s==LOCKER_STATUS_OK) s=mcu_control_init(&config,journal,&control);
    if(s==LOCKER_STATUS_OK) s=mcu_control_bind_sink(control,&cs);
    s205_diag[5]=(uint32_t)s;
    /* No auto-provision/format and no manufactured VERIFIED profile. */
    if(s!=LOCKER_STATUS_OK) { fail(); vTaskSuspend(NULL); }
    HAL_GPIO_WritePin(GPIOE,GPIO_PIN_3,GPIO_PIN_RESET); vTaskDelay(pdMS_TO_TICKS(2u));
    HAL_GPIO_WritePin(GPIOE,GPIO_PIN_3,GPIO_PIN_SET); vTaskDelay(pdMS_TO_TICKS(50u));
    reader.transfer=card_spi; reader.now_ms=now; reader.wait_ms=wait_ms;
    s205_diag[6]=(uint32_t)rc522_init(&reader);
    taskENTER_CRITICAL(); ready_flag=true; taskEXIT_CRITICAL();
    for(;;) {
        request_t r; bool available;
        taskENTER_CRITICAL(); available=sc!=0u;
        if(available) { r=storage_inbox[sh]; sh=(sh+1u)%4u; --sc; }
        taskEXIT_CRITICAL();
        if(available) storage_request(&r);
        (void)mcu_journal_process_one(journal); ++s205_diag[2];
        if(reader.ready && mcu_board_monotonic_ms()>=next_card) {
            rc522_uid_t uid; locker_status_t card_status=rc522_read_uid(&reader,&uid);
            next_card=mcu_board_monotonic_ms()+100u;
            if(card_status==LOCKER_STATUS_OK) {
                if(uid.length!=previous.length || memcmp(uid.bytes,previous.bytes,uid.length)!=0) {
                    control_message_t message={0}; message.kind=3; message.value.card=uid; (void)post(&message); previous=uid;
                }
                absent=0;
            } else if(card_status==LOCKER_STATUS_NOT_READY && ++absent>=3u) memset(&previous,0,sizeof(previous));
            s205_diag[7]=(uint32_t)card_status;
        }
        assembly_progress(LOCKER_MCU_TASK_ID_PERIPHERAL_STORAGE,++progress);
        vTaskDelay(pdMS_TO_TICKS(1u));
    }
}
static void health_task(void *u)
{
    uint64_t progress=0,ready_since=0; (void)u;
    for(;;) {
        uint64_t time=mcu_board_monotonic_ms();
        assembly_progress(LOCKER_MCU_TASK_ID_HEALTH,++progress);
        if(assembly_ready()) {
            if(!ready_since) ready_since=time;
            if(time-ready_since>=500u && mcu_health_evaluate(health,time)!=LOCKER_STATUS_OK) fail();
        }
        ++s205_diag[3]; vTaskDelay(pdMS_TO_TICKS(20u));
    }
}
void assembly_start(void)
{
    extern void communication_task(void *);
    static const mcu_health_config_t config={{100u,170u,5000u,1000u},5000u};
    if(started) return;
    started=true;
    if(mcu_health_init(&config,&health)!=LOCKER_STATUS_OK) env_assert_failed();
    if(xTaskCreate(control_task,"Control",1024u,NULL,2u,NULL)!=pdPASS ||
       xTaskCreate(communication_task,"Communication",1024u,NULL,2u,NULL)!=pdPASS ||
       xTaskCreate(storage_task,"Storage",1024u,NULL,1u,NULL)!=pdPASS ||
       xTaskCreate(health_task,"Health",256u,NULL,1u,NULL)!=pdPASS) env_assert_failed();
}
