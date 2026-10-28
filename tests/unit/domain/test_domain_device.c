#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 单元测试：设备桥（domain_device.c）与设备事件/响应入口
 * （domain_commands.c:974-1006）。
 *
 * 钉死的东西：
 *   1. 周期泵的三重门：100 ms 轮询间隔、500 ms 快照、200 ms 补取、1000 ms 健康；
 *      会话未建立时一个请求都不发（不能对着没握手的设备刷帧）。
 *   2. 事件只接受当前 journal_epoch 且严格大于已应用水位；重复/旧 epoch/跨 boot
 *      回退都不得改动内存状态，更不能推进操作。
 *   3. 只有 PULSE_STARTED 能把 ACCESS_PENDING 推到 ACCESS_ACTIVE；EXECUTE 回复里的
 *      ACCEPTED 不推进（契约 :221「ACK 不是开门」）。
 *   4. 缺口是两个独立标志：cursor.continuity_ok=false 表示「事件流不连续」，
 *      fetch_gap=true 表示「必须补取」。补传事件、CONTINUITY_LOST、JOURNAL_FAULT、
 *      快照 flags bit2 都必须同时置位。
 *   5. sink 回调跑在设备 reader 线程：环满只锁存溢出标志并返回 OK，绝不返回
 *      NO_SPACE（那会让设备侧 shutdown 会话，见 host/device/README.md:81）。
 *   6. 历史读卡（live_transport=false）只补记录，不产生柜前证明。
 */

#include "domain_test_support.h"

#include <stdio.h>
#include <string.h>

/* ---------- 小工具 ---------- */

/* 一条属于当前会话的事件；supply 用 0xffffffff 表示「本事件不带供电读数」。 */
static locker_device_event_t dv_event(const locker_core_t *core, uint16_t type, uint64_t seq,
                                      bool live)
{
    locker_device_event_t event;
    memset(&event, 0, sizeof(event));
    memcpy(event.key.device_id, DT_DEVICE_ID, sizeof(DT_DEVICE_ID));
    event.key.journal_epoch = core->cursor.journal_epoch;
    event.key.boot_id = core->cursor.through.boot_id;
    event.key.event_seq = seq;
    event.mono_ms = 100u + seq;
    event.physical_revision = 7u + seq;
    event.event_type = type;
    event.reason = 0u;
    event.flags = 0u;
    event.door = LOCKER_DOOR_STATE_OPEN;
    event.item = LOCKER_ITEM_STATE_PRESENT;
    event.supply_mv = 0xffffffffu;
    event.card_uid_len = 0u;
    event.live_transport = live;
    return event;
}

/* 在 core->active 里放一个可持久化的操作（baseline 必须有，否则写手会置 failed）。 */
static void dv_arm_operation(locker_core_t *core, uint8_t kind, uint8_t state, uint8_t id_seed)
{
    memset(&core->active, 0, sizeof(core->active));
    core->active.used = true;
    dt_uuid_fill(&core->active.id, id_seed);
    dt_uuid_fill(&core->active.tool_id, (uint8_t)(id_seed + 1u));
    dt_uuid_fill(&core->active.actor_id, (uint8_t)(id_seed + 2u));
    core->active.kind = kind;
    core->active.state = state;
    core->active.revision = 1u;
    memcpy(core->active.created_at, "2026-10-26T00:00:00Z", sizeof("2026-10-26T00:00:00Z"));
    memcpy(core->active.updated_at, "2026-10-26T00:00:00Z", sizeof("2026-10-26T00:00:00Z"));
    core->active.history_complete = true;
    core->active.has_baseline = true;
    core->active.baseline = core->physical;
}

/* 一条属于 core->active 的事件（operation_id 匹配，才会计入 source_events）。 */
static void dv_attach_active(locker_core_t *core, locker_device_event_t *event)
{
    event->operation_id = core->active.id;
}

/* ---------- 在途请求槽 ---------- */

static void test_request_slots(void)
{
    dt_fixture_t fixture;
    uint8_t purpose = DOMAIN_PURPOSE_NONE;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);

    /* request_no 从 1 起自增，且跳过 0。 */
    uint32_t first = domain_request_no(fixture.core, DOMAIN_PURPOSE_SNAPSHOT);
    uint32_t second = domain_request_no(fixture.core, DOMAIN_PURPOSE_FETCH);
    TEST_CHECK_U64(1u, first);
    TEST_CHECK_U64(2u, second);
    TEST_CHECK(fixture.core->pending[0].used);
    TEST_CHECK_U64(DOMAIN_PURPOSE_SNAPSHOT, fixture.core->pending[0].purpose);
    TEST_CHECK_U64(DOMAIN_PURPOSE_FETCH, fixture.core->pending[1].purpose);

    /* take 命中即释放槽，且回报 purpose。 */
    TEST_CHECK(domain_pending_take(fixture.core, first, &purpose));
    TEST_CHECK_U64(DOMAIN_PURPOSE_SNAPSHOT, purpose);
    TEST_CHECK(!fixture.core->pending[0].used);
    /* 同一号再取一次必须失败（已经回收）。 */
    TEST_CHECK(!domain_pending_take(fixture.core, first, &purpose));
    /* 0 号永远不是合法请求号。 */
    TEST_CHECK(!domain_pending_take(fixture.core, 0u, &purpose));

    /* clear 按 purpose 清，不动别的槽。 */
    domain_pending_clear(fixture.core, DOMAIN_PURPOSE_FETCH);
    TEST_CHECK(!fixture.core->pending[1].used);

    /* 8 个槽占满后返回 0，不再自增。 */
    for (uint32_t index = 0u; index < LOCKER_DOMAIN_PENDING_SLOTS; index += 1u) {
        TEST_CHECK(domain_request_no(fixture.core, DOMAIN_PURPOSE_HEALTH) != 0u);
    }
    TEST_CHECK_U64(0u, domain_request_no(fixture.core, DOMAIN_PURPOSE_HEALTH));

    dt_fixture_free(&fixture);
}

/* ---------- reset ---------- */

static void test_device_reset(void)
{
    dt_fixture_t fixture;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    fixture.core->ack_cursor_valid = true;
    fixture.core->execute_in_flight = true;
    fixture.core->pending_command_valid = true;
    fixture.core->fetch_gap = true;
    fixture.core->snapshot_requested_ms = 900u;
    fixture.core->last_pump_ms = 950u;
    TEST_CHECK(domain_request_no(fixture.core, DOMAIN_PURPOSE_SNAPSHOT) != 0u);

    domain_device_reset(fixture.core);

    /* 设备上下文全清。 */
    TEST_CHECK(!fixture.core->device_session_up);
    TEST_CHECK(!fixture.core->device_snapshot_valid);
    TEST_CHECK(!fixture.core->snapshot_in_flight);
    TEST_CHECK(!fixture.core->fetch_in_flight);
    TEST_CHECK(!fixture.core->ack_in_flight);
    TEST_CHECK(!fixture.core->ack_cursor_valid);
    TEST_CHECK(!fixture.core->execute_in_flight);
    TEST_CHECK(!fixture.core->pending_command_valid);
    TEST_CHECK(!fixture.core->fetch_gap);
    TEST_CHECK_U64(0u, fixture.core->snapshot_requested_ms);
    TEST_CHECK_U64(0u, fixture.core->last_pump_ms);
    TEST_CHECK(!fixture.core->cursor.valid);
    TEST_CHECK(!fixture.core->cursor.continuity_ok);
    TEST_CHECK_U64(0u, fixture.core->cursor.through.event_seq);
    TEST_CHECK(!fixture.core->physical.valid);
    TEST_CHECK(!fixture.core->pending[0].used);

    /* 会话身份不清：reset 只重置传输上下文，session.valid 由 HELLO/快照决定。 */
    TEST_CHECK(fixture.core->session.valid);
    TEST_CHECK_STRING(DT_DEVICE_ID, fixture.core->session.device_id);

    dt_fixture_free(&fixture);
}

/* ---------- 周期泵 ---------- */

static void test_pump_gates(void)
{
    dt_fixture_t fixture;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    fixture.core->now_ms = 1000u;
    fixture.core->last_pump_ms = 0u;
    fixture.core->last_health_ms = 0u;
    fixture.core->snapshot_in_flight = false;
    fixture.core->fetch_in_flight = false;
    fixture.core->ack_cursor_valid = false;

    /* 第一次泵：快照 + 补取 + 健康各一次；ACK 没有待确认水位，不发。 */
    domain_device_pump(fixture.core);
    TEST_CHECK_U64(1u, fixture.device->snapshot_calls);
    TEST_CHECK_U64(1u, fixture.device->fetch_calls);
    TEST_CHECK_U64(0u, fixture.device->ack_calls);
    TEST_CHECK_U64(1u, fixture.device->health_calls);
    TEST_CHECK(fixture.core->snapshot_in_flight);
    TEST_CHECK(fixture.core->fetch_in_flight);
    /* 补取上限是 4（契约 :225 limit<=4）。 */
    TEST_CHECK_U64(4u, fixture.device->last_fetch_limit);

    /* 50 ms 后再泵：100 ms 轮询门拦住，什么都不发。 */
    fixture.core->now_ms = 1050u;
    domain_device_pump(fixture.core);
    TEST_CHECK_U64(1u, fixture.device->snapshot_calls);
    TEST_CHECK_U64(1u, fixture.device->fetch_calls);
    TEST_CHECK_U64(1u, fixture.device->health_calls);

    /* 100 ms 后泵：轮询门放行，但在途标志仍然拦住快照与补取。 */
    fixture.core->now_ms = 1100u;
    domain_device_pump(fixture.core);
    TEST_CHECK_U64(1u, fixture.device->snapshot_calls);
    TEST_CHECK_U64(1u, fixture.device->fetch_calls);
    TEST_CHECK_U64(1u, fixture.device->health_calls);

    /* 回复到达清掉在途标志，但快照的 500 ms 间隔门仍在：200 ms 后不得重发。 */
    fixture.core->snapshot_in_flight = false;
    fixture.core->fetch_in_flight = false;
    fixture.core->now_ms = 1200u;
    domain_device_pump(fixture.core);
    TEST_CHECK_U64(1u, fixture.device->snapshot_calls);
    /* 补取是 200 ms 周期，1200-1000 已经到点。 */
    TEST_CHECK_U64(2u, fixture.device->fetch_calls);

    /* 到 500 ms 才允许下一次快照。 */
    fixture.core->snapshot_in_flight = false;
    fixture.core->now_ms = 1500u;
    domain_device_pump(fixture.core);
    TEST_CHECK_U64(2u, fixture.device->snapshot_calls);

    /* 健康是 1000 ms 周期，但泵本身还有 100 ms 轮询门，两个门是串联的：
       1500 泵过（last_pump=1500），1999 距 1500 有 499 ms → 放行泵体，
       但 1999-1000=999 < 1000 → 不发健康；2000 距 1999 只有 1 ms → 被轮询门挡住；
       2100 才既过轮询门（101 ms）又过健康周期（1100 ms）。 */
    fixture.core->now_ms = 1999u;
    domain_device_pump(fixture.core);
    TEST_CHECK_U64(1u, fixture.device->health_calls);
    fixture.core->now_ms = 2000u;
    domain_device_pump(fixture.core);
    TEST_CHECK_U64(1u, fixture.device->health_calls);
    fixture.core->now_ms = 2100u;
    domain_device_pump(fixture.core);
    TEST_CHECK_U64(2u, fixture.device->health_calls);

    /* 健康报文的进度取 Core 自己的计数器，supervisor 未知（S4-04 才拥有真实值）。 */
    TEST_CHECK_U64(fixture.core->instance_id.bytes[0], fixture.device->last_health.host_instance_id.bytes[0]);
    TEST_CHECK_U64(fixture.core->progress_counter, fixture.device->last_health.core_progress);
    TEST_CHECK_U64(0u, fixture.device->last_health.core_age_ms);
    TEST_CHECK_U64(LOCKER_SUPERVISOR_STATE_UNKNOWN, fixture.device->last_health.supervisor_state);

    /* 会话未建立：补取/ACK/健康都不发（否则等于对着没握手的设备刷帧），
       但快照必须照发——device_session_up 正是「第一次快照成功」的产物，
       用会话门挡快照会自我循环，真实链路上永远建立不了会话。 */
    fixture.core->device_session_up = false;
    fixture.core->snapshot_in_flight = false;
    fixture.core->now_ms = 5000u;
    fixture.core->last_pump_ms = 0u;
    domain_device_pump(fixture.core);
    TEST_CHECK_U64(3u, fixture.device->snapshot_calls);
    TEST_CHECK_U64(2u, fixture.device->fetch_calls);
    TEST_CHECK_U64(2u, fixture.device->health_calls);
    /* 快照已发但在途：重复泵不得再发一次。 */
    domain_device_pump(fixture.core);
    TEST_CHECK_U64(3u, fixture.device->snapshot_calls);

    /* 没有设备对象时泵是空操作，不得崩。 */
    dt_fixture_t bare;
    TEST_CHECK(dt_fixture_init(&bare, false));
    dt_core_ready(&bare);
    bare.core->device_session_up = true;
    domain_device_pump(bare.core);
    dt_fixture_free(&bare);

    dt_fixture_free(&fixture);
}

