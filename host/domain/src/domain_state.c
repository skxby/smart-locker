#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 业务核心：文档反序列化、公开投影、响应信封与进度发布。
 *
 * 这个文件只做「值 <-> 文档」与「Core 状态 -> 公开 JSON」，不做状态迁移。
 * 所有函数都跑在 Core 线程上，只有 domain_progress_publish 写的进度副本例外
 * （用 progress_mutex 保护，读侧非阻塞）。
 */

#include "domain_internal.h"

#include <stdlib.h>
#include <string.h>

/* ---------- JSON 取值小工具 ---------- */

const locker_json_node_t *domain_json_member(const locker_json_document_t *doc,
                                        const locker_json_node_t *object, const char *key)
{
    if (doc == NULL || object == NULL) {
        return NULL;
    }
    return locker_domain_json_get(doc, object, key);
}

bool domain_json_read_uuid(const locker_json_document_t *doc, const locker_json_node_t *node, locker_uuid_t *out)
{
    char text[LOCKER_DOMAIN_UUID_TEXT];
    size_t length = 0u;
    if (node == NULL) {
        return false;
    }
    if (!locker_domain_json_text(doc, node, text, sizeof(text), &length)) {
        return false;
    }
    return domain_uuid_from_text(text, length, out);
}

bool domain_json_read_u64(const locker_json_document_t *doc, const locker_json_node_t *node, uint64_t *out)
{
    char text[LOCKER_DOMAIN_U64_TEXT];
    size_t length = 0u;
    if (node == NULL) {
        return false;
    }
    if (!locker_domain_json_text(doc, node, text, sizeof(text), &length)) {
        return false;
    }
    return locker_u64_parse_decimal(text, length, out) == LOCKER_STATUS_OK;
}

bool domain_json_read_text(const locker_json_document_t *doc, const locker_json_node_t *node, char *out,
                      size_t out_size)
{
    size_t length = 0u;
    if (node == NULL || out == NULL || out_size == 0u) {
        return false;
    }
    return locker_domain_json_text(doc, node, out, out_size, &length);
}

bool domain_json_read_int(const locker_json_document_t *doc, const locker_json_node_t *node, int64_t *out)
{
    if (node == NULL || out == NULL) {
        return false;
    }
    return locker_domain_json_int64(doc, node, out);
}

bool domain_json_read_bool(const locker_json_document_t *doc, const locker_json_node_t *node, bool *out)
{
    if (node == NULL || out == NULL) {
        return false;
    }
    return locker_domain_json_boolean(doc, node, out);
}

bool domain_json_read_enum_state(const locker_json_document_t *doc, const locker_json_node_t *node, uint8_t *out)
{
    char text[32];
    size_t length = 0u;
    if (node == NULL) {
        return false;
    }
    if (!locker_domain_json_text(doc, node, text, sizeof(text), &length)) {
        return false;
    }
    return domain_state_from_name(text, length, out);
}

bool domain_json_read_enum_kind(const locker_json_document_t *doc, const locker_json_node_t *node, uint8_t *out)
{
    char text[16];
    size_t length = 0u;
    if (node == NULL) {
        return false;
    }
    if (!locker_domain_json_text(doc, node, text, sizeof(text), &length)) {
        return false;
    }
    return domain_kind_from_name(text, length, out);
}

bool domain_json_read_door(const locker_json_document_t *doc, const locker_json_node_t *node, uint8_t *out)
{
    char text[16];
    size_t length = 0u;
    if (node == NULL) {
        return false;
    }
    if (!locker_domain_json_text(doc, node, text, sizeof(text), &length)) {
        return false;
    }
    if (length == 7u && memcmp(text, "UNKNOWN", 7) == 0) {
        *out = LOCKER_DOOR_STATE_UNKNOWN;
        return true;
    }
    if (length == 6u && memcmp(text, "CLOSED", 6) == 0) {
        *out = LOCKER_DOOR_STATE_CLOSED;
        return true;
    }
    if (length == 4u && memcmp(text, "OPEN", 4) == 0) {
        *out = LOCKER_DOOR_STATE_OPEN;
        return true;
    }
    return false;
}

