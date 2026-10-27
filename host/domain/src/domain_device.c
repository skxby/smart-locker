#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 业务核心：设备适配器桥接。
 *
 * 这个文件是 Core 与 locker_device_t 之间的唯一接触面。约定（见
 * docs/architecture/internal-interfaces.md:114-137 与 host/device/README.md）：
 *
 *   - 所有函数只在 Core 线程执行；设备回调（sink）跑在设备 reader 线程，
 *     只把按值复制的 DTO 放进 event_ring / reply_ring，绝不在回调里等 Core。
 *   - 环满时不返回 NO_SPACE（那会让设备侧锁存并 shutdown 会话），而是锁存
 *     溢出标志并返回 OK；Core 随后用设备日志补取，缺口进 REVIEW_REQUIRED。
 *   - EXECUTE 每个物理 command 只发一次，超时只查询不重发（契约 :221）。
 *   - ACK_PERSISTED 只确认仓储已提交的连续水位（契约 :227）。
 *   - 端口调用本身可能阻塞到适配器的写超时（1000 ms），Core 不在此之上再叠加
 *     等待，也不在调用期间持有完成槽以外的锁。
 */

#include "domain_internal.h"

#include <string.h>

#include "locker/common/log.h"

#define DOMAIN_POLL_INTERVAL_MS 100u
#define DOMAIN_HEALTH_INTERVAL_MS 1000u
#define DOMAIN_SNAPSHOT_INTERVAL_MS 500u
#define DOMAIN_FETCH_INTERVAL_MS 200u
#define DOMAIN_ANCHOR_INTERVAL_MS 2000u
#define DOMAIN_PENDING_TIMEOUT_MS LOCKER_HOST_TIMEOUT_MS

/*
 * 线上 device_mode 与契约 locker_device_mode_t 是两套编号（真源见
 * contracts/contract-source.json:208-219 与 docs/architecture/uart-v1.md:48）：
 *
 *   wire : RECONCILING=0 READY=1 DEGRADED=2   （设备只会报这三种）
 *   契约 : OFFLINE=0 RECONCILING=1 READY=2 DEGRADED=3
 *
 * OFFLINE 是主机观察投影，MCU 永远不会报它，所以映射是 wire+1，不是强转。
 * 直接把 wire 值塞进 session.device_mode 会让 READY 判等恒假，
 * physical_ready_for_action 永远拒绝动作。
 */
static uint8_t session_mode_from_wire(uint8_t wire_mode)
{
    switch (wire_mode) {
    case LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING:
        return (uint8_t)LOCKER_DEVICE_MODE_RECONCILING;
    case LOCKER_WIRE_WIRE_DEVICE_MODE_READY:
        return (uint8_t)LOCKER_DEVICE_MODE_READY;
    case LOCKER_WIRE_WIRE_DEVICE_MODE_DEGRADED:
        return (uint8_t)LOCKER_DEVICE_MODE_DEGRADED;
    default:
        /* 未知值按最保守的可动作性处理：OFFLINE 不是 READY，动作一律被拒。 */
        return (uint8_t)LOCKER_DEVICE_MODE_OFFLINE;
    }
}

/* 事件键比较：先 boot 再 seq，与仓储的字节序一致。 */
static int key_compare(const locker_event_key_t *left, const locker_event_key_t *right)
{
    if (left->boot_id != right->boot_id) {
        return left->boot_id < right->boot_id ? -1 : 1;
    }
    if (left->event_seq != right->event_seq) {
        return left->event_seq < right->event_seq ? -1 : 1;
    }
    return 0;
}

/* ---------- 在途请求登记 ---------- */

uint32_t domain_request_no(locker_core_t *core, uint8_t purpose)
{
    for (uint32_t index = 0u; index < LOCKER_DOMAIN_PENDING_SLOTS; index += 1u) {
        domain_pending_t *slot = &core->pending[index];
        if (slot->used) {
            continue;
        }
        core->request_no_next += 1u;
        if (core->request_no_next == 0u) {
            core->request_no_next = 1u;
        }
        memset(slot, 0, sizeof(*slot));
        slot->used = true;
        slot->request_no = core->request_no_next;
        slot->purpose = purpose;
        slot->started_ms = domain_now(core);
        return slot->request_no;
    }
    return 0u;
}