/* ---------- 快照应用 ---------- */

static void test_snapshot_apply(void)
{
    dt_fixture_t fixture;
    locker_device_reply_t reply;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_OPEN, LOCKER_ITEM_STATE_PRESENT);
    fixture.core->snapshot_in_flight = true;

    memset(&reply, 0, sizeof(reply));
    reply.request_no = 1u;
    reply.message_type = (uint8_t)(2u | 0x80u);
    reply.body.get_snapshot.result = LOCKER_WIRE_RESULT_OK;
    reply.body.get_snapshot.session_id = fixture.core->session.session_id;
    dt_uuid_fill(&reply.body.get_snapshot.journal_epoch, 0x20u);
    reply.body.get_snapshot.boot_id = 3u;
    reply.body.get_snapshot.physical_revision = 42u;
    reply.body.get_snapshot.door = LOCKER_DOOR_STATE_CLOSED;
    reply.body.get_snapshot.item = LOCKER_ITEM_STATE_ABSENT;
    reply.body.get_snapshot.flags = 0x0001u;
    reply.body.get_snapshot.supply_mv = 11800u;
    reply.body.get_snapshot.dropped_events = 5u;
    reply.body.get_snapshot.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_DEGRADED;
    reply.body.get_snapshot.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_DEGRADED;
    reply.body.get_snapshot.action_watermark = 9u;

    domain_reply_apply(fixture.core, &reply);

    TEST_CHECK(!fixture.core->snapshot_in_flight);
    TEST_CHECK(fixture.core->device_session_up);
    TEST_CHECK(fixture.core->device_snapshot_valid);
    /* wire 的 DEGRADED(2) 必须映射成契约的 DEGRADED(3)，两套编号不能混用。 */
    TEST_CHECK_U64(LOCKER_DEVICE_MODE_DEGRADED, fixture.core->session.device_mode);
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_JOURNAL_STATE_DEGRADED, fixture.core->session.journal_state);
    TEST_CHECK_U64(3u, fixture.core->session.boot_id);
    TEST_CHECK_U64(9u, fixture.core->action_watermark);
    TEST_CHECK_U64(42u, fixture.core->physical.physical_revision);
    TEST_CHECK_U64(LOCKER_DOOR_STATE_CLOSED, fixture.core->physical.door);
    TEST_CHECK_U64(LOCKER_ITEM_STATE_ABSENT, fixture.core->physical.item);
    /* flags bit0 才带供电读数。 */
    TEST_CHECK(fixture.core->physical.supply_valid);
    TEST_CHECK_U64(11800u, fixture.core->physical.supply_mv);
    TEST_CHECK_U64(5u, fixture.core->physical.dropped_events);
    TEST_CHECK_U64(LOCKER_TIME_QUALITY_HOST_SYNCED, fixture.core->physical.time_quality);

    /* flags bit2 = 事件流不连续：物理快照与游标同时降级。 */
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(2u | 0x80u);
    reply.body.get_snapshot.flags = 0x0004u;
    reply.body.get_snapshot.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_READY;
    reply.body.get_snapshot.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
    fixture.core->snapshot_in_flight = true;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(!fixture.core->physical.continuity_ok);
    TEST_CHECK(!fixture.core->cursor.continuity_ok);
    TEST_CHECK_U64(LOCKER_DEVICE_MODE_READY, fixture.core->session.device_mode);

    /* 没有 flags bit0 时保留上一次的供电读数，不写成 0。 */
    TEST_CHECK(fixture.core->physical.supply_valid);
    TEST_CHECK_U64(11800u, fixture.core->physical.supply_mv);

    /* 失败结果只清在途标志，不改任何状态。 */
    uint64_t revision_before = fixture.core->physical.physical_revision;
    fixture.core->snapshot_in_flight = true;
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(2u | 0x80u);
    reply.body.get_snapshot.result = LOCKER_WIRE_RESULT_BAD_SESSION;
    reply.body.get_snapshot.physical_revision = 999u;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(!fixture.core->snapshot_in_flight);
    TEST_CHECK_U64(revision_before, fixture.core->physical.physical_revision);

    /* 晚到的旧快照不许回退物理事实。GET_SNAPSHOT 是异步应答：请求发出与应答
       到达之间可能已经应用了更新的 DEVICE_EVENT，一个采样更早的快照如果照样
       覆盖，门/物品会回到旧值，WAIT_CONFIRM 的冻结依据也跟着被误判成
       PHYSICAL_CHANGED（r203200 的 TSan 轮偶发失败即此）。 */
    {
        locker_device_reply_t fresh_reply;
        memset(&fresh_reply, 0, sizeof(fresh_reply));
        fresh_reply.message_type = (uint8_t)(2u | 0x80u);
        fresh_reply.body.get_snapshot.result = LOCKER_WIRE_RESULT_OK;
        fresh_reply.body.get_snapshot.session_id = fixture.core->session.session_id;
        fresh_reply.body.get_snapshot.boot_id = fixture.core->physical.boot_id;
        fresh_reply.body.get_snapshot.physical_revision = fixture.core->physical.physical_revision + 5u;
        fresh_reply.body.get_snapshot.door = LOCKER_DOOR_STATE_OPEN;
        fresh_reply.body.get_snapshot.item = LOCKER_ITEM_STATE_PRESENT;
        fresh_reply.body.get_snapshot.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_READY;
        fresh_reply.body.get_snapshot.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
        fixture.core->snapshot_in_flight = true;
        domain_reply_apply(fixture.core, &fresh_reply);
        uint64_t accepted = fixture.core->physical.physical_revision;
        TEST_CHECK_U64(revision_before + 5u, accepted);
        TEST_CHECK_U64(LOCKER_DOOR_STATE_OPEN, fixture.core->physical.door);

        /* 同一 boot 下版本更小的快照：整体丢弃，门/物品/版本都不动。 */
        memset(&fresh_reply, 0, sizeof(fresh_reply));
        fresh_reply.message_type = (uint8_t)(2u | 0x80u);
        fresh_reply.body.get_snapshot.result = LOCKER_WIRE_RESULT_OK;
        fresh_reply.body.get_snapshot.session_id = fixture.core->session.session_id;
        fresh_reply.body.get_snapshot.boot_id = fixture.core->physical.boot_id;
        fresh_reply.body.get_snapshot.physical_revision = accepted - 3u;
        fresh_reply.body.get_snapshot.door = LOCKER_DOOR_STATE_CLOSED;
        fresh_reply.body.get_snapshot.item = LOCKER_ITEM_STATE_ABSENT;
        fresh_reply.body.get_snapshot.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_READY;
        fresh_reply.body.get_snapshot.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
        fixture.core->snapshot_in_flight = true;
        domain_reply_apply(fixture.core, &fresh_reply);
        TEST_CHECK_U64(accepted, fixture.core->physical.physical_revision);
        TEST_CHECK_U64(LOCKER_DOOR_STATE_OPEN, fixture.core->physical.door);
        TEST_CHECK_U64(LOCKER_ITEM_STATE_PRESENT, fixture.core->physical.item);

        /* 换 boot 说明设备重启过：新事实优先，即使版本号更小。 */
        memset(&fresh_reply, 0, sizeof(fresh_reply));
        fresh_reply.message_type = (uint8_t)(2u | 0x80u);
        fresh_reply.body.get_snapshot.result = LOCKER_WIRE_RESULT_OK;
        fresh_reply.body.get_snapshot.session_id = fixture.core->session.session_id;
        fresh_reply.body.get_snapshot.boot_id = fixture.core->physical.boot_id + 1u;
        fresh_reply.body.get_snapshot.physical_revision = 1u;
        fresh_reply.body.get_snapshot.door = LOCKER_DOOR_STATE_CLOSED;
        fresh_reply.body.get_snapshot.item = LOCKER_ITEM_STATE_ABSENT;
        fresh_reply.body.get_snapshot.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_READY;
        fresh_reply.body.get_snapshot.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
        fixture.core->snapshot_in_flight = true;
        domain_reply_apply(fixture.core, &fresh_reply);
        TEST_CHECK_U64(1u, fixture.core->physical.physical_revision);
        TEST_CHECK_U64(LOCKER_DOOR_STATE_CLOSED, fixture.core->physical.door);
    }

    /* 首次拿到快照只记 epoch，游标水位保持「全零原点」。 */
    dt_fixture_t fresh;
    TEST_CHECK(dt_fixture_init(&fresh, true));
    dt_core_ready(&fresh);
    fresh.core->cursor.valid = false;
    /* 真实时序里 HELLO 回复先于快照回复到达；设备身份由 HELLO 学得，
       与快照游标无关——这里显式学一次，确保后续断言不会因为身份缺失而误判。 */
    domain_learn_device_id(fresh.core, DT_DEVICE_ID);
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(2u | 0x80u);
    dt_uuid_fill(&reply.body.get_snapshot.journal_epoch, 0x20u);
    reply.body.get_snapshot.boot_id = 7u;
    reply.body.get_snapshot.flags = 0x0001u;
    reply.body.get_snapshot.supply_mv = 12000u;
    reply.body.get_snapshot.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING;
    reply.body.get_snapshot.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
    domain_reply_apply(fresh.core, &reply);
    TEST_CHECK(fresh.core->cursor.valid);
    TEST_CHECK(locker_uuid_equal(&fresh.core->cursor.journal_epoch,
                                &reply.body.get_snapshot.journal_epoch));
    /* uart-v1.md:38 只有 (0,0) 是合法原点，(boot,0) 半零游标会被设备端口
       device_cursor_matches 直接判非法。快照不能把 boot_id 写进水位，否则第一次
       FETCH 恒 CONFLICT → import_cursor 不推进 → ACK 不产生 → 设备停在
       RECONCILING（集成测试暴露过）。 */
    TEST_CHECK_U64(0u, fresh.core->cursor.through.boot_id);
    TEST_CHECK_U64(0u, fresh.core->cursor.through.event_seq);
    /* 但 EventKey 的 device_id/journal_epoch 是必填项（internal-data.schema.json
       的 EventKey 要求 ^[0-9a-f]{24}$ 与合法 UUID）：durable_cursor 会被写进
       device_state，身份为空会让落库校验失败。所以身份与 epoch 要补上。 */
    TEST_CHECK_STRING(DT_DEVICE_ID, fresh.core->cursor.through.device_id);
    TEST_CHECK(locker_uuid_equal(&fresh.core->cursor.through.journal_epoch,
                                &reply.body.get_snapshot.journal_epoch));
    TEST_CHECK_STRING(DT_DEVICE_ID, fresh.core->physical.device_id);
    /* RECONCILING 是 wire 0 → 契约 1。 */
    TEST_CHECK_U64(LOCKER_DEVICE_MODE_RECONCILING, fresh.core->session.device_mode);
    dt_fixture_free(&fresh);

    dt_fixture_free(&fixture);
}