bool domain_json_read_item(const locker_json_document_t *doc, const locker_json_node_t *node, uint8_t *out)
{
    char text[16];
    size_t length = 0u;
    if (node == NULL) {
        return false;
    }
    if (!locker_domain_json_text(doc, node, text, sizeof(text), &length)) {
        return false;
    }
    if (length == 7u && memcmp(text, "UNKNOWN", 7) == 0) {
        *out = LOCKER_ITEM_STATE_UNKNOWN;
        return true;
    }
    if (length == 6u && memcmp(text, "ABSENT", 6) == 0) {
        *out = LOCKER_ITEM_STATE_ABSENT;
        return true;
    }
    if (length == 7u && memcmp(text, "PRESENT", 7) == 0) {
        *out = LOCKER_ITEM_STATE_PRESENT;
        return true;
    }
    return false;
}

bool domain_json_read_time_quality(const locker_json_document_t *doc, const locker_json_node_t *node, uint8_t *out)
{
    char text[16];
    size_t length = 0u;
    if (node == NULL) {
        return false;
    }
    if (!locker_domain_json_text(doc, node, text, sizeof(text), &length)) {
        return false;
    }
    if (length == 8u && memcmp(text, "UNSYNCED", 8) == 0) {
        *out = LOCKER_TIME_QUALITY_UNSYNCED;
        return true;
    }
    if (length == 9u && memcmp(text, "ESTIMATED", 9) == 0) {
        *out = LOCKER_TIME_QUALITY_ESTIMATED;
        return true;
    }
    if (length == 11u && memcmp(text, "HOST_SYNCED", 11) == 0) {
        *out = LOCKER_TIME_QUALITY_HOST_SYNCED;
        return true;
    }
    return false;
}

/* ---------- EventKey / PhysicalSnapshot ---------- */