void domain_pending_clear(locker_core_t *core, uint8_t purpose)
{
    for (uint32_t index = 0u; index < LOCKER_DOMAIN_PENDING_SLOTS; index += 1u) {
        if (core->pending[index].used && core->pending[index].purpose == purpose) {
            memset(&core->pending[index], 0, sizeof(core->pending[index]));
        }
    }
}

bool domain_pending_take(locker_core_t *core, uint32_t request_no, uint8_t *out_purpose)
{
    if (request_no == 0u) {
        return false;
    }
    for (uint32_t index = 0u; index < LOCKER_DOMAIN_PENDING_SLOTS; index += 1u) {
        domain_pending_t *slot = &core->pending[index];
        if (slot->used && slot->request_no == request_no) {
            if (out_purpose != NULL) {
                *out_purpose = slot->purpose;
            }
            memset(slot, 0, sizeof(*slot));
            return true;
        }
    }
    return false;
}

/* ---------- 设备上下文 ---------- */

void domain_device_reset(locker_core_t *core)
{
    memset(core->pending, 0, sizeof(core->pending));
    core->device_session_up = false;
    core->device_snapshot_valid = false;
    core->snapshot_in_flight = false;
    core->fetch_in_flight = false;
    core->ack_in_flight = false;
    core->ack_cursor_valid = false;
    core->execute_in_flight = false;
    core->pending_command_valid = false;
    core->fetch_gap = false;
    core->event_overflow = false;
    core->reply_overflow = false;
    core->snapshot_requested_ms = 0u;
    core->last_fetch_ms = 0u;
    core->last_health_ms = 0u;
    core->last_pump_ms = 0u;
    core->action_watermark = 0u;
    core->cursor.valid = false;
    core->cursor.continuity_ok = false;
    memset(&core->cursor.through, 0, sizeof(core->cursor.through));
    memset(&core->physical, 0, sizeof(core->physical));
}

void domain_device_drain(locker_core_t *core)
{
    domain_event_slot_t event;
    while (locker_bounded_queue_try_pop(&core->event_ring, &event) == LOCKER_STATUS_OK) {
        if (event.completion != UINT32_MAX) {
            domain_completion_fail(core, event.completion, LOCKER_STATUS_NOT_READY);
        }
    }
    domain_reply_slot_t reply;
    while (locker_bounded_queue_try_pop(&core->reply_ring, &reply) == LOCKER_STATUS_OK) {
        if (reply.completion != UINT32_MAX) {
            domain_completion_fail(core, reply.completion, LOCKER_STATUS_NOT_READY);
        }
    }
    core->event_overflow = false;
    core->reply_overflow = false;
}

/* ---------- 周期泵 ---------- */

static void request_snapshot(locker_core_t *core)
{
    if (core->device == NULL) {
        return;
    }
    /* 在途不重发；间隔门保证 500 ms 的轮询周期真的成立（否则每个 100 ms 泵
       都会再发一次，等于把快照轮询加速 5 倍）。回复迟到由 pending 槽超时清理。 */
    if (core->snapshot_in_flight) {
        return;
    }
    if (core->snapshot_requested_ms != 0u &&
        domain_now(core) - core->snapshot_requested_ms < DOMAIN_SNAPSHOT_INTERVAL_MS) {
        return;
    }
    uint32_t request_no = domain_request_no(core, DOMAIN_PURPOSE_SNAPSHOT);
    if (request_no == 0u) {
        return;
    }
    locker_status_t status = locker_device_get_snapshot(core->device, request_no);
    if (status != LOCKER_STATUS_OK) {
        domain_pending_clear(core, DOMAIN_PURPOSE_SNAPSHOT);
        return;
    }
    core->snapshot_in_flight = true;
    core->snapshot_requested_ms = domain_now(core);
}

