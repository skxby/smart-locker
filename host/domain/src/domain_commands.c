#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 业务核心：命令、查询、状态机推进与超时。
 *
 * 状态表真源是 docs/stages/stage-00-architecture.md:106-128；本文件是它唯一的
 * 实现点，全部函数只在 Core 线程执行。几条不可放宽的规则：
 *
 *   - 只有 ACK 不进入开门：ACCEPTED 只说明意图被接纳，ACCESS_ACTIVE 必须等
 *     PULSE_STARTED 事实（apply_execute 里判定）。
 *   - 完成必须同时满足「物理变化」与「本人确认」；只有离位快照不算证据。
 *   - 历史读卡（live_transport == false）永远不能产生柜前证明。
 *   - 终结后释放占位；REVIEW_REQUIRED 保留占位（x-rules 第 6 条）。
 *   - 任何写事务失败都不发送动作：outbox 只有在 commit_change 成功后才交给
 *     设备适配器（internal-interfaces.md:114）。
 */

#include "domain_internal.h"

#include <string.h>

#include "locker/common/log.h"

/* 幂等记录的有效期（契约 LOCKER_IDEMPOTENCY_DAYS 天）。 */
#define DOMAIN_IDEMPOTENCY_MS (LOCKER_IDEMPOTENCY_DAYS * 24u * 60u * 60u * 1000u)
/* 恢复/列表扫描的最大页数，防止一个损坏库把 Core 卡在循环里。 */
#define DOMAIN_SCAN_PAGES 64u

/* ---------- 通用小工具 ---------- */

static bool kind_matches_state(uint8_t kind, uint8_t state)
{
    (void)state;
    return kind == LOCKER_OPERATION_KIND_BORROW || kind == LOCKER_OPERATION_KIND_RETURN;
}

static uint8_t expected_item_for_kind(uint8_t kind)
{
    return kind == LOCKER_OPERATION_KIND_BORROW ? LOCKER_ITEM_STATE_PRESENT : LOCKER_ITEM_STATE_ABSENT;
}

/* 期望的物品终态：借出后应缺席、归还后应在位。 */
static uint8_t resulting_item_for_kind(uint8_t kind)
{
    return kind == LOCKER_OPERATION_KIND_BORROW ? LOCKER_ITEM_STATE_ABSENT : LOCKER_ITEM_STATE_PRESENT;
}

static bool physical_ready_for_action(const locker_core_t *core)
{
    if (!core->device_session_up || !core->session.valid || !core->device_snapshot_valid) {
        return false;
    }
    if (core->session.device_mode != LOCKER_DEVICE_MODE_READY) {
        return false;
    }
    if (!core->cursor.continuity_ok || core->fetch_gap) {
        return false;
    }
    if (core->session.journal_state != LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY) {
        return false;
    }
    if (core->physical.door != LOCKER_DOOR_STATE_CLOSED) {
        return false;
    }
    return true;
}

static void response_finish(locker_core_t *core, uint32_t slot, locker_status_t status, bool has_operation,
                            const locker_uuid_t *operation_id)
{
    if (slot == UINT32_MAX) {
        return;
    }
    domain_completion_accept(core, slot, status, operation_id, has_operation);
    domain_completion_finish(core, slot, status, NULL, 0u, core->revision);
}

static void response_finish_json(locker_core_t *core, uint32_t slot, locker_status_t status, bool has_operation,
                                 const locker_uuid_t *operation_id, const locker_json_writer_t *writer)
{
    if (slot == UINT32_MAX) {
        return;
    }
    domain_completion_accept(core, slot, status, operation_id, has_operation);
    if (status != LOCKER_STATUS_OK || writer == NULL || !locker_domain_json_writer_ok(writer)) {
        domain_completion_finish(core, slot, status == LOCKER_STATUS_OK ? LOCKER_STATUS_NO_SPACE : status, NULL,
                                 0u, core->revision);
        return;
    }
    domain_completion_finish(core, slot, LOCKER_STATUS_OK, writer->buffer, writer->length, core->revision);
}

/* ---------- 占位与生命周期 ---------- */

/* 终结操作：释放占位、清活动副本、发布变化。调用方负责已经提交过状态。 */
static locker_status_t finish_operation(locker_core_t *core, domain_operation_t *operation, uint8_t state,
                                        const char *reason_code)
{
    if (operation == NULL || !operation->used) {
        return LOCKER_STATUS_INVALID;
    }
    locker_status_t status = domain_operation_set_state(core, operation, state, reason_code);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    domain_change_t change;
    domain_change_init(core, &change, core->revision, core->revision + 1u);
    status = domain_operation_persist(core, &change, operation);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    if (!domain_change_delete(&change, "slot_guards", "1")) {
        return LOCKER_STATUS_NO_SPACE;
    }
    status = domain_change_commit(core, &change);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    domain_publish(core, &operation->id, "operation", core->revision);
    return LOCKER_STATUS_OK;
}

/* 把操作推进到核验：保留占位（x-rules 6），并持久化原因。 */
static locker_status_t require_review(locker_core_t *core, domain_operation_t *operation, const char *reason_code)
{
    if (operation == NULL || !operation->used) {
        return LOCKER_STATUS_INVALID;
    }
    if (operation->state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED) {
        return LOCKER_STATUS_OK;
    }
    locker_status_t status = domain_operation_set_state(core, operation, LOCKER_OPERATION_STATE_REVIEW_REQUIRED,
                                                        reason_code);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = domain_persist_operation(core, operation);
    if (status == LOCKER_STATUS_OK) {
        /* 核验必须同步到活动工作副本：调用方可能拿的是刚从仓储读出的局部副本
           （confirm/cancel 就是这样），只改局部副本会让 Core 内存里留着旧状态，
           快照投影、超时推进和后续命令都按过期事实判断。 */
        if (core->active.used && locker_uuid_equal(&core->active.id, &operation->id)) {
            core->active = *operation;
        }
        domain_publish(core, &operation->id, "operation", core->revision);
    }
    return status;
}

/* ---------- 幂等 ---------- */

static bool build_request_hash(const locker_command_t *command, char *out, size_t out_size)
{
    locker_domain_sha256_t hash;
    locker_domain_sha256_init(&hash);
    char actor[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&command->principal.user_id, actor, sizeof(actor))) {
        return false;
    }
    locker_domain_sha256_update(&hash, actor, strlen(actor));
    locker_domain_sha256_update(&hash, "\0", 1u);
    locker_domain_sha256_update(&hash, command->kind, domain_strnlen(command->kind, sizeof(command->kind)));
    locker_domain_sha256_update(&hash, "\0", 1u);
    char path[LOCKER_DOMAIN_UUID_TEXT + 8u];
    if (domain_uuid_to_text(&command->resource_id, path, LOCKER_DOMAIN_UUID_TEXT)) {
        locker_domain_sha256_update(&hash, path, strlen(path));
    }
    locker_domain_sha256_update(&hash, "\0", 1u);
    locker_domain_sha256_update(&hash, command->arguments_json,
                                domain_strnlen(command->arguments_json, sizeof(command->arguments_json)));
    uint8_t digest[32];
    locker_domain_sha256_final(&hash, digest);
    static const char k_hex[] = "0123456789abcdef";
    if (out_size < 65u) {
        return false;
    }
    for (uint32_t i = 0u; i < 32u; ++i) {
        out[i * 2u] = k_hex[digest[i] >> 4];
        out[i * 2u + 1u] = k_hex[digest[i] & 15u];
    }
    out[64] = '\0';
    return true;
}