static bool parse_event_key(const locker_json_document_t *doc, const locker_json_node_t *node,
                            locker_event_key_t *out)
{
    if (doc == NULL || node == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    const locker_json_node_t *device = domain_json_member(doc, node, "device_id");
    if (!domain_json_read_text(doc, device, out->device_id, sizeof(out->device_id))) {
        return false;
    }
    if (strlen(out->device_id) != 24u) {
        return false;
    }
    if (!domain_json_read_uuid(doc, domain_json_member(doc, node, "journal_epoch"), &out->journal_epoch)) {
        return false;
    }
    if (!domain_json_read_u64(doc, domain_json_member(doc, node, "boot_id"), &out->boot_id)) {
        return false;
    }
    if (!domain_json_read_u64(doc, domain_json_member(doc, node, "event_seq"), &out->event_seq)) {
        return false;
    }
    return true;
}

static bool parse_physical(const locker_json_document_t *doc, const locker_json_node_t *node,
                           domain_physical_t *out)
{
    if (doc == NULL || node == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!domain_json_read_text(doc, domain_json_member(doc, node, "device_id"), out->device_id, sizeof(out->device_id))) {
        return false;
    }
    if (!domain_json_read_uuid(doc, domain_json_member(doc, node, "journal_epoch"), &out->journal_epoch)) {
        return false;
    }
    if (!domain_json_read_u64(doc, domain_json_member(doc, node, "boot_id"), &out->boot_id)) {
        return false;
    }
    if (!domain_json_read_u64(doc, domain_json_member(doc, node, "physical_revision"), &out->physical_revision)) {
        return false;
    }
    if (!domain_json_read_door(doc, domain_json_member(doc, node, "door"), &out->door)) {
        return false;
    }
    if (!domain_json_read_item(doc, domain_json_member(doc, node, "item"), &out->item)) {
        return false;
    }
    const locker_json_node_t *supply = domain_json_member(doc, node, "supply_mv");
    if (supply != NULL && supply->type != LOCKER_JSON_NULL) {
        int64_t value = 0;
        if (!domain_json_read_int(doc, supply, &value) || value < 0 || value > 100000) {
            return false;
        }
        out->supply_valid = true;
        out->supply_mv = (uint32_t)value;
    }
    int64_t age = 0;
    if (!domain_json_read_int(doc, domain_json_member(doc, node, "observed_age_ms"), &age) || age < 0) {
        return false;
    }
    if (!domain_json_read_time_quality(doc, domain_json_member(doc, node, "time_quality"), &out->time_quality)) {
        return false;
    }
    if (!domain_json_read_bool(doc, domain_json_member(doc, node, "continuity_ok"), &out->continuity_ok)) {
        return false;
    }
    if (!domain_json_read_u64(doc, domain_json_member(doc, node, "dropped_events"), &out->dropped_events)) {
        return false;
    }
    if (!parse_event_key(doc, domain_json_member(doc, node, "durable_cursor"), &out->durable_cursor)) {
        return false;
    }
    out->valid = true;
    return true;
}

/* ---------- OperationRecord 反序列化 ---------- */

static bool parse_evidence(const locker_json_document_t *doc, const locker_json_node_t *node,
                           domain_operation_t *out)
{
    if (!domain_json_read_bool(doc, domain_json_member(doc, node, "history_complete"), &out->history_complete)) {
        return false;
    }
    if (!domain_json_read_bool(doc, domain_json_member(doc, node, "door_open_seen"), &out->door_open_seen)) {
        return false;
    }
    if (!domain_json_read_bool(doc, domain_json_member(doc, node, "item_transition_seen"), &out->item_transition_seen)) {
        return false;
    }
    if (!domain_json_read_bool(doc, domain_json_member(doc, node, "manual_resolution"), &out->manual_resolution)) {
        return false;
    }
    const locker_json_node_t *physical = domain_json_member(doc, node, "physical");
    if (physical != NULL && physical->type != LOCKER_JSON_NULL) {
        if (!parse_physical(doc, physical, &out->physical)) {
            return false;
        }
        out->has_physical = true;
    }
    const locker_json_node_t *sources = domain_json_member(doc, node, "source_events");
    if (sources == NULL || sources->type != LOCKER_JSON_ARRAY) {
        return false;
    }
    out->source_event_count = 0u;
    for (uint32_t index = 0u; index < sources->count; index += 1u) {
        const locker_json_node_t *item = locker_domain_json_at(doc, sources, index);
        locker_event_key_t key;
        if (!parse_event_key(doc, item, &key)) {
            return false;
        }
        if (out->source_event_count < LOCKER_DOMAIN_SOURCE_EVENTS) {
            out->source_events[out->source_event_count] = key;
            out->source_event_count += 1u;
        } else {
            out->history_complete = false;
        }
    }
    return true;
}

static bool parse_operation_object(const locker_json_document_t *doc, const locker_json_node_t *node,
                                   domain_operation_t *out)
{
    if (!domain_json_read_uuid(doc, domain_json_member(doc, node, "id"), &out->id)) {
        return false;
    }
    if (!domain_json_read_uuid(doc, domain_json_member(doc, node, "tool_id"), &out->tool_id)) {
        return false;
    }
    if (!domain_json_read_uuid(doc, domain_json_member(doc, node, "actor_id"), &out->actor_id)) {
        return false;
    }
    if (!domain_json_read_enum_kind(doc, domain_json_member(doc, node, "kind"), &out->kind)) {
        return false;
    }
    if (!domain_json_read_enum_state(doc, domain_json_member(doc, node, "state"), &out->state)) {
        return false;
    }
    if (!domain_json_read_u64(doc, domain_json_member(doc, node, "operation_revision"), &out->revision)) {
        return false;
    }
    const locker_json_node_t *command = domain_json_member(doc, node, "command_id");
    out->has_command = false;
    if (command != NULL && command->type != LOCKER_JSON_NULL) {
        if (!domain_json_read_uuid(doc, command, &out->command_id)) {
            return false;
        }
        out->has_command = true;
    }
    const locker_json_node_t *reason = domain_json_member(doc, node, "reason_code");
    out->reason_code[0] = '\0';
    if (reason != NULL && reason->type != LOCKER_JSON_NULL) {
        if (!domain_json_read_text(doc, reason, out->reason_code, sizeof(out->reason_code))) {
            return false;
        }
    }
    if (!domain_json_read_text(doc, domain_json_member(doc, node, "created_at"), out->created_at, sizeof(out->created_at))) {
        return false;
    }
    if (!domain_json_read_text(doc, domain_json_member(doc, node, "updated_at"), out->updated_at, sizeof(out->updated_at))) {
        return false;
    }
    /*
     * created_ms/updated_ms 是运行时字段，文档里只有墙钟文本；必须在这里还原成
     * 毫秒值，否则重启恢复时 updated_ms 恒为 0，deadline 会退化成「now + TTL」，
     * 把早已过期的申请当成刚创建（domain_core.c:143 与 :484 都直接用它算截止点）。
     */
    if (!locker_domain_time_parse_utc(out->created_at, strlen(out->created_at), &out->created_ms)) {
        return false;
    }
    if (!locker_domain_time_parse_utc(out->updated_at, strlen(out->updated_at), &out->updated_ms)) {
        return false;
    }
    return parse_evidence(doc, domain_json_member(doc, node, "evidence"), out);
}

static bool parse_operation_record(const locker_json_document_t *doc, const locker_json_node_t *root,
                                   domain_operation_t *out)
{
    if (doc == NULL || root == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!parse_operation_object(doc, domain_json_member(doc, root, "operation"), out)) {
        return false;
    }
    if (!parse_physical(doc, domain_json_member(doc, root, "baseline"), &out->baseline)) {
        return false;
    }
    out->has_baseline = true;
    const locker_json_node_t *loan = domain_json_member(doc, root, "related_loan_id");
    out->has_loan = false;
    if (loan != NULL && loan->type != LOCKER_JSON_NULL) {
        if (!domain_json_read_uuid(doc, loan, &out->related_loan_id)) {
            return false;
        }
        out->has_loan = true;
    }
    const locker_json_node_t *proof = domain_json_member(doc, root, "proof_source");
    out->proof_source = 0u;
    if (proof != NULL && proof->type != LOCKER_JSON_NULL) {
        char text[16];
        size_t length = 0u;
        if (!locker_domain_json_text(doc, proof, text, sizeof(text), &length)) {
            return false;
        }
        if (length == 4u && memcmp(text, "CARD", 4) == 0) {
            out->proof_source = 1u;
        } else if (length == 8u && memcmp(text, "LOCAL_UI", 8) == 0) {
            out->proof_source = 2u;
        } else {
            return false;
        }
    }
    if (!domain_json_read_bool(doc, domain_json_member(doc, root, "proof_consumed"), &out->proof_consumed)) {
        return false;
    }
    out->used = true;
    return true;
}

bool domain_operation_from_record(const locker_json_document_t *doc, const locker_json_node_t *root,
                                 domain_operation_t *out)
{
    return parse_operation_record(doc, root, out);
}

bool domain_import_cursor_from_record(const locker_json_document_t *doc, const locker_json_node_t *root,
                                      locker_core_t *core)
{
    if (doc == NULL || root == NULL || core == NULL) {
        return false;
    }
    char device_id[25];
    if (!domain_json_read_text(doc, domain_json_member(doc, root, "device_id"), device_id, sizeof(device_id))) {
        return false;
    }
    locker_uuid_t epoch;
    if (!domain_json_read_uuid(doc, domain_json_member(doc, root, "journal_epoch"), &epoch)) {
        return false;
    }
    locker_event_key_t through;
    if (!parse_event_key(doc, domain_json_member(doc, root, "through"), &through)) {
        return false;
    }
    bool continuity = false;
    if (!domain_json_read_bool(doc, domain_json_member(doc, root, "continuity_ok"), &continuity)) {
        return false;
    }
    if (!core->import_cursor_valid) {
        core->import_cursor = through;
        core->import_cursor_valid = true;
        core->import_continuity = continuity;
        core->cursor.valid = true;
        core->cursor.journal_epoch = epoch;
        memcpy(core->cursor.device_id, device_id, sizeof(core->cursor.device_id));
        core->cursor.through = through;
        core->cursor.continuity_ok = continuity;
        core->ack_cursor = through;
        core->ack_cursor_valid = true;
    }
    return true;
}

bool domain_json_read_enum_device_mode(const locker_json_document_t *doc, const locker_json_node_t *node,
                                      uint8_t *out)
{
    char text[16];
    size_t length = 0u;
    if (node == NULL) {
        return false;
    }
    if (!locker_domain_json_text(doc, node, text, sizeof(text), &length)) {
        return false;
    }
    static const char *const k_modes[] = {"OFFLINE", "RECONCILING", "READY", "DEGRADED"};
    for (uint8_t i = 0u; i < 4u; ++i) {
        if (strlen(k_modes[i]) == length && memcmp(k_modes[i], text, length) == 0) {
            *out = i;
            return true;
        }
    }
    return false;
}

locker_status_t domain_operation_load(locker_core_t *core, const locker_uuid_t *id, int slot,
                                      domain_operation_t *out)
{
    if (core == NULL || id == NULL || out == NULL || slot < 0 || slot > 1) {
        return LOCKER_STATUS_INVALID;
    }
    char key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(id, key, sizeof(key))) {
        return LOCKER_STATUS_INVALID;
    }
    domain_doc_t doc;
    locker_status_t status = domain_repo_doc(core, slot, "operations", key, &doc);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    if (!doc.found || doc.value == NULL) {
        return LOCKER_STATUS_NOT_READY;
    }
    if (!parse_operation_record(&doc.doc, doc.value, out)) {
        return LOCKER_STATUS_IO_ERROR;
    }
    /* 运行时计时字段不在文档里：用最近一次更新时间重建保守截止点。 */
    if (out->state == LOCKER_OPERATION_STATE_REQUESTED) {
        out->requested_deadline_ms = out->updated_ms + LOCKER_REQUEST_TTL_MS;
    } else if (out->state == LOCKER_OPERATION_STATE_WAIT_CONFIRM) {
        out->wait_confirm_deadline_ms = out->updated_ms + LOCKER_WAIT_CONFIRM_MS;
    }
    return LOCKER_STATUS_OK;
}