static void request_fetch(locker_core_t *core)
{
    if (core->device == NULL || core->fetch_in_flight) {
        return;
    }
    locker_event_key_t after;
    memset(&after, 0, sizeof(after));
    if (core->cursor.valid) {
        after = core->cursor.through;
    }
    locker_status_t status = locker_device_fetch_events(core->device, &after, 4u);
    if (status != LOCKER_STATUS_OK) {
        return;
    }
    core->fetch_in_flight = true;
    core->last_fetch_ms = domain_now(core);
}

static void request_ack(locker_core_t *core)
{
    if (core->device == NULL || core->ack_in_flight || !core->ack_cursor_valid) {
        return;
    }
    locker_status_t status = locker_device_ack_persisted(core->device, &core->ack_cursor);
    if (status != LOCKER_STATUS_OK) {
        return;
    }
    core->ack_in_flight = true;
}

static void send_health(locker_core_t *core)
{
    if (core->device == NULL) {
        return;
    }
    locker_health_report_t report;
    memset(&report, 0, sizeof(report));
    report.host_instance_id = core->instance_id;
    report.ui_instance_id = core->instance_id;
    report.core_progress = core->progress_counter;
    report.ui_progress = core->progress_counter;
    report.core_age_ms = 0u;
    report.ui_age_ms = 0u;
    report.supervisor_state = LOCKER_SUPERVISOR_STATE_UNKNOWN;
    (void)locker_device_send_health(core->device, &report);
}

void domain_device_pump(locker_core_t *core)
{
    if (core->device == NULL) {
        return;
    }
    uint64_t now = domain_now(core);
    if (core->last_pump_ms != 0u && now - core->last_pump_ms < DOMAIN_POLL_INTERVAL_MS) {
        return;
    }
    core->last_pump_ms = now;

    /* 快照必须在会话门之前请求：device_session_up 是「快照已经成功过」的结果，
       拿它当快照的前置就是循环等待，真实链路上永远发不出第一次 GET_SNAPSHOT。
       设备端在 HELLO 完成前会返回 NOT_READY，这里清槽后下个泵周期重试。 */
    if (!core->snapshot_in_flight ||
        now - core->snapshot_requested_ms >= DOMAIN_SNAPSHOT_INTERVAL_MS) {
        request_snapshot(core);
    }

    if (!core->device_session_up) {
        return;
    }
    if (!core->fetch_in_flight || now - core->last_fetch_ms >= DOMAIN_FETCH_INTERVAL_MS) {
        request_fetch(core);
    }
    request_ack(core);
    if (core->last_health_ms == 0u || now - core->last_health_ms >= DOMAIN_HEALTH_INTERVAL_MS) {
        core->last_health_ms = now;
        send_health(core);
    }
}

void domain_fetch_pump(locker_core_t *core)
{
    request_fetch(core);
}

/* ---------- 事件应用 ---------- */

static bool event_belongs_to_active(locker_core_t *core, const locker_device_event_t *event)
{
    if (!core->active.used) {
        return false;
    }
    if (locker_uuid_is_nil(&event->operation_id)) {
        return false;
    }
    return locker_uuid_equal(&event->operation_id, &core->active.id);
}

static void apply_physical_event(locker_core_t *core, const locker_device_event_t *event)
{
    domain_physical_t *physical = &core->physical;
    physical->valid = true;
    memcpy(physical->device_id, core->cursor.device_id, sizeof(physical->device_id));
    physical->journal_epoch = event->key.journal_epoch;
    physical->boot_id = event->key.boot_id;
    physical->physical_revision = event->physical_revision;
    physical->door = event->door;
    physical->item = event->item;
    if (event->supply_mv != 0xffffffffu) {
        physical->supply_valid = true;
        physical->supply_mv = event->supply_mv;
    }
    physical->observed_mono_ms = domain_now(core);
    physical->continuity_ok = core->cursor.continuity_ok;
    physical->dropped_events = 0u;
    physical->durable_cursor = event->key;
    core->device_snapshot_valid = true;
}

/* 目标变化：BORROW 期望物品离位，RETURN 期望物品在位。 */
static bool expected_item_for(const domain_operation_t *operation, uint8_t item)
{
    if (operation->kind == LOCKER_OPERATION_KIND_BORROW) {
        return item == LOCKER_ITEM_STATE_ABSENT;
    }
    if (operation->kind == LOCKER_OPERATION_KIND_RETURN) {
        return item == LOCKER_ITEM_STATE_PRESENT;
    }
    return false;
}