/*
 * 幂等重放：同一 actor + 同一 request_hash 的重复意图返回原响应，不产生新
 * operation；哈希不同则 CONFLICT（internal-data.schema.json 的
 * IdempotencyRecord 只存 Core 计算的哈希，Repo 不自行序列化）。
 */
typedef enum domain_idempotency_result {
    DOMAIN_IDEMPOTENCY_NONE = 0,
    DOMAIN_IDEMPOTENCY_REPLAY = 1,
    DOMAIN_IDEMPOTENCY_CONFLICT = 2
} domain_idempotency_result_t;

static domain_idempotency_result_t idempotency_lookup(locker_core_t *core, const locker_command_t *command,
                                                      const char *request_hash, locker_json_writer_t *writer,
                                                      uint64_t *out_revision)
{
    if (locker_uuid_is_nil(&command->idempotency_key)) {
        return DOMAIN_IDEMPOTENCY_NONE;
    }
    char key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&command->idempotency_key, key, sizeof(key))) {
        return DOMAIN_IDEMPOTENCY_CONFLICT;
    }
    domain_doc_t doc;
    if (domain_repo_doc(core, 0, "idempotency", key, &doc) != LOCKER_STATUS_OK) {
        return DOMAIN_IDEMPOTENCY_NONE;
    }
    if (!doc.found || doc.value == NULL) {
        return DOMAIN_IDEMPOTENCY_NONE;
    }
    locker_uuid_t actor;
    if (!domain_json_read_uuid(&doc.doc, domain_json_member(&doc.doc, doc.value, "actor_id"), &actor) ||
        !locker_uuid_equal(&actor, &command->principal.user_id)) {
        return DOMAIN_IDEMPOTENCY_CONFLICT;
    }
    char stored_hash[65];
    if (!domain_json_read_text(&doc.doc, domain_json_member(&doc.doc, doc.value, "request_hash"), stored_hash,
                               sizeof(stored_hash)) ||
        !domain_text_equal(stored_hash, request_hash)) {
        return DOMAIN_IDEMPOTENCY_CONFLICT;
    }
    const locker_json_node_t *response = domain_json_member(&doc.doc, doc.value, "response_json");
    if (response == NULL || response->type != LOCKER_JSON_STRING) {
        return DOMAIN_IDEMPOTENCY_CONFLICT;
    }
    /* response_json 是原样保存的完整响应体（含 data/snapshot_revision/request_id）。 */
    size_t length = 0u;
    if (writer != NULL) {
        locker_domain_json_literal(writer, "");
        if (!locker_domain_json_text(&doc.doc, response, writer->buffer, writer->capacity, &length)) {
            return DOMAIN_IDEMPOTENCY_CONFLICT;
        }
        writer->length = length;
    }
    if (out_revision != NULL) {
        *out_revision = doc.revision;
    }
    return DOMAIN_IDEMPOTENCY_REPLAY;
}

/* ---------- 投影写手 ---------- */

static void write_tool_object(locker_json_writer_t *writer, locker_core_t *core)
{
    if (!core->tool_valid) {
        locker_domain_json_null(writer);
        return;
    }
    core->tool_availability = domain_tool_availability(core);
    domain_write_tool(writer, &core->tool_id, core->tool_name, core->tool_revision, core->tool_availability);
}

static void write_loan_object(locker_json_writer_t *writer, locker_core_t *core)
{
    if (!core->loan_open) {
        locker_domain_json_null(writer);
        return;
    }
    /* 归还操作没有开着的 loan 时也要能回出历史记录，这里只投影当前有效借用。 */
    domain_write_loan(writer, &core->loan_id, &core->loan_tool_id, &core->loan_borrower_id, &core->loan_id, NULL,
                      false, core->loan_opened_at, NULL, false);
}

/* ---------- OPERATION_CREATE ---------- */

static locker_status_t parse_create_request(locker_core_t *core, const locker_command_t *command,
                                            locker_uuid_t *out_tool_id, uint8_t *out_kind, uint64_t *out_tool_revision)
{
    locker_json_document_t doc;
    memset(&doc, 0, sizeof(doc));
    doc.nodes = core->arg_nodes;
    doc.node_capacity = LOCKER_DOMAIN_DOC_NODES;
    doc.members = core->arg_members;
    doc.member_capacity = LOCKER_DOMAIN_DOC_MEMBERS;
    size_t length = domain_strnlen(command->arguments_json, sizeof(command->arguments_json));
    if (length == 0u || !locker_domain_json_parse(&doc, command->arguments_json, length)) {
        return LOCKER_STATUS_INVALID;
    }
    const locker_json_node_t *root = locker_domain_json_root(&doc);
    if (root == NULL || root->type != LOCKER_JSON_OBJECT) {
        return LOCKER_STATUS_INVALID;
    }
    if (!domain_json_read_uuid(&doc, domain_json_member(&doc, root, "tool_id"), out_tool_id)) {
        return LOCKER_STATUS_INVALID;
    }
    if (!domain_json_read_enum_kind(&doc, domain_json_member(&doc, root, "kind"), out_kind)) {
        return LOCKER_STATUS_INVALID;
    }
    if (!domain_json_read_u64(&doc, domain_json_member(&doc, root, "tool_revision"), out_tool_revision)) {
        return LOCKER_STATUS_INVALID;
    }
    return LOCKER_STATUS_OK;
}

