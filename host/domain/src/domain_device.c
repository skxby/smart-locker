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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "locker/common/log.h"

/* S2-07: the fetch/import path used to fail completely silently - a refused
 * request, a device error result and an import that returned NOT_READY all
 * looked like "connected but nothing happens", and the cabinet sat in
 * RECONCILING with an empty ledger. These lines are off unless
 * LOCKER_DEVICE_TRACE is set, and the budget stops a 5 Hz retry from flooding
 * the console: the first failures are what identify the cause. */
static void fetch_trace(const char *what, long value)
{
    static int budget = 24;
    if (budget <= 0 || getenv("LOCKER_DEVICE_TRACE") == NULL) {
        return;
    }
    --budget;
    fprintf(stderr, "fetch %s=%ld%s\n", what, value, budget == 0 ? " (further lines suppressed)" : "");
    fflush(stderr);
}

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
    if (core->import_cursor_valid) {
        /* 补取必须从「仓储已提交水位」继续，而不是视图游标 cursor.through：
           live 事件只推进视图游标、不落仓储（落库统一走 domain_import_page），
           用视图游标当 after 会跳过仓储还没见过的 seq，sim 返回的页与
           仓储已存水位不相邻，locker_repo_import_events 直接 CONFLICT，
           apply_fetch 随即双置 fetch_gap/continuity_ok（r184000 实测：
           after=1/6 的页被拒 status=6，CONFIRM 从此 physical_ready_for_action
           恒假，业务永远无法完成借出）。 */
        after = core->import_cursor;
    }
    locker_status_t status = locker_device_fetch_events(core->device, &after, 4u);
    if (status != LOCKER_STATUS_OK) {
        fetch_trace("refused", (long)status);
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
    /* UI 层在 S1-04 根本不存在：必须报「未知」，不能把 Core 自己的实例 ID 和
       进度计数器抄过去。以前 ui_instance_id=host_instance_id、
       ui_progress=core_progress、ui_age_ms=0，等于让 Core 活着就证明 UI 活着，
       监护侧据此会认为 UI 一直在新鲜推进（审计 R1-B-7，
       internal-interfaces.md:154「未知线上零 UUID / UINT32_MAX age」）。 */
    locker_uuid_clear(&report.ui_instance_id);
    report.core_progress = core->progress_counter;
    report.ui_progress = 0u;
    report.core_age_ms = 0u;
    report.ui_age_ms = UINT32_MAX;
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
    if (!core->config.runtime_health_owned && (core->last_health_ms == 0u || now - core->last_health_ms >= DOMAIN_HEALTH_INTERVAL_MS)) {
        core->last_health_ms = now;
        send_health(core);
    }
}

void domain_fetch_pump(locker_core_t *core)
{
    request_fetch(core);
}

/* ---------- 事件应用 ---------- */

static void wait_confirm_conflicted(locker_core_t *core);
static locker_status_t device_advance_persisted(locker_core_t *core, domain_operation_t *operation, uint8_t state,
                                                const char *reason_code);

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
    /* 连续性标志由 domain_event_apply 在本函数之前锁存（含 HISTORY_INCOMPLETE
       位与 live_transport 判断），这里只做一次快照复制，不能覆盖它。 */
    physical->continuity_ok = core->cursor.continuity_ok;
    /* dropped_events 是「当前 epoch 的累计关键事件丢失数」（uart-v1.md:50），
       不是「本帧丢了几条」。事件里不带这个累计值，就只能保留上一次快照报告的
       数字；以前这里写死 0，等于每来一条事件就把「已知丢过 N 条」抹成 0，
       与 HISTORY_INCOMPLETE 的锁存语义直接矛盾（审计 R4-B-2）。 */
    physical->durable_cursor = event->key;
    core->device_snapshot_valid = true;
    /* 新的物理事实到达：等确认的依据立刻按它检查（R1-B-4）。
       快照路径同样要走这条规则，不能只在 DEVICE_EVENT 里查。 */
    wait_confirm_conflicted(core);
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

/*
 * WAIT_CONFIRM 期间的新物理事实必须作废确认依据。
 *
 * 进 WAIT_CONFIRM 时冻结了当时的 door/item/physical_revision。此后只要出现
 * 任何一种变化——门又开了、物品离开期望值、物理版本推进、物品变成 UNKNOWN——
 * 就说明「用户看到的柜内状态」已经和用户将要确认的那次动作无关了。
 * 以前 confirm 只比当前门/物品和那个旧的 item_transition_seen 布尔，
 * 「开一次门再关回去、物品放回原位」就能把冲突洗掉，用户照旧点确认完成
 * （审计 R1-B-4：探针实测开门后仍为 WAIT_CONFIRM，随后可正常 complete）。
 *
 * 依据：stage-00-architecture.md:118「每个确认只对应那一组物理证据」、
 * internal-interfaces.md:116。
 */