static void operation_advance_from_evidence(locker_core_t *core, domain_operation_t *operation)
{
    if (operation->state != LOCKER_OPERATION_STATE_ACCESS_ACTIVE) {
        return;
    }
    if (!operation->door_open_seen || core->physical.door != LOCKER_DOOR_STATE_CLOSED) {
        return;
    }
    bool changed = expected_item_for(operation, core->physical.item);
    uint8_t target = changed ? (uint8_t)LOCKER_OPERATION_STATE_WAIT_CONFIRM
                             : (uint8_t)LOCKER_OPERATION_STATE_NO_CHANGE;
    const char *reason = changed ? "PHYSICAL_CONFIRMED" : "NO_ITEM_CHANGE";
    if (domain_operation_set_state(core, operation, target, reason) != LOCKER_STATUS_OK) {
        return;
    }
    domain_operation_capture_physical(operation, &core->physical);
    (void)domain_persist_operation(core, operation);
    domain_publish(core, &operation->id, "operation", core->revision);
    if (!changed) {
        /* NO_CHANGE 是终态，占位必须释放，账本不变。 */
        operation->used = false;
        core->active.used = false;
    }
}

void domain_event_apply(locker_core_t *core, const locker_device_event_t *event)
{
    if (core == NULL || event == NULL) {
        return;
    }
    /* 只接受当前日志的事件；跨 epoch 的历史补传只走仓储导入，不改内存状态。 */
    if (core->cursor.valid && !locker_uuid_equal(&core->cursor.journal_epoch, &event->key.journal_epoch)) {
        return;
    }
    if (core->cursor.valid && key_compare(&event->key, &core->cursor.through) <= 0) {
        return; /* 已应用过 */
    }
    core->cursor.valid = true;
    core->cursor.journal_epoch = event->key.journal_epoch;
    core->cursor.through = event->key;
    /* live 事件带 24 位 device_id（适配器按握手身份填写）：用它补齐身份，
       补取页面才能构造出合法的 EventKey（wire 游标不含 device_id）。 */
    domain_learn_device_id(core, event->key.device_id);
    if (!event->live_transport) {
        core->cursor.continuity_ok = false;
        core->fetch_gap = true;
    }

    apply_physical_event(core, event);

    switch (event->event_type) {
    case LOCKER_DEVICE_EVENT_DOOR_OPENED:
        if (core->active.used) {
            core->active.door_open_seen = true;
            if (core->active.state == LOCKER_OPERATION_STATE_ACCESS_ACTIVE &&
                core->active.door_opened_ms == 0u) {
                core->active.door_opened_ms = domain_now(core);
            }
        }
        break;
    case LOCKER_DEVICE_EVENT_ITEM_REMOVED:
    case LOCKER_DEVICE_EVENT_ITEM_PRESENT:
        if (core->active.used) {
            core->active.item_transition_seen = true;
        }
        break;
    case LOCKER_DEVICE_EVENT_PULSE_STARTED:
        if (core->active.used &&
            core->active.state == LOCKER_OPERATION_STATE_ACCESS_PENDING) {
            if (domain_operation_set_state(core, &core->active, LOCKER_OPERATION_STATE_ACCESS_ACTIVE,
                                           "PULSE_STARTED") == LOCKER_STATUS_OK) {
                core->active.pulse_started_ms = domain_now(core);
                (void)domain_persist_operation(core, &core->active);
                domain_publish(core, &core->active.id, "operation", core->revision);
            }
        }
        break;
    case LOCKER_DEVICE_EVENT_CONTINUITY_LOST:
    case LOCKER_DEVICE_EVENT_JOURNAL_FAULT:
        core->cursor.continuity_ok = false;
        core->fetch_gap = true;
        break;
    default:
        break;
    }

    if (core->active.used && event_belongs_to_active(core, event)) {
        domain_operation_add_source_event(&core->active, &event->key);
        if (!event->live_transport) {
            core->active.history_complete = false;
        }
    }
    operation_advance_from_evidence(core, &core->active);
}