static locker_status_t command_operation_create(locker_core_t *core, const locker_command_t *command, uint32_t slot)
{
    locker_uuid_t tool_id;
    uint8_t kind = 0u;
    uint64_t tool_revision = 0u;
    locker_status_t status = parse_create_request(core, command, &tool_id, &kind, &tool_revision);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return status;
    }
    if (core->phase != DOMAIN_PHASE_READY) {
        response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
        return LOCKER_STATUS_NOT_READY;
    }
    /* 只有一个柜格：工具 ID 必须与已投影的一致。 */
    if (core->tool_valid && !locker_uuid_equal(&tool_id, &core->tool_id)) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    if (core->active.used && !domain_terminal_state(core->active.state)) {
        /* 两申请竞争：后到的明确 SLOT_BUSY，不排队等待。 */
        response_finish(core, slot, LOCKER_STATUS_BUSY, false, NULL);
        return LOCKER_STATUS_BUSY;
    }
    if (kind == LOCKER_OPERATION_KIND_BORROW) {
        if (core->loan_open) {
            response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
            return LOCKER_STATUS_CONFLICT;
        }
        if (!physical_ready_for_action(core) || core->physical.item != LOCKER_ITEM_STATE_PRESENT) {
            response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
            return LOCKER_STATUS_NOT_READY;
        }
    } else {
        /* 归还必须是当前借用人本人，且柜里有物品在位。 */
        if (!core->loan_open) {
            response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
            return LOCKER_STATUS_CONFLICT;
        }
        if (!locker_uuid_equal(&core->loan_borrower_id, &command->principal.user_id)) {
            response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
            return LOCKER_STATUS_INVALID;
        }
        if (!physical_ready_for_action(core) || core->physical.item != LOCKER_ITEM_STATE_ABSENT) {
            response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
            return LOCKER_STATUS_NOT_READY;
        }
    }
    (void)tool_revision;
    (void)kind_matches_state;

    domain_operation_t operation;
    domain_operation_reset(&operation);
    operation.used = true;
    if (locker_clock_port_new_uuid(&core->clock, &operation.id) != LOCKER_STATUS_OK) {
        response_finish(core, slot, LOCKER_STATUS_IO_ERROR, false, NULL);
        return LOCKER_STATUS_IO_ERROR;
    }
    operation.tool_id = tool_id;
    operation.actor_id = command->principal.user_id;
    operation.kind = kind;
    operation.state = LOCKER_OPERATION_STATE_REQUESTED;
    operation.revision = 1u;
    operation.history_complete = true;
    operation.has_baseline = true;
    operation.baseline = core->physical;
    domain_operation_capture_physical(&operation, &core->physical);
    domain_operation_touch(core, &operation);
    operation.requested_deadline_ms = domain_now(core) + LOCKER_REQUEST_TTL_MS;

    /* 先把响应体拼好：同一事务里还要写幂等记录，重放必须逐字相同。 */
    char key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&operation.id, key, sizeof(key))) {
        response_finish(core, slot, LOCKER_STATUS_IO_ERROR, false, NULL);
        return LOCKER_STATUS_IO_ERROR;
    }
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    if (!domain_response_data_object(&writer)) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }
    domain_write_operation_projection(&writer, core, &operation);
    domain_response_end(&writer, core->revision + 1u, &command->request_id);
    if (!locker_domain_json_writer_ok(&writer)) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }

    char request_hash[65];
    if (!build_request_hash(command, request_hash, sizeof(request_hash))) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    char expires[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    uint64_t wall = 0u;
    if (locker_clock_port_wall_utc_ms(&core->clock, &wall) != LOCKER_STATUS_OK ||
        !locker_domain_time_format_utc(wall + DOMAIN_IDEMPOTENCY_MS, expires, sizeof(expires))) {
        response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
        return LOCKER_STATUS_NOT_READY;
    }

    domain_change_t change;
    domain_change_init(core, &change, core->revision, core->revision + 1u);
    if (!domain_change_check(&change, "slot_guards", "1", "ABSENT") ||
        !domain_change_check(&change, "operations", key, "ABSENT")) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }
    status = domain_operation_persist(core, &change, &operation);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    if (!domain_change_put_begin(&change, "slot_guards", "1")) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }
    domain_write_slot_guard(&change.writer, &operation.id);
    domain_change_put_end(&change);
    if (!locker_uuid_is_nil(&command->idempotency_key)) {
        char idem_key[LOCKER_DOMAIN_UUID_TEXT];
        if (!domain_uuid_to_text(&command->idempotency_key, idem_key, sizeof(idem_key))) {
            response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
            return LOCKER_STATUS_INVALID;
        }
        if (!domain_change_put_begin(&change, "idempotency", idem_key)) {
            response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
            return LOCKER_STATUS_NO_SPACE;
        }
        domain_write_idempotency(&change.writer, &command->principal.user_id, "POST", command->kind, 
                                 &command->idempotency_key, request_hash, 201, writer.buffer, expires);
        domain_change_put_end(&change);
    }
    status = domain_change_commit(core, &change);
    if (status != LOCKER_STATUS_OK) {
        /* 事务失败：不留任何内存态，也不发动作。 */
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    core->active = operation;
    domain_publish(core, &operation.id, "operation", core->revision);
    /* 落库后的响应 snapshot_revision 用提交后的真实版本。 */
    locker_json_writer_t final_writer;
    domain_response_begin(&final_writer, core->response_buffer, sizeof(core->response_buffer));
    if (domain_response_data_object(&final_writer)) {
        domain_write_operation_projection(&final_writer, core, &operation);
        domain_response_end(&final_writer, core->revision, &command->request_id);
    }
    response_finish_json(core, slot, LOCKER_STATUS_OK, true, &operation.id, &final_writer);
    return LOCKER_STATUS_OK;
}

/* ---------- OPERATION_CONFIRM / CANCEL / RESOLVE ---------- */

static bool parse_revision_request(locker_core_t *core, const locker_command_t *command, uint64_t *out_revision,
                                   char *out_reason, size_t reason_size, bool require_reason, bool *has_reason)
{
    locker_json_document_t doc;
    memset(&doc, 0, sizeof(doc));
    doc.nodes = core->arg_nodes;
    doc.node_capacity = LOCKER_DOMAIN_DOC_NODES;
    doc.members = core->arg_members;
    doc.member_capacity = LOCKER_DOMAIN_DOC_MEMBERS;
    size_t length = domain_strnlen(command->arguments_json, sizeof(command->arguments_json));
    if (length == 0u || !locker_domain_json_parse(&doc, command->arguments_json, length)) {
        return false;
    }
    const locker_json_node_t *root = locker_domain_json_root(&doc);
    if (root == NULL || root->type != LOCKER_JSON_OBJECT) {
        return false;
    }
    if (!domain_json_read_u64(&doc, domain_json_member(&doc, root, "operation_revision"), out_revision)) {
        return false;
    }
    if (out_reason != NULL && reason_size > 0u) {
        out_reason[0] = '\0';
        if (has_reason != NULL) {
            *has_reason = false;
        }
        const locker_json_node_t *reason = domain_json_member(&doc, root, "reason");
        if (reason != NULL && reason->type == LOCKER_JSON_STRING) {
            if (!domain_json_read_text(&doc, reason, out_reason, reason_size)) {
                return false;
            }
            if (has_reason != NULL) {
                *has_reason = true;
            }
        } else if (require_reason) {
            return false;
        }
    }
    return true;
}