/* ---------- 事件：水位与 epoch ---------- */

static void test_event_cursor_rules(void)
{
    dt_fixture_t fixture;
    locker_device_event_t event;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);

    /* 正常事件推进水位。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 5u, true);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(5u, fixture.core->cursor.through.event_seq);
    TEST_CHECK_U64(LOCKER_DOOR_STATE_OPEN, fixture.core->physical.door);
    TEST_CHECK(fixture.core->physical.valid);
    /* live 事件不动连续性。 */
    TEST_CHECK(fixture.core->cursor.continuity_ok);
    TEST_CHECK(!fixture.core->fetch_gap);

    /* 重复（相等）与回退（更小）都被丢弃。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_CLOSED, 5u, true);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(5u, fixture.core->cursor.through.event_seq);
    TEST_CHECK_U64(LOCKER_DOOR_STATE_OPEN, fixture.core->physical.door);

    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_CLOSED, 4u, true);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(5u, fixture.core->cursor.through.event_seq);
    TEST_CHECK_U64(LOCKER_DOOR_STATE_OPEN, fixture.core->physical.door);

    /* 换 boot_id 但 epoch 相同、boot 更大：接受（设备重启过）。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_BOOT, 1u, true);
    event.key.boot_id = 2u;
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(2u, fixture.core->cursor.through.boot_id);
    TEST_CHECK_U64(1u, fixture.core->cursor.through.event_seq);

    /* 不同 journal_epoch 的事件一律拒绝：那是另一个日志世代的历史。 */
    uint64_t seq_before = fixture.core->cursor.through.event_seq;
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 99u, true);
    dt_uuid_fill(&event.key.journal_epoch, 0x77u);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(seq_before, fixture.core->cursor.through.event_seq);
    TEST_CHECK(!locker_uuid_equal(&fixture.core->cursor.journal_epoch, &event.key.journal_epoch));

    /* 补传事件（live_transport=false）：记状态但锁存缺口。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_CLOSED, 200u, false);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK(!fixture.core->cursor.continuity_ok);
    TEST_CHECK(fixture.core->fetch_gap);
    TEST_CHECK_U64(200u, fixture.core->cursor.through.event_seq);

    /* 事件里的 device_id 会补齐身份（wire 游标不带 device_id）。 */
    TEST_CHECK_STRING(DT_DEVICE_ID, fixture.core->cursor.device_id);

    dt_fixture_free(&fixture);
}

/* ---------- 事件：缺口标志 ---------- */

static void test_event_gap_flags(void)
{
    dt_fixture_t fixture;
    locker_device_event_t event;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(fixture.core->cursor.continuity_ok);

    /* CONTINUITY_LOST 与 JOURNAL_FAULT 都要求同时置两个标志。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_CONTINUITY_LOST, 1u, true);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK(!fixture.core->cursor.continuity_ok);
    TEST_CHECK(fixture.core->fetch_gap);

    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(fixture.core->cursor.continuity_ok);
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_JOURNAL_FAULT, 2u, true);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK(!fixture.core->cursor.continuity_ok);
    TEST_CHECK(fixture.core->fetch_gap);

    /* 事件环溢出：原子标志由设备线程置位，Core 消费成缺口。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    atomic_store(&fixture.core->event_overflow_flag, true);
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 3u, true);
    domain_handle_device_event(fixture.core, &event, UINT32_MAX);
    TEST_CHECK(fixture.core->event_overflow);
    TEST_CHECK(fixture.core->fetch_gap);
    TEST_CHECK(!fixture.core->cursor.continuity_ok);
    TEST_CHECK(!atomic_load(&fixture.core->event_overflow_flag));

    /* 响应环溢出只置 reply_overflow 与 fetch_gap（不降连续性：响应不是事件流）。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    atomic_store(&fixture.core->reply_overflow_flag, true);
    locker_device_reply_t reply;
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(9u | 0x80u);
    domain_handle_device_reply(fixture.core, &reply, UINT32_MAX);
    TEST_CHECK(fixture.core->reply_overflow);
    TEST_CHECK(fixture.core->fetch_gap);

    dt_fixture_free(&fixture);
}

/* ---------- 事件：脉冲与操作推进 ---------- */

static void test_event_pulse_advances(void)
{
    dt_fixture_t fixture;
    locker_device_event_t event;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_PENDING, 0x50u);

    /* PULSE_STARTED 是唯一能把 ACCESS_PENDING 推到 ACCESS_ACTIVE 的事件。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_PULSE_STARTED, 1u, true);
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_ACTIVE, fixture.core->active.state);
    TEST_CHECK_U64(1u, fixture.core->active.pulse_started_ms != 0u ? 1u : 0u);
    TEST_CHECK_STRING("PULSE_STARTED", fixture.core->active.reason_code);
    TEST_CHECK_U64(1u, fixture.core->active.source_event_count);
    TEST_CHECK(fixture.core->active.used);
    /* 落库 + 发布各一次。 */
    TEST_CHECK_U64(1u, fixture.repo->commit_calls);
    TEST_CHECK_U64(1u, fixture.publisher->publish_calls);
    /* resource 必须是契约 WsChange 的枚举值（tool/operation/loan/device/review/user），
       不是 "operation.changed" 这类事件名。 */
    TEST_CHECK_STRING("operation", fixture.publisher->last.resource);

    /* 同一事件重复到达不得再次推进（水位挡住）。 */
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(1u, fixture.core->active.source_event_count);

    /* 操作不在 ACCESS_PENDING 时 PULSE_STARTED 什么都不改。 */
    dt_fixture_t other;
    TEST_CHECK(dt_fixture_init(&other, true));
    dt_core_device_ready(&other, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(other.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_ACTIVE, 0x50u);
    event = dv_event(other.core, LOCKER_DEVICE_EVENT_PULSE_STARTED, 1u, true);
    dv_attach_active(other.core, &event);
    domain_event_apply(other.core, &event);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_ACTIVE, other.core->active.state);
    TEST_CHECK_U64(0u, other.core->active.pulse_started_ms);
    dt_fixture_free(&other);

    dt_fixture_free(&fixture);
}

/* ---------- 事件：开门/物品证据 ---------- */

static void test_event_physical_evidence(void)
{
    dt_fixture_t fixture;
    locker_device_event_t event;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_ACTIVE, 0x50u);

    /* 只开门、物品没变化：不能算完成，也不能释放占位。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 1u, true);
    event.door = LOCKER_DOOR_STATE_OPEN;
    event.item = LOCKER_ITEM_STATE_PRESENT;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK(fixture.core->active.door_open_seen);
    TEST_CHECK_U64(1u, fixture.core->active.door_opened_ms != 0u ? 1u : 0u);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_ACTIVE, fixture.core->active.state);

    /* 物品仍在其位且门已关：NO_CHANGE 终态，占位释放，账本不变（不写 loan）。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_CLOSED, 2u, true);
    event.door = LOCKER_DOOR_STATE_CLOSED;
    event.item = LOCKER_ITEM_STATE_PRESENT;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_NO_CHANGE, fixture.core->active.state);
    TEST_CHECK_STRING("NO_ITEM_CHANGE", fixture.core->active.reason_code);
    TEST_CHECK(!fixture.core->active.used);
    TEST_CHECK(!fixture.core->loan_open);

    /* 借出：开门 + 物品离位 + 门关 → WAIT_CONFIRM（还要本人确认才完成）。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_ACTIVE, 0x60u);
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 1u, true);
    event.door = LOCKER_DOOR_STATE_OPEN;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_ACTIVE, fixture.core->active.state);

    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_ITEM_REMOVED, 2u, true);
    event.door = LOCKER_DOOR_STATE_OPEN;
    event.item = LOCKER_ITEM_STATE_ABSENT;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK(fixture.core->active.item_transition_seen);
    /* 门还开着，不能推进。 */
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_ACTIVE, fixture.core->active.state);

    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_CLOSED, 3u, true);
    event.door = LOCKER_DOOR_STATE_CLOSED;
    event.item = LOCKER_ITEM_STATE_ABSENT;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_WAIT_CONFIRM, fixture.core->active.state);
    TEST_CHECK_STRING("PHYSICAL_CONFIRMED", fixture.core->active.reason_code);
    TEST_CHECK(fixture.core->active.used);
    TEST_CHECK(fixture.core->active.has_physical);
    TEST_CHECK_U64(fixture.core->now_ms + LOCKER_WAIT_CONFIRM_MS,
                   fixture.core->active.wait_confirm_deadline_ms);

    /* 归还方向相反：门关 + 物品在位才算归还到位。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_ABSENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_RETURN,
                     LOCKER_OPERATION_STATE_ACCESS_ACTIVE, 0x70u);
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 1u, true);
    event.door = LOCKER_DOOR_STATE_OPEN;
    event.item = LOCKER_ITEM_STATE_ABSENT;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_CLOSED, 2u, true);
    event.door = LOCKER_DOOR_STATE_CLOSED;
    event.item = LOCKER_ITEM_STATE_ABSENT;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    /* 归还但物品仍在位 = 没放回去 → NO_CHANGE。 */
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_NO_CHANGE, fixture.core->active.state);
    TEST_CHECK(!fixture.core->active.used);

    dt_fixture_free(&fixture);
}