/* ---------- 响应应用 ---------- */

static void apply_snapshot(locker_core_t *core, const locker_wire_get_snapshot_response_t *snapshot)
{
    if (snapshot->result != 0u) {
        core->snapshot_in_flight = false;
        return;
    }
    core->device_session_up = true;
    core->session.valid = true;
    core->session.session_id = snapshot->session_id;
    core->session.journal_epoch = snapshot->journal_epoch;
    core->session.boot_id = snapshot->boot_id;
    core->session.device_mode = session_mode_from_wire(snapshot->device_mode);
    core->session.journal_state = snapshot->journal_state;
    core->session.action_watermark = snapshot->action_watermark;
    core->action_watermark = snapshot->action_watermark;

    domain_physical_t *physical = &core->physical;
    physical->valid = true;
    physical->journal_epoch = snapshot->journal_epoch;
    physical->boot_id = snapshot->boot_id;
    physical->physical_revision = snapshot->physical_revision;
    physical->door = snapshot->door;
    physical->item = snapshot->item;
    if ((snapshot->flags & 0x0001u) != 0u) {
        physical->supply_valid = true;
        physical->supply_mv = snapshot->supply_mv;
    }
    physical->observed_mono_ms = domain_now(core);
    physical->time_quality = LOCKER_TIME_QUALITY_HOST_SYNCED;
    physical->continuity_ok = core->cursor.continuity_ok;
    physical->dropped_events = snapshot->dropped_events;
    physical->durable_cursor = core->cursor.through;
    if ((snapshot->flags & 0x0004u) != 0u) {
        physical->continuity_ok = false;
        core->cursor.continuity_ok = false;
    }
    core->device_snapshot_valid = true;

    if (!core->cursor.valid) {
        core->cursor.valid = true;
        core->cursor.journal_epoch = snapshot->journal_epoch;
        core->cursor.through.boot_id = snapshot->boot_id;
        core->cursor.through.event_seq = 0u;
        core->cursor.through.journal_epoch = snapshot->journal_epoch;
    }
    core->snapshot_in_flight = false;
}

static void apply_fetch(locker_core_t *core, const locker_wire_fetch_events_response_t *page)
{
    core->fetch_in_flight = false;
    if (page->result != 0u) {
        return;
    }
    /* 先落库再 ACK（卡 :117「设备重连屏障：先补取落库再 ACK」）。导入失败不能
       把内存水位推过去：那会让 ACK 提前、重启后事件永久丢失。 */
    locker_status_t imported = domain_import_page(core, page);
    if (imported != LOCKER_STATUS_OK && imported != LOCKER_STATUS_NOT_READY) {
        core->fetch_gap = true;
        core->cursor.continuity_ok = false;
    }
    if (page->gap_before_page != 0u) {
        core->cursor.continuity_ok = false;
        core->fetch_gap = true;
    }
    if (page->has_more != 0u) {
        core->last_fetch_ms = 0u; /* 立即续取 */
    }
    if (core->device_snapshot_valid) {
        (void)domain_persist_device_state(core);
    }
}

static void apply_ack(locker_core_t *core, const locker_wire_ack_persisted_response_t *ack)
{
    core->ack_in_flight = false;
    if (ack->result != 0u) {
        return;
    }
    /* ack_persisted 是 wire 游标（boot_id/event_seq），与核心事件键的
       device/epoch 部分无关：只有不落后于待确认水位才算确认完成。 */
    if (ack->ack_persisted.boot_id > core->ack_cursor.boot_id ||
        (ack->ack_persisted.boot_id == core->ack_cursor.boot_id &&
         ack->ack_persisted.event_seq >= core->ack_cursor.event_seq)) {
        core->ack_cursor_valid = false;
    }
}