static locker_status_t command_operation_confirm(locker_core_t *core, const locker_command_t *command, uint32_t slot)
{
    uint64_t revision = 0u;
    if (!parse_revision_request(core, command, &revision, NULL, 0u, false, NULL)) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    domain_operation_t operation;
    locker_status_t status = domain_operation_load(core, &command->resource_id, 0, &operation);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    if (!locker_uuid_equal(&operation.actor_id, &command->principal.user_id)) {
        /* 错误用户：不能替别人确认。 */
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    if (operation.revision != revision) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    if (operation.state != LOCKER_OPERATION_STATE_WAIT_CONFIRM) {
        /* 提前确认（还在 ACCESS_ACTIVE/REQUESTED）一律拒绝。 */
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    /* 确认前重验当前物理条件：门必须闭、物品必须已到期望终态、连续性完好。 */
    if (!physical_ready_for_action(core) || core->physical.item != resulting_item_for_kind(operation.kind) ||
        !operation.item_transition_seen) {
        status = require_review(core, &operation, "CONFIRM_PRECONDITION");
        response_finish(core, slot, status == LOCKER_STATUS_OK ? LOCKER_STATUS_CONFLICT : status, false, NULL);
        return status;
    }
    core->active = operation;
    /* 先把 loan_id 挂到操作上，让 OperationRecord.related_loan_id 与终结同事务落库。 */
    status = domain_loan_prepare(core, &core->active);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, true, &core->active.id);
        return status;
    }
    status = finish_operation(core, &core->active, LOCKER_OPERATION_STATE_COMPLETED, "CONFIRMED");
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    /* 借出建 loan、归还关 loan；账本写失败不宣称成功。 */
    status = domain_loan_apply(core, &core->active);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, true, &core->active.id);
        return status;
    }
    domain_operation_reset(&core->active);
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    domain_operation_t completed;
    if (domain_operation_load(core, &operation.id, 1, &completed) != LOCKER_STATUS_OK) {
        completed = operation;
    }
    if (domain_response_data_object(&writer)) {
        domain_write_operation_projection(&writer, core, &completed);
        domain_response_end(&writer, core->revision, &command->request_id);
    }
    response_finish_json(core, slot, LOCKER_STATUS_OK, true, &operation.id, &writer);
    return LOCKER_STATUS_OK;
}

static locker_status_t command_operation_cancel(locker_core_t *core, const locker_command_t *command, uint32_t slot)
{
    uint64_t revision = 0u;
    char reason[257];
    bool has_reason = false;
    if (!parse_revision_request(core, command, &revision, reason, sizeof(reason), true, &has_reason) || !has_reason) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    domain_operation_t operation;
    locker_status_t status = domain_operation_load(core, &command->resource_id, 0, &operation);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    if (!locker_uuid_equal(&operation.actor_id, &command->principal.user_id)) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    if (operation.revision != revision) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    if (operation.state == LOCKER_OPERATION_STATE_REQUESTED) {
        /* 还没有任何动作意图：可以安全取消并释放占位。 */
        core->active = operation;
        status = finish_operation(core, &core->active, LOCKER_OPERATION_STATE_CANCELLED, "CANCELLED");
        if (status != LOCKER_STATUS_OK) {
            response_finish(core, slot, status, false, NULL);
            return status;
        }
        domain_operation_reset(&core->active);
        locker_json_writer_t writer;
        domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
        domain_operation_t cancelled;
        if (domain_operation_load(core, &operation.id, 1, &cancelled) != LOCKER_STATUS_OK) {
            cancelled = operation;
        }
        if (domain_response_data_object(&writer)) {
            domain_write_operation_projection(&writer, core, &cancelled);
            domain_response_end(&writer, core->revision, &command->request_id);
        }
        response_finish_json(core, slot, LOCKER_STATUS_OK, true, &operation.id, &writer);
        return LOCKER_STATUS_OK;
    }
    if (domain_terminal_state(operation.state)) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    /* 可能已执行：取消只能是核验请求，不能声称取消成功（internal-interfaces.md:110）。 */
    status = require_review(core, &operation, "CANCEL_AFTER_ACTION");
    response_finish(core, slot, status == LOCKER_STATUS_OK ? LOCKER_STATUS_CONFLICT : status, false, NULL);
    return status;
}

static locker_status_t command_operation_resolve(locker_core_t *core, const locker_command_t *command, uint32_t slot)
{
    if (command->principal.role != LOCKER_ROLE_ADMIN) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    locker_json_document_t doc;
    memset(&doc, 0, sizeof(doc));
    doc.nodes = core->arg_nodes;
    doc.node_capacity = LOCKER_DOMAIN_DOC_NODES;
    doc.members = core->arg_members;
    doc.member_capacity = LOCKER_DOMAIN_DOC_MEMBERS;
    size_t length = domain_strnlen(command->arguments_json, sizeof(command->arguments_json));
    if (length == 0u || !locker_domain_json_parse(&doc, command->arguments_json, length)) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    const locker_json_node_t *root = locker_domain_json_root(&doc);
    uint64_t revision = 0u;
    uint64_t physical_revision = 0u;
    uint8_t resolution = 0u;
    char reason[257];
    if (root == NULL || !domain_json_read_u64(&doc, domain_json_member(&doc, root, "operation_revision"), &revision) ||
        !domain_json_read_u64(&doc, domain_json_member(&doc, root, "physical_revision"), &physical_revision) ||
        !domain_json_read_text(&doc, domain_json_member(&doc, root, "resolution"), reason, sizeof(reason))) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    {
        size_t resolution_length = strlen(reason);
        if (!domain_resolution_from_name(reason, resolution_length, &resolution)) {
            response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
            return LOCKER_STATUS_INVALID;
        }
    }
    char note[257];
    if (!domain_json_read_text(&doc, domain_json_member(&doc, root, "reason"), note, sizeof(note)) ||
        strlen(note) < 5u) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    domain_operation_t operation;
    locker_status_t status = domain_operation_load(core, &command->resource_id, 0, &operation);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    if (operation.state != LOCKER_OPERATION_STATE_REVIEW_REQUIRED) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    if (operation.revision != revision) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    /* 核验只能匹配原操作类型，不能把借出核验成归还。 */
    if ((resolution == LOCKER_RESOLUTION_COMPLETE_BORROW && operation.kind != LOCKER_OPERATION_KIND_BORROW) ||
        (resolution == LOCKER_RESOLUTION_COMPLETE_RETURN && operation.kind != LOCKER_OPERATION_KIND_RETURN)) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    /* 门必须关闭、物品必须明确：传感器 UNKNOWN 时不允许强制完成。 */
    if (!core->device_snapshot_valid || core->physical.door != LOCKER_DOOR_STATE_CLOSED ||
        core->physical.item == LOCKER_ITEM_STATE_UNKNOWN ||
        core->physical.physical_revision != physical_revision) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    core->active = operation;
    core->active.manual_resolution = true;
    uint8_t target = resolution == LOCKER_RESOLUTION_NO_CHANGE ? LOCKER_OPERATION_STATE_NO_CHANGE
                                                              : LOCKER_OPERATION_STATE_COMPLETED;
    if (resolution != LOCKER_RESOLUTION_NO_CHANGE) {
        status = domain_loan_prepare(core, &core->active);
        if (status != LOCKER_STATUS_OK) {
            response_finish(core, slot, status, true, &core->active.id);
            return status;
        }
    }
    status = finish_operation(core, &core->active, target, "ADMIN_RESOLVED");
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    if (resolution != LOCKER_RESOLUTION_NO_CHANGE) {
        status = domain_loan_apply(core, &core->active);
        if (status != LOCKER_STATUS_OK) {
            response_finish(core, slot, status, true, &core->active.id);
            return status;
        }
    }
    domain_operation_reset(&core->active);
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    domain_operation_t resolved;
    if (domain_operation_load(core, &operation.id, 1, &resolved) != LOCKER_STATUS_OK) {
        resolved = operation;
    }
    if (domain_response_data_object(&writer)) {
        domain_write_operation_projection(&writer, core, &resolved);
        domain_response_end(&writer, core->revision, &command->request_id);
    }
    response_finish_json(core, slot, LOCKER_STATUS_OK, true, &operation.id, &writer);
    return LOCKER_STATUS_OK;
}

