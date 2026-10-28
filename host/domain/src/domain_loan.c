#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 业务核心：有效借用投影（loans）。
 *
 * 借还账本只有 Core 线程写（module-ownership.md:49）。两条约束来自真源：
 *   - loans 以 loan.id 为 key；归还关闭原记录，禁止覆盖借用者（x-rules 第 7 条）。
 *   - 操作终结与账本变更必须都落库，任何一步失败都不能宣称借还成功。
 *
 * 本文件只处理 loans 集合与 core->loan_* 内存副本；operations 的
 * related_loan_id 由 domain_loan_prepare 在终结事务之前写入。
 *
 * **终结与账本必须同一个事务**：先 `domain_loan_check` 写条件、再
 * `domain_loan_apply` 写变更，两者都挂在调用方的 change 上，由调用方连同
 * 操作状态一起提交。分两次提交会在中途失败时留下「操作 COMPLETED 但 loans
 * 没动」的半截状态——借出侧账上说已借出却没有有效借用、归还侧账上已完成但
 * 借用仍 open，正是 x-rules 第 7 条与
 * `docs/architecture/reviews/interface-expert.md:29` 要求排除的情形。
 *
 * RepositoryChange 的 checks 段必须在 mutations 段之前一次性写完
 * （见 domain_change.c 的 domain_change_check 约束），所以这里严格分成
 * check / apply 两个阶段，不能合成一次调用。
 */

#include "domain_internal.h"

#include <string.h>

/* 借出时先分配 loan_id 并挂到 operation 上，让 OperationRecord.related_loan_id
   与 loans 记录在同一个语义步里出现。 */
locker_status_t domain_loan_prepare(locker_core_t *core, domain_operation_t *operation)
{
    if (core == NULL || operation == NULL || !operation->used) {
        return LOCKER_STATUS_INVALID;
    }
    if (operation->kind == LOCKER_OPERATION_KIND_BORROW) {
        if (core->loan_open) {
            /* 单柜格：已有有效借用时不允许再借。 */
            return LOCKER_STATUS_CONFLICT;
        }
        locker_uuid_t loan_id;
        if (locker_clock_port_new_uuid(&core->clock, &loan_id) != LOCKER_STATUS_OK) {
            return LOCKER_STATUS_IO_ERROR;
        }
        operation->has_loan = true;
        operation->related_loan_id = loan_id;
        return LOCKER_STATUS_OK;
    }
    if (operation->kind == LOCKER_OPERATION_KIND_RETURN) {
        if (!core->loan_open) {
            return LOCKER_STATUS_CONFLICT;
        }
        /* 归还只能关闭自己的那条借用记录。 */
        if (!locker_uuid_equal(&core->loan_borrower_id, &operation->actor_id)) {
            return LOCKER_STATUS_INVALID;
        }
        operation->has_loan = true;
        operation->related_loan_id = core->loan_id;
        return LOCKER_STATUS_OK;
    }
    return LOCKER_STATUS_INVALID;
}

/* 第一阶段：把账本条件写进调用方的 change（必须在任何 mutation 之前调用）。 */
locker_status_t domain_loan_check(locker_core_t *core, const domain_operation_t *operation,
                                  domain_change_t *change)
{
    if (core == NULL || operation == NULL || change == NULL || !operation->used) {
        return LOCKER_STATUS_INVALID;
    }
    if (!operation->has_loan) {
        return LOCKER_STATUS_INVALID;
    }
    if (operation->kind != LOCKER_OPERATION_KIND_BORROW && operation->kind != LOCKER_OPERATION_KIND_RETURN) {
        return LOCKER_STATUS_INVALID;
    }
    char key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&operation->related_loan_id, key, sizeof(key))) {
        return LOCKER_STATUS_INVALID;
    }
    /* 借出要求该 loan_id 尚不存在；归还要求原记录仍在（且借用者已在
       domain_loan_prepare 校验过是本人）。 */
    const char *condition = operation->kind == LOCKER_OPERATION_KIND_BORROW ? "ABSENT" : "EXISTS";
    if (!domain_change_check(change, "loans", key, condition)) {
        return LOCKER_STATUS_NO_SPACE;
    }
    return LOCKER_STATUS_OK;
}