static void wait_confirm_conflicted(locker_core_t *core)
{
    domain_operation_t *operation = &core->active;
    if (!operation->used || operation->state != LOCKER_OPERATION_STATE_WAIT_CONFIRM) {
        return;
    }
    if (!domain_wait_confirm_basis_conflicted(core, operation)) {
        return;
    }
    /* 转核验这一步本身要落库。写失败时内存可能仍是 WAIT_CONFIRM（candidate
       只在提交成功后装回），如果只依赖状态位，存储一恢复就能直接 confirm 完成，
       等于把一次已知冲突洗掉。所以无论落库成功与否，先在内存里闭锁
       （审计 R3-B-3 探针 B）。 */
    operation->wait_confirm_blocked = true;
    (void)domain_require_review(core, operation, "PHYSICAL_CHANGED");
}

static void operation_advance_from_evidence(locker_core_t *core, domain_operation_t *operation)
{
    if (operation->state != LOCKER_OPERATION_STATE_ACCESS_ACTIVE) {
        return;
    }
    if (!operation->door_open_seen || core->physical.door != LOCKER_DOOR_STATE_CLOSED) {
        return;
    }
    /* 已知缺口时不能自动宣布「完整开关门且始终未变」或「完整开关门且目标已变」：
       stage-00-architecture.md:115-116 的两条自动终结路径都以「完整开关门」为前提，
       uart-v1.md:370 要求存在已知缺口时关联业务进核验并保留占位。
       以前跳号只把 cursor.continuity_ok 置假，证据推进照旧跑完，于是
       seq=1,3,4 这种缺一条的流照样自动 NO_CHANGE、删 slot_guards、登记释放
       （审计 R3-B-12：探针实测 guard 被删）。丢掉的那条可能就是「物品离位」。 */
    if (!core->cursor.continuity_ok || core->fetch_gap || !operation->history_complete) {
        (void)domain_require_review(core, operation, "HISTORY_INCOMPLETE");
        return;
    }
    /* 传感器没给出明确物品状态时不能替用户宣布「什么都没变」：
       NO_CHANGE 的语义是「回到了原基线、账本不变」（stage-00-architecture.md:116），
       UNKNOWN 既不能证明回到基线，也不能证明变化发生——只能进核验。
       以前这里把 UNKNOWN 直接当 changed=false，于是「开着门、物品状态读不出来」
       被静默结案成 NO_CHANGE，还顺手释放了占位（审计 R3-B-11 探针①）。 */
    if (core->physical.item == LOCKER_ITEM_STATE_UNKNOWN) {
        (void)domain_require_review(core, operation, "PHYSICAL_UNKNOWN");
        return;
    }
    bool changed = expected_item_for(operation, core->physical.item);
    if (!changed) {
        /* 没变成期望终态还不够：必须确认确实回到了原基线。
           原基线本身就是 UNKNOWN（申请时传感器没读到），或者当前物品既不等于
           期望终态也不等于原基线，都不能宣布 NO_CHANGE。 */
        if (!operation->has_baseline || operation->baseline.item == LOCKER_ITEM_STATE_UNKNOWN ||
            core->physical.item != operation->baseline.item) {
            (void)domain_require_review(core, operation, "PHYSICAL_MISMATCH");
            return;
        }
    }
    uint8_t target = changed ? (uint8_t)LOCKER_OPERATION_STATE_WAIT_CONFIRM
                             : (uint8_t)LOCKER_OPERATION_STATE_NO_CHANGE;
    const char *reason = changed ? "PHYSICAL_CONFIRMED" : "NO_ITEM_CHANGE";

    /* 状态跃迁、物理快照、占位释放在同一个候选副本 + 同一个 change 里完成：
       NO_CHANGE 是终态，占位必须和终态一起落库。只清内存 used 不删
       slot_guards，下一次申请的 ABSENT 检查会永远冲突，重启后也修不回来
       （审计 R1-B-2：探针实测 state=NO_CHANGE、active=0、guard_remains=1）。
       提交失败则内存保持 ACCESS_ACTIVE，下一帧证据还会再试一次。 */
    domain_operation_t candidate = *operation;
    if (domain_operation_set_state(core, &candidate, target, reason) != LOCKER_STATUS_OK) {
        return;
    }
    domain_operation_capture_physical(&candidate, &core->physical);
    domain_change_t change;
    domain_change_init(core, &change, core->revision, core->revision + 1u);
    if (domain_operation_persist(core, &change, &candidate) != LOCKER_STATUS_OK) {
        return;
    }
    if (!changed && !domain_change_delete(&change, "slot_guards", "1")) {
        return;
    }
    if (domain_change_commit(core, &change) != LOCKER_STATUS_OK) {
        return;
    }
    /* NO_CHANGE 是终态：柜格占位（内存 used 与仓库 slot_guards）都释放，
       但状态与原因要留在这里——上层要靠它解释「开了门却什么都没拿走」，
       整个 memset 掉会让操作看起来从未发生过（测试与 5xx 说明都读 state）。 */
    if (!changed) {
        candidate.used = false;
    }
    *operation = candidate;
    if (!changed && candidate.has_command) {
        /* NO_CHANGE 同样是终态：设备侧那次动作上下文也必须释放，
           否则下一次 EXECUTE 永远 BUSY（审计 R3-B-2 探针：NO_CHANGE 之后
           release_pending=0、release_calls=0，柜子只能用一次）。 */
        core->release_pending = true;
        core->release_sent = false;
        core->release_ms = domain_now(core);
        core->release_operation_id = candidate.id;
        core->release_command_id = candidate.command_id;
    }
    if (changed && operation == &core->active) {
        /* 进 WAIT_CONFIRM 时冻结物理依据：等待用户确认期间任何新的物理事实
           都必须让这份依据作废并立刻进核验（见 wait_confirm_conflicted）。
           代际（boot_id/journal_epoch）必须一起冻结，否则换 boot 后版本号
           从头计数，旧依据会被误判成仍然有效（审计 R3-B-3）。 */
        core->active.wait_confirm_basis_valid = true;
        core->active.wait_confirm_physical_revision = core->physical.physical_revision;
        core->active.wait_confirm_item = core->physical.item;
        core->active.wait_confirm_door = core->physical.door;
        core->active.wait_confirm_boot_id = core->physical.boot_id;
        core->active.wait_confirm_journal_epoch = core->physical.journal_epoch;
        core->active.wait_confirm_blocked = false;
    }
    domain_publish(core, &candidate.id, "operation", core->revision);
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
    /* 跳号检查：同一 boot 内事件序号必须逐条 +1。
       设备端口只校验会话与解码，不替 Core 检查连续性（port.c:354/386 只做
       cursor_matches 的「不倒退」判断）；这里不查的话，seq=1,3,4 这样跳号
       到达的事件流会被当成完整历史，直接推进到 WAIT_CONFIRM 并允许确认完成，
       丢掉的那一条（可能就是「物品离位」）永远补不回来
       （审计 R3-B-12：探针实测 continuity=1、fetch_gap=0、history_complete=1）。
       跨 boot 的序号回绕是正常的（设备重启后从 1 重新计数），不在这里判错。 */
    if (core->cursor.through.boot_id == event->key.boot_id && core->cursor.through.event_seq != 0u &&
        event->key.event_seq != core->cursor.through.event_seq + 1u) {
        core->cursor.continuity_ok = false;
        core->fetch_gap = true;
        if (core->active.used) {
            core->active.history_complete = false;
        }
    }
    core->cursor.through = event->key;
    /* live 事件带 24 位 device_id（适配器按握手身份填写）：用它补齐身份，
       补取页面才能构造出合法的 EventKey（wire 游标不含 device_id）。 */
    domain_learn_device_id(core, event->key.device_id);
    /* HISTORY_INCOMPLETE（observation_flags bit2）是**锁存**，不是一次快照结论：
       uart-v1.md:50 明确「即便 event_seq 连续也不能认为无丢失」，:353 要求
       CONTINUITY_LOST 同时置 HISTORY_INCOMPLETE 并关闭新开锁资格。
       以前只有快照路径看这一位，实时事件完全不读 event->flags——设备报「我丢了
       事件」时主机照旧按序号连续推进到 WAIT_CONFIRM 并允许确认完成
       （审计 R4-B-2 探针 A）。 */
    if ((event->flags & DOMAIN_OBSERVATION_FLAG_HISTORY_INCOMPLETE) != 0u) {
        core->cursor.continuity_ok = false;
        core->fetch_gap = true;
        if (core->active.used) {
            core->active.history_complete = false;
        }
    }
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
            /* 事件路径也走「候选副本 + 一次提交」：提交失败时内存保持
               ACCESS_PENDING，下一帧证据或查询补救还能再推一次，
               而不是留下「内存已激活、库里还挂着」的分叉（审计 R3-B-7）。 */
            (void)device_advance_persisted(core, &core->active, LOCKER_OPERATION_STATE_ACCESS_ACTIVE,
                                           "PULSE_STARTED");
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
    /* 等确认期间的新物理事实立刻作废确认依据（R1-B-4）。
       放在 advance 之前：冲突优先于「按旧证据推进」。 */
    wait_confirm_conflicted(core);
    operation_advance_from_evidence(core, &core->active);
}