locker_status_t domain_persist_operation(locker_core_t *core, domain_operation_t *operation)
{
    if (core == NULL || operation == NULL || !operation->used) {
        return LOCKER_STATUS_INVALID;
    }
    char key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&operation->id, key, sizeof(key))) {
        return LOCKER_STATUS_INVALID;
    }
    domain_change_t change;
    domain_change_init(core, &change, core->revision, core->revision + 1u);
    locker_status_t status = domain_operation_persist(core, &change, operation);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    return domain_change_commit(core, &change);
}

/* ---------- principal 复检 ---------- */

static bool session_record_usable(locker_core_t *core, const locker_json_document_t *doc,
                                  const locker_json_node_t *root, const locker_principal_t *principal,
                                  uint64_t now_wall_ms)
{
    locker_uuid_t user_id;
    if (!domain_json_read_uuid(doc, domain_json_member(doc, root, "user_id"), &user_id)) {
        return false;
    }
    if (!locker_uuid_equal(&user_id, &principal->user_id)) {
        return false;
    }
    bool revoked = true;
    if (!domain_json_read_bool(doc, domain_json_member(doc, root, "revoked"), &revoked)) {
        return false;
    }
    if (revoked) {
        return false;
    }
    char expires[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    if (!domain_json_read_text(doc, domain_json_member(doc, root, "expires_at"), expires, sizeof(expires))) {
        return false;
    }
    uint64_t expires_ms = 0u;
    if (!locker_domain_time_parse_utc(expires, strlen(expires), &expires_ms)) {
        return false;
    }
    (void)core;
    return expires_ms > now_wall_ms;
}

static bool user_record_usable(const locker_json_document_t *doc, const locker_json_node_t *root,
                               const locker_principal_t *principal)
{
    const locker_json_node_t *user = domain_json_member(doc, root, "user");
    if (user == NULL) {
        return false;
    }
    locker_uuid_t id;
    if (!domain_json_read_uuid(doc, domain_json_member(doc, user, "id"), &id)) {
        return false;
    }
    if (!locker_uuid_equal(&id, &principal->user_id)) {
        return false;
    }
    bool enabled = false;
    if (!domain_json_read_bool(doc, domain_json_member(doc, user, "enabled"), &enabled) || !enabled) {
        return false;
    }
    char role[16];
    size_t length = 0u;
    if (!locker_domain_json_text(doc, domain_json_member(doc, user, "role"), role, sizeof(role), &length)) {
        return false;
    }
    uint8_t parsed = 0u;
    if (!domain_role_from_name(role, length, &parsed)) {
        return false;
    }
    return parsed == (uint8_t)principal->role;
}

locker_status_t domain_principal_check(locker_core_t *core, const locker_principal_t *principal)
{
    if (core == NULL || principal == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!principal->authenticated) {
        return LOCKER_STATUS_INVALID;
    }
    if (locker_uuid_is_nil(&principal->session_id) || locker_uuid_is_nil(&principal->user_id)) {
        return LOCKER_STATUS_INVALID;
    }
    if (principal->role != LOCKER_ROLE_USER && principal->role != LOCKER_ROLE_ADMIN) {
        return LOCKER_STATUS_INVALID;
    }
    uint64_t now_wall = 0u;
    if (locker_clock_port_wall_utc_ms(&core->clock, &now_wall) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_NOT_READY;
    }
    char key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&principal->session_id, key, sizeof(key))) {
        return LOCKER_STATUS_INVALID;
    }
    domain_doc_t session;
    locker_status_t status = domain_repo_doc(core, 0, "sessions", key, &session);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    if (!session.found || session.value == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!session_record_usable(core, &session.doc, session.value, principal, now_wall)) {
        return LOCKER_STATUS_INVALID;
    }
    if (!domain_uuid_to_text(&principal->user_id, key, sizeof(key))) {
        return LOCKER_STATUS_INVALID;
    }
    domain_doc_t user;
    status = domain_repo_doc(core, 1, "users", key, &user);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    if (!user.found || user.value == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!user_record_usable(&user.doc, user.value, principal)) {
        return LOCKER_STATUS_INVALID;
    }
    return LOCKER_STATUS_OK;
}