/* ---------- 柜前证明 ---------- */

static locker_status_t presence_apply(locker_core_t *core, const locker_presence_t *presence, uint8_t proof_source)
{
    if (core->phase != DOMAIN_PHASE_READY) {
        return LOCKER_STATUS_NOT_READY;
    }
    domain_operation_t operation;
    locker_status_t status = domain_operation_load(core, &presence->operation_id, 0, &operation);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    if (!locker_uuid_equal(&operation.actor_id, &presence->principal.user_id)) {
        /* 证明只能属于操作本人。 */
        return LOCKER_STATUS_INVALID;
    }
    if (operation.revision != presence->operation_revision) {
        return LOCKER_STATUS_CONFLICT;
    }
    if (operation.state != LOCKER_OPERATION_STATE_REQUESTED) {
        /* 已提交动作意图后不再接受新的到柜证明。 */
        return LOCKER_STATUS_CONFLICT;
    }
    if (operation.proof_consumed) {
        /* 一份柜前证明只算一次；过期作废由 operation_arm 的期限分支负责。 */
        return LOCKER_STATUS_CONFLICT;
    }
    operation.proof_source = proof_source;
    operation.proof_ms = domain_now(core);
    operation.proof_consumed = true;
    core->active = operation;
    return LOCKER_STATUS_OK;
}

/*
 * REQUESTED -> ACCESS_PENDING：柜前证明 + 物理前置条件 + 视觉门禁都满足才提交
 * 唯一 command。事务里同时写 operation（含 command_id）、outbox 与占位保持。
 */
static locker_status_t operation_arm(locker_core_t *core)
{
    domain_operation_t *operation = &core->active;
    if (!operation->used || operation->state != LOCKER_OPERATION_STATE_REQUESTED) {
        return LOCKER_STATUS_NOT_READY;
    }
    if (!operation->proof_consumed) {
        return LOCKER_STATUS_NOT_READY;
    }
    uint64_t now = domain_now(core);
    if (operation->proof_ms == 0u || now < operation->proof_ms ||
        now - operation->proof_ms > LOCKER_PRESENCE_TTL_MS) {
        /* 15 秒证明过期：必须重新到柜，不能拿旧证明开门。 */
        operation->proof_consumed = false;
        operation->proof_source = 0u;
        (void)require_review(core, operation, "PRESENCE_EXPIRED");
        return LOCKER_STATUS_CONFLICT;
    }
    if (core->vision_pre_required) {
        /* REQUIRED 策略需要 PRE_ACCESS 视觉证据；S1-04 只支持
           DISABLED_FOR_FOUNDATION，S2-14 才接入真实视觉门禁。 */
        return LOCKER_STATUS_NOT_READY;
    }
    if (!physical_ready_for_action(core) ||
        core->physical.item != expected_item_for_kind(operation->kind)) {
        return LOCKER_STATUS_NOT_READY;
    }
    locker_uuid_t command_id;
    if (locker_clock_port_new_uuid(&core->clock, &command_id) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_IO_ERROR;
    }
    operation->command_id = command_id;
    operation->has_command = true;
    operation->action_no = core->next_action_no;
    operation->outbox_present = true;
    if (locker_clock_port_new_uuid(&core->clock, &operation->outbox_id) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_IO_ERROR;
    }
    operation->outbox_session_id = core->session.session_id;
    operation->outbox_journal_epoch = core->session.journal_epoch;
    operation->outbox_boot_id = core->session.boot_id;
    operation->outbox_physical_revision = core->physical.physical_revision;
    operation->outbox_not_after_ms = now + LOCKER_PREPARE_DEADLINE_MS;
    operation->outbox_state = 1u; /* PREPARED */
    operation->send_started = false;
    operation->access_pending_ms = now;

    locker_status_t status = domain_operation_set_state(core, operation, LOCKER_OPERATION_STATE_ACCESS_PENDING,
                                                        "ARMED");
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    char operation_key[LOCKER_DOMAIN_UUID_TEXT];
    char outbox_key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&operation->id, operation_key, sizeof(operation_key)) ||
        !domain_uuid_to_text(&operation->outbox_id, outbox_key, sizeof(outbox_key))) {
        return LOCKER_STATUS_INVALID;
    }
    domain_change_t change;
    domain_change_init(core, &change, core->revision, core->revision + 1u);
    status = domain_operation_persist(core, &change, operation);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    if (!domain_change_put_begin(&change, "outbox", outbox_key)) {
        return LOCKER_STATUS_NO_SPACE;
    }
    domain_write_outbox(&change.writer, operation, "PREPARED", NULL);
    domain_change_put_end(&change);
    status = domain_change_commit(core, &change);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    core->next_action_no += 1u;
    domain_publish(core, &operation->id, "operation", core->revision);
    return LOCKER_STATUS_OK;
}

/* ---------- outbox 泵 ---------- */

/*
 * 只有本次进程实例新建的 outbox 项才允许发送；发送前先持久化
 * 「已尝试发送」检查点，检查点提交失败就绝不发送（internal-interfaces.md:114）。
 */
void domain_outbox_pump(locker_core_t *core)
{
    domain_operation_t *operation = &core->active;
    if (!operation->used || !operation->outbox_present) {
        return;
    }
    if (operation->state != LOCKER_OPERATION_STATE_ACCESS_PENDING || operation->send_started ||
        core->execute_in_flight) {
        return;
    }
    if (!core->device_session_up || !core->session.valid) {
        return;
    }
    uint64_t now = domain_now(core);
    if (operation->outbox_not_after_ms != 0u && now > operation->outbox_not_after_ms) {
        /* 准备期限已过：不再发送，交核验。 */
        (void)require_review(core, operation, "PREPARE_DEADLINE");
        return;
    }
    if (core->session.boot_id != operation->outbox_boot_id ||
        !locker_uuid_equal(&core->session.journal_epoch, &operation->outbox_journal_epoch)) {
        (void)require_review(core, operation, "SESSION_CHANGED");
        return;
    }
    if (core->physical.physical_revision != operation->outbox_physical_revision) {
        (void)require_review(core, operation, "PHYSICAL_CHANGED");
        return;
    }
    /* 检查点：先把 outbox 置 SEND_ATTEMPTED 并持久化，再发。 */
    char outbox_key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&operation->outbox_id, outbox_key, sizeof(outbox_key))) {
        (void)require_review(core, operation, "OUTBOX_KEY");
        return;
    }
    char attempted[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    uint64_t wall = 0u;
    if (locker_clock_port_wall_utc_ms(&core->clock, &wall) != LOCKER_STATUS_OK ||
        !locker_domain_time_format_utc(wall, attempted, sizeof(attempted))) {
        (void)require_review(core, operation, "CHECKPOINT_CLOCK");
        return;
    }
    domain_change_t change;
    domain_change_init(core, &change, core->revision, core->revision + 1u);
    if (!domain_change_put_begin(&change, "outbox", outbox_key)) {
        (void)require_review(core, operation, "CHECKPOINT_SPACE");
        return;
    }
    domain_write_outbox(&change.writer, operation, "SEND_ATTEMPTED", attempted);
    domain_change_put_end(&change);
    if (domain_change_commit(core, &change) != LOCKER_STATUS_OK) {
        /* 检查点没落库：禁止发送，进核验。 */
        (void)require_review(core, operation, "CHECKPOINT_FAILED");
        return;
    }
    operation->outbox_state = 2u; /* SEND_ATTEMPTED */
    operation->send_started = true;
    operation->send_started_ms = now;

    locker_status_t status = domain_execute_intent(core, operation);
    if (status != LOCKER_STATUS_OK) {
        if (status == LOCKER_STATUS_BUSY) {
            return;
        }
        /* 端口本身拒绝：结果未知，进核验而不是重发。 */
        (void)require_review(core, operation, "SEND_FAILED");
    }
}