/* ---------- 响应应用 ---------- */

static void apply_snapshot(locker_core_t *core, const locker_wire_get_snapshot_response_t *snapshot)
{
    if (snapshot->result != 0u) {
        core->snapshot_in_flight = false;
        /* 快照取不到（设备忙/日志不可用/链路拒绝）就不再持有「已知物理事实」：
           保留 device_snapshot_valid=true 会让缓存值无限期有效，管理员或超时
           推进都能拿着几小时前的 door/item 去改账（审计 R1-B-12）。
           物理事实本身保留在 physical 里供诊断，但投影与准入改用 valid=false。 */
        core->device_snapshot_valid = false;
        core->physical.valid = false;
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

    /* 设备换了 journal epoch，说明它的日志被重新 provision 过：库里保存的
       导入/ACK 游标属于一条已经不存在的日志。端口按 epoch 校验游标身份
       （port.c:device_cursor_matches），旧 epoch 的游标会让 FETCH 永远
       CONFLICT——现场表现是「连着、但一条事件都不导入、也永远对不了账」
       （S2-07 实测 fetch refused=6）。这里把游标归零，让 FETCH 从新日志
       开头重新接入；已导入的旧 epoch 事件仍留在仓储里。连续性不在这里放宽，
       仍由首个导入页重新推导，对账资格照旧由 reconcile 把关。 */
    if (core->cursor.valid &&
        !locker_uuid_equal(&core->cursor.journal_epoch, &snapshot->journal_epoch)) {
        core->cursor.valid = false;
        memset(&core->cursor.through, 0, sizeof(core->cursor.through));
        core->cursor.continuity_ok = false;
        core->import_cursor_valid = false;
        memset(&core->import_cursor, 0, sizeof(core->import_cursor));
        core->import_continuity = false;
        core->ack_cursor_valid = false;
        memset(&core->ack_cursor, 0, sizeof(core->ack_cursor));
        core->fetch_gap = false;
        fetch_trace("epoch_changed", (long)snapshot->boot_id);
    }

    if (!core->cursor.valid) {
        core->cursor.valid = true;
        core->cursor.journal_epoch = snapshot->journal_epoch;
        /* through 是完整 EventKey：补取请求把它原样当 after 送设备端口，
           落库时又被 PhysicalSnapshot.durable_cursor 引用（两项都绕不过去）。
           两个约束必须同时满足：
           1) uart-v1.md:38 —— cursor 的 (0,0) 是「开始/空日志」哨兵，
              禁止只有 boot_id/event_seq 之一为 0。所以水位必须留在 (0,0)，
              不能把快照的 boot_id 配 0 号 event_seq（半零游标会被
              port.c:device_cursor_matches 第一行直接判非法 → CONFLICT →
              补取永不发出 → import_cursor 不推进 → ACK 不产生 →
              设备停在 RECONCILING）。
           2) internal-data.schema.json 的 EventKey 要求 device_id 匹配
              ^[0-9a-f]{24}$ 且 journal_epoch 是合法 UUID；空 device_id 会让
              device_state 文档校验失败。
           因此这里只补身份与 epoch，boot/seq 保持 0；水位随后由
           domain_import_page 用仓储提交值推进。设备身份来自更早的 HELLO 回复
           （domain_reply_apply case 1 → domain_learn_device_id）。 */
        memset(&core->cursor.through, 0, sizeof(core->cursor.through));
        memcpy(core->cursor.through.device_id, core->cursor.device_id,
               sizeof(core->cursor.through.device_id));
        core->cursor.through.journal_epoch = snapshot->journal_epoch;
    }

    domain_physical_t *physical = &core->physical;
    /* GET_SNAPSHOT 是异步应答：请求发出与应答到达之间可能已经应用了更新的
       DEVICE_EVENT。物理版本随每次门/物品变化严格递增，所以「快照版本不小于
       现有版本」才允许覆盖；否则一个晚到的旧快照会把刚成立的新事实回退掉，
       门/物品回到旧值、WAIT_CONFIRM 的冻结依据也跟着被误判成冲突
       （r203200 三套里 TSan 那轮的偶发失败就是这个时序）。同 boot 才比较版本，
       换 boot 说明设备重启过，新事实优先。 */
    bool accept_physical = !physical->valid || snapshot->boot_id != physical->boot_id ||
                           snapshot->physical_revision >= physical->physical_revision;
    if (accept_physical) {
        physical->valid = true;
        /* 设备身份只能来自 HELLO（domain_learn_device_uid）：GET_SNAPSHOT 回复本身
           不带 device_uid。这里必须把已知身份写进 physical，否则
           domain_persist_device_state 会因 device_id 不是 24 位十六进制直接
           NOT_READY，device_state 集合永远为空、重启后拿不到上次的物理基线。 */
        memcpy(physical->device_id, core->cursor.device_id, sizeof(physical->device_id));
        physical->journal_epoch = snapshot->journal_epoch;
        physical->boot_id = snapshot->boot_id;
        physical->physical_revision = snapshot->physical_revision;
        physical->door = snapshot->door;
        physical->item = snapshot->item;
        if ((snapshot->flags & DOMAIN_OBSERVATION_FLAG_SUPPLY_VALID) != 0u) {
            physical->supply_valid = true;
            physical->supply_mv = snapshot->supply_mv;
        }
        physical->observed_mono_ms = domain_now(core);
        physical->time_quality = LOCKER_TIME_QUALITY_HOST_SYNCED;
        physical->continuity_ok = core->cursor.continuity_ok;
        physical->dropped_events = snapshot->dropped_events;
        physical->durable_cursor = core->cursor.through;
        if ((snapshot->flags & DOMAIN_OBSERVATION_FLAG_HISTORY_INCOMPLETE) != 0u) {
            /* 锁存：新会话、后续连续页、RESUME_READY 都不能清（uart-v1.md:50/:402）。 */
            physical->continuity_ok = false;
            core->cursor.continuity_ok = false;
            core->fetch_gap = true;
            if (core->active.used) {
                core->active.history_complete = false;
            }
        }
    }
    core->device_snapshot_valid = true;
    core->snapshot_in_flight = false;
    /* 记录设备侧当前持有的动作上下文。这是主机唯一能「看见」MCU has_active
       的地方，两个用途都不能省：
       ① 释放 CAS 失败（CONFLICT）后不能凭「物理版本变了」就宣布设备已放手，
          uart-v1.md:402 要求先重新查询——新快照里 active_operation_id 已经
          不是我们要释放的那条，才算真放手（审计 R3-B-2 后半）。
       ② 进程重启后内存里的 release_pending 全部丢失，而 MCU 侧 has_active
          还在；不靠快照补登记，重启后的第一次 EXECUTE 就会一直 BUSY。
       旧快照同样不能用来判定放手：那时设备可能还没执行到我们的释放请求。 */
    if (accept_physical) {
        core->device_active_operation_id = snapshot->active_operation_id;
        core->device_active_command_id = snapshot->active_command_id;
        core->device_active_valid = true;
    }
    /* 重启补登记：终结落库后崩溃，内存里的 release_pending 全丢，而 MCU 侧
       has_active 还在。不靠快照把它补回来，重启后的第一次 EXECUTE 会一直 BUSY，
       柜子从此只能用一次（审计 R3-B-2：以前 device_active_* 只有赋值、没有
       任何消费者，release_calls 永远是 0）。
       **但必须先在账本里查到这条操作已经终结**：快照里的 active_operation_id
       也可能是一条还没走完的操作（比如主机刚重启、内存里 active 还没恢复）。
       未经账本核对就发 RELEASE_CONTEXT，等于凭一条设备侧缓存去替业务收尾，
       而释放之后那条未决操作再也拿不到动作上下文（审计 round-5 第 1 项）。 */
    if (accept_physical && !core->release_pending && !locker_uuid_is_nil(&snapshot->active_operation_id)) {
        const bool ours = core->active.used && locker_uuid_equal(&core->active.id, &snapshot->active_operation_id);
        domain_operation_t settled;
        const bool loaded = !ours && domain_operation_load(core, &snapshot->active_operation_id, 1, &settled) == LOCKER_STATUS_OK;
        if (!ours && loaded && domain_terminal_state(settled.state)) {
            core->release_pending = true;
            core->release_sent = false;
            core->release_recheck = false;
            core->release_ms = domain_now(core);
            core->release_operation_id = snapshot->active_operation_id;
            core->release_command_id = snapshot->active_command_id;
        }
        /* 查不到或仍未决：什么都不做。未决操作继续走它自己的超时/核验出口，
           绝不能凭快照就替它放手（uart-v1.md:402 只允许在「已提交终态」后释放）。 */
    }
    if (core->release_pending && accept_physical) {
        if (locker_uuid_is_nil(&snapshot->active_operation_id)) {
            /* 设备侧没有活动上下文了：释放目的已经达成（可能是我们上一次
               请求其实生效了但回执丢了，也可能是设备自己清了）。 */
            core->release_pending = false;
            core->release_sent = false;
            core->release_recheck = false;
        } else if (!locker_uuid_equal(&snapshot->active_operation_id, &core->release_operation_id)) {
            /* 设备挂着的是别的动作：不是我们要释放的那条，也说明那条已经不在了。 */
            core->release_pending = false;
            core->release_sent = false;
            core->release_recheck = false;
        } else if (core->release_recheck) {
            /* 重新查询的结论：设备仍挂着**我们要释放的**那条上下文。
               uart-v1.md:402 要求「先重新查询再决定」，不是「永不再试」。
               快照把 physical_revision 刷新过了，于是解除只查询状态，让泵用
               新的 guard 重发一次；再撞 CONFLICT 会重新进入查询，形成
               「查询 → 重发 → 查询」的收敛循环，而不是旧版本请求的死循环。 */
            core->release_recheck = false;
            core->release_sent = false;
        }
    }
    /* 快照是权威的物理事实：WAIT_CONFIRM 期间新快照里的门/物品/版本若与冻结
       依据不符，必须立刻进核验。只在事件路径查冲突不够——设备重启或补取之后
       往往先拿到快照、事件还没到，那时旧依据已经失效（审计 R3-B-3：
       探针冻结 revision=7、收到 revision=8 的成功快照后仍停在 WAIT_CONFIRM）。 */
    wait_confirm_conflicted(core);
}

static void apply_fetch(locker_core_t *core, const locker_wire_fetch_events_response_t *page)
{
    core->fetch_in_flight = false;
    if (page->result != 0u) {
        /* A device-side refusal used to vanish here: no gap flag, no retry
         * backoff change, no log - the ledger simply stayed empty. */
        fetch_trace("reply_result", (long)page->result);
        return;
    }
    /* 先落库再 ACK（卡 :117「设备重连屏障：先补取落库再 ACK」）。导入失败不能
       把内存水位推过去：那会让 ACK 提前、重启后事件永久丢失。 */
    locker_status_t imported = domain_import_page(core, page);
    if (imported != LOCKER_STATUS_OK && imported != LOCKER_STATUS_NOT_READY) {
        fetch_trace("import_status", (long)imported);
        core->fetch_gap = true;
        core->cursor.continuity_ok = false;
    } else if (imported == LOCKER_STATUS_NOT_READY) {
        fetch_trace("import_not_ready", (long)page->event_count);
    }
    if (imported == LOCKER_STATUS_INVALID) {
        /* One line with the page boundaries: the storage import rejects a page
         * whose first_available/next_after/last_committed disagree with its
         * events, and without these numbers the rejection is untraceable. */
        fprintf(stderr,
                "fetch page invalid: n=%u has_more=%u gap=%u "
                "fa=%llu/%llu lc=%llu/%llu na=%llu/%llu ev0=%llu/%llu evN=%llu/%llu\n",
                (unsigned)page->event_count, (unsigned)page->has_more,
                (unsigned)page->gap_before_page,
                (unsigned long long)page->first_available.boot_id,
                (unsigned long long)page->first_available.event_seq,
                (unsigned long long)page->last_committed.boot_id,
                (unsigned long long)page->last_committed.event_seq,
                (unsigned long long)page->next_after.boot_id,
                (unsigned long long)page->next_after.event_seq,
                page->event_count ? (unsigned long long)page->events[0].boot_id : 0ull,
                page->event_count ? (unsigned long long)page->events[0].event_seq : 0ull,
                page->event_count ? (unsigned long long)page->events[page->event_count - 1u].boot_id : 0ull,
                page->event_count ? (unsigned long long)page->events[page->event_count - 1u].event_seq : 0ull);
        fflush(stderr);
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

/*
 * 设备路径上的状态推进：候选副本 + 一次提交，成功才动内存。
 *
 * 以前这里直接 domain_operation_set_state(core, &core->active, ...) 再
 * (void)domain_persist_operation(...)：提交失败时内存已是新状态、库里还是旧状态，
 * 而 domain_require_review 遇到「内存已经是 REVIEW_REQUIRED」会短路返回 OK，
 * 于是核验结果永远补不进仓储（审计 R3-B-7：探针注入 commit_status=IO_ERROR 后
 * active=REVIEW_REQUIRED、stored=ACCESS_PENDING、publish_calls=1）。
 */
static locker_status_t device_advance_persisted(locker_core_t *core, domain_operation_t *operation, uint8_t state,
                                                const char *reason_code)
{
    domain_operation_t candidate = *operation;
    locker_status_t status = domain_operation_set_state(core, &candidate, state, reason_code);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    if (state == LOCKER_OPERATION_STATE_ACCESS_ACTIVE) {
        candidate.pulse_started_ms = domain_now(core);
    }
    status = domain_persist_operation(core, &candidate);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    *operation = candidate;
    if (core->active.used && locker_uuid_equal(&core->active.id, &candidate.id)) {
        core->active = candidate;
    }
    domain_publish(core, &candidate.id, "operation", core->revision);
    return LOCKER_STATUS_OK;
}

static void apply_execute(locker_core_t *core, const locker_wire_execute_unlock_response_t *response)
{
    core->execute_in_flight = false;
    domain_operation_t *operation = &core->active;

    /* 失败应答没有 body：codec 把非零 result 折叠成 18 字节前缀
       （「A failed response is exactly the 18-byte prefix and nothing else」），
       command_id / operation_id 全零，拿它们做匹配必然失败——EXECUTE 已发出、
       应答已回来，操作却永远停在 ACCESS_PENDING 等到 OPEN_TIMEOUT
       （集成测试五个注入阶段实测卡死）。EXECUTE 同一时刻最多一条在飞
       （outbox_pump 的 execute_in_flight 门），所以失败应答直接落到当前
       挂起的命令上；成功应答仍然要求 body 与 pending 一致。 */
    if (response->result != LOCKER_WIRE_RESULT_OK &&
        locker_uuid_is_nil(&response->command_id) &&
        locker_uuid_is_nil(&response->operation_id)) {
        /* 只在「已武装、等执行应答」时才落状态：失败应答不会启动脉冲，
           若状态已经前进（事件流已推进），这条失败只能是迟到的旧应答。 */
        if (!operation->used || operation->state != LOCKER_OPERATION_STATE_ACCESS_PENDING) {
            return;
        }
        switch (response->result) {
        case LOCKER_WIRE_RESULT_REPLAY_BLOCKED:
        case LOCKER_WIRE_RESULT_ACTION_GAP:
        case LOCKER_WIRE_RESULT_BAD_PRECONDITION:
        case LOCKER_WIRE_RESULT_PHYSICAL_CHANGED:
        case LOCKER_WIRE_RESULT_JOURNAL_CHANGED:
            /* 设备侧明确拒绝：结果已定，清 pending 停掉查询补救。 */
            core->pending_command_valid = false;
            (void)domain_require_review(core, operation, "COMMAND_REJECTED");
            break;
        case LOCKER_WIRE_RESULT_UNKNOWN_RESULT:
        case LOCKER_WIRE_RESULT_LOG_UNAVAILABLE:
            /* 结果未知：设备自己都无法裁决，同样停止查询（查也查不回）。 */
            core->pending_command_valid = false;
            (void)domain_require_review(core, operation, "UNKNOWN_RESULT");
            break;
        default:
            /* 会话/传输层失败码（NOT_READY/BUSY 等）：命令仍可能已被受理，
               保留 pending 让 domain_timeouts 的「只查询不重发」补救继续工作。 */
            return;
        }
        /* 状态推进已经由 domain_require_review 提交并发布，这里不再补一次：
           补写会让「提交失败」被后面的成功覆盖成假象（审计 R3-B-7）。 */
        return;
    }

    if (core->pending_command_valid && !locker_uuid_equal(&response->command_id, &core->pending_command_id)) {
        return; /* 不是当前命令的回复 */
    }
    core->pending_command_valid = false;
    if (!operation->used || locker_uuid_is_nil(&response->operation_id) ||
        !locker_uuid_equal(&response->operation_id, &operation->id)) {
        return;
    }
    switch (response->result) {
    case LOCKER_WIRE_RESULT_OK:
        if (response->command_state == LOCKER_COMMAND_STATE_PULSE_STARTED) {
            (void)device_advance_persisted(core, operation, LOCKER_OPERATION_STATE_ACCESS_ACTIVE, "PULSE_STARTED");
        }
        break;
    case LOCKER_WIRE_RESULT_REPLAY_BLOCKED:
    case LOCKER_WIRE_RESULT_ACTION_GAP:
    case LOCKER_WIRE_RESULT_BAD_PRECONDITION:
    case LOCKER_WIRE_RESULT_PHYSICAL_CHANGED:
    case LOCKER_WIRE_RESULT_JOURNAL_CHANGED:
        (void)domain_require_review(core, operation, "COMMAND_REJECTED");
        break;
    case LOCKER_WIRE_RESULT_UNKNOWN_RESULT:
    case LOCKER_WIRE_RESULT_LOG_UNAVAILABLE:
        (void)domain_require_review(core, operation, "UNKNOWN_RESULT");
        break;
    default:
        break;
    }
}

static void apply_query_command(locker_core_t *core, const locker_wire_query_command_response_t *response)
{
    domain_operation_t *operation = &core->active;
    /* 查询是对一条真实命令的核对，应答总是回显完整 body；
       但若设备/链路层折叠出 body 全零的失败应答（同 apply_execute 的
       18 字节前缀形态），ID 门会把它拦成静默丢弃——查询本来就是为了
       解开「结果未知」，此时应按失败映射进复核，而不是卡住状态机。 */
    const bool body_nil = locker_uuid_is_nil(&response->operation_id);
    if (!body_nil && operation->used &&
        !locker_uuid_equal(&response->operation_id, &operation->id)) {
        return;
    }
    /* 查询就是「在飞 EXECUTE 的收尾手段」：应答一回来就说明主机不再等它了。
       以前只在 apply_execute / reset 清这个标志，丢应答的路径上
       execute_in_flight 永久为真：outbox_pump 的发送门永远关闭，后续任何
       合法新命令都发不出去；「放弃等待」又要求 !execute_in_flight 才清
       pending，于是整台柜子卡死在一条旧命令上（审计 R1-B-11）。 */
    if (core->pending_command_valid && !locker_uuid_is_nil(&core->pending_command_id)) {
        core->execute_in_flight = false;
    }
    if (!operation->used) {
        /* 业务早已终结（例如事件流先把操作推进到 WAIT_CONFIRM 再走完），
           这条查询只是给在飞命令收尸：结论已经不影响任何业务状态，
           但 in-flight 与 pending 必须在这里清掉，否则新命令永远发不出
           （审计 R3-B-8：操作终结后 apply_query_command 因 !used 提前返回）。 */
        core->pending_command_valid = false;
        core->pending_command_queried = false;
        return;
    }
    if (response->result != LOCKER_WIRE_RESULT_OK) {
        /* 查询给出了确定结论：无论能否对应到 operation，都停止继续查询。 */
        core->pending_command_valid = false;
        core->pending_command_queried = false;
        if (!body_nil) {
            (void)domain_require_review(core, operation, "QUERY_UNRESOLVED");
        }
        return;
    }
    core->pending_command_valid = false;
    core->pending_command_queried = false;
    if (response->command_state == LOCKER_COMMAND_STATE_PULSE_STARTED &&
        operation->state == LOCKER_OPERATION_STATE_ACCESS_PENDING) {
        (void)device_advance_persisted(core, operation, LOCKER_OPERATION_STATE_ACCESS_ACTIVE, "PULSE_STARTED");
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
        /* 失败应答被折叠成 18 字节前缀（body 全零）：device_mode=0 会被
           解释成 OFFLINE 并把已经 READY 的会话拉下马，之后再也没人恢复
           （只有 HELLO/RESUME_READY 能再写 mode）。只有 result==OK 的
           应答才允许改会话状态——失败的 RESUME_READY 只能说明「设备还
           没准备好放行」，不能反向证明会话失效。 */
        if (reply->body.resume_ready.result == LOCKER_WIRE_RESULT_OK) {
            core->session.valid = true;
            core->session.device_mode = session_mode_from_wire(reply->body.resume_ready.device_mode);
        }
        break;
    case 9u:
        /* 同 case 5：host_health 的失败应答（NOT_READY 等）body 全零，
           无条件写 mode 会把 READY 会话误降级（集成测试 borrow 流程实测
           CONFIRM_PRECONDITION 由这里引起）。 */
        if (reply->body.host_health.result == LOCKER_WIRE_RESULT_OK) {
            core->session.device_mode = session_mode_from_wire(reply->body.host_health.device_mode);
        }
        break;
    case 11u:
        if (reply->body.release_context.result == LOCKER_WIRE_RESULT_OK) {
            core->physical.physical_revision = reply->body.release_context.physical_revision;
            /* 设备侧已经放掉动作上下文：下一次 EXECUTE 不会再撞 has_active。
               释放本身不改连续性、不动水位、不重开旧动作（uart-v1.md:402）。 */
            core->release_pending = false;
            core->release_sent = false;
            core->release_recheck = false;
        }
        break;
    case 10u:
        /* TIME_ANCHOR：mcu 与主机单调时钟的保守偏移区间。没有这个分支，
           主机时间会被直接当成 MCU 时间（审计 R1-B-6）。 */
        domain_anchor_apply(core, reply);
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
    /* 发送必须原样透传 arm 阶段已经提交落库的那一组值，不能在这里重新采一遍。
       以前这里用当前 session/physical 重建 guard、把 not_after 写成
       「主机单调时钟 + 脉冲上限」，于是落库的冻结意图（outbox_* 与
       outbox_not_after_ms）与实际发出的帧可以不一致：审计探针实测
       stored=503000（MCU 时钟域）、sent=2000（主机时钟域），MCU 拿到的是一个
       荒谬的期限（审计 R3-B-1，internal-interfaces.md:125/:133）。
       冻结值仍然要与当前会话对得上，否则就是换了会话/换了物理版本，
       这种时候不能发，交给泵的 SESSION_CHANGED / PHYSICAL_CHANGED 分支进核验。 */
    if (operation->outbox_not_after_ms == 0u) {
        return LOCKER_STATUS_NOT_READY; /* 没有冻结期限就不发（宁可进核验）。 */
    }
    if (core->session.boot_id != operation->outbox_boot_id ||
        !locker_uuid_equal(&core->session.journal_epoch, &operation->outbox_journal_epoch) ||
        !locker_uuid_equal(&core->session.session_id, &operation->outbox_session_id)) {
        return LOCKER_STATUS_CONFLICT;
    }
    if (core->physical.physical_revision != operation->outbox_physical_revision) {
        return LOCKER_STATUS_CONFLICT;
    }
    locker_unlock_intent_t intent;
    memset(&intent, 0, sizeof(intent));
    intent.operation_id = operation->id;
    intent.command_id = operation->command_id;
    intent.session_id = operation->outbox_session_id;
    intent.journal_epoch = operation->outbox_journal_epoch;
    intent.expected_boot_id = operation->outbox_boot_id;
    intent.expected_physical_revision = operation->outbox_physical_revision;
    intent.action_no = operation->action_no;
    intent.not_after_mcu_ms = operation->outbox_not_after_ms;
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