static void apply_execute(locker_core_t *core, const locker_wire_execute_unlock_response_t *response)
{
    core->execute_in_flight = false;
    if (core->pending_command_valid && !locker_uuid_equal(&response->command_id, &core->pending_command_id)) {
        return; /* 不是当前命令的回复 */
    }
    core->pending_command_valid = false;
    domain_operation_t *operation = &core->active;
    if (!operation->used || locker_uuid_is_nil(&response->operation_id) ||
        !locker_uuid_equal(&response->operation_id, &operation->id)) {
        return;
    }
    switch (response->result) {
    case LOCKER_WIRE_RESULT_OK:
        if (response->command_state == LOCKER_COMMAND_STATE_PULSE_STARTED) {
            if (domain_operation_set_state(core, operation, LOCKER_OPERATION_STATE_ACCESS_ACTIVE,
                                           "PULSE_STARTED") == LOCKER_STATUS_OK) {
                operation->pulse_started_ms = domain_now(core);
                (void)domain_persist_operation(core, operation);
                domain_publish(core, &operation->id, "operation", core->revision);
            }
        }
        break;
    case LOCKER_WIRE_RESULT_REPLAY_BLOCKED:
    case LOCKER_WIRE_RESULT_ACTION_GAP:
    case LOCKER_WIRE_RESULT_BAD_PRECONDITION:
    case LOCKER_WIRE_RESULT_PHYSICAL_CHANGED:
    case LOCKER_WIRE_RESULT_JOURNAL_CHANGED:
        (void)domain_operation_set_state(core, operation, LOCKER_OPERATION_STATE_REVIEW_REQUIRED,
                                         "COMMAND_REJECTED");
        (void)domain_persist_operation(core, operation);
        domain_publish(core, &operation->id, "operation", core->revision);
        break;
    case LOCKER_WIRE_RESULT_UNKNOWN_RESULT:
    case LOCKER_WIRE_RESULT_LOG_UNAVAILABLE:
        (void)domain_operation_set_state(core, operation, LOCKER_OPERATION_STATE_REVIEW_REQUIRED,
                                         "UNKNOWN_RESULT");
        (void)domain_persist_operation(core, operation);
        domain_publish(core, &operation->id, "operation", core->revision);
        break;
    default:
        break;
    }
}

static void apply_query_command(locker_core_t *core, const locker_wire_query_command_response_t *response)
{
    domain_operation_t *operation = &core->active;
    if (!operation->used || locker_uuid_is_nil(&response->operation_id)) {
        return;
    }
    if (!locker_uuid_equal(&response->operation_id, &operation->id)) {
        return;
    }
    if (response->result != LOCKER_WIRE_RESULT_OK) {
        (void)domain_operation_set_state(core, operation, LOCKER_OPERATION_STATE_REVIEW_REQUIRED,
                                         "QUERY_UNRESOLVED");
        (void)domain_persist_operation(core, operation);
        domain_publish(core, &operation->id, "operation", core->revision);
        return;
    }
    if (response->command_state == LOCKER_COMMAND_STATE_PULSE_STARTED &&
        operation->state == LOCKER_OPERATION_STATE_ACCESS_PENDING) {
        if (domain_operation_set_state(core, operation, LOCKER_OPERATION_STATE_ACCESS_ACTIVE,
                                       "PULSE_STARTED") == LOCKER_STATUS_OK) {
            operation->pulse_started_ms = domain_now(core);
            (void)domain_persist_operation(core, operation);
            domain_publish(core, &operation->id, "operation", core->revision);
        }
    }
}

void domain_reply_apply(locker_core_t *core, const locker_device_reply_t *reply)
{
    if (core == NULL || reply == NULL) {
        return;
    }
    uint8_t type = (uint8_t)(reply->message_type & 0x7fu);
    uint8_t purpose = DOMAIN_PURPOSE_NONE;
    (void)domain_pending_take(core, reply->request_no, &purpose);

    switch (type) {
    case 2u:
        apply_snapshot(core, &reply->body.get_snapshot);
        break;
    case 3u:
        apply_fetch(core, &reply->body.fetch_events);
        break;
    case 4u:
        apply_ack(core, &reply->body.ack_persisted);
        break;
    case 7u:
        apply_execute(core, &reply->body.execute_unlock);
        break;
    case 8u:
        apply_query_command(core, &reply->body.query_command);
        break;
    case 1u:
        /* HELLO 是设备身份的唯一权威来源（12 字节 UID -> 24 位小写 hex）。
           适配器已核对过 expected_device_id 与必要能力位，Core 只记录身份。 */
        if (reply->body.hello.result == 0u) {
            domain_learn_device_uid(core, reply->body.hello.device_uid);
            core->session.device_mode = session_mode_from_wire(reply->body.hello.device_mode);
            core->session.journal_state = reply->body.hello.journal_state;
        }
        break;
    case 5u:
        core->session.valid = true;
        core->session.device_mode = session_mode_from_wire(reply->body.resume_ready.device_mode);
        break;
    case 9u:
        core->session.device_mode = session_mode_from_wire(reply->body.host_health.device_mode);
        break;
    case 11u:
        core->physical.physical_revision = reply->body.release_context.physical_revision;
        break;
    default:
        break;
    }
}