/* ---------- 超时推进 ---------- */

void domain_timeouts(locker_core_t *core)
{
    uint64_t now = domain_now(core);
    domain_operation_t *operation = &core->active;
    if (operation->used) {
        switch (operation->state) {
        case LOCKER_OPERATION_STATE_REQUESTED:
            if (operation->requested_deadline_ms != 0u && now >= operation->requested_deadline_ms) {
                /* 120 秒无动作意图：取消并释放占位。 */
                (void)finish_operation(core, operation, LOCKER_OPERATION_STATE_CANCELLED, "REQUEST_EXPIRED");
                domain_operation_reset(operation);
            }
            break;
        case LOCKER_OPERATION_STATE_ACCESS_PENDING:
            /* 脉冲事实起 30 秒未见开门 -> 核验。 */
            if (operation->access_pending_ms != 0u && now - operation->access_pending_ms >= LOCKER_WAIT_OPEN_MS) {
                (void)require_review(core, operation, "OPEN_TIMEOUT");
                break;
            }
            /* 发送后宿主超时仍未拿到结果：只查询，不重发。 */
            if (operation->send_started && core->pending_command_valid && !core->pending_command_queried &&
                core->pending_command_ms != 0u && now - core->pending_command_ms >= LOCKER_HOST_TIMEOUT_MS) {
                if (domain_query_command(core, &core->pending_command_id) == LOCKER_STATUS_OK) {
                    core->pending_command_queried = true;
                    /* 查询发出后重新起算等待窗口。不重起算的话，同一个 tick 里
                       既发出查询又立刻丢弃待确认命令，回复永远对不上，等于把
                       「只查一次」退化成「查了就忘」。 */
                    core->pending_command_ms = now;
                }
            }
            break;
        case LOCKER_OPERATION_STATE_ACCESS_ACTIVE:
            if (operation->door_opened_ms != 0u && now - operation->door_opened_ms >= LOCKER_WAIT_CLOSE_MS) {
                (void)require_review(core, operation, "CLOSE_TIMEOUT");
            }
            break;
        case LOCKER_OPERATION_STATE_WAIT_CONFIRM:
            if (operation->wait_confirm_deadline_ms != 0u && now >= operation->wait_confirm_deadline_ms) {
                (void)require_review(core, operation, "CONFIRM_TIMEOUT");
            }
            break;
        default:
            break;
        }
    }
    /* 在途请求超时：清槽，让泵重新发起（回复迟到时按 request_no 找不到，忽略）。 */
    for (uint32_t i = 0u; i < LOCKER_DOMAIN_PENDING_SLOTS; ++i) {
        domain_pending_t *slot = &core->pending[i];
        if (!slot->used) {
            continue;
        }
        if (slot->started_ms != 0u && now - slot->started_ms >= LOCKER_HOST_TIMEOUT_MS) {
            uint8_t purpose = slot->purpose;
            memset(slot, 0, sizeof(*slot));
            if (purpose == DOMAIN_PURPOSE_SNAPSHOT) {
                core->snapshot_in_flight = false;
            } else if (purpose == DOMAIN_PURPOSE_FETCH) {
                core->fetch_in_flight = false;
            } else if (purpose == DOMAIN_PURPOSE_ACK) {
                core->ack_in_flight = false;
            }
        }
    }
    /* 只有「已经查过一次、且查询后的窗口也走完」才放弃等待：还没查就清掉
       会白白丢掉一次可用的只读核对机会。 */
    if (core->pending_command_valid && core->pending_command_queried && core->pending_command_ms != 0u &&
        now - core->pending_command_ms >= LOCKER_HOST_TIMEOUT_MS && !core->execute_in_flight) {
        core->pending_command_valid = false;
        core->pending_command_queried = false;
    }
}

/* ---------- 设备事件 / 响应 ---------- */

void domain_handle_device_event(locker_core_t *core, const locker_device_event_t *event, uint32_t completion)
{
    if (atomic_exchange(&core->event_overflow_flag, false)) {
        core->event_overflow = true;
        core->fetch_gap = true;
        core->cursor.continuity_ok = false;
    }
    if (event->event_type == LOCKER_DEVICE_EVENT_CARD_OBSERVED && event->live_transport &&
        event->card_uid_len != 0u && event->card_uid_len <= sizeof(core->last_card.uid)) {
        /* 历史补传（live_transport == false）只补记录，绝不产生柜前证明。 */
        core->last_card.valid = true;
        core->last_card.mono_ms = domain_now(core);
        core->last_card.uid_len = event->card_uid_len;
        memcpy(core->last_card.uid, event->card_uid, event->card_uid_len);
        core->last_card.user_resolved = false;
    }
    domain_event_apply(core, event);
    if (completion != UINT32_MAX) {
        domain_completion_fail(core, completion, LOCKER_STATUS_OK);
    }
}

void domain_handle_device_reply(locker_core_t *core, const locker_device_reply_t *reply, uint32_t completion)
{
    if (atomic_exchange(&core->reply_overflow_flag, false)) {
        core->reply_overflow = true;
        core->fetch_gap = true;
    }
    domain_reply_apply(core, reply);
    if (completion != UINT32_MAX) {
        domain_completion_fail(core, completion, LOCKER_STATUS_OK);
    }
}

/* ---------- 认证 ---------- */

void domain_handle_auth(locker_core_t *core, const domain_job_t *job)
{
    (void)core;
    (void)job;
    /* 真实凭据校验归 S1-05；见 locker_core_authenticate 的说明。 */
    if (job->completion != UINT32_MAX) {
        domain_completion_accept(core, job->completion, LOCKER_STATUS_NOT_READY, NULL, false);
        domain_completion_finish(core, job->completion, LOCKER_STATUS_NOT_READY, NULL, 0u, core->revision);
    }
}

