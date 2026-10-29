#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 业务核心：生命周期与单所有者循环。
 *
 * 线程模型（docs/architecture/module-ownership.md:49-52、internal-interfaces.md:88-100）：
 *   - locker_core_start 只创建**一个** Core 线程；借还状态、仓储写连接与设备泵
 *     全部归它所有，其它线程一律不碰 core->active / core->revision 这类字段。
 *   - submit/query/apply_device_event/apply_device_reply/tick 只把**按值复制**的
 *     作业放进有界队列；队列满返回 NO_SPACE（调用方映射 503 QUEUE_FULL），绝不
 *     静默丢弃，也绝不把请求栈上的指针带进 Core。
 *   - 设备 sink 回调跑在设备 reader 线程，只往事件/响应环 try_push；环满时置
 *     原子溢出标志并返回 OK（返回 NO_SPACE 会让设备侧 shutdown 会话），由 Core
 *     线程消费成缺口状态并用设备日志补取。
 *   - stop 先停止准入，再投一个 STOP 作业让线程把队列排空后退出；超时不等于
 *     取消，未排空的作业保留其终态，绝不中途丢弃已开始的写事务。
 */

#include "domain_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "locker/common/log.h"

/* Core 线程的主循环节拍：只在队列空闲时用，绝不代替 tick 推进业务超时。 */
#define DOMAIN_IDLE_WAIT_MS 20u
/* 恢复期按集合分页读取的页大小（schema 允许 1..100）。 */
#define DOMAIN_RECOVERY_PAGE 50u

/* ---------- 队列 ---------- */

static void clear_job(domain_job_t *job)
{
    volatile unsigned char *bytes=(volatile unsigned char *)job;
    for (size_t i=0;i<sizeof(*job);++i) bytes[i]=0;
}