/* ---------- 动作发送 ---------- */

locker_status_t domain_execute_intent(locker_core_t *core, domain_operation_t *operation)
{
    if (core->device == NULL || !core->device_session_up || !core->session.valid) {
        return LOCKER_STATUS_NOT_READY;
    }
    if (core->execute_in_flight) {
        return LOCKER_STATUS_BUSY;
    }
    locker_unlock_intent_t intent;
    memset(&intent, 0, sizeof(intent));
    intent.operation_id = operation->id;
    intent.command_id = operation->command_id;
    intent.session_id = core->session.session_id;
    intent.journal_epoch = core->session.journal_epoch;
    intent.expected_boot_id = core->session.boot_id;
    intent.expected_physical_revision = core->physical.physical_revision;
    intent.action_no = operation->action_no;
    intent.not_after_mcu_ms = core->now_ms + LOCKER_LOCK_PULSE_MAX_MS;
    intent.slot_id = 1u;
    intent.kind = (locker_operation_kind_t)operation->kind;
    locker_status_t status = locker_device_execute_once(core->device, &intent);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    core->execute_in_flight = true;
    core->pending_command_id = operation->command_id;
    core->pending_command_valid = true;
    core->pending_command_ms = domain_now(core);
    return LOCKER_STATUS_OK;
}

locker_status_t domain_query_command(locker_core_t *core, const locker_uuid_t *command_id)
{
    if (core->device == NULL || !core->device_session_up) {
        return LOCKER_STATUS_NOT_READY;
    }
    return locker_device_query_command(core->device, command_id);
}

/* ---------- sink ---------- */

static locker_status_t core_sink_reply(void *user, const locker_device_reply_t *reply)
{
    locker_core_t *core = (locker_core_t *)user;
    if (core == NULL || reply == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    domain_reply_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    slot.reply = *reply;
    slot.completion = UINT32_MAX;
    if (locker_bounded_queue_try_push(&core->reply_ring, &slot) != LOCKER_STATUS_OK) {
        /* 不能返回 NO_SPACE：那会让设备侧 shutdown 会话。这里跑在设备 reader 线程上，
           只能写原子标志；reply_overflow / fetch_gap 是 Core 线程私有字段，由
           domain_handle_device_reply 消费标志时置位。 */
        atomic_store(&core->reply_overflow_flag, true);
    }
    return LOCKER_STATUS_OK;
}

static locker_status_t core_sink_event(void *user, const locker_device_event_t *event)
{
    locker_core_t *core = (locker_core_t *)user;
    if (core == NULL || event == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    domain_event_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    slot.event = *event;
    slot.completion = UINT32_MAX;
    if (locker_bounded_queue_try_push(&core->event_ring, &slot) != LOCKER_STATUS_OK) {
        /* 同上：reader 线程只置原子标志，缺口与不连续由 Core 消费标志时锁存。 */
        atomic_store(&core->event_overflow_flag, true);
    }
    return LOCKER_STATUS_OK;
}

locker_status_t domain_device_bind(locker_core_t *core)
{
    if (core == NULL || core->device == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    locker_device_sink_t sink;
    memset(&sink, 0, sizeof(sink));
    sink.user = core;
    sink.reply = core_sink_reply;
    sink.event = core_sink_event;
    return locker_device_bind_sink(core->device, &sink);
}
