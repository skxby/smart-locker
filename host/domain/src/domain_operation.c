#define _POSIX_C_SOURCE 200809L

#include "domain_internal.h"

#include <string.h>

/* 操作工作副本与持久化投影。
 *
 * 状态机的事实来源是 docs/stages/stage-00-architecture.md:106-128 的状态表；
 * 本文件只负责「把内存里的操作写成 schema 认可的 OperationRecord」，以及状态
 * 优先级与允许动作的推导，不做任何设备 IO。 */

void domain_operation_reset(domain_operation_t *operation)
{
    if (operation == NULL) {
        return;
    }
    memset(operation, 0, sizeof(*operation));
}

void domain_operation_allowed_actions(const domain_operation_t *operation, uint8_t *out, uint32_t *count)
{
    if (operation == NULL || out == NULL || count == NULL) {
        return;
    }
    uint32_t used = 0u;
    switch (operation->state) {
    case LOCKER_OPERATION_STATE_REQUESTED:
        out[used++] = LOCKER_ALLOWED_ACTION_PRESENT_AT_CABINET;
        out[used++] = LOCKER_ALLOWED_ACTION_WAIT;
        out[used++] = LOCKER_ALLOWED_ACTION_CANCEL;
        break;
    case LOCKER_OPERATION_STATE_ACCESS_PENDING:
        /* 命令可能已被接纳：取消会变成结果未知，因此只给等待。 */
        out[used++] = LOCKER_ALLOWED_ACTION_WAIT;
        break;
    case LOCKER_OPERATION_STATE_ACCESS_ACTIVE:
        out[used++] = LOCKER_ALLOWED_ACTION_WAIT;
        break;
    case LOCKER_OPERATION_STATE_WAIT_CONFIRM:
        out[used++] = LOCKER_ALLOWED_ACTION_CONFIRM;
        out[used++] = LOCKER_ALLOWED_ACTION_WAIT;
        break;
    case LOCKER_OPERATION_STATE_REVIEW_REQUIRED:
        out[used++] = LOCKER_ALLOWED_ACTION_ADMIN_REVIEW;
        break;
    default:
        break;
    }
    *count = used;
}

void domain_operation_touch(locker_core_t *core, domain_operation_t *operation)
{
    if (core == NULL || operation == NULL) {
        return;
    }
    uint64_t now = domain_now(core);
    char text[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    uint64_t wall = 0u;
    if (locker_clock_port_wall_utc_ms(&core->clock, &wall) != LOCKER_STATUS_OK) {
        wall = 0u;
    }
    if (!locker_domain_time_format_utc(wall, text, sizeof(text))) {
        text[0] = '\0';
    }
    if (operation->created_at[0] == '\0') {
        domain_text_copy(operation->created_at, sizeof(operation->created_at), text);
        operation->created_ms = now;
    }
    domain_text_copy(operation->updated_at, sizeof(operation->updated_at), text);
    operation->updated_ms = now;
    operation->revision += 1u;
}

void domain_operation_add_source_event(domain_operation_t *operation, const locker_event_key_t *key)
{
    if (operation == NULL || key == NULL) {
        return;
    }
    for (uint32_t i = 0u; i < operation->source_event_count; ++i) {
        const locker_event_key_t *existing = &operation->source_events[i];
        if (locker_uuid_equal(&existing->journal_epoch, &key->journal_epoch) &&
            domain_text_equal(existing->device_id, key->device_id) && existing->boot_id == key->boot_id &&
            existing->event_seq == key->event_seq) {
            return;
        }
    }
    if (operation->source_event_count >= LOCKER_DOMAIN_SOURCE_EVENTS) {
        /* 证据超出上限：保留「历史不完整」事实，绝不静默截断成完整。 */
        operation->history_complete = false;
        return;
    }
    operation->source_events[operation->source_event_count++] = *key;
}

void domain_operation_capture_physical(domain_operation_t *operation, const domain_physical_t *physical)
{
    if (operation == NULL || physical == NULL || !physical->valid) {
        return;
    }
    operation->has_physical = true;
    operation->physical = *physical;
}

locker_status_t domain_operation_persist(locker_core_t *core, domain_change_t *change,
                                         const domain_operation_t *operation)
{
    if (core == NULL || change == NULL || operation == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    char key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&operation->id, key, sizeof(key))) {
        return LOCKER_STATUS_INVALID;
    }
    if (!domain_change_put_begin(change, "operations", key)) {
        return LOCKER_STATUS_NO_SPACE;
    }
    domain_write_operation_record(&change->writer, operation, &core->instance_id, domain_now(core));
    domain_change_put_end(change);
    if (change->broken || !locker_domain_json_writer_ok(&change->writer)) {
        return LOCKER_STATUS_NO_SPACE;
    }
    return LOCKER_STATUS_OK;
}

/* 状态只能沿状态表向前推进：更弱的事实不得把已发生的强事实回退。
   终态与 REVIEW_REQUIRED 都是「已经发生」的事实，任何晚到的弱事实都只能被记录。 */
static bool transition_allowed(uint8_t from, uint8_t to)
{
    if (from == to) {
        return false;
    }
    if (domain_terminal_state(from)) {
        return false;
    }
    uint8_t from_rank = 0u;
    uint8_t to_rank = 0u;
    if (!domain_state_rank(from, &from_rank) || !domain_state_rank(to, &to_rank)) {
        return false;
    }
    return to_rank > from_rank;
}

locker_status_t domain_operation_set_state(locker_core_t *core, domain_operation_t *operation,
                                           uint8_t state, const char *reason_code)
{
    if (core == NULL || operation == NULL || !operation->used) {
        return LOCKER_STATUS_INVALID;
    }
    if (domain_state_name(state) == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!transition_allowed(operation->state, state)) {
        return LOCKER_STATUS_CONFLICT;
    }
    operation->state = state;
    if (reason_code != NULL) {
        domain_text_copy(operation->reason_code, sizeof(operation->reason_code), reason_code);
    }
    switch (state) {
    case LOCKER_OPERATION_STATE_WAIT_CONFIRM:
        operation->wait_confirm_deadline_ms = domain_now(core) + LOCKER_WAIT_CONFIRM_MS;
        break;
    case LOCKER_OPERATION_STATE_REVIEW_REQUIRED:
        operation->wait_confirm_deadline_ms = 0u;
        break;
    default:
        break;
    }
    domain_operation_touch(core, operation);
    return LOCKER_STATUS_OK;
}
