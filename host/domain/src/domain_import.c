#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 业务核心：设备日志页导入、ACK 水位与恢复屏障落库。
 *
 * 三条路径的职责（契约 :203/:213/:227）：
 *   - domain_import_page：把一页 FETCH_EVENTS 响应交给仓储做「新事件去重 +
 *     连续游标推进」的同一事务，成功后才推进 import_cursor。冲突（缺口不能
 *     被游标越过）返回 CONFLICT，由调用方锁存缺口，绝不部分落库。
 *   - domain_ack_refresh：只把「仓储已提交的连续水位」记为待 ACK，绝不用内存
 *     里随 live 事件前进的 cursor.through（那还没落库，ACK 早了会丢事件）。
 *   - domain_persist_device_state：把最近一次可信快照写成 device_state 记录，
 *     供重启后的核对使用；内容没变就不提交事务。
 *
 * 设备标识（device_id）的来源只有两处，都不经新增公共接口：
 *   - HELLO 响应里的 12 字节 device_uid，转 24 位小写 hex（uart-v1.md:96）；
 *   - live DEVICE_EVENT 帧的 event.key.device_id（适配器按握手身份填写）。
 * FETCH_EVENTS 的游标只有 boot/seq，不带 device_id，所以 Core 必须自己记住它。
 */

#include "domain_internal.h"

#include <string.h>

#include "locker/common/log.h"

/* DeviceId 在 internal-data.schema.json 里是 ^[0-9a-f]{24}$。 */
static bool device_id_text_valid(const char *text)
{
    if (text == NULL) {
        return false;
    }
    for (size_t index = 0u; index < 24u; ++index) {
        char ch = text[index];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
            return false;
        }
    }
    return text[24] == '\0';
}

void domain_learn_device_id(locker_core_t *core, const char *device_id)
{
    if (core == NULL || !device_id_text_valid(device_id)) {
        return;
    }
    if (core->cursor.device_id[0] != '\0' && strcmp(core->cursor.device_id, device_id) != 0) {
        /* 同一会话里设备身份变了：不做「以新盖旧」，让上层按缺口处理。 */
        core->fetch_gap = true;
        core->cursor.continuity_ok = false;
        return;
    }
    memcpy(core->cursor.device_id, device_id, sizeof(core->cursor.device_id));
    memcpy(core->session.device_id, device_id, sizeof(core->session.device_id));
}

void domain_learn_device_uid(locker_core_t *core, const uint8_t uid[12])
{
    if (core == NULL || uid == NULL) {
        return;
    }
    static const char k_hex[] = "0123456789abcdef";
    char text[25];
    for (size_t index = 0u; index < 12u; ++index) {
        text[index * 2u] = k_hex[(uid[index] >> 4) & 0x0fu];
        text[index * 2u + 1u] = k_hex[uid[index] & 0x0fu];
    }
    text[24] = '\0';
    domain_learn_device_id(core, text);
}

/* wire 游标 + 已知 device/epoch -> 完整事件键。 */
static void key_from_wire(locker_event_key_t *out, const char *device_id, const locker_uuid_t *epoch,
                          const locker_wire_cursor_t *cursor)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->device_id, device_id, sizeof(out->device_id));
    out->journal_epoch = *epoch;
    out->boot_id = cursor->boot_id;
    out->event_seq = cursor->event_seq;
}