/* ---------- 公开投影 ---------- */

void domain_write_operation_projection(locker_json_writer_t *writer, locker_core_t *core,
                                       const domain_operation_t *operation)
{
    (void)core;
    domain_write_operation(writer, operation);
}

/* 工具可用性是派生投影，不由客户端断言：只有 READY、无占用、无有效借用时
   才是 AVAILABLE；REVIEW_REQUIRED 期间整格进 REVIEW，任何人不得再借。 */
static uint8_t core_tool_availability(const locker_core_t *core)
{
    if (core->phase != DOMAIN_PHASE_READY) {
        return LOCKER_TOOL_AVAILABILITY_UNAVAILABLE;
    }
    if (core->active.used) {
        if (core->active.state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED) {
            return LOCKER_TOOL_AVAILABILITY_REVIEW;
        }
        if (core->active.state == LOCKER_OPERATION_STATE_COMPLETED ||
            core->active.state == LOCKER_OPERATION_STATE_CANCELLED ||
            core->active.state == LOCKER_OPERATION_STATE_NO_CHANGE ||
            core->active.state == LOCKER_OPERATION_STATE_FAILED) {
            /* 终态不再占位。 */
        } else {
            return LOCKER_TOOL_AVAILABILITY_RESERVED;
        }
    }
    if (core->loan_open) {
        return LOCKER_TOOL_AVAILABILITY_BORROWED;
    }
    return LOCKER_TOOL_AVAILABILITY_AVAILABLE;
}