/* 第二阶段：把账本变更追加到调用方的 change（与操作终结同一事务）。
   out_stamp 回传写进文档的 opened_at/closed_at 文本，供提交成功后更新内存副本。 */
locker_status_t domain_loan_apply(locker_core_t *core, const domain_operation_t *operation,
                                  domain_change_t *change, char *out_stamp, size_t stamp_size)
{
    if (core == NULL || operation == NULL || change == NULL || !operation->used) {
        return LOCKER_STATUS_INVALID;
    }
    if (!operation->has_loan) {
        return LOCKER_STATUS_INVALID;
    }
    char opened_at[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    uint64_t wall = 0u;
    if (locker_clock_port_wall_utc_ms(&core->clock, &wall) != LOCKER_STATUS_OK ||
        !locker_domain_time_format_utc(wall, opened_at, sizeof(opened_at))) {
        return LOCKER_STATUS_NOT_READY;
    }
    if (out_stamp != NULL && stamp_size != 0u) {
        size_t length = strlen(opened_at);
        if (length >= stamp_size) {
            length = stamp_size - 1u;
        }
        memcpy(out_stamp, opened_at, length);
        out_stamp[length] = '\0';
    }
    char key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&operation->related_loan_id, key, sizeof(key))) {
        return LOCKER_STATUS_INVALID;
    }
    if (operation->kind == LOCKER_OPERATION_KIND_BORROW) {
        if (!domain_change_put_begin(change, "loans", key)) {
            return LOCKER_STATUS_NO_SPACE;
        }
        domain_write_loan(&change->writer, &operation->related_loan_id, &operation->tool_id, &operation->actor_id,
                          &operation->id, NULL, false, opened_at, NULL, false);
        domain_change_put_end(change);
        return change->broken ? LOCKER_STATUS_NO_SPACE : LOCKER_STATUS_OK;
    }
    if (operation->kind != LOCKER_OPERATION_KIND_RETURN) {
        return LOCKER_STATUS_INVALID;
    }
    if (!core->loan_open || !locker_uuid_equal(&core->loan_id, &operation->related_loan_id)) {
        return LOCKER_STATUS_CONFLICT;
    }
    if (!domain_change_put_begin(change, "loans", key)) {
        return LOCKER_STATUS_NO_SPACE;
    }
    /* 借用者与 tool 必须保持原值，只补 return_operation_id 与 closed_at。 */
    domain_write_loan(&change->writer, &core->loan_id, &core->loan_tool_id, &core->loan_borrower_id,
                      &core->loan_borrow_operation_id, &operation->id, true, core->loan_opened_at, opened_at, true);
    domain_change_put_end(change);
    return change->broken ? LOCKER_STATUS_NO_SPACE : LOCKER_STATUS_OK;
}

/* 账本事务提交成功之后才更新内存副本：提交失败的 change 不会走到这里。 */
void domain_loan_commit_in_memory(locker_core_t *core, const domain_operation_t *operation,
                                  const char *timestamp_text)
{
    if (core == NULL || operation == NULL || !operation->used || !operation->has_loan) {
        return;
    }
    if (operation->kind == LOCKER_OPERATION_KIND_BORROW) {
        core->loan_open = true;
        core->loan_id = operation->related_loan_id;
        core->loan_tool_id = operation->tool_id;
        core->loan_borrower_id = operation->actor_id;
        core->loan_borrow_operation_id = operation->id;
        if (timestamp_text != NULL) {
            size_t length = strlen(timestamp_text);
            if (length >= sizeof(core->loan_opened_at)) {
                length = sizeof(core->loan_opened_at) - 1u;
            }
            memcpy(core->loan_opened_at, timestamp_text, length);
            core->loan_opened_at[length] = '\0';
        } else {
            core->loan_opened_at[0] = '\0';
        }
        return;
    }
    if (operation->kind != LOCKER_OPERATION_KIND_RETURN) {
        return;
    }
    if (!locker_uuid_equal(&core->loan_id, &operation->related_loan_id)) {
        return;
    }
    core->loan_open = false;
    locker_uuid_clear(&core->loan_id);
    locker_uuid_clear(&core->loan_tool_id);
    locker_uuid_clear(&core->loan_borrower_id);
    locker_uuid_clear(&core->loan_borrow_operation_id);
    core->loan_opened_at[0] = '\0';
}