/* ---------- 查询 ---------- */

static locker_status_t query_operation_get(locker_core_t *core, const locker_query_t *query, uint32_t slot)
{
    domain_operation_t operation;
    locker_status_t status = domain_operation_load(core, &query->resource_id, 0, &operation);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    if (domain_response_data_object(&writer)) {
        domain_write_operation_projection(&writer, core, &operation);
        domain_response_end(&writer, core->revision, &query->request_id);
    }
    response_finish_json(core, slot, LOCKER_STATUS_OK, true, &operation.id, &writer);
    return LOCKER_STATUS_OK;
}

static void scan_operations(locker_core_t *core, locker_json_writer_t *writer, uint32_t *written, bool reviews_only)
{
    char after[LOCKER_DOMAIN_KEY_TEXT];
    after[0] = '\0';
    for (uint32_t page = 0u; page < DOMAIN_SCAN_PAGES; ++page) {
        domain_doc_t doc;
        locker_status_t status = domain_repo_page(core, "operations", after[0] == '\0' ? NULL : after, 100u, &doc);
        if (status != LOCKER_STATUS_OK) {
            return;
        }
        const locker_json_node_t *documents =
            domain_json_member(&doc.doc, locker_domain_json_root(&doc.doc), "documents");
        if (documents == NULL || documents->type != LOCKER_JSON_ARRAY) {
            return;
        }
        for (uint32_t i = 0u; i < documents->count; ++i) {
            const locker_json_node_t *document = locker_domain_json_at(&doc.doc, documents, i);
            const locker_json_node_t *value = domain_json_member(&doc.doc, document, "value");
            if (value == NULL) {
                return;
            }
            domain_operation_t operation;
            if (!domain_operation_from_record(&doc.doc, value, &operation)) {
                return;
            }
            if (reviews_only && operation.state != LOCKER_OPERATION_STATE_REVIEW_REQUIRED) {
                continue;
            }
            if (*written > 0u) {
                locker_domain_json_literal(writer, ",");
            }
            domain_write_operation_projection(writer, core, &operation);
            *written += 1u;
            if (*written >= 100u) {
                return;
            }
        }
        const locker_json_node_t *next =
            domain_json_member(&doc.doc, locker_domain_json_root(&doc.doc), "next_after_key");
        if (next == NULL || next->type != LOCKER_JSON_STRING) {
            return;
        }
        size_t length = 0u;
        if (!locker_domain_json_text(&doc.doc, next, after, sizeof(after), &length)) {
            return;
        }
    }
}

static locker_status_t query_list(locker_core_t *core, const locker_query_t *query, uint32_t slot, bool reviews)
{
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    if (!domain_response_data_object(&writer)) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }
    locker_domain_json_key(&writer, "items");
    locker_domain_json_literal(&writer, "[");
    uint32_t written = 0u;
    if (reviews) {
        scan_operations(core, &writer, &written, true);
    }
    locker_domain_json_literal(&writer, "]");
    if (!reviews) {
        /* loans / events 分页游标：首版一次返回全部，未读完由后续卡接续。 */
        locker_domain_json_literal(&writer, ",\"next_cursor\":");
        locker_domain_json_null(&writer);
    }
    locker_domain_json_literal(&writer, "}");
    domain_response_end(&writer, core->revision, &query->request_id);
    response_finish_json(core, slot, LOCKER_STATUS_OK, false, NULL, &writer);
    return LOCKER_STATUS_OK;
}

static locker_status_t query_loans(locker_core_t *core, const locker_query_t *query, uint32_t slot)
{
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    if (!domain_response_data_object(&writer)) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }
    locker_domain_json_key(&writer, "items");
    locker_domain_json_literal(&writer, "[");
    if (core->loan_open) {
        write_loan_object(&writer, core);
    }
    locker_domain_json_literal(&writer, "],\"next_cursor\":");
    locker_domain_json_null(&writer);
    locker_domain_json_literal(&writer, "}");
    domain_response_end(&writer, core->revision, &query->request_id);
    response_finish_json(core, slot, LOCKER_STATUS_OK, false, NULL, &writer);
    return LOCKER_STATUS_OK;
}

static locker_status_t query_device_get(locker_core_t *core, const locker_query_t *query, uint32_t slot)
{
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    if (domain_response_data_object(&writer)) {
        domain_write_device_projection(&writer, core);
        domain_response_end(&writer, core->revision, &query->request_id);
    }
    response_finish_json(core, slot, LOCKER_STATUS_OK, false, NULL, &writer);
    return LOCKER_STATUS_OK;
}

static locker_status_t query_tools(locker_core_t *core, const locker_query_t *query, uint32_t slot, bool single)
{
    if (single && !locker_uuid_equal(&query->resource_id, &core->tool_id)) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    if (!domain_response_data_object(&writer)) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }
    if (single) {
        write_tool_object(&writer, core);
    } else {
        locker_domain_json_key(&writer, "items");
        locker_domain_json_literal(&writer, "[");
        if (core->tool_valid) {
            write_tool_object(&writer, core);
        }
        locker_domain_json_literal(&writer, "]");
    }
    locker_domain_json_literal(&writer, "}");
    domain_response_end(&writer, core->revision, &query->request_id);
    response_finish_json(core, slot, LOCKER_STATUS_OK, false, NULL, &writer);
    return LOCKER_STATUS_OK;
}

static locker_status_t query_snapshot(locker_core_t *core, const locker_query_t *query, uint32_t slot)
{
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    if (!domain_response_data_object(&writer)) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }
    locker_domain_json_key(&writer, "device");
    domain_write_device_projection(&writer, core);
    locker_domain_json_literal(&writer, ",\"tools\":[");
    if (core->tool_valid) {
        write_tool_object(&writer, core);
    }
    locker_domain_json_literal(&writer, "],\"active_operations\":[");
    if (core->active.used && !domain_terminal_state(core->active.state)) {
        domain_write_operation_projection(&writer, core, &core->active);
    }
    locker_domain_json_literal(&writer, "],\"loans\":[");
    if (core->loan_open) {
        write_loan_object(&writer, core);
    }
    locker_domain_json_literal(&writer, "]}");
    domain_response_end(&writer, core->revision, &query->request_id);
    response_finish_json(core, slot, LOCKER_STATUS_OK, false, NULL, &writer);
    return LOCKER_STATUS_OK;
}