/* ---------- 事件：历史读卡不产生柜前证明 ---------- */

static void test_event_card_history(void)
{
    dt_fixture_t fixture;
    locker_device_event_t event;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(!fixture.core->last_card.valid);

    /* 历史补传的读卡：不写 last_card。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_CARD_OBSERVED, 1u, false);
    event.card_uid_len = 4u;
    memset(event.card_uid, 0xab, sizeof(event.card_uid));
    domain_handle_device_event(fixture.core, &event, UINT32_MAX);
    TEST_CHECK(!fixture.core->last_card.valid);

    fixture.core->anchor_valid=true; fixture.core->anchor_sampled_ms=fixture.core->now_ms;
    fixture.core->anchor_offset_low_ms=0; fixture.core->anchor_offset_high_ms=0;
    /* 当前会话、未处理序号及新鲜时间锚共同允许 live 观察。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_CARD_OBSERVED, 2u, true);
    event.mono_ms=fixture.core->now_ms;
    event.card_uid_len = 4u;
    memset(event.card_uid, 0xab, sizeof(event.card_uid));
    domain_handle_device_event(fixture.core, &event, UINT32_MAX);
    TEST_CHECK(fixture.core->last_card.valid);
    TEST_CHECK_U64(4u, fixture.core->last_card.uid_len);
    TEST_CHECK_U64(0xab, fixture.core->last_card.uid[0]);
    TEST_CHECK(!fixture.core->last_card.user_resolved);
    TEST_CHECK_U64(fixture.core->now_ms, fixture.core->last_card.mono_ms);

    /* 已由历史/实时游标覆盖的帧不能重新产生新观察。 */
    fixture.core->last_card.valid=false;
    domain_handle_device_event(fixture.core,&event,UINT32_MAX);
    TEST_CHECK(!fixture.core->last_card.valid);
    /* 没有可信时间锚时拒绝把到达时间冒充观察时间。 */
    fixture.core->anchor_valid=false;
    event=dv_event(fixture.core,LOCKER_DEVICE_EVENT_CARD_OBSERVED,3u,true); event.card_uid_len=4;
    domain_handle_device_event(fixture.core,&event,UINT32_MAX);
    TEST_CHECK(!fixture.core->last_card.valid);
    /* uid 长度越界（0 或 >10）不写。 */
    fixture.core->last_card.valid = false;
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_CARD_OBSERVED, 3u, true);
    event.card_uid_len = 0u;
    domain_handle_device_event(fixture.core, &event, UINT32_MAX);
    TEST_CHECK(!fixture.core->last_card.valid);
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_CARD_OBSERVED, 4u, true);
    event.card_uid_len = 11u;
    domain_handle_device_event(fixture.core, &event, UINT32_MAX);
    TEST_CHECK(!fixture.core->last_card.valid);

    dt_fixture_free(&fixture);
}

/* ---------- EXECUTE 回复 ---------- */

static void test_execute_response(void)
{
    dt_fixture_t fixture;
    locker_device_reply_t reply;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_PENDING, 0x50u);
    dt_uuid_fill(&fixture.core->pending_command_id, 0x90u);
    fixture.core->pending_command_valid = true;
    fixture.core->execute_in_flight = true;

    /* ACCEPTED 不推进状态机（ACK 不是开门）。 */
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(7u | 0x80u);
    reply.body.execute_unlock.result = LOCKER_WIRE_RESULT_OK;
    reply.body.execute_unlock.command_id = fixture.core->pending_command_id;
    reply.body.execute_unlock.operation_id = fixture.core->active.id;
    reply.body.execute_unlock.command_state = LOCKER_COMMAND_STATE_ACCEPTED;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_PENDING, fixture.core->active.state);
    TEST_CHECK(!fixture.core->execute_in_flight);
    TEST_CHECK(!fixture.core->pending_command_valid);
    TEST_CHECK_U64(0u, fixture.publisher->publish_calls);

    /* PULSE_STARTED 才推进到 ACCESS_ACTIVE。 */
    fixture.core->pending_command_valid = true;
    fixture.core->execute_in_flight = true;
    reply.body.execute_unlock.command_state = LOCKER_COMMAND_STATE_PULSE_STARTED;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_ACTIVE, fixture.core->active.state);
    TEST_CHECK_STRING("PULSE_STARTED", fixture.core->active.reason_code);
    TEST_CHECK_U64(1u, fixture.publisher->publish_calls);

    /* 不是当前命令的回复：保留 pending，不动状态。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_PENDING, 0x50u);
    dt_uuid_fill(&fixture.core->pending_command_id, 0x90u);
    fixture.core->pending_command_valid = true;
    fixture.core->execute_in_flight = true;
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(7u | 0x80u);
    dt_uuid_fill(&reply.body.execute_unlock.command_id, 0x91u);
    reply.body.execute_unlock.operation_id = fixture.core->active.id;
    reply.body.execute_unlock.result = LOCKER_WIRE_RESULT_OK;
    reply.body.execute_unlock.command_state = LOCKER_COMMAND_STATE_PULSE_STARTED;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(fixture.core->pending_command_valid);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_PENDING, fixture.core->active.state);
    TEST_CHECK_U64(0x90u, fixture.core->pending_command_id.bytes[0]);

    /* 设备明确拒绝：进 REVIEW_REQUIRED，不重试。 */
    const uint16_t rejections[] = {
        LOCKER_WIRE_RESULT_REPLAY_BLOCKED, LOCKER_WIRE_RESULT_ACTION_GAP,
        LOCKER_WIRE_RESULT_BAD_PRECONDITION, LOCKER_WIRE_RESULT_PHYSICAL_CHANGED,
        LOCKER_WIRE_RESULT_JOURNAL_CHANGED,
    };
    for (size_t index = 0u; index < sizeof(rejections) / sizeof(rejections[0]); index += 1u) {
        dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
        dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                         LOCKER_OPERATION_STATE_ACCESS_PENDING, 0x50u);
        fixture.core->pending_command_valid = true;
        fixture.core->execute_in_flight = true;
        fixture.core->pending_command_id = fixture.core->active.command_id;
        memset(&reply, 0, sizeof(reply));
        reply.message_type = (uint8_t)(7u | 0x80u);
        reply.body.execute_unlock.result = rejections[index];
        reply.body.execute_unlock.operation_id = fixture.core->active.id;
        domain_reply_apply(fixture.core, &reply);
        TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, fixture.core->active.state);
        TEST_CHECK_STRING("COMMAND_REJECTED", fixture.core->active.reason_code);
    }

    /* 结果未知（UNKNOWN_RESULT / LOG_UNAVAILABLE）：同样进复核，绝不当成功。 */
    const uint16_t unknowns[] = {LOCKER_WIRE_RESULT_UNKNOWN_RESULT, LOCKER_WIRE_RESULT_LOG_UNAVAILABLE};
    for (size_t index = 0u; index < sizeof(unknowns) / sizeof(unknowns[0]); index += 1u) {
        dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
        dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                         LOCKER_OPERATION_STATE_ACCESS_PENDING, 0x50u);
        fixture.core->pending_command_valid = true;
        fixture.core->execute_in_flight = true;
        memset(&reply, 0, sizeof(reply));
        reply.message_type = (uint8_t)(7u | 0x80u);
        reply.body.execute_unlock.result = unknowns[index];
        reply.body.execute_unlock.operation_id = fixture.core->active.id;
        domain_reply_apply(fixture.core, &reply);
        TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, fixture.core->active.state);
        TEST_CHECK_STRING("UNKNOWN_RESULT", fixture.core->active.reason_code);
    }

    /* 没有活动操作时回复不产生任何状态。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    fixture.core->active.used = false;
    fixture.core->execute_in_flight = true;
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(7u | 0x80u);
    reply.body.execute_unlock.result = LOCKER_WIRE_RESULT_OK;
    reply.body.execute_unlock.command_state = LOCKER_COMMAND_STATE_PULSE_STARTED;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(!fixture.core->active.used);
    TEST_CHECK(!fixture.core->execute_in_flight);

    dt_fixture_free(&fixture);
}

/* ---------- QUERY_COMMAND 回复 ---------- */

static void test_query_command_response(void)
{
    dt_fixture_t fixture;
    locker_device_reply_t reply;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_PENDING, 0x50u);

    /* 查询到 PULSE_STARTED：说明动作真的执行过，推进到 ACCESS_ACTIVE。 */
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(8u | 0x80u);
    reply.body.query_command.result = LOCKER_WIRE_RESULT_OK;
    reply.body.query_command.operation_id = fixture.core->active.id;
    reply.body.query_command.command_state = LOCKER_COMMAND_STATE_PULSE_STARTED;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_ACTIVE, fixture.core->active.state);
    TEST_CHECK_STRING("PULSE_STARTED", fixture.core->active.reason_code);

    /* 查询失败：进复核（结果未知不是未执行，也不是执行成功）。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_PENDING, 0x50u);
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(8u | 0x80u);
    reply.body.query_command.result = LOCKER_WIRE_RESULT_NOT_READY;
    reply.body.query_command.operation_id = fixture.core->active.id;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, fixture.core->active.state);
    TEST_CHECK_STRING("QUERY_UNRESOLVED", fixture.core->active.reason_code);

    /* 操作 id 不符：不动状态。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_PENDING, 0x50u);
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(8u | 0x80u);
    reply.body.query_command.result = LOCKER_WIRE_RESULT_OK;
    reply.body.query_command.command_state = LOCKER_COMMAND_STATE_PULSE_STARTED;
    dt_uuid_fill(&reply.body.query_command.operation_id, 0x99u);
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_PENDING, fixture.core->active.state);

    dt_fixture_free(&fixture);
}

/* ---------- ACK 回复 ---------- */