locker_status_t domain_import_page(locker_core_t *core, const locker_wire_fetch_events_response_t *page)
{
    if (core == NULL || page == NULL || core->repo == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (page->event_count == 0u) {
        /* 空页没有可导入的事实；仓储要求 count>=1，不能拿空页去推进水位。 */
        return LOCKER_STATUS_OK;
    }
    if (page->event_count > LOCKER_DOMAIN_EVENT_PAGE_MAX) {
        return LOCKER_STATUS_INVALID;
    }
    if (core->cursor.device_id[0] == '\0') {
        /* 还不知道设备身份：先不落库，等 HELLO 或首个 live 事件补上。 */
        return LOCKER_STATUS_NOT_READY;
    }

    locker_event_batch_t batch;
    memset(&batch, 0, sizeof(batch));
    batch.count = page->event_count;
    batch.has_more = page->has_more != 0u;
    batch.gap_before_page = page->gap_before_page != 0u;
    key_from_wire(&batch.first_available, core->cursor.device_id, &page->journal_epoch, &page->first_available);
    key_from_wire(&batch.last_committed, core->cursor.device_id, &page->journal_epoch, &page->last_committed);
    key_from_wire(&batch.next_after, core->cursor.device_id, &page->journal_epoch, &page->next_after);
    for (uint8_t index = 0u; index < page->event_count; ++index) {
        const locker_wire_event96_t *record = &page->events[index];
        locker_device_event_t *event = &batch.events[index];
        memset(event, 0, sizeof(*event));
        memcpy(event->key.device_id, core->cursor.device_id, sizeof(event->key.device_id));
        event->key.journal_epoch = page->journal_epoch;
        event->key.boot_id = record->boot_id;
        event->key.event_seq = record->event_seq;
        event->mono_ms = record->mono_ms;
        event->physical_revision = record->physical_revision;
        event->detail = record->detail;
        event->event_type = record->event_type;
        event->reason = record->reason;
        event->flags = record->flags;
        event->operation_id = record->operation_id;
        event->command_id = record->command_id;
        event->door = (locker_door_state_t)record->door;
        event->item = (locker_item_state_t)record->item;
        event->supply_mv = record->supply_mv;
        event->card_uid_len = record->card_uid_len;
        memcpy(event->card_uid, record->card_uid, sizeof(event->card_uid));
        /* 补取到的事件是历史，不是柜前实时证明（契约 :202、卡 :74）。 */
        event->live_transport = false;
    }

    locker_event_key_t committed;
    memset(&committed, 0, sizeof(committed));
    /* 投影变更传 NULL：Core 不为事件猜业务投影（契约 :213）。 */
    locker_status_t status = locker_repo_import_events(core->repo, &batch, NULL, &committed);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    bool first_cursor = !core->import_cursor_valid;
    core->import_cursor = committed;
    core->import_cursor_valid = true;
    if (first_cursor) {
        core->import_continuity = page->gap_before_page == 0u;
    } else if (page->gap_before_page != 0u) {
        core->import_continuity = false;
    }
    /* 补取页面里只要有一条事件带 HISTORY_INCOMPLETE，本页整体就不能算连续：
       序号连续不代表没丢（uart-v1.md:50/:105）。gap_before_page 只说「本页与
       上一页之间有没有缺口」，管不到页内事件自己报的丢失，两者必须分开判
       （审计 R4-B-2 探针 B：连续页 + record.flags 含 0x0004，导入后仍 continuity=true）。 */
    bool page_incomplete = false;
    for (uint8_t index = 0u; index < page->event_count; ++index) {
        if ((page->events[index].flags & DOMAIN_OBSERVATION_FLAG_HISTORY_INCOMPLETE) != 0u) {
            page_incomplete = true;
            break;
        }
    }
    if (page_incomplete) {
        core->import_continuity = false;
    }
    /* 流连续性派生：仓储判定的「连续历史」是唯一能证明没丢事件的依据
       （uart-v1.md:370）。没有这一步，实时首启路径上的
       core->cursor.continuity_ok 永远是 calloc 的 false，于是
       domain_commands.c:physical_ready_for_action 恒假、OPERATION_CREATE
       恒返回 NOT_READY，业务永远起步不了（集成测试实测 got 5）。
       反向必须锁存：HISTORY_INCOMPLETE 一旦成立就不能靠后续连续页洗白
       （uart-v1.md:50「RESUME_READY 不能清该锁存」），所以只有
       「仓储认定连续」且「本会话没有已锁存缺口」才置真，否则一律锁死。 */
    bool continuous = !core->fetch_gap && core->import_continuity;
    core->cursor.continuity_ok = continuous;
    if (core->physical.valid) {
        /* device_state.physical 与动作准入读同一事实，不能一边真一边假。 */
        core->physical.continuity_ok = continuous;
        if (continuous) {
            core->physical.durable_cursor = core->import_cursor;
        }
    }
    domain_ack_refresh(core);
    return LOCKER_STATUS_OK;
}

void domain_ack_refresh(locker_core_t *core)
{
    if (core == NULL || !core->import_cursor_valid) {
        return;
    }
    core->ack_cursor = core->import_cursor;
    core->ack_cursor_valid = true;
}

locker_status_t domain_persist_device_state(locker_core_t *core)
{
    if (core == NULL || core->repo == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!core->physical.valid || !device_id_text_valid(core->physical.device_id)) {
        /* 没有可信快照就没有可写的 device_state：宁可不写，也不写占位行。 */
        return LOCKER_STATUS_NOT_READY;
    }
    uint8_t mode = domain_core_device_mode(core);
    if (core->persisted_state_valid && strcmp(core->persisted_device_id, core->physical.device_id) == 0 &&
        core->persisted_boot_id == core->physical.boot_id &&
        core->persisted_physical_revision == core->physical.physical_revision &&
        core->persisted_mode == mode) {
        return LOCKER_STATUS_OK;
    }

    domain_change_t change;
    domain_change_init(core, &change, core->revision, core->revision + 1u);
    if (core->persisted_state_valid && strcmp(core->persisted_device_id, core->physical.device_id) != 0) {
        /* 设备被换过：旧身份行必须同事务删除，否则恢复期会读到两台设备。 */
        if (!domain_change_delete(&change, "device_state", core->persisted_device_id)) {
            return LOCKER_STATUS_NO_SPACE;
        }
    }
    if (!domain_change_put_begin(&change, "device_state", core->physical.device_id)) {
        return LOCKER_STATUS_NO_SPACE;
    }
    const locker_uuid_t *active_command = NULL;
    if (core->active.used && core->active.has_command) {
        active_command = &core->active.command_id;
    }
    /*
     * simulated：locker_core_config_t 没有这个字段，Core 也无法从设备端口读到
     * 适配器的 simulated 开关，因此这里只能写 false。已登记为契约缺口 CR-S104-1，
     * S1-06 Runtime 装配时必须回填真实值（见 docs/evidence/stage-01/S1-04/handoff.md）。
     */
    domain_write_device_state(&change.writer, core->cabinet_name, LOCKER_CONTRACT_VERSION, false, mode,
                              &core->physical, core->revision + 1u, active_command, active_command != NULL,
                              domain_now(core));
    domain_change_put_end(&change);
    locker_status_t status = domain_change_commit(core, &change);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    core->persisted_state_valid = true;
    memcpy(core->persisted_device_id, core->physical.device_id, sizeof(core->persisted_device_id));
    core->persisted_boot_id = core->physical.boot_id;
    core->persisted_physical_revision = core->physical.physical_revision;
    core->persisted_mode = mode;
    return LOCKER_STATUS_OK;
}