void domain_handle_query(locker_core_t *core, const domain_job_t *job)
{
    const locker_query_t *query = &job->body.query;
    uint32_t slot = job->completion;
    locker_status_t status = domain_principal_check(core, &query->principal);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return;
    }
    if (strcmp(query->kind, LOCKER_CORE_DEVICE_GET) == 0) {
        query_device_get(core, query, slot);
    } else if (strcmp(query->kind, LOCKER_CORE_SNAPSHOT_GET) == 0) {
        query_snapshot(core, query, slot);
    } else if (strcmp(query->kind, LOCKER_CORE_TOOLS_LIST) == 0) {
        query_tools(core, query, slot, false);
    } else if (strcmp(query->kind, LOCKER_CORE_TOOL_GET) == 0) {
        query_tools(core, query, slot, true);
    } else if (strcmp(query->kind, LOCKER_CORE_OPERATION_GET) == 0) {
        query_operation_get(core, query, slot);
    } else if (strcmp(query->kind, LOCKER_CORE_LOANS_LIST) == 0) {
        query_loans(core, query, slot);
    } else if (strcmp(query->kind, LOCKER_CORE_REVIEWS_LIST) == 0) {
        query_list(core, query, slot, true);
    } else if (strcmp(query->kind, LOCKER_CORE_EVENTS_LIST) == 0) {
        /* 阶段 1 的 18 个仓储集合里没有 events：公开事件流由审计/设备事件派生，
           归 S1-05 适配层；这里返回空页而不是伪造历史。 */
        query_list(core, query, slot, false);
    } else {
        /* SESSION、USERS、CARD、VISION 四类命令归 S1-05 / S2：不空实现冒充。 */
        response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
    }
}

/* ---------- 命令 ---------- */

void domain_handle_command(locker_core_t *core, const domain_job_t *job)
{
    const locker_command_t *command = &job->body.command;
    uint32_t slot = job->completion;
    locker_status_t status = domain_principal_check(core, &command->principal);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return;
    }
    if (core->phase != DOMAIN_PHASE_READY) {
        response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
        return;
    }
    /* 幂等：同一 key + 同一哈希返回原响应，不产生新 operation。 */
    char request_hash[65];
    if (strcmp(command->kind, LOCKER_CORE_OPERATION_CREATE) == 0 && locker_uuid_is_nil(&command->idempotency_key)) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return;
    }
    if (!build_request_hash(command, request_hash, sizeof(request_hash))) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return;
    }
    locker_json_writer_t replay;
    locker_domain_json_writer_init(&replay, core->response_buffer, sizeof(core->response_buffer));
    domain_idempotency_result_t idem = idempotency_lookup(core, command, request_hash, &replay, NULL);
    if (idem == DOMAIN_IDEMPOTENCY_CONFLICT) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return;
    }
    if (idem == DOMAIN_IDEMPOTENCY_REPLAY) {
        response_finish_json(core, slot, LOCKER_STATUS_OK, false, NULL, &replay);
        return;
    }
    if (strcmp(command->kind, LOCKER_CORE_OPERATION_CREATE) == 0) {
        command_operation_create(core, command, slot);
    } else if (strcmp(command->kind, LOCKER_CORE_OPERATION_CONFIRM) == 0) {
        command_operation_confirm(core, command, slot);
    } else if (strcmp(command->kind, LOCKER_CORE_OPERATION_CANCEL) == 0) {
        command_operation_cancel(core, command, slot);
    } else if (strcmp(command->kind, LOCKER_CORE_OPERATION_RESOLVE) == 0) {
        command_operation_resolve(core, command, slot);
    } else {
        response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
    }
}

void domain_handle_presence(locker_core_t *core, const domain_job_t *job)
{
    const locker_presence_t *presence = &job->body.presence.presence;
    locker_status_t status = domain_principal_check(core, &presence->principal);
    if (status != LOCKER_STATUS_OK) {
        return;
    }
    status = presence_apply(core, presence, 2u /* LOCAL_UI */);
    if (status != LOCKER_STATUS_OK) {
        return;
    }
    /* 到柜证明立即尝试推进到 ACCESS_PENDING；物理条件不满足就留在 REQUESTED。 */
    status = operation_arm(core);
    if (status == LOCKER_STATUS_OK) {
        domain_outbox_pump(core);
    }
}

void domain_handle_maintenance(locker_core_t *core, const domain_job_t *job)
{
    const locker_maintenance_t *request = &job->body.maintenance.maintenance;
    locker_status_t status = domain_principal_check(core, &request->principal);
    if (status != LOCKER_STATUS_OK) {
        return;
    }
    if (request->principal.role != LOCKER_ROLE_ADMIN) {
        return;
    }
    /* 维护核验不是普通确认的别名：不改写操作历史、不发脉冲、不清缺口。 */
    if (core->phase != DOMAIN_PHASE_READY || !core->device_snapshot_valid) {
        return;
    }
    if (core->physical.door != LOCKER_DOOR_STATE_CLOSED || core->physical.item == LOCKER_ITEM_STATE_UNKNOWN) {
        return;
    }
    if (core->physical.physical_revision != request->physical_revision ||
        core->revision != request->snapshot_revision) {
        return;
    }
    /* 只解除可以解除的 hold：重新做一次 CAS 放行，让设备回到 READY。 */
    locker_ready_guard_t guard;
    memset(&guard, 0, sizeof(guard));
    guard.session_id = core->session.session_id;
    guard.journal_epoch = core->session.journal_epoch;
    guard.expected_boot_id = core->session.boot_id;
    guard.expected_physical_revision = core->physical.physical_revision;
    guard.persisted_through = core->import_cursor;
    (void)locker_device_resume_ready(core->device, &guard);
}

void domain_handle_tick(locker_core_t *core, uint64_t tick_ms)
{
    domain_now_advance(core, tick_ms);
    domain_timeouts(core);
    if (core->phase == DOMAIN_PHASE_READY) {
        /*
         * 到柜证明与物理前置条件可能错开：证明提交时门磁还没稳定，arm 会留在
         * REQUESTED。这里在证明仍新鲜的窗口内继续尝试，过期由 arm 自己转
         * REVIEW_REQUIRED，保证「15 秒证明过期」有确定出口而不是永远挂着。
         */
        if (core->active.used && core->active.state == LOCKER_OPERATION_STATE_REQUESTED &&
            core->active.proof_consumed) {
            (void)operation_arm(core);
        }
        domain_outbox_pump(core);
    }
    domain_device_pump(core);
    domain_anchor_request(core);
    domain_progress_publish(core);
    domain_completion_reap(core, core->now_ms);
}

void domain_handle_job(locker_core_t *core, const domain_job_t *job)
{
    switch ((domain_job_kind_t)job->kind) {
    case DOMAIN_JOB_COMMAND:
        domain_handle_command(core, job);
        break;
    case DOMAIN_JOB_QUERY:
        domain_handle_query(core, job);
        break;
    case DOMAIN_JOB_AUTH:
        domain_handle_auth(core, job);
        break;
    case DOMAIN_JOB_PRESENCE:
        domain_handle_presence(core, job);
        break;
    case DOMAIN_JOB_MAINTENANCE:
        domain_handle_maintenance(core, job);
        break;
    case DOMAIN_JOB_TICK:
        domain_handle_tick(core, job->body.tick_ms);
        break;
    case DOMAIN_JOB_DEVICE_EVENT:
        domain_handle_device_event(core, &job->body.event, job->completion);
        break;
    case DOMAIN_JOB_DEVICE_REPLY:
        domain_handle_device_reply(core, &job->body.reply, job->completion);
        break;
    case DOMAIN_JOB_STOP:
    default:
        break;
    }
}