static void test_ack_response(void)
{
    dt_fixture_t fixture;
    locker_device_reply_t reply;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dt_uuid_fill(&fixture.core->ack_cursor.journal_epoch, 0x20u);
    fixture.core->ack_cursor.boot_id = 1u;
    fixture.core->ack_cursor.event_seq = 10u;
    fixture.core->ack_cursor_valid = true;
    fixture.core->ack_in_flight = true;

    /* 设备确认到水位：清掉待确认标记。 */
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(4u | 0x80u);
    reply.body.ack_persisted.result = LOCKER_WIRE_RESULT_OK;
    reply.body.ack_persisted.ack_persisted.boot_id = 1u;
    reply.body.ack_persisted.ack_persisted.event_seq = 10u;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(!fixture.core->ack_cursor_valid);
    TEST_CHECK(!fixture.core->ack_in_flight);

    /* 落后于待确认水位的 ACK 不算确认：仍要重发。 */
    fixture.core->ack_cursor_valid = true;
    fixture.core->ack_in_flight = true;
    reply.body.ack_persisted.ack_persisted.event_seq = 9u;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(fixture.core->ack_cursor_valid);

    /* 失败结果：保留待确认标记。 */
    fixture.core->ack_in_flight = true;
    reply.body.ack_persisted.result = LOCKER_WIRE_RESULT_NOT_READY;
    reply.body.ack_persisted.ack_persisted.event_seq = 10u;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(fixture.core->ack_cursor_valid);

    /* ACK 只影响游标，绝不改操作状态（仅 ACK 不进入开门）。 */
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_PENDING, 0x50u);
    fixture.core->ack_in_flight = true;
    reply.body.ack_persisted.result = LOCKER_WIRE_RESULT_OK;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_PENDING, fixture.core->active.state);

    dt_fixture_free(&fixture);
}

/* ---------- FETCH 回复与导入 ---------- */

static void test_fetch_response(void)
{
    dt_fixture_t fixture;
    locker_device_reply_t reply;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    fixture.core->fetch_in_flight = true;
    fixture.core->last_fetch_ms = 5000u;

    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(3u | 0x80u);
    reply.body.fetch_events.result = LOCKER_WIRE_RESULT_OK;
    reply.body.fetch_events.journal_epoch = fixture.core->cursor.journal_epoch;
    reply.body.fetch_events.event_count = 2u;
    reply.body.fetch_events.events[0].boot_id = 1u;
    reply.body.fetch_events.events[0].event_seq = 11u;
    reply.body.fetch_events.events[0].event_type = LOCKER_DEVICE_EVENT_DOOR_OPENED;
    reply.body.fetch_events.events[1].boot_id = 1u;
    reply.body.fetch_events.events[1].event_seq = 12u;
    reply.body.fetch_events.events[1].event_type = LOCKER_DEVICE_EVENT_DOOR_CLOSED;

    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(!fixture.core->fetch_in_flight);
    /* 事件页交给仓储做去重与水位推进，投影必须传 NULL（Core 不替事件猜业务）。 */
    TEST_CHECK_U64(1u, fixture.repo->import_calls);
    TEST_CHECK_U64(2u, fixture.repo->last_import_count);
    TEST_CHECK(!fixture.repo->last_import_has_projection);
    TEST_CHECK(fixture.core->import_cursor_valid);
    TEST_CHECK_U64(12u, fixture.core->import_cursor.event_seq);
    /* 仓储已提交水位 → 待 ACK。 */
    TEST_CHECK(fixture.core->ack_cursor_valid);
    TEST_CHECK_U64(12u, fixture.core->ack_cursor.event_seq);

    /* 页有缺口：锁存不连续 + 待补取。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    fixture.core->fetch_in_flight = true;
    reply.body.fetch_events.gap_before_page = 1u;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(!fixture.core->cursor.continuity_ok);
    TEST_CHECK(fixture.core->fetch_gap);

    /* has_more：立刻续取（把上次时间清零，绕过 200 ms 间隔）。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    fixture.core->fetch_in_flight = true;
    fixture.core->last_fetch_ms = 9000u;
    reply.body.fetch_events.gap_before_page = 0u;
    reply.body.fetch_events.has_more = 1u;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK_U64(0u, fixture.core->last_fetch_ms);
    TEST_CHECK(fixture.core->cursor.continuity_ok);

    /* 导入失败（仓储报错）：缺口锁存，绝不把内存水位推过去。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    fixture.core->fetch_in_flight = true;
    fixture.repo->import_status = LOCKER_STATUS_IO_ERROR;
    reply.body.fetch_events.has_more = 0u;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(fixture.core->fetch_gap);
    TEST_CHECK(!fixture.core->cursor.continuity_ok);
    TEST_CHECK(!fixture.core->import_cursor_valid);
    TEST_CHECK(!fixture.core->ack_cursor_valid);

    /* 失败结果：只清在途标志。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    fixture.core->fetch_in_flight = true;
    fixture.repo->import_status = LOCKER_STATUS_OK;
    reply.body.fetch_events.result = LOCKER_WIRE_RESULT_CURSOR_GAP;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(!fixture.core->fetch_in_flight);
    TEST_CHECK(fixture.core->cursor.continuity_ok);

    dt_fixture_free(&fixture);
}

/* ---------- 其余响应类型 ---------- */

static void test_other_replies(void)
{
    dt_fixture_t fixture;
    locker_device_reply_t reply;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_ready(&fixture);
    fixture.core->cursor.valid = true;
    fixture.core->cursor.device_id[0] = '\0';

    /* HELLO：12 字节 uid 转 24 位小写 hex 作为设备身份。 */
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(1u | 0x80u);
    reply.body.hello.result = LOCKER_WIRE_RESULT_OK;
    for (uint32_t index = 0u; index < 12u; index += 1u) {
        reply.body.hello.device_uid[index] = (uint8_t)(0xa0u + index);
    }
    reply.body.hello.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_DEGRADED;
    reply.body.hello.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_FAULT;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK_STRING("a0a1a2a3a4a5a6a7a8a9aaab", fixture.core->cursor.device_id);
    TEST_CHECK_STRING("a0a1a2a3a4a5a6a7a8a9aaab", fixture.core->session.device_id);
    TEST_CHECK_U64(LOCKER_DEVICE_MODE_DEGRADED, fixture.core->session.device_mode);
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_JOURNAL_STATE_FAULT, fixture.core->session.journal_state);

    /* HELLO 失败：身份与模式都不写。 */
    fixture.core->session.device_mode = LOCKER_DEVICE_MODE_OFFLINE;
    reply.body.hello.result = LOCKER_WIRE_RESULT_BAD_VERSION;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK_U64(LOCKER_DEVICE_MODE_OFFLINE, fixture.core->session.device_mode);

    /* 换设备身份：锁存缺口且不覆盖已记住的 device_id。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(1u | 0x80u);
    reply.body.hello.result = LOCKER_WIRE_RESULT_OK;
    memset(reply.body.hello.device_uid, 0xff, sizeof(reply.body.hello.device_uid));
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(fixture.core->fetch_gap);
    TEST_CHECK(!fixture.core->cursor.continuity_ok);
    TEST_CHECK_STRING(DT_DEVICE_ID, fixture.core->cursor.device_id);

    /* RESUME_READY：会话恢复可用，模式来自设备。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    fixture.core->session.valid = false;
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(5u | 0x80u);
    reply.body.resume_ready.result = LOCKER_WIRE_RESULT_OK;
    reply.body.resume_ready.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_READY;
    reply.body.resume_ready.physical_revision = 55u;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(fixture.core->session.valid);
    TEST_CHECK_U64(LOCKER_DEVICE_MODE_READY, fixture.core->session.device_mode);

    /* HOST_HEALTH：只更新模式。 */
    fixture.core->session.device_mode = LOCKER_DEVICE_MODE_OFFLINE;
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(9u | 0x80u);
    reply.body.host_health.result = LOCKER_WIRE_RESULT_OK;
    reply.body.host_health.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK_U64(LOCKER_DEVICE_MODE_RECONCILING, fixture.core->session.device_mode);

    /* RELEASE_CONTEXT：只更新物理版本。 */
    fixture.core->physical.physical_revision = 1u;
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(11u | 0x80u);
    reply.body.release_context.result = LOCKER_WIRE_RESULT_OK;
    reply.body.release_context.physical_revision = 77u;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK_U64(77u, fixture.core->physical.physical_revision);

    /* 未知类型与空指针：不崩、不改状态。 */
    uint64_t revision_before = fixture.core->physical.physical_revision;
    memset(&reply, 0, sizeof(reply));
    reply.message_type = 0x7fu;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK_U64(revision_before, fixture.core->physical.physical_revision);
    domain_reply_apply(fixture.core, NULL);
    domain_event_apply(fixture.core, NULL);

    dt_fixture_free(&fixture);
}

/* ---------- sink 回调与 bind ---------- */

static void test_sink_and_bind(void)
{
    dt_fixture_t fixture;
    locker_device_event_t event;
    locker_device_reply_t reply;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);

    /* bind 把 Core 自己的两个回调装到设备上。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_device_bind(fixture.core));
    TEST_CHECK(fixture.device->bound);
    TEST_CHECK_U64(1u, fixture.device->bind_calls);
    TEST_CHECK(fixture.device->sink.reply != NULL);
    TEST_CHECK(fixture.device->sink.event != NULL);
    TEST_CHECK(fixture.device->sink.user == (void *)fixture.core);

    /* 环未满：事件进环，返回 OK。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 1u, true);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      fixture.device->sink.event(fixture.device->sink.user, &event));
    TEST_CHECK_U64(1u, locker_bounded_queue_size(&fixture.core->event_ring));
    TEST_CHECK(!fixture.core->event_overflow);

    /* 环满：仍然返回 OK（返回 NO_SPACE 会让设备侧 shutdown 会话），只锁存溢出标志。 */
    for (uint32_t index = 0u; index < 256u; index += 1u) {
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                          fixture.device->sink.event(fixture.device->sink.user, &event));
    }
    /* reader 线程只置原子标志：缺口是 Core 线程私有状态，必须由 Core 消费
       标志时才锁存，不能在回调里直接写。 */
    TEST_CHECK(atomic_load(&fixture.core->event_overflow_flag));
    TEST_CHECK(!fixture.core->fetch_gap);
    TEST_CHECK(fixture.core->cursor.continuity_ok);
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 2u, true);
    domain_handle_device_event(fixture.core, &event, UINT32_MAX);
    TEST_CHECK(!atomic_load(&fixture.core->event_overflow_flag));
    TEST_CHECK(fixture.core->event_overflow);
    TEST_CHECK(fixture.core->fetch_gap);
    TEST_CHECK(!fixture.core->cursor.continuity_ok);

    /* 回复环同理。 */
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(9u | 0x80u);
    for (uint32_t index = 0u; index < 256u; index += 1u) {
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                          fixture.device->sink.reply(fixture.device->sink.user, &reply));
    }
    TEST_CHECK(atomic_load(&fixture.core->reply_overflow_flag));

    /* 空指针一律 INVALID，不崩。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, fixture.device->sink.event(NULL, &event));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, fixture.device->sink.event(fixture.core, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, fixture.device->sink.reply(NULL, &reply));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, fixture.device->sink.reply(fixture.core, NULL));

    /* bind 失败要上抛，不能假装绑定成功。 */
    fixture.device->bind_status = LOCKER_STATUS_IO_ERROR;
    TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR, domain_device_bind(fixture.core));
    fixture.device->bind_status = LOCKER_STATUS_OK;

    dt_fixture_free(&fixture);
}

/* ---------- 发出动作的守卫 ---------- */

