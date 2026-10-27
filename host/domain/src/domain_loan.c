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

/* 终结之后写账本：借出新建 Loan，归还关闭原记录。 */
locker_status_t domain_loan_apply(locker_core_t *core, domain_operation_t *operation)
{
    if (core == NULL || operation == NULL || !operation->used) {
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
    char key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&operation->related_loan_id, key, sizeof(key))) {
        return LOCKER_STATUS_INVALID;
    }
    domain_change_t change;
    domain_change_init(core, &change, core->revision, core->revision + 1u);
    locker_status_t status = LOCKER_STATUS_OK;
    if (operation->kind == LOCKER_OPERATION_KIND_BORROW) {
        if (!domain_change_check(&change, "loans", key, "ABSENT")) {
            return LOCKER_STATUS_NO_SPACE;
        }
        if (!domain_change_put_begin(&change, "loans", key)) {
            return LOCKER_STATUS_NO_SPACE;
        }
        domain_write_loan(&change.writer, &operation->related_loan_id, &operation->tool_id, &operation->actor_id,
                          &operation->id, NULL, false, opened_at, NULL, false);
        domain_change_put_end(&change);
        status = domain_change_commit(core, &change);
        if (status != LOCKER_STATUS_OK) {
            return status;
        }
        core->loan_open = true;
        core->loan_id = operation->related_loan_id;
        core->loan_tool_id = operation->tool_id;
        core->loan_borrower_id = operation->actor_id;
        core->loan_borrow_operation_id = operation->id;
        memcpy(core->loan_opened_at, opened_at, sizeof(core->loan_opened_at));
        domain_publish(core, &operation->related_loan_id, "loan", core->revision);
        return LOCKER_STATUS_OK;
    }
    if (!core->loan_open || !locker_uuid_equal(&core->loan_id, &operation->related_loan_id)) {
        return LOCKER_STATUS_CONFLICT;
    }
    if (!domain_change_check(&change, "loans", key, "EXISTS")) {
        return LOCKER_STATUS_NO_SPACE;
    }
    if (!domain_change_put_begin(&change, "loans", key)) {
        return LOCKER_STATUS_NO_SPACE;
    }
    /* 借用者与 tool 必须保持原值，只补 return_operation_id 与 closed_at。 */
    domain_write_loan(&change.writer, &core->loan_id, &core->loan_tool_id, &core->loan_borrower_id,
                      &core->loan_borrow_operation_id, &operation->id, true, core->loan_opened_at, opened_at, true);
    domain_change_put_end(&change);
    status = domain_change_commit(core, &change);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    domain_publish(core, &core->loan_id, "loan", core->revision);
    core->loan_open = false;
    locker_uuid_clear(&core->loan_id);
    locker_uuid_clear(&core->loan_tool_id);
    locker_uuid_clear(&core->loan_borrower_id);
    locker_uuid_clear(&core->loan_borrow_operation_id);
    core->loan_opened_at[0] = '\0';
    return LOCKER_STATUS_OK;
}