static locker_status_t job_enqueue(locker_core_t *core, const domain_job_t *job)
{
    if (core == NULL || job == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    locker_status_t status = locker_bounded_queue_try_push(&core->queue, job);
    if (status != LOCKER_STATUS_OK) {
        /* 有界队列显式拒绝：调用方拿到 NO_SPACE，映射为 503 QUEUE_FULL。 */
        return LOCKER_STATUS_NO_SPACE;
    }
    pthread_mutex_lock(&core->mutex);
    pthread_cond_signal(&core->cond);
    pthread_mutex_unlock(&core->mutex);
    return LOCKER_STATUS_OK;
}

/* 把队列里已有的作业全部处理完（STOP 之前的屏障）。 */
static void drain_queue(locker_core_t *core)
{
    domain_job_t job;
    while (locker_bounded_queue_try_pop(&core->queue, &job) == LOCKER_STATUS_OK) {
        domain_handle_job(core, &job);
        clear_job(&job);
        memset(core->response_buffer,0,sizeof(core->response_buffer));
    }
}

static bool stop_requested(const locker_core_t *core)
{
    return atomic_load_explicit(&core->stop_requested, memory_order_acquire);
}

static void set_stop_requested(locker_core_t *core)
{
    atomic_store_explicit(&core->stop_requested, true, memory_order_release);
}

/* ---------- 时钟 ---------- */

static uint64_t core_clock_now(locker_core_t *core)
{
    uint64_t now = 0u;
    if (locker_clock_port_monotonic_ms(&core->clock, &now) != LOCKER_STATUS_OK) {
        /* 时钟不可用时退回 Core 自己记录的 now_ms：只增不减，绝不回到 0。 */
        return core->now_ms;
    }
    return now;
}

/* ---------- 恢复屏障 ---------- */

/*
 * 开放业务前的恢复（internal-data.schema.json 的 x-rules 第 10 条、
 * internal-interfaces.md:100）。只做读取与内存重建：
 *   - 未完成的 operations 载入工作副本；
 *   - loans 重建有效借用投影；
 *   - device_state 载入上次快照与 active command；
 *   - import_cursors 载入仓储已提交的连续水位。
 * 恢复出来的 outbox **不**重新入发送队列（:114），可能已发送的意图进核验。
 */
static locker_status_t recover_collection_page(locker_core_t *core, const char *collection,
                                               const char *after_key, domain_doc_t *doc, bool *has_more,
                                               char *next_key, size_t next_key_size);

/*
 * 把一个「墙钟 UTC 起点 + 时长」的期限换算到单调时间域。
 * 完整说明见 domain_internal.h 的声明处（domain_operation_load 也要用，
 * 两条入口共用同一套规则，不能各写一份）。
 */

static locker_status_t recover_operations(locker_core_t *core)
{
    char after[LOCKER_DOMAIN_KEY_TEXT];
    after[0] = '\0';
    bool has_more = false;
    for (uint32_t page = 0u; page < 64u; ++page) {
        domain_doc_t doc;
        char next[LOCKER_DOMAIN_KEY_TEXT];
        next[0] = '\0';
        locker_status_t status = recover_collection_page(core, "operations", after[0] == '\0' ? NULL : after,
                                                         &doc, &has_more, next, sizeof(next));
        if (status != LOCKER_STATUS_OK) {
            return status;
        }
        const locker_json_node_t *documents =
            domain_json_member(&doc.doc, locker_domain_json_root(&doc.doc), "documents");
        if (documents == NULL || documents->type != LOCKER_JSON_ARRAY) {
            return LOCKER_STATUS_IO_ERROR;
        }
        for (uint32_t i = 0u; i < documents->count; ++i) {
            const locker_json_node_t *document = locker_domain_json_at(&doc.doc, documents, i);
            if (document == NULL) {
                return LOCKER_STATUS_IO_ERROR;
            }
            const locker_json_node_t *value = domain_json_member(&doc.doc, document, "value");
            if (value == NULL) {
                return LOCKER_STATUS_IO_ERROR;
            }
            /* 只关心未完成的操作：终态历史不重建工作副本。 */
            const locker_json_node_t *operation = domain_json_member(&doc.doc, value, "operation");
            uint8_t state = LOCKER_OPERATION_STATE_REQUESTED;
            if (!domain_json_read_enum_state(&doc.doc, domain_json_member(&doc.doc, operation, "state"), &state)) {
                return LOCKER_STATUS_IO_ERROR;
            }
            if (domain_terminal_state(state)) {
                continue;
            }
            if (core->active.used) {
                /* 首版只允许一个活动操作（slot_guards 只有一个 key "1"）。 */
                continue;
            }
            domain_operation_t loaded;
            if (!domain_operation_from_record(&doc.doc, value, &loaded)) {
                return LOCKER_STATUS_IO_ERROR;
            }
            core->active = loaded;
            /* 恢复出的截止点必须换算到**单调时间域**再比较。
               OperationRecord 的 created_at/updated_at 是墙钟 UTC 文本，
               parse 出来的 updated_ms 是 Unix epoch 毫秒（量级 1.7e12），
               而 core->now_ms 是进程单调毫秒（量级 1e6）。直接把两者相加
               会得到 1.7e12 量级的 deadline，永远大于 now —— 重启前挂着的
               申请/待确认操作就再也不会超时，占位一直不释放（审计 R1-B-5：
               探针实测 mono=1000、wait_confirm_deadline_ms=1761004920000）。
               正确做法：先按同一时刻的墙钟算出「还剩多久」，再把剩余时长
               折算到当前单调时间。墙钟不可用或已经过期时，剩余时长按 0 处理
               （保守：立即到期，交给超时路径重新核验），不能当作「刚创建」。 */
            uint64_t now_wall = 0u;
            const bool wall_ok = locker_clock_port_wall_utc_ms(&core->clock, &now_wall) == LOCKER_STATUS_OK;
            const uint64_t now_mono = domain_now(core);
            if (loaded.state == LOCKER_OPERATION_STATE_REQUESTED) {
                core->active.requested_deadline_ms =
                    domain_deadline_in_monotonic(loaded.updated_ms, LOCKER_REQUEST_TTL_MS, wall_ok, now_wall, now_mono);
            } else if (loaded.state == LOCKER_OPERATION_STATE_WAIT_CONFIRM) {
                core->active.wait_confirm_deadline_ms =
                    domain_deadline_in_monotonic(loaded.updated_ms, LOCKER_WAIT_CONFIRM_MS, wall_ok, now_wall, now_mono);
            }
        }
        if (!has_more) {
            return LOCKER_STATUS_OK;
        }
        memcpy(after, next, sizeof(after));
    }
    return LOCKER_STATUS_OK;
}

/*
 * 把一个「墙钟 UTC 起点 + 时长」的期限换算到单调时间域。
 * 完整说明见 domain_internal.h 的声明处（domain_operation_load 也用它）。
 */
uint64_t domain_deadline_in_monotonic(uint64_t wall_start_ms, uint64_t duration_ms, bool wall_ok,
                                      uint64_t now_wall_ms, uint64_t now_mono_ms)
{
    if (!wall_ok || wall_start_ms == 0u) {
        return now_mono_ms;
    }
    if (now_wall_ms >= wall_start_ms) {
        uint64_t elapsed = now_wall_ms - wall_start_ms;
        if (elapsed >= duration_ms) {
            return now_mono_ms; /* 已过期：立即到期 */
        }
        return now_mono_ms + (duration_ms - elapsed);
    }
    return now_mono_ms + duration_ms;
}

static locker_status_t recover_loans(locker_core_t *core)
{
    char after[LOCKER_DOMAIN_KEY_TEXT];
    after[0] = '\0';
    bool has_more = false;
    for (uint32_t page = 0u; page < 64u; ++page) {
        domain_doc_t doc;
        char next[LOCKER_DOMAIN_KEY_TEXT];
        next[0] = '\0';
        locker_status_t status = recover_collection_page(core, "loans", after[0] == '\0' ? NULL : after, &doc,
                                                         &has_more, next, sizeof(next));
        if (status != LOCKER_STATUS_OK) {
            return status;
        }
        const locker_json_node_t *documents =
            domain_json_member(&doc.doc, locker_domain_json_root(&doc.doc), "documents");
        if (documents == NULL || documents->type != LOCKER_JSON_ARRAY) {
            return LOCKER_STATUS_IO_ERROR;
        }
        for (uint32_t i = 0u; i < documents->count; ++i) {
            const locker_json_node_t *document = locker_domain_json_at(&doc.doc, documents, i);
            if (document == NULL) {
                return LOCKER_STATUS_IO_ERROR;
            }
            const locker_json_node_t *value = domain_json_member(&doc.doc, document, "value");
            if (value == NULL) {
                return LOCKER_STATUS_IO_ERROR;
            }
            bool open = false;
            const locker_json_node_t *closed = domain_json_member(&doc.doc, value, "closed_at");
            open = (closed == NULL || closed->type == LOCKER_JSON_NULL);
            if (!open) {
                continue;
            }
            locker_uuid_t id;
            locker_uuid_t tool_id;
            locker_uuid_t borrower;
            if (!domain_json_read_uuid(&doc.doc, domain_json_member(&doc.doc, value, "id"), &id) ||
                !domain_json_read_uuid(&doc.doc, domain_json_member(&doc.doc, value, "tool_id"), &tool_id) ||
                !domain_json_read_uuid(&doc.doc, domain_json_member(&doc.doc, value, "borrower_id"), &borrower)) {
                return LOCKER_STATUS_IO_ERROR;
            }
            char opened[LOCKER_DOMAIN_TIMESTAMP_TEXT];
            if (!domain_json_read_text(&doc.doc, domain_json_member(&doc.doc, value, "opened_at"), opened,
                                       sizeof(opened))) {
                return LOCKER_STATUS_IO_ERROR;
            }
            core->loan_open = true;
            core->loan_id = id;
            core->loan_tool_id = tool_id;
            core->loan_borrower_id = borrower;
            memcpy(core->loan_opened_at, opened, sizeof(core->loan_opened_at));
        }
        if (!has_more) {
            return LOCKER_STATUS_OK;
        }
        memcpy(after, next, sizeof(after));
    }
    return LOCKER_STATUS_OK;
}

static locker_status_t recover_import_cursor(locker_core_t *core)
{
    char after[LOCKER_DOMAIN_KEY_TEXT];
    after[0] = '\0';
    bool has_more = false;
    for (uint32_t page = 0u; page < 16u; ++page) {
        domain_doc_t doc;
        char next[LOCKER_DOMAIN_KEY_TEXT];
        next[0] = '\0';
        locker_status_t status = recover_collection_page(core, "import_cursors", after[0] == '\0' ? NULL : after,
                                                         &doc, &has_more, next, sizeof(next));
        if (status != LOCKER_STATUS_OK) {
            return status;
        }
        const locker_json_node_t *documents =
            domain_json_member(&doc.doc, locker_domain_json_root(&doc.doc), "documents");
        if (documents == NULL || documents->type != LOCKER_JSON_ARRAY) {
            return LOCKER_STATUS_IO_ERROR;
        }
        for (uint32_t i = 0u; i < documents->count; ++i) {
            const locker_json_node_t *document = locker_domain_json_at(&doc.doc, documents, i);
            if (document == NULL) {
                return LOCKER_STATUS_IO_ERROR;
            }
            const locker_json_node_t *value = domain_json_member(&doc.doc, document, "value");
            if (value == NULL) {
                return LOCKER_STATUS_IO_ERROR;
            }
            if (!domain_import_cursor_from_record(&doc.doc, value, core)) {
                return LOCKER_STATUS_IO_ERROR;
            }
        }
        if (!has_more) {
            return LOCKER_STATUS_OK;
        }
        memcpy(after, next, sizeof(after));
    }
    return LOCKER_STATUS_OK;
}

static locker_status_t recover_device_state(locker_core_t *core)
{
    domain_doc_t doc;
    locker_status_t status = domain_repo_page(core, "device_state", NULL, 1u, &doc);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    const locker_json_node_t *documents =
        domain_json_member(&doc.doc, locker_domain_json_root(&doc.doc), "documents");
    if (documents == NULL || documents->type != LOCKER_JSON_ARRAY) {
        return LOCKER_STATUS_IO_ERROR;
    }
    if (documents->count == 0u) {
        return LOCKER_STATUS_OK;
    }
    const locker_json_node_t *document = locker_domain_json_at(&doc.doc, documents, 0u);
    if (document == NULL) {
        return LOCKER_STATUS_IO_ERROR;
    }
    const locker_json_node_t *value = domain_json_member(&doc.doc, document, "value");
    if (value == NULL) {
        return LOCKER_STATUS_IO_ERROR;
    }
    const locker_json_node_t *device = domain_json_member(&doc.doc, value, "device");
    if (device == NULL) {
        return LOCKER_STATUS_IO_ERROR;
    }
    char device_id[25];
    if (domain_json_read_text(&doc.doc, domain_json_member(&doc.doc, device, "device_id"), device_id,
                              sizeof(device_id))) {
        memcpy(core->persisted_device_id, device_id, sizeof(core->persisted_device_id));
    }
    uint8_t mode = LOCKER_DEVICE_MODE_OFFLINE;
    if (domain_json_read_enum_device_mode(&doc.doc, domain_json_member(&doc.doc, device, "mode"), &mode)) {
        core->persisted_mode = mode;
    }
    core->persisted_state_valid = true;
    return LOCKER_STATUS_OK;
}

/*
 * 恢复本柜唯一的工具投影（tools 集合 → core->tool_*）。
 *
 * 以前这一步完全不存在：`core->tool_valid` 全仓只有读取、没有任何赋值，
 * 于是生产路径上工具查询永远是空、`TOOL_GET` 永远 NOT_READY，而 create 又
 * 因为 `if (core->tool_valid && …)` 直接跳过工具校验——两个后果都很严重：
 * 客户端无法从查询进入申请，而直接提交 UUID 的请求却能带着不存在的工具或
 * 陈旧版本进入账本（审计 R1-B-9）。单测里之所以没暴露，是因为夹具手动把
 * 这些字段填好了。
 *
 * 一个柜格只允许一件启用的工具（x-rules：单柜单工具）；多于一件、或者
 * enabled=false，都属于配置错误，此时保持 tool_valid=false，让申请被明确
 * 拒绝，而不是随便挑一件。工具名沿用仓储里的值，缺失时回落成空串。
 */
static locker_status_t recover_tools(locker_core_t *core)
{
    domain_doc_t doc;
    locker_status_t status = domain_repo_page(core, "tools", NULL, 2u, &doc);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    const locker_json_node_t *documents =
        domain_json_member(&doc.doc, locker_domain_json_root(&doc.doc), "documents");
    if (documents == NULL || documents->type != LOCKER_JSON_ARRAY) {
        return LOCKER_STATUS_IO_ERROR;
    }
    core->tool_valid = false;
    for (uint32_t i = 0u; i < documents->count; ++i) {
        const locker_json_node_t *document = locker_domain_json_at(&doc.doc, documents, i);
        if (document == NULL) {
            return LOCKER_STATUS_IO_ERROR;
        }
        const locker_json_node_t *value = domain_json_member(&doc.doc, document, "value");
        if (value == NULL) {
            return LOCKER_STATUS_IO_ERROR;
        }
        locker_uuid_t id;
        if (!domain_json_read_uuid(&doc.doc, domain_json_member(&doc.doc, value, "id"), &id)) {
            return LOCKER_STATUS_IO_ERROR;
        }
        bool enabled = true;
        (void)domain_json_read_bool(&doc.doc, domain_json_member(&doc.doc, value, "enabled"), &enabled);
        if (!enabled) {
            continue;
        }
        if (core->tool_valid) {
            /* 多于一件启用工具：配置与单柜格模型冲突，宁可拒绝申请。 */
            core->tool_valid = false;
            return LOCKER_STATUS_OK;
        }
        uint64_t revision = 0u;
        char name[65];
        name[0] = '\0';
        (void)domain_json_read_text(&doc.doc, domain_json_member(&doc.doc, value, "name"), name, sizeof(name));
        (void)domain_json_read_u64(&doc.doc, domain_json_member(&doc.doc, value, "tool_revision"), &revision);
        core->tool_id = id;
        core->tool_revision = revision;
        domain_text_copy(core->tool_name, sizeof(core->tool_name), name);
        core->tool_availability = LOCKER_TOOL_AVAILABILITY_AVAILABLE;
        core->tool_valid = true;
    }
    return LOCKER_STATUS_OK;
}

locker_status_t domain_recover(locker_core_t *core)
{
    if (core == NULL || core->repo == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    core->phase = DOMAIN_PHASE_RECOVERING;
    locker_status_t status = domain_repo_refresh_revision(core);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = recover_operations(core);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = recover_loans(core);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    /* 工具投影必须在恢复阶段完成：create 要靠它校验 tool_id/tool_revision，
       TOOL_GET 要靠它出内容（审计 R1-B-9）。 */
    status = recover_tools(core);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = recover_import_cursor(core);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = recover_device_state(core);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    /* 动作号必须抬到已见水位之上：sim 要求 action_no == watermark + 1，
       重启后复用旧号会被 REPLAY_BLOCKED。 */
    core->next_action_no = core->action_watermark + 1u;
    if (core->next_action_no == 0u) {
        core->next_action_no = 1u;
    }
    /* 恢复出来的可能已发送意图进核验，不恢复发送队列（internal-interfaces.md:114）。
       WAIT_CONFIRM 同样要进核验：stage-00-architecture.md:119 把「服务/MCU 重启」
       与「可能已执行」并列，一律 REVIEW_REQUIRED 并保持占位。理由不是形式主义——
       冻结的物理依据（wait_confirm_basis_valid/revision/item/door）只活在内存里，
       重启后没有证据能证明「门仍然关着、物品仍是那一次变动后的样子」，此时允许
       本人确认就等于凭一份无法追溯的记忆改账（审计 R3-B-4：探针实测重启后
       basis_valid=0 仍能 confirm 成功并开出借用）。 */
    if (core->active.used && (core->active.state == LOCKER_OPERATION_STATE_ACCESS_PENDING ||
                              core->active.state == LOCKER_OPERATION_STATE_ACCESS_ACTIVE ||
                              core->active.state == LOCKER_OPERATION_STATE_WAIT_CONFIRM)) {
        /* 走 domain_require_review 而不是「先 set_state 再 persist 不管结果」：
           后者提交失败时内存已是 REVIEW_REQUIRED、库里还是旧状态，而后续任何
           重试都会被「已经是核验」短路成 OK，核验原因永远补不进仓储
           （审计 R3-B-7 的同一类缺陷）。 */
        status = domain_require_review(core, &core->active, "RECOVERED_AFTER_RESTART");
        if (status != LOCKER_STATUS_OK) {
            /* 恢复核验没落库就等于「恢复没完成」：此时既不能设 recovery_ready
               （线程会据此进 READY 并接受写命令），也不能假装恢复成功。
               直接把错误交回调用方，phase 停在 RECOVERING（审计 R3-B-7：
               以前这里 (void) 吞掉错误、照样 recovery_ready=true 返回 OK，
               库和内存可能都还停在 ACCESS_ACTIVE 或 WAIT_CONFIRM）。 */
            return status;
        }
    }
    core->recovery_ready = true;
    core->recovery_done = true;
    return LOCKER_STATUS_OK;
}

static locker_status_t recover_collection_page(locker_core_t *core, const char *collection,
                                               const char *after_key, domain_doc_t *doc, bool *has_more,
                                               char *next_key, size_t next_key_size)
{
    locker_status_t status = domain_repo_recovery_page(core, collection, after_key, DOMAIN_RECOVERY_PAGE, doc);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    const locker_json_node_t *next = domain_json_member(&doc->doc, locker_domain_json_root(&doc->doc), "next_after_key");
    *has_more = next != NULL && next->type == LOCKER_JSON_STRING;
    if (*has_more) {
        size_t length = 0u;
        if (!locker_domain_json_text(&doc->doc, next, next_key, next_key_size, &length)) {
            return LOCKER_STATUS_IO_ERROR;
        }
    }
    return LOCKER_STATUS_OK;
}

/* ---------- 主循环 ---------- */

static void core_tick_internal(locker_core_t *core, uint64_t monotonic_ms)
{
    /* 只有一条推进顺序：线程循环和显式 tick 必须走同一段代码。
       以前这里是 domain_handle_tick 的复制品，两边各漏一样东西——线程侧
       没有「证明新鲜时重试 arm」，显式 tick 侧没有释放泵，于是同一份状态
       在两条入口上会走出不同结果（审计 R3-B-2 的顺序问题就出在这里）。
       顺序本身写在 domain_handle_tick 里：先释放上一轮动作的上下文，
       再考虑发送新动作。 */
    domain_handle_tick(core, monotonic_ms);
}

static void core_run_once(locker_core_t *core)
{
    domain_job_t job;
    bool drained = false;
    while (locker_bounded_queue_try_pop(&core->queue, &job) == LOCKER_STATUS_OK) {
        drained = true;
        if (job.kind == DOMAIN_JOB_STOP) {
            if (job.completion != UINT32_MAX) {
                domain_completion_finish(core, job.completion, &job.completion_token, LOCKER_STATUS_OK, NULL, 0u,
                                         core->revision);
            }
            return;
        }
        domain_handle_job(core, &job);
        clear_job(&job);
        memset(core->response_buffer,0,sizeof(core->response_buffer));
    }
    /* 设备环：事件与响应都按值复制进来，只在这里出队应用。 */
    domain_event_slot_t event;
    while (locker_bounded_queue_try_pop(&core->event_ring, &event) == LOCKER_STATUS_OK) {
        /* 读卡帧的 MCU 时间戳取自设备刚刚发出的时刻。先刷新主机单调时钟，
           否则沿用上次 tick 的时间（最多相差一个空闲周期），新鲜帧会被
           domain_anchor_mcu_now 误判为来自「未来」，柜前证明偶发丢失。 */
        domain_now_advance(core, core_clock_now(core));
        domain_handle_device_event(core, &event.event, event.completion);
        drained = true;
    }
    domain_reply_slot_t reply;
    while (locker_bounded_queue_try_pop(&core->reply_ring, &reply) == LOCKER_STATUS_OK) {
        domain_handle_device_reply(core, &reply.reply, reply.completion);
        drained = true;
    }
    (void)drained;
    /* 真实循环进展：只有完成一次出队+泵才推进，不能靠定时中断自增。 */
    core_tick_internal(core, core_clock_now(core));
}

static void *core_thread_main(void *argument)
{
    locker_core_t *core = (locker_core_t *)argument;
    core->now_ms = core_clock_now(core);
    locker_status_t status = domain_recover(core);
    core->recovery_status = status;
    if (status == LOCKER_STATUS_OK) {
        core->phase = DOMAIN_PHASE_READY;
    } else {
        /* 恢复失败不清库、不猜状态：保持 RECONCILING，全部写命令回 NOT_READY。 */
        core->phase = DOMAIN_PHASE_RECOVERING;
    }
    domain_progress_publish(core);

    for (;;) {
        core_run_once(core);
        if (stop_requested(core)) {
            break;
        }
        pthread_mutex_lock(&core->mutex);
        if (locker_bounded_queue_size(&core->queue) == 0u && !stop_requested(core)) {
            struct timespec wait;
            clock_gettime(CLOCK_REALTIME, &wait);
            wait.tv_nsec += (long)DOMAIN_IDLE_WAIT_MS * 1000000L;
            if (wait.tv_nsec >= 1000000000L) {
                wait.tv_sec += 1;
                wait.tv_nsec -= 1000000000L;
            }
            (void)pthread_cond_timedwait(&core->cond, &core->mutex, &wait);
        }
        pthread_mutex_unlock(&core->mutex);
    }

    drain_queue(core);
    domain_device_drain(core);
    domain_phase_t phase = DOMAIN_PHASE_STOPPED;
    core->phase = phase;
    atomic_store_explicit(&core->thread_exited, true, memory_order_release);
    return NULL;
}

/* ---------- 生命周期 ---------- */

static void core_free(locker_core_t *core)
{
    if (core == NULL) {
        return;
    }
    if (core->queue_storage != NULL) {
        locker_bounded_queue_destroy(&core->queue);
        free(core->queue_storage);
    }
    if (core->event_storage != NULL) {
        locker_bounded_queue_destroy(&core->event_ring);
        free(core->event_storage);
    }
    if (core->reply_storage != NULL) {
        locker_bounded_queue_destroy(&core->reply_ring);
        free(core->reply_storage);
    }
    for (uint32_t i = 0u; i < LOCKER_DOMAIN_COMPLETION_SLOTS; ++i) {
        domain_completion_release(core,i);
    }
    pthread_cond_destroy(&core->completion_cond);
    pthread_mutex_destroy(&core->completion_mutex);
    pthread_mutex_destroy(&core->progress_mutex);
    pthread_cond_destroy(&core->cond);
    pthread_mutex_destroy(&core->mutex);
    free(core);
}

locker_status_t locker_core_create(const locker_core_config_t *config, locker_repo_t *repo, locker_device_t *device,
                                   locker_publisher_t *publisher, locker_core_t **out)
{
    if (out == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    *out = NULL;
    if (config == NULL || repo == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!locker_clock_port_is_valid(&config->clock)) {
        return LOCKER_STATUS_INVALID;
    }
    uint32_t capacity = config->queue_capacity;
    if (capacity == 0u) {
        capacity = LOCKER_CORE_QUEUE_CAPACITY;
    }
    size_t cabinet_length;
    locker_core_t *core = (locker_core_t *)calloc(1u, sizeof(locker_core_t));
    if (core == NULL) {
        return LOCKER_STATUS_NO_SPACE;
    }
    core->config = *config;
    core->repo = repo;
    core->device = device;
    core->publisher = publisher;
    core->clock = config->clock;
    core->instance_id = config->instance_id;
    core->completion_retention_ms = config->completion_retention_ms != 0u
                                        ? config->completion_retention_ms
                                        : LOCKER_COMPLETION_RETENTION_MS;
    /* 用 strnlen 而不是 strlen：调用方给了没写 NUL 的满长度数组时，
       必须在自己的缓冲边界内停下并判 INVALID，绝不能读出 config 之外。 */
    cabinet_length = strnlen(config->cabinet_name, sizeof(core->cabinet_name));
    if (cabinet_length >= sizeof(core->cabinet_name)) {
        core_free(core);
        return LOCKER_STATUS_INVALID;
    }
    memcpy(core->cabinet_name, config->cabinet_name, cabinet_length + 1u);

    core->queue_storage = (domain_job_t *)calloc(capacity, sizeof(domain_job_t));
    core->event_storage = (domain_event_slot_t *)calloc(capacity, sizeof(domain_event_slot_t));
    core->reply_storage = (domain_reply_slot_t *)calloc(capacity, sizeof(domain_reply_slot_t));
    if (core->queue_storage == NULL || core->event_storage == NULL || core->reply_storage == NULL) {
        core_free(core);
        return LOCKER_STATUS_NO_SPACE;
    }
    if (locker_bounded_queue_init(&core->queue, core->queue_storage, capacity, sizeof(domain_job_t)) !=
            LOCKER_STATUS_OK ||
        locker_bounded_queue_init(&core->event_ring, core->event_storage, capacity, sizeof(domain_event_slot_t)) !=
            LOCKER_STATUS_OK ||
        locker_bounded_queue_init(&core->reply_ring, core->reply_storage, capacity, sizeof(domain_reply_slot_t)) !=
            LOCKER_STATUS_OK) {
        core_free(core);
        return LOCKER_STATUS_IO_ERROR;
    }
    if (pthread_mutex_init(&core->mutex, NULL) != 0 || pthread_cond_init(&core->cond, NULL) != 0 ||
        pthread_mutex_init(&core->completion_mutex, NULL) != 0 ||
        pthread_cond_init(&core->completion_cond, NULL) != 0 ||
        pthread_mutex_init(&core->progress_mutex, NULL) != 0) {
        core_free(core);
        return LOCKER_STATUS_IO_ERROR;
    }
    atomic_init(&core->event_overflow_flag, false);
    atomic_init(&core->reply_overflow_flag, false);
    atomic_init(&core->stop_requested, false);
    atomic_init(&core->thread_exited, false);

    core->phase = DOMAIN_PHASE_INIT;
    core->next_action_no = 1u;
    core->initialized = true;
    *out = core;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_core_start(locker_core_t *core)
{
    if (core == NULL || !core->initialized) {
        return LOCKER_STATUS_INVALID;
    }
    if (core->thread_running) {
        return LOCKER_STATUS_CONFLICT;
    }
    if (core->device != NULL) {
        locker_status_t bind = domain_device_bind(core);
        if (bind != LOCKER_STATUS_OK) {
            return bind;
        }
    }
    atomic_store_explicit(&core->stop_requested, false, memory_order_release);
    atomic_store_explicit(&core->thread_exited, false, memory_order_release);
    if (pthread_create(&core->thread, NULL, core_thread_main, core) != 0) {
        return LOCKER_STATUS_IO_ERROR;
    }
    core->thread_running = true;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_core_stop(locker_core_t *core, uint32_t timeout_ms)
{
    if (core == NULL || !core->initialized) {
        return LOCKER_STATUS_INVALID;
    }
    if (!core->thread_running) {
        return LOCKER_STATUS_OK;
    }
    set_stop_requested(core);
    pthread_mutex_lock(&core->mutex);
    pthread_cond_broadcast(&core->cond);
    pthread_mutex_unlock(&core->mutex);

    /* 停止准入后投一个 STOP 作业，让线程在排空队列后退出。 */
    domain_job_t job;
    memset(&job, 0, sizeof(job));
    job.kind = DOMAIN_JOB_STOP;
    job.completion = UINT32_MAX;
    (void)job_enqueue(core, &job);

    uint64_t deadline = core_clock_now(core) + (timeout_ms != 0u ? timeout_ms : LOCKER_HOST_TIMEOUT_MS);
    bool joined = false;
    while (core_clock_now(core) <= deadline) {
        if (atomic_load_explicit(&core->thread_exited, memory_order_acquire)) {
            joined = true;
            break;
        }
        struct timespec pause;
        pause.tv_sec = 0;
        pause.tv_nsec = 5L * 1000000L;
        (void)nanosleep(&pause, NULL);
    }
    if (!joined) {
        /* 超时不等于取消：不强行 terminate 线程，把状态如实报给调用方。 */
        return LOCKER_STATUS_BUSY;
    }
    (void)pthread_join(core->thread, NULL);
    core->thread_running = false;
    core->stop_requested_joined = true;
    return LOCKER_STATUS_OK;
}

void locker_core_destroy(locker_core_t *core)
{
    if (core == NULL) {
        return;
    }
    if (core->thread_running) {
        /* 契约 :285：只有在成功 stop 且适配器回调停止之后才允许销毁。 */
        (void)locker_core_stop(core, LOCKER_HOST_TIMEOUT_MS);
    }
    core->initialized = false;
    core_free(core);
}

/* ---------- 提交与结果 ---------- */

locker_status_t locker_core_submit(locker_core_t *core, const locker_command_t *command, locker_receipt_t *receipt)
{
    if (core == NULL || command == NULL || receipt == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    memset(receipt, 0, sizeof(*receipt));
    if (!core->thread_running) {
        return LOCKER_STATUS_NOT_READY;
    }
    locker_uuid_t token;
    if (locker_clock_port_new_uuid(&core->clock, &token) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_IO_ERROR;
    }
    uint32_t slot = domain_completion_acquire(core, &token);
    if (slot == UINT32_MAX) {
        return LOCKER_STATUS_BUSY;
    }
    domain_job_t job;
    memset(&job, 0, sizeof(job));
    job.kind = DOMAIN_JOB_COMMAND;
    job.completion = slot;
    /* 槽号不是身份：等待方超时后会释放、槽可能被下一个请求复用。把令牌
       随作业一起下发，Core 线程写结果前才能核对「这个槽还是不是我那一个」
       （审计 R3-B-14）。 */
    job.completion_token = token;
    job.body.command = *command;
    locker_status_t status = job_enqueue(core, &job);
    clear_job(&job);
    if (status != LOCKER_STATUS_OK) {
        domain_completion_release_owned(core, slot, &token);
        return status;
    }
    receipt->submission_token = token;
    receipt->request_id = command->request_id;
    receipt->has_operation = false;
    receipt->status = LOCKER_STATUS_QUEUED;
    return LOCKER_STATUS_QUEUED;
}

locker_status_t locker_core_wait_result(locker_core_t *core, const locker_uuid_t *submission_token,
                                        uint32_t timeout_ms, locker_query_result_t *result)
{
    if (core == NULL || submission_token == NULL || result == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    memset(result, 0, sizeof(*result));
    uint32_t slot = domain_completion_find(core, submission_token);
    if (slot == UINT32_MAX) {
        /* 未提交过或已过保留期：调用方必须用原幂等键重查（契约 :201）。 */
        return LOCKER_STATUS_NOT_READY;
    }
    return domain_completion_wait_final(core, slot, submission_token, timeout_ms, result);
}

locker_status_t locker_core_apply_device_event(locker_core_t *core, const locker_device_event_t *event)
{
    if (core == NULL || event == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!core->thread_running) {
        return LOCKER_STATUS_NOT_READY;
    }
    domain_job_t job;
    memset(&job, 0, sizeof(job));
    job.kind = DOMAIN_JOB_DEVICE_EVENT;
    job.completion = UINT32_MAX;
    job.body.event = *event;
    return job_enqueue(core, &job);
}

locker_status_t locker_core_apply_device_reply(locker_core_t *core, const locker_device_reply_t *reply)
{
    if (core == NULL || reply == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!core->thread_running) {
        return LOCKER_STATUS_NOT_READY;
    }
    domain_job_t job;
    memset(&job, 0, sizeof(job));
    job.kind = DOMAIN_JOB_DEVICE_REPLY;
    job.completion = UINT32_MAX;
    job.body.reply = *reply;
    return job_enqueue(core, &job);
}

locker_status_t locker_core_tick(locker_core_t *core, uint64_t monotonic_ms)
{
    if (core == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!core->thread_running) {
        return LOCKER_STATUS_NOT_READY;
    }
    domain_job_t job;
    memset(&job, 0, sizeof(job));
    job.kind = DOMAIN_JOB_TICK;
    job.completion = UINT32_MAX;
    job.body.tick_ms = monotonic_ms;
    return job_enqueue(core, &job);
}

locker_status_t locker_core_query(locker_core_t *core, const locker_query_t *query, locker_query_result_t *result)
{
    if (core == NULL || query == NULL || result == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    memset(result, 0, sizeof(*result));
    if (!core->thread_running) {
        return LOCKER_STATUS_NOT_READY;
    }
    locker_uuid_t token;
    if (locker_clock_port_new_uuid(&core->clock, &token) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_IO_ERROR;
    }
    uint32_t slot = domain_completion_acquire(core, &token);
    if (slot == UINT32_MAX) {
        return LOCKER_STATUS_BUSY;
    }
    domain_job_t job;
    memset(&job, 0, sizeof(job));
    job.kind = DOMAIN_JOB_QUERY;
    job.completion = slot;
    job.completion_token = token;
    job.body.query = *query;
    locker_status_t status = job_enqueue(core, &job);
    if (status != LOCKER_STATUS_OK) {
        domain_completion_release_owned(core, slot, &token);
        return status;
    }
    status = domain_completion_wait_final(core, slot, &token, LOCKER_CORE_RESULT_WAIT_MS, result);
    /* 超时后槽可能已经被回收并分配给下一个请求：必须按令牌释放，
       裸下标会把别人的在途结果抹掉（审计 R3-B-14）。 */
    domain_completion_release_owned(core, slot, &token);
    return status;
}

locker_status_t locker_core_authenticate(locker_core_t *core, const locker_auth_request_t *request,
                                         locker_principal_t *result)
{
    if (core == NULL || request == NULL || result == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    memset(result, 0, sizeof(*result));
    if (request->token_length == 0u || request->token_length >= sizeof(request->token)) {
        return LOCKER_STATUS_INVALID;
    }
    if (!core->thread_running) return LOCKER_STATUS_NOT_READY;
    locker_uuid_t token;
    uint32_t slot = domain_completion_acquire(core, &token);
    if (slot == UINT32_MAX) return LOCKER_STATUS_BUSY;
    domain_job_t job;
    memset(&job, 0, sizeof(job));
    job.kind = DOMAIN_JOB_AUTH;
    job.completion = slot;
    job.completion_token = token;
    job.body.auth = *request;
    locker_status_t status = job_enqueue(core, &job);
    clear_job(&job);
    if (status == LOCKER_STATUS_OK) {
        locker_query_result_t reply;
        memset(&reply, 0, sizeof(reply));
        status = domain_completion_wait_final(core, slot, &token, LOCKER_CORE_RESULT_WAIT_MS, &reply);
        if (status == LOCKER_STATUS_OK) {
            status = reply.status;
            if (status == LOCKER_STATUS_OK &&
                !domain_completion_take_principal(core, slot, &token, result))
                status = LOCKER_STATUS_NOT_READY;
        }
    }
    domain_completion_release_owned(core, slot, &token);
    return status;
}

locker_status_t locker_core_get_progress(locker_core_t *core, locker_progress_t *result)
{
    if (core == NULL || result == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    memset(result, 0, sizeof(*result));
    pthread_mutex_lock(&core->progress_mutex);
    bool valid = core->progress_valid;
    if (valid) {
        *result = core->progress;
    }
    pthread_mutex_unlock(&core->progress_mutex);
    if (!valid) {
        /* 没有真实循环样本时保持未知，绝不合成一个自增计数（契约 :337）。 */
        return LOCKER_STATUS_NOT_READY;
    }
    return LOCKER_STATUS_OK;
}

/* ---------- 本机私有入口 ---------- */

locker_status_t locker_local_presence_submit(locker_core_t *core, const locker_peer_t *peer,
                                             const locker_presence_t *presence, locker_receipt_t *receipt)
{
    if (core == NULL || peer == NULL || presence == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    /* 只接受已验证的本机 UI peer：未验证的 peer 一律拒绝（internal-interfaces.md:108）。 */
    if (!peer->verified_ui_peer && !peer->verified_admin_peer) {
        return LOCKER_STATUS_INVALID;
    }
    if (!core->thread_running) {
        return LOCKER_STATUS_NOT_READY;
    }
    domain_job_t job;
    memset(&job, 0, sizeof(job));
    job.kind = DOMAIN_JOB_PRESENCE;
    job.completion = UINT32_MAX;
    job.body.presence.peer = *peer;
    job.body.presence.presence = *presence;
    if (receipt) {
        memset(receipt, 0, sizeof(*receipt));
        if (locker_uuid_is_nil(&presence->request_id) || locker_uuid_is_nil(&presence->idempotency_key)) return LOCKER_STATUS_INVALID;
        job.completion = domain_completion_acquire(core, &job.completion_token);
        if (job.completion == UINT32_MAX) return LOCKER_STATUS_BUSY;
        locker_status_t status = job_enqueue(core, &job);
        if (status != LOCKER_STATUS_OK) { domain_completion_release_owned(core, job.completion, &job.completion_token); return status; }
        receipt->submission_token = job.completion_token;
        receipt->status=LOCKER_STATUS_QUEUED;
        receipt->request_id=presence->request_id;
        return LOCKER_STATUS_QUEUED;
    }
    return job_enqueue(core, &job);
}

locker_status_t locker_local_maintenance_reconcile(locker_core_t *core, const locker_peer_t *peer,
                                                   const locker_maintenance_t *request, locker_receipt_t *receipt)
{
    if (core == NULL || peer == NULL || request == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!peer->verified_admin_peer || !memchr(request->reason,0,sizeof(request->reason))) {
        return LOCKER_STATUS_INVALID;
    }
    if (!core->thread_running) {
        return LOCKER_STATUS_NOT_READY;
    }
    domain_job_t job;
    memset(&job, 0, sizeof(job));
    job.kind = DOMAIN_JOB_MAINTENANCE;
    job.completion = UINT32_MAX;
    job.body.maintenance.peer = *peer;
    job.body.maintenance.maintenance = *request;
    if (receipt) {
        memset(receipt, 0, sizeof(*receipt));
        if (locker_uuid_is_nil(&request->request_id) || locker_uuid_is_nil(&request->idempotency_key)) return LOCKER_STATUS_INVALID;
        job.completion = domain_completion_acquire(core, &job.completion_token);
        if (job.completion == UINT32_MAX) return LOCKER_STATUS_BUSY;
        locker_status_t status = job_enqueue(core, &job);
        if (status != LOCKER_STATUS_OK) { domain_completion_release_owned(core, job.completion, &job.completion_token); return status; }
        receipt->submission_token = job.completion_token;
        receipt->status=LOCKER_STATUS_QUEUED; receipt->request_id=request->request_id;
        return LOCKER_STATUS_QUEUED;
    }
    return job_enqueue(core, &job);
}

/* S4-04 拥有该端口；本卡不提供空实现冒充（stage-01-task-cards.md:67-75）。 */
locker_status_t locker_core_apply_supervisor_health(locker_core_t *core,
                                                    const locker_supervisor_health_t *health)
{
    (void)core;
    (void)health;
    return LOCKER_STATUS_NOT_READY;
}