static void test_execute_guards(void)
{
    dt_fixture_t fixture;
    domain_operation_t operation;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_PENDING, 0x50u);
    operation = fixture.core->active;
    dt_uuid_fill(&operation.command_id, 0x90u);
    operation.has_command = true;
    operation.action_no = 5u;
    /* 真实的发送路径只可能来自 operation_arm：那时冻结的 guard 与 MCU 时钟域
       的期限已经随事务落库，发送必须原样透传它们（审计 R3-B-1）。夹具在这里
       把同一组字段补齐，才能验证「透传」而不是「重采」。 */
    dt_uuid_fill(&operation.outbox_id, 0x91u);
    operation.outbox_present = true;
    operation.outbox_session_id = fixture.core->session.session_id;
    operation.outbox_journal_epoch = fixture.core->session.journal_epoch;
    operation.outbox_boot_id = fixture.core->session.boot_id;
    operation.outbox_physical_revision = fixture.core->physical.physical_revision;
    operation.outbox_not_after_ms = fixture.core->now_ms + LOCKER_PREPARE_DEADLINE_MS;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_execute_intent(fixture.core, &operation));
    TEST_CHECK_U64(1u, fixture.device->execute_calls);
    TEST_CHECK(fixture.device->has_intent);
    TEST_CHECK_U64(5u, fixture.device->last_intent.action_no);
    TEST_CHECK_U64(1u, fixture.device->last_intent.slot_id);
    TEST_CHECK_U64(LOCKER_OPERATION_KIND_BORROW, fixture.device->last_intent.kind);
    /* 期望物理版本与期限都来自冻结的 outbox 意图，而不是发送瞬间重新采样。 */
    TEST_CHECK_U64(operation.outbox_physical_revision,
                   fixture.device->last_intent.expected_physical_revision);
    TEST_CHECK_U64(operation.outbox_not_after_ms, fixture.device->last_intent.not_after_mcu_ms);
    TEST_CHECK(locker_uuid_equal(&operation.outbox_session_id, &fixture.device->last_intent.session_id));
    TEST_CHECK(fixture.core->execute_in_flight);
    TEST_CHECK(fixture.core->pending_command_valid);
    TEST_CHECK_U64(0x90u, fixture.core->pending_command_id.bytes[0]);

    /* 在途时再发：BUSY，不再调端口。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_BUSY, domain_execute_intent(fixture.core, &operation));
    TEST_CHECK_U64(1u, fixture.device->execute_calls);

    /* 会话未建立 / 没有设备：NOT_READY。 */
    fixture.core->execute_in_flight = false;
    fixture.core->device_session_up = false;
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, domain_execute_intent(fixture.core, &operation));
    fixture.core->device_session_up = true;
    fixture.core->session.valid = false;
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, domain_execute_intent(fixture.core, &operation));

    dt_fixture_t bare;
    TEST_CHECK(dt_fixture_init(&bare, false));
    dt_core_ready(&bare);
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, domain_execute_intent(bare.core, &operation));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, domain_query_command(bare.core, &operation.command_id));
    dt_fixture_free(&bare);

    /* 端口返回 BUSY 时不得置在途标志（否则再也发不出去）。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    fixture.core->execute_in_flight = false;
    fixture.device->execute_status = LOCKER_STATUS_BUSY;
    TEST_CHECK_STATUS(LOCKER_STATUS_BUSY, domain_execute_intent(fixture.core, &operation));
    TEST_CHECK(!fixture.core->execute_in_flight);
    fixture.device->execute_status = LOCKER_STATUS_OK;

    /* 查询命令只在会话可用时下发。 */
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_query_command(fixture.core, &operation.command_id));
    TEST_CHECK_U64(1u, fixture.device->query_calls);
    TEST_CHECK_U64(0x90u, fixture.device->last_query_command.bytes[0]);
    fixture.core->device_session_up = false;
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, domain_query_command(fixture.core, &operation.command_id));
    TEST_CHECK_U64(1u, fixture.device->query_calls);

    dt_fixture_free(&fixture);
}

/* ---------- 补取入口 ---------- */

static void test_fetch_pump(void)
{
    dt_fixture_t fixture;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    fixture.core->fetch_in_flight = false;

    /* 无仓储水位：after 用全零（首次补取从日志头开始）。 */
    domain_fetch_pump(fixture.core);
    TEST_CHECK_U64(1u, fixture.device->fetch_calls);
    TEST_CHECK(fixture.device->has_after);
    TEST_CHECK_U64(0u, fixture.device->last_after.boot_id);
    TEST_CHECK_U64(0u, fixture.device->last_after.event_seq);

    /* after 用仓储已提交水位（import_cursor），不是视图游标：live 事件只推进
       视图游标、不落仓储，用 cursor.through 会跳过仓储还没见过的 seq，
       sim 返回的页与仓储水位不相邻，导入必然 CONFLICT（集成实测 status=6）。 */
    fixture.core->fetch_in_flight = false;
    fixture.core->import_cursor_valid = true;
    memcpy(fixture.core->import_cursor.device_id, DT_DEVICE_ID, sizeof(DT_DEVICE_ID));
    fixture.core->import_cursor.journal_epoch = fixture.core->session.journal_epoch;
    fixture.core->import_cursor.boot_id = 1u;
    fixture.core->import_cursor.event_seq = 12u;
    domain_fetch_pump(fixture.core);
    TEST_CHECK_U64(2u, fixture.device->fetch_calls);
    TEST_CHECK(fixture.device->has_after);
    TEST_CHECK_U64(12u, fixture.device->last_after.event_seq);
    TEST_CHECK_STRING(DT_DEVICE_ID, fixture.device->last_after.device_id);

    /* 在途不重发。 */
    domain_fetch_pump(fixture.core);
    TEST_CHECK_U64(2u, fixture.device->fetch_calls);

    /* 端口失败不置在途标志。 */
    fixture.core->fetch_in_flight = false;
    fixture.device->fetch_status = LOCKER_STATUS_NOT_READY;
    domain_fetch_pump(fixture.core);
    TEST_CHECK(!fixture.core->fetch_in_flight);

    dt_fixture_free(&fixture);
}

/* 真实启动路径：没有任何外部把 device_session_up 置真，会话必须由
   「泵发快照 → 快照回复 → apply_snapshot 置会话」这条链路自己建立。
   这是 S1-04 的恢复屏障起点，也是曾经把会话门当快照前置造成的死锁点。 */
static void test_session_bootstrap(void)
{
    dt_fixture_t fixture;
    locker_device_reply_t reply;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_ready(&fixture);
    /* 不调用 dt_core_device_ready：会话、快照、物理全是未建立状态。 */
    TEST_CHECK(!fixture.core->device_session_up);
    TEST_CHECK(!fixture.core->device_snapshot_valid);
    TEST_CHECK(!fixture.core->session.valid);
    TEST_CHECK(fixture.core->device != NULL);

    fixture.core->now_ms = 1000u;
    fixture.core->last_pump_ms = 0u;
    fixture.core->last_health_ms = 0u;

    /* 第一泵：会话还没有，但快照必须发出去。 */
    domain_device_pump(fixture.core);
    TEST_CHECK_U64(1u, fixture.device->snapshot_calls);
    TEST_CHECK(fixture.core->snapshot_in_flight);
    /* 会话没建立，补取/健康都不该发。 */
    TEST_CHECK_U64(0u, fixture.device->fetch_calls);
    TEST_CHECK_U64(0u, fixture.device->health_calls);

    /* 快照回复到达：apply_snapshot 建立会话与物理镜像。 */
    memset(&reply, 0, sizeof(reply));
    reply.request_no = fixture.device->last_request_no;
    reply.message_type = (uint8_t)(2u | 0x80u);
    reply.body.get_snapshot.result = LOCKER_WIRE_RESULT_OK;
    dt_uuid_fill(&reply.body.get_snapshot.session_id, 0x10u);
    dt_uuid_fill(&reply.body.get_snapshot.journal_epoch, 0x20u);
    reply.body.get_snapshot.boot_id = 1u;
    reply.body.get_snapshot.physical_revision = 7u;
    reply.body.get_snapshot.door = LOCKER_DOOR_STATE_CLOSED;
    reply.body.get_snapshot.item = LOCKER_ITEM_STATE_PRESENT;
    reply.body.get_snapshot.flags = 0x0001u;
    reply.body.get_snapshot.supply_mv = 12000u;
    reply.body.get_snapshot.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_READY;
    reply.body.get_snapshot.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
    domain_reply_apply(fixture.core, &reply);

    TEST_CHECK(fixture.core->device_session_up);
    TEST_CHECK(fixture.core->device_snapshot_valid);
    TEST_CHECK(fixture.core->session.valid);
    TEST_CHECK(fixture.core->physical.valid);
    TEST_CHECK_U64(7u, fixture.core->physical.physical_revision);

    /* 会话就绪后的泵：补取与健康才会发。 */
    fixture.core->now_ms = 1200u;
    domain_device_pump(fixture.core);
    TEST_CHECK_U64(1u, fixture.device->fetch_calls);
    TEST_CHECK_U64(1u, fixture.device->health_calls);

    dt_fixture_free(&fixture);
}

/* WAIT_CONFIRM 的冻结依据必须被**快照**作废。设备重启或补取之后，先到的
   往往是快照而不是事件；只在事件路径查冲突，会让「用户看到的柜内状态」与
   「用户将要确认的那次动作」脱钩而无人发现（审计 R3-B-3：探针冻结
   revision=7，收到 revision=8 的成功快照后仍停在 WAIT_CONFIRM 并可正常完成）。 */
static void test_snapshot_confirm_conflict(void)
{
    dt_fixture_t fixture;
    locker_device_event_t event;
    locker_device_reply_t reply;
    domain_operation_t loaded;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_ACTIVE, 0x50u);

    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 1u, true);
    event.door = LOCKER_DOOR_STATE_OPEN;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);

    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_CLOSED, 2u, true);
    event.door = LOCKER_DOOR_STATE_CLOSED;
    event.item = LOCKER_ITEM_STATE_ABSENT;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_WAIT_CONFIRM, fixture.core->active.state);
    TEST_CHECK(fixture.core->active.wait_confirm_basis_valid);
    /* dv_event 的 physical_revision = 7 + seq，seq=2 → 冻结在 9。 */
    TEST_CHECK_U64(9u, fixture.core->active.wait_confirm_physical_revision);

    fixture.core->snapshot_in_flight = true;
    memset(&reply, 0, sizeof(reply));
    reply.request_no = 1u;
    reply.message_type = (uint8_t)(2u | 0x80u);
    reply.body.get_snapshot.result = LOCKER_WIRE_RESULT_OK;
    reply.body.get_snapshot.session_id = fixture.core->session.session_id;
    reply.body.get_snapshot.journal_epoch = fixture.core->session.journal_epoch;
    reply.body.get_snapshot.boot_id = 1u;
    reply.body.get_snapshot.physical_revision = 12u;
    reply.body.get_snapshot.door = LOCKER_DOOR_STATE_OPEN;
    reply.body.get_snapshot.item = LOCKER_ITEM_STATE_PRESENT;
    reply.body.get_snapshot.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_READY;
    reply.body.get_snapshot.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
    domain_reply_apply(fixture.core, &reply);

    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, fixture.core->active.state);
    TEST_CHECK_STRING("PHYSICAL_CHANGED", fixture.core->active.reason_code);
    /* 结论必须落库：只改内存会让重启后回到 WAIT_CONFIRM 再被确认完成。 */
    memset(&loaded, 0, sizeof(loaded));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_operation_load(fixture.core, &fixture.core->active.id, 1, &loaded));
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, loaded.state);
    TEST_CHECK_STRING("PHYSICAL_CHANGED", loaded.reason_code);

    dt_fixture_free(&fixture);
}