uint8_t domain_tool_availability(const locker_core_t *core)
{
    return core_tool_availability(core);
}

void domain_write_tool_projection(locker_json_writer_t *writer, locker_core_t *core)
{
    if (core == NULL || !core->tool_valid) {
        locker_domain_json_null(writer);
        return;
    }
    core->tool_availability = core_tool_availability(core);
    domain_write_tool(writer, &core->tool_id, core->tool_name, core->tool_revision,
                      core->tool_availability);
}

uint8_t domain_core_device_mode(const locker_core_t *core)
{
    if (core->phase == DOMAIN_PHASE_READY) {
        return LOCKER_DEVICE_MODE_READY;
    }
    if (core->phase == DOMAIN_PHASE_RECOVERING) {
        return LOCKER_DEVICE_MODE_RECONCILING;
    }
    return LOCKER_DEVICE_MODE_OFFLINE;
}

void domain_write_device_projection(locker_json_writer_t *writer, locker_core_t *core)
{
    const domain_physical_t *physical = core->device_snapshot_valid ? &core->physical : NULL;
    uint8_t mode = domain_core_device_mode(core);
    locker_domain_json_literal(writer, "{\"device_id\":");
    if (physical != NULL) {
        locker_domain_json_string(writer, physical->device_id);
    } else {
        locker_domain_json_string(writer, "000000000000000000000000");
    }
    locker_domain_json_literal(writer, ",\"cabinet_name\":");
    locker_domain_json_string(writer, core->cabinet_name);
    locker_domain_json_literal(writer, ",\"contract_version\":");
    locker_domain_json_string(writer, LOCKER_CONTRACT_VERSION);
    locker_domain_json_literal(writer, ",\"simulated\":");
    locker_domain_json_bool(writer, false);
    locker_domain_json_literal(writer, ",\"mode\":");
    locker_domain_json_string(writer, domain_mode_name(mode));
    locker_domain_json_literal(writer, ",\"health_reasons\":[]");
    locker_domain_json_literal(writer, ",\"ui_restart_count\":null,\"physical\":");
    if (physical != NULL) {
        domain_write_physical(writer, physical);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, "}");
}

