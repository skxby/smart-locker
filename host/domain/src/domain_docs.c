#define _POSIX_C_SOURCE 200809L

#include "domain_internal.h"

#include <string.h>

/* 文档构造：写入仓储的 value 必须逐字满足 internal-data.schema.json 的记录定义，
   additionalProperties 一律 false，所以这里的字段集合与顺序就是契约本身。

   注意 U64 的线上类型是**十进制字符串**（internal-data.schema.json 的 $defs.U64），
   只有 schema 里显式声明 integer 的字段（observed_age_ms / supply_mv / slot_id /
   flags / event_type / reason / response_status）才写裸数字。两者混用会被仓储的
   schema 校验器拒绝，因此这里用两个不同的入口区分。 */

static void write_u64_text(locker_json_writer_t *writer, uint64_t value)
{
    char text[LOCKER_DOMAIN_U64_TEXT];
    if (locker_u64_format_decimal(value, text, sizeof(text)) != LOCKER_STATUS_OK) {
        writer->failed = true;
        return;
    }
    locker_domain_json_string(writer, text);
}

void domain_write_event_key(locker_json_writer_t *writer, const locker_event_key_t *key)
{
    locker_domain_json_literal(writer, "{\"device_id\":");
    locker_domain_json_string(writer, key->device_id);
    locker_domain_json_literal(writer, ",\"journal_epoch\":");
    char epoch[LOCKER_DOMAIN_UUID_TEXT];
    if (domain_uuid_to_text(&key->journal_epoch, epoch, sizeof(epoch))) {
        locker_domain_json_string(writer, epoch);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"boot_id\":");
    write_u64_text(writer, key->boot_id);
    locker_domain_json_literal(writer, ",\"event_seq\":");
    write_u64_text(writer, key->event_seq);
    locker_domain_json_literal(writer, "}");
}

void domain_write_physical(locker_json_writer_t *writer, const domain_physical_t *physical)
{
    locker_domain_json_literal(writer, "{\"device_id\":");
    locker_domain_json_string(writer, physical->device_id);
    locker_domain_json_literal(writer, ",\"journal_epoch\":");
    char epoch[LOCKER_DOMAIN_UUID_TEXT];
    if (domain_uuid_to_text(&physical->journal_epoch, epoch, sizeof(epoch))) {
        locker_domain_json_string(writer, epoch);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"boot_id\":");
    write_u64_text(writer, physical->boot_id);
    locker_domain_json_literal(writer, ",\"physical_revision\":");
    write_u64_text(writer, physical->physical_revision);
    locker_domain_json_literal(writer, ",\"door\":");
    locker_domain_json_string(writer, domain_door_name(physical->door));
    locker_domain_json_literal(writer, ",\"item\":");
    locker_domain_json_string(writer, domain_item_name(physical->item));
    locker_domain_json_literal(writer, ",\"supply_mv\":");
    if (physical->supply_valid) {
        locker_domain_json_u64(writer, physical->supply_mv);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, ",\"observed_age_ms\":0");
    locker_domain_json_literal(writer, ",\"time_quality\":");
    locker_domain_json_string(writer, domain_time_quality_name(physical->time_quality));
    locker_domain_json_literal(writer, ",\"continuity_ok\":");
    locker_domain_json_bool(writer, physical->continuity_ok);
    locker_domain_json_literal(writer, ",\"dropped_events\":");
    write_u64_text(writer, physical->dropped_events);
    locker_domain_json_literal(writer, ",\"durable_cursor\":");
    domain_write_event_key(writer, &physical->durable_cursor);
    locker_domain_json_literal(writer, "}");
}

void domain_write_evidence(locker_json_writer_t *writer, const domain_operation_t *operation)
{
    locker_domain_json_literal(writer, "{\"history_complete\":");
    locker_domain_json_bool(writer, operation->history_complete);
    locker_domain_json_literal(writer, ",\"door_open_seen\":");
    locker_domain_json_bool(writer, operation->door_open_seen);
    locker_domain_json_literal(writer, ",\"item_transition_seen\":");
    locker_domain_json_bool(writer, operation->item_transition_seen);
    locker_domain_json_literal(writer, ",\"physical\":");
    if (operation->has_physical) {
        domain_write_physical(writer, &operation->physical);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, ",\"source_events\":[");
    uint32_t count = operation->source_event_count;
    if (count > LOCKER_DOMAIN_SOURCE_EVENTS) {
        count = LOCKER_DOMAIN_SOURCE_EVENTS;
    }
    for (uint32_t i = 0u; i < count; ++i) {
        if (i > 0u) {
            locker_domain_json_literal(writer, ",");
        }
        domain_write_event_key(writer, &operation->source_events[i]);
    }
    locker_domain_json_literal(writer, "],\"manual_resolution\":");
    locker_domain_json_bool(writer, operation->manual_resolution);
    locker_domain_json_literal(writer, "}");
}