/* 传感器读不出物品、或当前物品既不等于期望终态也不等于原基线时，都不能
   替用户宣布「什么都没变」：NO_CHANGE 的语义是「回到原基线、账本不变」。
   审计 R3-B-11 探针①：ACCESS_ACTIVE 见开门关门、item=UNKNOWN → 直接
   NO_CHANGE 并释放占位。 */
static void test_event_item_unknown_and_mismatch(void)
{
    dt_fixture_t fixture;
    locker_device_event_t event;

    /* ① 物品 UNKNOWN → PHYSICAL_UNKNOWN（不是 NO_CHANGE）。 */
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_ACTIVE, 0x50u);
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 1u, true);
    event.door = LOCKER_DOOR_STATE_OPEN;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_CLOSED, 2u, true);
    event.door = LOCKER_DOOR_STATE_CLOSED;
    event.item = LOCKER_ITEM_STATE_UNKNOWN;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, fixture.core->active.state);
    TEST_CHECK_STRING("PHYSICAL_UNKNOWN", fixture.core->active.reason_code);
    TEST_CHECK(fixture.core->active.used);
    dt_fixture_free(&fixture);

    /* ② 原基线不可知（UNKNOWN）→ 也不能宣布 NO_CHANGE。 */
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_ACTIVE, 0x60u);
    fixture.core->active.baseline.item = LOCKER_ITEM_STATE_UNKNOWN;
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 1u, true);
    event.door = LOCKER_DOOR_STATE_OPEN;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_CLOSED, 2u, true);
    event.door = LOCKER_DOOR_STATE_CLOSED;
    event.item = LOCKER_ITEM_STATE_PRESENT;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, fixture.core->active.state);
    TEST_CHECK_STRING("PHYSICAL_MISMATCH", fixture.core->active.reason_code);
    dt_fixture_free(&fixture);
}

/* 同 boot 内事件序号必须逐条 +1。跳号到达（1、3、4）意味着丢掉的那一条
   可能就是「物品离位」，此时既不能声称历史完整，也不能直接进确认
   （审计 R3-B-12：探针实测 continuity=1、fetch_gap=0、history_complete=1）。 */
static void test_event_sequence_gap(void)
{
    dt_fixture_t fixture;
    locker_device_event_t event;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_ACTIVE, 0x50u);
    TEST_CHECK(fixture.core->cursor.continuity_ok);
    TEST_CHECK(fixture.core->active.history_complete);

    /* seq=1 是首条（水位还是 0）：正常应用，不判缺口。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 1u, true);
    event.door = LOCKER_DOOR_STATE_OPEN;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK(fixture.core->cursor.continuity_ok);
    TEST_CHECK(!fixture.core->fetch_gap);
    TEST_CHECK_U64(1u, fixture.core->cursor.through.event_seq);

    /* 跳到 3：中间那条 2 永远补不回来。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_ITEM_REMOVED, 3u, true);
    event.door = LOCKER_DOOR_STATE_OPEN;
    event.item = LOCKER_ITEM_STATE_ABSENT;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK(!fixture.core->cursor.continuity_ok);
    TEST_CHECK(fixture.core->fetch_gap);
    TEST_CHECK(!fixture.core->active.history_complete);
    TEST_CHECK_U64(3u, fixture.core->cursor.through.event_seq);

    dt_fixture_free(&fixture);
}

/* 设备路径的状态推进也必须是「候选副本 + 一次提交」：提交失败时内存不能
   已经跳到核验，否则 domain_require_review 遇到「内存已是 REVIEW_REQUIRED」
   会短路返回 OK，核验结论永远补不进仓储
   （审计 R3-B-7：探针注入 commit_status=IO_ERROR 后 active=REVIEW_REQUIRED、
   stored=ACCESS_PENDING、publish_calls=1）。 */
static void test_reply_commit_failure(void)
{
    dt_fixture_t fixture;
    locker_device_reply_t reply;
    domain_operation_t loaded;
    locker_uuid_t id;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_PENDING, 0x50u);
    id = fixture.core->active.id;
    dt_uuid_fill(&fixture.core->active.command_id, 0x90u);
    fixture.core->active.has_command = true;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_persist_operation(fixture.core, &fixture.core->active));
    fixture.core->pending_command_valid = true;
    fixture.core->pending_command_id = fixture.core->active.command_id;
    fixture.core->execute_in_flight = true;

    /* 折叠成 18 字节前缀的失败应答（body 全零），落 BAD_PRECONDITION。 */
    fixture.repo->commit_status = LOCKER_STATUS_IO_ERROR;
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(7u | 0x80u);
    reply.body.execute_unlock.result = LOCKER_WIRE_RESULT_BAD_PRECONDITION;
    domain_reply_apply(fixture.core, &reply);

    /* 内存与库都停在 ACCESS_PENDING：这次核验没能落库，下一帧还能再推一次。 */
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_PENDING, fixture.core->active.state);
    memset(&loaded, 0, sizeof(loaded));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_operation_load(fixture.core, &id, 1, &loaded));
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_PENDING, loaded.state);

    /* 提交恢复后同样的应答能把结论写进去（证明只是「这次失败」，不是永久拒绝）。 */
    fixture.repo->commit_status = LOCKER_STATUS_OK;
    fixture.core->pending_command_valid = true;
    fixture.core->pending_command_id = fixture.core->active.command_id;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, fixture.core->active.state);
    memset(&loaded, 0, sizeof(loaded));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_operation_load(fixture.core, &id, 1, &loaded));
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, loaded.state);
    TEST_CHECK_STRING("COMMAND_REJECTED", loaded.reason_code);

    dt_fixture_free(&fixture);
}

/* NO_CHANGE 也是终态：设备侧那次动作上下文必须释放，否则下一次 EXECUTE
   永远 BUSY，柜子只能用一次（审计 R3-B-2 探针：NO_CHANGE 之后
   release_pending=0、release_calls=0）。释放以快照里的 active_operation_id
   为准闭合，而不是凭物理版本变了就宣布设备已放手（uart-v1.md:402）。 */
static void test_release_after_no_change(void)
{
    dt_fixture_t fixture;
    locker_device_event_t event;
    locker_device_reply_t reply;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_ACTIVE, 0x50u);
    dt_uuid_fill(&fixture.core->active.command_id, 0x90u);
    fixture.core->active.has_command = true;
    TEST_CHECK(!fixture.core->release_pending);

    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 1u, true);
    event.door = LOCKER_DOOR_STATE_OPEN;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_CLOSED, 2u, true);
    event.door = LOCKER_DOOR_STATE_CLOSED;
    event.item = LOCKER_ITEM_STATE_PRESENT;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_NO_CHANGE, fixture.core->active.state);

    /* NO_CHANGE 之后必须登记释放。 */
    TEST_CHECK(fixture.core->release_pending);
    TEST_CHECK(!fixture.core->release_sent);
    TEST_CHECK_U64(0x50u, fixture.core->release_operation_id.bytes[0]);
    TEST_CHECK_U64(0x90u, fixture.core->release_command_id.bytes[0]);

    /* 泵把释放发出去，guard 带的是被释放那条动作的身份。 */
    domain_release_pump(fixture.core);
    TEST_CHECK_U64(1u, fixture.device->release_calls);
    TEST_CHECK(fixture.core->release_sent);
    TEST_CHECK_U64(0x50u, fixture.device->last_release.operation_id.bytes[0]);
    TEST_CHECK_U64(0x90u, fixture.device->last_release.command_id.bytes[0]);

    /* CAS 失败（CONFLICT）：不能凭「物理版本变了」宣布已放手，只能转查询。 */
    fixture.device->release_status = LOCKER_STATUS_CONFLICT;
    fixture.core->release_sent = false;
    domain_release_pump(fixture.core);
    TEST_CHECK(fixture.core->release_recheck);
    TEST_CHECK(fixture.core->release_pending);
    /* 只查询期间不再重发。 */
    {
        uint32_t before = fixture.device->release_calls;
        domain_release_pump(fixture.core);
        TEST_CHECK_U64(before, fixture.device->release_calls);
    }
    fixture.device->release_status = LOCKER_STATUS_OK;

    /* 快照仍挂着我们要释放的那条：解除只查询、允许用新版本重发一次。 */
    fixture.core->snapshot_in_flight = true;
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(2u | 0x80u);
    reply.body.get_snapshot.result = LOCKER_WIRE_RESULT_OK;
    reply.body.get_snapshot.session_id = fixture.core->session.session_id;
    reply.body.get_snapshot.journal_epoch = fixture.core->session.journal_epoch;
    reply.body.get_snapshot.boot_id = 1u;
    reply.body.get_snapshot.physical_revision = 20u;
    reply.body.get_snapshot.door = LOCKER_DOOR_STATE_CLOSED;
    reply.body.get_snapshot.item = LOCKER_ITEM_STATE_PRESENT;
    reply.body.get_snapshot.active_operation_id = fixture.core->release_operation_id;
    reply.body.get_snapshot.active_command_id = fixture.core->release_command_id;
    reply.body.get_snapshot.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_READY;
    reply.body.get_snapshot.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(!fixture.core->release_recheck);
    TEST_CHECK(!fixture.core->release_sent);
    TEST_CHECK(fixture.core->release_pending);
    domain_release_pump(fixture.core);
    TEST_CHECK_U64(3u, fixture.device->release_calls);
    TEST_CHECK_U64(20u, fixture.device->last_release.expected_physical_revision);

    /* 新快照里设备已不再持有任何动作：释放目的达成，标志清空。 */
    fixture.core->snapshot_in_flight = true;
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(2u | 0x80u);
    reply.body.get_snapshot.result = LOCKER_WIRE_RESULT_OK;
    reply.body.get_snapshot.session_id = fixture.core->session.session_id;
    reply.body.get_snapshot.journal_epoch = fixture.core->session.journal_epoch;
    reply.body.get_snapshot.boot_id = 1u;
    reply.body.get_snapshot.physical_revision = 21u;
    reply.body.get_snapshot.door = LOCKER_DOOR_STATE_CLOSED;
    reply.body.get_snapshot.item = LOCKER_ITEM_STATE_PRESENT;
    reply.body.get_snapshot.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_READY;
    reply.body.get_snapshot.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(!fixture.core->release_pending);
    TEST_CHECK(!fixture.core->release_sent);
    TEST_CHECK(!fixture.core->release_recheck);
    TEST_CHECK(fixture.core->device_active_valid);
    TEST_CHECK(locker_uuid_is_nil(&fixture.core->device_active_operation_id));

    dt_fixture_free(&fixture);
}