/* ---------- 响应信封 ---------- */

void domain_response_begin(locker_json_writer_t *writer, char *buffer, size_t capacity)
{
    locker_domain_json_writer_init(writer, buffer, capacity);
    locker_domain_json_literal(writer, "{\"data\":");
}

bool domain_response_data_object(locker_json_writer_t *writer)
{
    if (writer == NULL || !locker_domain_json_writer_ok(writer)) {
        return false;
    }
    locker_domain_json_literal(writer, "{");
    return true;
}

void domain_response_end(locker_json_writer_t *writer, uint64_t revision, const locker_uuid_t *request_id)
{
    char text[LOCKER_DOMAIN_U64_TEXT];
    char uuid_text[LOCKER_DOMAIN_UUID_TEXT];
    if (writer == NULL || !locker_domain_json_writer_ok(writer)) {
        return;
    }
    locker_domain_json_literal(writer, "},\"snapshot_revision\":");
    if (locker_u64_format_decimal(revision, text, sizeof(text)) == LOCKER_STATUS_OK) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"request_id\":");
    if (request_id != NULL && domain_uuid_to_text(request_id, uuid_text, sizeof(uuid_text))) {
        locker_domain_json_string(writer, uuid_text);
    } else {
        locker_domain_json_string(writer, "00000000-0000-0000-0000-000000000000");
    }
    locker_domain_json_literal(writer, "}");
}

/* ---------- 事件发布 ---------- */

void domain_publish(locker_core_t *core, const locker_uuid_t *operation_id, const char *resource,
                    uint64_t revision)
{
    if (core == NULL || core->publisher == NULL) {
        return;
    }
    locker_resource_change_t change;
    memset(&change, 0, sizeof(change));
    if (operation_id != NULL) {
        if (!domain_uuid_to_text(operation_id, change.id, sizeof(change.id))) {
            return;
        }
    }
    change.snapshot_revision = revision;
    if (resource != NULL) {
        size_t length = strlen(resource);
        if (length >= sizeof(change.resource)) {
            return;
        }
        memcpy(change.resource, resource, length + 1u);
    }
    (void)locker_events_try_publish(core->publisher, &change);
}

/* ---------- 进度源样本 ---------- */

void domain_progress_publish(locker_core_t *core)
{
    if (core == NULL) {
        return;
    }
    core->progress_counter += 1u;
    uint64_t sampled = domain_now(core);
    pthread_mutex_lock(&core->progress_mutex);
    core->progress.instance_id = core->instance_id;
    core->progress.progress = core->progress_counter;
    core->progress.sampled_mono_ms = sampled;
    core->progress.observed_age_ms = 0u;
    core->progress.target = LOCKER_PROGRESS_TARGET_SERVICE;
    core->progress_valid = true;
    pthread_mutex_unlock(&core->progress_mutex);
}