void domain_write_operation(locker_json_writer_t *writer, const domain_operation_t *operation)
{
    char text[LOCKER_DOMAIN_UUID_TEXT];
    locker_domain_json_literal(writer, "{\"id\":");
    if (domain_uuid_to_text(&operation->id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"tool_id\":");
    if (domain_uuid_to_text(&operation->tool_id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"kind\":");
    locker_domain_json_string(writer, domain_kind_name(operation->kind));
    locker_domain_json_literal(writer, ",\"actor_id\":");
    if (domain_uuid_to_text(&operation->actor_id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"state\":");
    locker_domain_json_string(writer, domain_state_name(operation->state));
    locker_domain_json_literal(writer, ",\"operation_revision\":");
    write_u64_text(writer, operation->revision);
    locker_domain_json_literal(writer, ",\"command_id\":");
    if (operation->has_command && domain_uuid_to_text(&operation->command_id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, ",\"reason_code\":");
    if (operation->reason_code[0] != '\0') {
        locker_domain_json_string(writer, operation->reason_code);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, ",\"created_at\":");
    locker_domain_json_string(writer, operation->created_at);
    locker_domain_json_literal(writer, ",\"updated_at\":");
    locker_domain_json_string(writer, operation->updated_at);
    locker_domain_json_literal(writer, ",\"allowed_actions\":[");
    uint8_t actions[6];
    uint32_t count = 0u;
    domain_operation_allowed_actions(operation, actions, &count);
    for (uint32_t i = 0u; i < count; ++i) {
        if (i > 0u) {
            locker_domain_json_literal(writer, ",");
        }
        locker_domain_json_string(writer, domain_action_name(actions[i]));
    }
    locker_domain_json_literal(writer, "],\"evidence\":");
    domain_write_evidence(writer, operation);
    locker_domain_json_literal(writer, "}");
}

void domain_write_operation_record(locker_json_writer_t *writer, const domain_operation_t *operation,
                                  const locker_uuid_t *host_instance_id)
{
    char text[LOCKER_DOMAIN_UUID_TEXT];
    locker_domain_json_literal(writer, "{\"operation\":");
    domain_write_operation(writer, operation);
    locker_domain_json_literal(writer, ",\"baseline\":");
    if (operation->has_baseline) {
        domain_write_physical(writer, &operation->baseline);
    } else {
        /* schema 要求 baseline 非空；没有基线时不允许落库（调用方必须保证）。 */
        writer->failed = true;
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, ",\"related_loan_id\":");
    if (operation->has_loan && domain_uuid_to_text(&operation->related_loan_id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, ",\"host_instance_id\":");
    if (host_instance_id != NULL && domain_uuid_to_text(host_instance_id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        /* schema 要求 host_instance_id 非空 UUID；缺失时禁止落库。 */
        writer->failed = true;
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, ",\"proof_source\":");
    const char *proof = domain_proof_name(operation->proof_source);
    if (proof != NULL) {
        locker_domain_json_string(writer, proof);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, ",\"proof_consumed\":");
    locker_domain_json_bool(writer, operation->proof_consumed);
    locker_domain_json_literal(writer, "}");
}
void domain_write_outbox(locker_json_writer_t *writer, const domain_operation_t *operation,
                         const char *state, const char *attempted_at)
{
    char text[LOCKER_DOMAIN_UUID_TEXT];
    locker_domain_json_literal(writer, "{\"id\":");
    if (domain_uuid_to_text(&operation->outbox_id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"operation_id\":");
    if (domain_uuid_to_text(&operation->id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"session_id\":");
    if (domain_uuid_to_text(&operation->outbox_session_id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"journal_epoch\":");
    if (domain_uuid_to_text(&operation->outbox_journal_epoch, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"expected_boot_id\":");
    write_u64_text(writer, operation->outbox_boot_id);
    locker_domain_json_literal(writer, ",\"expected_physical_revision\":");
    write_u64_text(writer, operation->outbox_physical_revision);
    locker_domain_json_literal(writer, ",\"action_no\":");
    write_u64_text(writer, operation->action_no);
    locker_domain_json_literal(writer, ",\"not_after_mcu_ms\":");
    write_u64_text(writer, operation->outbox_not_after_ms);
    locker_domain_json_literal(writer, ",\"kind\":");
    locker_domain_json_string(writer, domain_kind_name(operation->kind));
    locker_domain_json_literal(writer, ",\"slot_id\":1");
    locker_domain_json_literal(writer, ",\"state\":");
    locker_domain_json_string(writer, (state != NULL) ? state : domain_outbox_state_name(operation->outbox_state));
    locker_domain_json_literal(writer, ",\"attempted_at\":");
    if (attempted_at != NULL && attempted_at[0] != '\0') {
        locker_domain_json_string(writer, attempted_at);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, "}");
}

void domain_write_audit(locker_json_writer_t *writer, const locker_uuid_t *id, const locker_uuid_t *actor,
                        const locker_uuid_t *operation, const char *type, const char *reason,
                        const char *occurred_at, bool has_occurred, const locker_event_key_t *source,
                        uint8_t time_quality)
{
    char text[LOCKER_DOMAIN_UUID_TEXT];
    locker_domain_json_literal(writer, "{\"id\":");
    if (domain_uuid_to_text(id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"actor_id\":");
    if (actor != NULL && domain_uuid_to_text(actor, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, ",\"operation_id\":");
    if (operation != NULL && domain_uuid_to_text(operation, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, ",\"type\":");
    locker_domain_json_string(writer, (type != NULL) ? type : "EVENT");
    locker_domain_json_literal(writer, ",\"reason\":");
    locker_domain_json_string(writer, (reason != NULL && reason[0] != '\0') ? reason : "-");
    locker_domain_json_literal(writer, ",\"occurred_at\":");
    if (has_occurred && occurred_at != NULL && occurred_at[0] != '\0') {
        locker_domain_json_string(writer, occurred_at);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, ",\"time_quality\":");
    locker_domain_json_string(writer, domain_time_quality_name(time_quality));
    locker_domain_json_literal(writer, ",\"source_event\":");
    if (source != NULL) {
        domain_write_event_key(writer, source);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, "}");
}

void domain_write_slot_guard(locker_json_writer_t *writer, const locker_uuid_t *operation_id)
{
    char text[LOCKER_DOMAIN_UUID_TEXT];
    locker_domain_json_literal(writer, "{\"slot_id\":1,\"operation_id\":");
    if (domain_uuid_to_text(operation_id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, "}");
}

void domain_write_import_cursor(locker_json_writer_t *writer, const locker_uuid_t *epoch,
                                const char *device_id, const locker_event_key_t *through,
                                bool continuity_ok)
{
    char text[LOCKER_DOMAIN_UUID_TEXT];
    locker_domain_json_literal(writer, "{\"device_id\":");
    locker_domain_json_string(writer, device_id);
    locker_domain_json_literal(writer, ",\"journal_epoch\":");
    if (domain_uuid_to_text(epoch, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"through\":");
    domain_write_event_key(writer, through);
    locker_domain_json_literal(writer, ",\"continuity_ok\":");
    locker_domain_json_bool(writer, continuity_ok);
    locker_domain_json_literal(writer, "}");
}

void domain_write_device_state(locker_json_writer_t *writer, const char *cabinet_name,
                               const char *contract_version, bool simulated, uint8_t mode,
                               const domain_physical_t *physical, uint64_t snapshot_revision,
                               const locker_uuid_t *active_command, bool has_active_command)
{
    char text[LOCKER_DOMAIN_UUID_TEXT];
    locker_domain_json_literal(writer, "{\"device\":{\"device_id\":");
    if (physical != NULL && physical->valid) {
        locker_domain_json_string(writer, physical->device_id);
    } else {
        locker_domain_json_string(writer, "000000000000000000000000");
    }
    locker_domain_json_literal(writer, ",\"cabinet_name\":");
    locker_domain_json_string(writer, cabinet_name);
    locker_domain_json_literal(writer, ",\"contract_version\":");
    locker_domain_json_string(writer, contract_version);
    locker_domain_json_literal(writer, ",\"simulated\":");
    locker_domain_json_bool(writer, simulated);
    locker_domain_json_literal(writer, ",\"mode\":");
    locker_domain_json_string(writer, domain_mode_name(mode));
    locker_domain_json_literal(writer, ",\"health_reasons\":[]");
    locker_domain_json_literal(writer, ",\"ui_restart_count\":null,\"physical\":");
    if (physical != NULL && physical->valid) {
        domain_write_physical(writer, physical);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, "},\"last_snapshot_revision\":");
    write_u64_text(writer, snapshot_revision);
    locker_domain_json_literal(writer, ",\"active_command_id\":");
    if (has_active_command && active_command != NULL &&
        domain_uuid_to_text(active_command, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, "}");
}

void domain_write_loan(locker_json_writer_t *writer, const locker_uuid_t *id, const locker_uuid_t *tool_id,
                       const locker_uuid_t *borrower_id, const locker_uuid_t *borrow_operation,
                       const locker_uuid_t *return_operation, bool has_return, const char *opened_at,
                       const char *closed_at, bool has_closed)
{
    char text[LOCKER_DOMAIN_UUID_TEXT];
    locker_domain_json_literal(writer, "{\"id\":");
    if (domain_uuid_to_text(id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"tool_id\":");
    if (domain_uuid_to_text(tool_id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"borrower_id\":");
    if (domain_uuid_to_text(borrower_id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"borrow_operation_id\":");
    if (domain_uuid_to_text(borrow_operation, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"return_operation_id\":");
    if (has_return && return_operation != NULL && domain_uuid_to_text(return_operation, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, ",\"opened_at\":");
    locker_domain_json_string(writer, opened_at);
    locker_domain_json_literal(writer, ",\"closed_at\":");
    if (has_closed && closed_at != NULL && closed_at[0] != '\0') {
        locker_domain_json_string(writer, closed_at);
    } else {
        locker_domain_json_null(writer);
    }
    locker_domain_json_literal(writer, "}");
}

void domain_write_tool(locker_json_writer_t *writer, const locker_uuid_t *id, const char *name,
                       uint64_t revision, uint8_t availability)
{
    char text[LOCKER_DOMAIN_UUID_TEXT];
    locker_domain_json_literal(writer, "{\"id\":");
    if (domain_uuid_to_text(id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"name\":");
    locker_domain_json_string(writer, name);
    locker_domain_json_literal(writer, ",\"slot_id\":1,\"availability\":");
    locker_domain_json_string(writer, domain_availability_name(availability));
    locker_domain_json_literal(writer, ",\"tool_revision\":");
    write_u64_text(writer, revision);
    locker_domain_json_literal(writer, "}");
}

void domain_write_tool_record(locker_json_writer_t *writer, const locker_uuid_t *id, const char *name,
                              uint64_t revision, bool enabled)
{
    char text[LOCKER_DOMAIN_UUID_TEXT];
    locker_domain_json_literal(writer, "{\"id\":");
    if (domain_uuid_to_text(id, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"name\":");
    locker_domain_json_string(writer, name);
    locker_domain_json_literal(writer, ",\"slot_id\":1,\"tool_revision\":");
    write_u64_text(writer, revision);
    locker_domain_json_literal(writer, ",\"enabled\":");
    locker_domain_json_bool(writer, enabled);
    locker_domain_json_literal(writer, "}");
}

void domain_write_idempotency(locker_json_writer_t *writer, const locker_uuid_t *actor, const char *method,
                              const char *path, const locker_uuid_t *key, const char *request_hash,
                              int response_status, const char *response_json, const char *expires_at)
{
    char text[LOCKER_DOMAIN_UUID_TEXT];
    locker_domain_json_literal(writer, "{\"actor_id\":");
    if (domain_uuid_to_text(actor, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"method\":");
    locker_domain_json_string(writer, method);
    locker_domain_json_literal(writer, ",\"path\":");
    locker_domain_json_string(writer, path);
    locker_domain_json_literal(writer, ",\"key\":");
    if (domain_uuid_to_text(key, text, sizeof(text))) {
        locker_domain_json_string(writer, text);
    } else {
        writer->failed = true;
    }
    locker_domain_json_literal(writer, ",\"request_hash\":");
    locker_domain_json_string(writer, request_hash);
    locker_domain_json_literal(writer, ",\"response_status\":");
    locker_domain_json_int(writer, response_status);
    locker_domain_json_literal(writer, ",\"response_json\":");
    locker_domain_json_string(writer, (response_json != NULL) ? response_json : "{}");
    locker_domain_json_literal(writer, ",\"expires_at\":");
    locker_domain_json_string(writer, expires_at);
    locker_domain_json_literal(writer, "}");
}