/* 确认依据必须绑定完整代际，而不只是 physical_revision。
   physical_revision 只在同一次 boot 内单调（internal-interfaces.md:12），
   换 boot 之后 revision 从头计数，只比大小会把「设备重启后的旧事实」当成
   「更早的陈旧事实」放过（审计 R3-B-3 探针 A：冻结 boot=1/rev=9，
   送来 boot=2/rev=1 的成功快照仍保持 WAIT_CONFIRM）。 */
static void test_confirm_basis_generation(void)
{
    dt_fixture_t fixture;
    locker_device_event_t event;
    locker_device_reply_t reply;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_ACTIVE, 0x50u);

    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 1u, true);
    event.door = LOCKER_DOOR_STATE_OPEN;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_CLOSED, 2u, true);
    event.door = LOCKER_DOOR_STATE_CLOSED;
    event.item = LOCKER_ITEM_STATE_ABSENT;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_WAIT_CONFIRM, fixture.core->active.state);
    TEST_CHECK(fixture.core->active.wait_confirm_basis_valid);
    TEST_CHECK_U64(1u, fixture.core->active.wait_confirm_boot_id);
    TEST_CHECK(locker_uuid_equal(&fixture.core->active.wait_confirm_journal_epoch,
                                 &fixture.core->physical.journal_epoch));

    /* 设备重启过（boot 2）、revision 比冻结值小：换 boot 说明事实序列已经重来，
       这不是「更早的陈旧事实」，必须作废确认依据。 */
    fixture.core->snapshot_in_flight = true;
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(2u | 0x80u);
    reply.body.get_snapshot.result = LOCKER_WIRE_RESULT_OK;
    reply.body.get_snapshot.session_id = fixture.core->session.session_id;
    reply.body.get_snapshot.journal_epoch = fixture.core->session.journal_epoch;
    reply.body.get_snapshot.boot_id = 2u;
    reply.body.get_snapshot.physical_revision = 1u;
    reply.body.get_snapshot.door = LOCKER_DOOR_STATE_CLOSED;
    reply.body.get_snapshot.item = LOCKER_ITEM_STATE_ABSENT;
    reply.body.get_snapshot.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_READY;
    reply.body.get_snapshot.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, fixture.core->active.state);
    TEST_CHECK_STRING("PHYSICAL_CHANGED", fixture.core->active.reason_code);

    dt_fixture_free(&fixture);
}

/* 终结落库后崩溃：内存里的 release_pending 全丢，而 MCU 侧的 has_active 还在。
   重启后必须从快照的 active_operation_id 重新登记释放，否则 release_calls
   永远是 0，柜子只能用一次（审计 R3-B-2 尚存项）。 */
static void test_release_after_restart(void)
{
    dt_fixture_t fixture;
    locker_device_reply_t reply;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_ABSENT);
    /* 恢复后内存里没有活动操作，但设备侧还挂着上一次动作。 */
    TEST_CHECK(!fixture.core->active.used);
    TEST_CHECK(!fixture.core->release_pending);

    /* 设备报的那条操作在账本里已经终结：这才是允许补登记释放的前提。 */
    locker_uuid_t settled_id;
    locker_uuid_t tool_id;
    locker_uuid_t actor_id;
    dt_uuid_fill(&settled_id, 0x77u);
    dt_uuid_fill(&tool_id, 0x22u);
    dt_uuid_fill(&actor_id, 0x51u);
    TEST_CHECK(dt_seed_operation(&fixture, &settled_id, &tool_id, &actor_id, LOCKER_OPERATION_KIND_BORROW,
                                 LOCKER_OPERATION_STATE_COMPLETED, 3u, "2026-10-26T00:00:00Z"));

    fixture.core->snapshot_in_flight = true;
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(2u | 0x80u);
    reply.body.get_snapshot.result = LOCKER_WIRE_RESULT_OK;
    reply.body.get_snapshot.session_id = fixture.core->session.session_id;
    reply.body.get_snapshot.journal_epoch = fixture.core->session.journal_epoch;
    reply.body.get_snapshot.boot_id = 1u;
    reply.body.get_snapshot.physical_revision = 7u;
    reply.body.get_snapshot.door = LOCKER_DOOR_STATE_CLOSED;
    reply.body.get_snapshot.item = LOCKER_ITEM_STATE_ABSENT;
    reply.body.get_snapshot.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_READY;
    reply.body.get_snapshot.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
    reply.body.get_snapshot.active_operation_id = settled_id;
    dt_uuid_fill(&reply.body.get_snapshot.active_command_id, 0x78u);
    domain_reply_apply(fixture.core, &reply);

    TEST_CHECK(fixture.core->release_pending);
    TEST_CHECK(!fixture.core->release_sent);
    TEST_CHECK_U64(0x77u, fixture.core->release_operation_id.bytes[0]);
    TEST_CHECK_U64(0x78u, fixture.core->release_command_id.bytes[0]);
    domain_release_pump(fixture.core);
    TEST_CHECK_U64(1u, fixture.device->release_calls);
    TEST_CHECK_U64(0x77u, fixture.device->last_release.operation_id.bytes[0]);
    /* 释放未完成期间不许武装新动作：那条路径由
       test_domain_commands.c::test_presence_blocked_while_releasing 覆盖。 */

    dt_fixture_free(&fixture);
}

/* 快照里挂着的操作如果还没终结（账本里查不到、或仍是非终态），
   **不能**凭设备侧缓存就发 RELEASE_CONTEXT——那等于用一条缓存替业务收尾，
   释放之后那条未决操作再也拿不到动作上下文（审计 round-5 第 1 项）。 */
static void test_release_requires_settled_operation(void)
{
    dt_fixture_t fixture;
    locker_device_reply_t reply;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_ABSENT);
    TEST_CHECK(!fixture.core->release_pending);

    /* 情形一：账本里根本没有这条操作。 */
    fixture.core->snapshot_in_flight = true;
    memset(&reply, 0, sizeof(reply));
    reply.message_type = (uint8_t)(2u | 0x80u);
    reply.body.get_snapshot.result = LOCKER_WIRE_RESULT_OK;
    reply.body.get_snapshot.session_id = fixture.core->session.session_id;
    reply.body.get_snapshot.journal_epoch = fixture.core->session.journal_epoch;
    reply.body.get_snapshot.boot_id = 1u;
    reply.body.get_snapshot.physical_revision = 7u;
    reply.body.get_snapshot.door = LOCKER_DOOR_STATE_CLOSED;
    reply.body.get_snapshot.item = LOCKER_ITEM_STATE_ABSENT;
    reply.body.get_snapshot.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_READY;
    reply.body.get_snapshot.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
    dt_uuid_fill(&reply.body.get_snapshot.active_operation_id, 0x91u);
    dt_uuid_fill(&reply.body.get_snapshot.active_command_id, 0x92u);
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(!fixture.core->release_pending);
    domain_release_pump(fixture.core);
    TEST_CHECK_U64(0u, fixture.device->release_calls);

    /* 情形二：账本里有，但仍是未决状态（ACCESS_ACTIVE）。 */
    locker_uuid_t pending_id;
    locker_uuid_t tool_id;
    locker_uuid_t actor_id;
    dt_uuid_fill(&pending_id, 0x91u);
    dt_uuid_fill(&tool_id, 0x22u);
    dt_uuid_fill(&actor_id, 0x52u);
    TEST_CHECK(dt_seed_operation(&fixture, &pending_id, &tool_id, &actor_id, LOCKER_OPERATION_KIND_BORROW,
                                 LOCKER_OPERATION_STATE_ACCESS_ACTIVE, 4u, "2026-10-26T00:00:00Z"));
    fixture.core->snapshot_in_flight = true;
    domain_reply_apply(fixture.core, &reply);
    TEST_CHECK(!fixture.core->release_pending);
    domain_release_pump(fixture.core);
    TEST_CHECK_U64(0u, fixture.device->release_calls);

    dt_fixture_free(&fixture);
}

/* HISTORY_INCOMPLETE 是锁存：设备报「我丢过关键事件」之后，序号连续也不能
   当成完整证据推进到确认（uart-v1.md:50、审计 R4-B-2 探针 A/B）。 */
static void test_event_history_incomplete(void)
{
    dt_fixture_t fixture;
    locker_device_event_t event;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 1u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    dv_arm_operation(fixture.core, LOCKER_OPERATION_KIND_BORROW,
                     LOCKER_OPERATION_STATE_ACCESS_ACTIVE, 0x50u);

    /* 序号连续（1、2），但第二条自己带 HISTORY_INCOMPLETE。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_OPENED, 1u, true);
    event.door = LOCKER_DOOR_STATE_OPEN;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK(fixture.core->cursor.continuity_ok);

    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_ITEM_REMOVED, 2u, true);
    event.door = LOCKER_DOOR_STATE_OPEN;
    event.item = LOCKER_ITEM_STATE_ABSENT;
    event.flags = DOMAIN_OBSERVATION_FLAG_HISTORY_INCOMPLETE;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK(!fixture.core->cursor.continuity_ok);
    TEST_CHECK(fixture.core->fetch_gap);
    TEST_CHECK(!fixture.core->active.history_complete);

    /* 之后门关、物品也符合 BORROW 的期望终态：仍然不能推进到 WAIT_CONFIRM。 */
    event = dv_event(fixture.core, LOCKER_DEVICE_EVENT_DOOR_CLOSED, 3u, true);
    event.door = LOCKER_DOOR_STATE_CLOSED;
    event.item = LOCKER_ITEM_STATE_ABSENT;
    dv_attach_active(fixture.core, &event);
    domain_event_apply(fixture.core, &event);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, fixture.core->active.state);
    TEST_CHECK_STRING("HISTORY_INCOMPLETE", fixture.core->active.reason_code);

    dt_fixture_free(&fixture);
}

int main(void)
{
    test_request_slots();
    test_device_reset();
    test_pump_gates();
    test_session_bootstrap();
    test_snapshot_apply();
    test_event_cursor_rules();
    test_event_gap_flags();
    test_event_pulse_advances();
    test_event_physical_evidence();
    test_event_card_history();
    test_execute_response();
    test_query_command_response();
    test_ack_response();
    test_fetch_response();
    test_other_replies();
    test_sink_and_bind();
    test_execute_guards();
    test_fetch_pump();
    test_snapshot_confirm_conflict();
    test_event_item_unknown_and_mismatch();
    test_event_sequence_gap();
    test_reply_commit_failure();
    test_release_after_no_change();
    test_confirm_basis_generation();
    test_release_after_restart();
    test_release_requires_settled_operation();
    test_event_history_incomplete();
    TEST_REPORT("domain.test_domain_device");
}
