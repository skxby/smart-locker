/*
 * S1-03 UART v1 payload codec.
 *
 * Every field is written and read explicitly; no struct is copied to or from the
 * wire. Field order and widths follow contracts/uart-layouts.json and
 * docs/architecture/uart-v1.md:54-355.
 */

#include "locker/device/codec.h"

#include <string.h>

/* Field widths used to derive the layout lengths below. */
#define W_U8 1u
#define W_U16 2u
#define W_U32 4u
#define W_U64 8u
#define W_UUID 16u
#define W_CURSOR 16u
#define W_BYTES10 10u
#define W_BYTES12 12u
#define W_EVENT96 96u

#define REQUEST_PREFIX_SIZE W_UUID
#define RESPONSE_PREFIX_SIZE (W_UUID + W_U16)

/* Request body lengths (excluding the 16-byte request prefix). */
#define HELLO_REQ_BODY (W_UUID + W_UUID + W_U8)
#define SNAPSHOT_REQ_BODY 0u
#define FETCH_REQ_BODY (W_UUID + W_CURSOR + W_U8)
#define ACK_REQ_BODY (W_UUID + W_CURSOR)
#define RESUME_REQ_BODY (W_UUID + W_U64 + W_U64 + W_CURSOR)
#define DISARM_REQ_BODY W_U16
#define EXECUTE_REQ_BODY \
    (W_UUID + W_U64 + W_U64 + W_U64 + W_U64 + W_UUID + W_UUID + W_U16 + W_U8)
#define QUERY_REQ_BODY (W_UUID + W_UUID)
#define HEALTH_REQ_BODY (W_UUID + W_UUID + W_U64 + W_U64 + W_U32 + W_U32 + W_U8)
#define ANCHOR_REQ_BODY (W_U32 + W_U64 + W_U64)
#define RELEASE_REQ_BODY (W_UUID + W_U64 + W_U64 + W_UUID + W_UUID)

/* Response body lengths (excluding the 18-byte response prefix). */
#define HELLO_RSP_BODY \
    (W_BYTES12 + W_UUID + W_U64 + W_U8 + W_U8 + W_U8 + W_U32 + W_U8 + W_U8 + W_U64)
#define SNAPSHOT_RSP_BODY                                                          \
    (W_UUID + W_U64 + W_U64 + W_U64 + W_U8 + W_U8 + W_U32 + W_U16 + W_U64 +         \
     W_CURSOR + W_CURSOR + W_CURSOR + W_UUID + W_UUID + W_U8 + W_UUID + W_U64 +     \
     W_U64 + W_U8 + W_U8)
#define FETCH_RSP_BODY_FIXED (W_UUID + W_CURSOR + W_CURSOR + W_CURSOR + W_U8 + W_U8 + W_U8)
#define ACK_RSP_BODY W_CURSOR
#define RESUME_RSP_BODY (W_U64 + W_U8)
#define DISARM_RSP_BODY (W_U8 + W_U64)
#define COMMAND_RSP_BODY (W_UUID + W_UUID + W_U8 + W_U64 + W_UUID + W_U64 + W_U64 + W_U16)
#define HEALTH_RSP_BODY (W_U64 + W_U8)
#define ANCHOR_RSP_BODY (W_U32 + W_U64 + W_U64 + W_U64)
#define RELEASE_RSP_BODY (W_U8 + W_U64)

#define MAX_EVENT_COUNT 4u

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t length;
    bool failed;
} writer_t;

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
} reader_t;

static void put_u8(writer_t *w, uint8_t value)
{
    if (w->failed || w->length + 1u > w->capacity) {
        w->failed = true;
        return;
    }
    w->data[w->length++] = value;
}

static void put_u16(writer_t *w, uint16_t value)
{
    put_u8(w, (uint8_t)(value & 0xffu));
    put_u8(w, (uint8_t)((value >> 8) & 0xffu));
}

static void put_u32(writer_t *w, uint32_t value)
{
    put_u16(w, (uint16_t)(value & 0xffffu));
    put_u16(w, (uint16_t)((value >> 16) & 0xffffu));
}

static void put_u64(writer_t *w, uint64_t value)
{
    put_u32(w, (uint32_t)(value & 0xffffffffu));
    put_u32(w, (uint32_t)((value >> 32) & 0xffffffffu));
}

static void put_bytes(writer_t *w, const uint8_t *data, size_t length)
{
    if (length == 0u) {
        return;
    }
    if (w->failed || w->length + length > w->capacity) {
        w->failed = true;
        return;
    }
    memcpy(&w->data[w->length], data, length);
    w->length += length;
}

static void put_uuid(writer_t *w, const locker_uuid_t *uuid)
{
    put_bytes(w, uuid->bytes, W_UUID);
}

static void put_cursor(writer_t *w, const locker_wire_cursor_t *cursor)
{
    put_u64(w, cursor->boot_id);
    put_u64(w, cursor->event_seq);
}

static bool get_u8(reader_t *r, uint8_t *out)
{
    if (r->offset + 1u > r->length) {
        return false;
    }
    *out = r->data[r->offset];
    r->offset += 1u;
    return true;
}

static bool get_u16(reader_t *r, uint16_t *out)
{
    uint8_t low = 0u;
    uint8_t high = 0u;
    if (!get_u8(r, &low) || !get_u8(r, &high)) {
        return false;
    }
    *out = (uint16_t)((uint16_t)low | (uint16_t)((uint16_t)high << 8));
    return true;
}

static bool get_u32(reader_t *r, uint32_t *out)
{
    uint16_t low = 0u;
    uint16_t high = 0u;
    if (!get_u16(r, &low) || !get_u16(r, &high)) {
        return false;
    }
    *out = (uint32_t)low | ((uint32_t)high << 16);
    return true;
}

static bool get_u64(reader_t *r, uint64_t *out)
{
    uint32_t low = 0u;
    uint32_t high = 0u;
    if (!get_u32(r, &low) || !get_u32(r, &high)) {
        return false;
    }
    *out = (uint64_t)low | ((uint64_t)high << 32);
    return true;
}

static bool get_bytes(reader_t *r, uint8_t *out, size_t length)
{
    if (r->offset + length > r->length) {
        return false;
    }
    if (length != 0u) {
        memcpy(out, &r->data[r->offset], length);
    }
    r->offset += length;
    return true;
}

static bool get_uuid(reader_t *r, locker_uuid_t *out)
{
    return get_bytes(r, out->bytes, W_UUID);
}

static bool get_cursor(reader_t *r, locker_wire_cursor_t *out)
{
    return get_u64(r, &out->boot_id) && get_u64(r, &out->event_seq);
}

static bool uuid_is_nil(const locker_uuid_t *uuid)
{
    size_t index;
    for (index = 0u; index < W_UUID; ++index) {
        if (uuid->bytes[index] != 0u) {
            return false;
        }
    }
    return true;
}

bool locker_wire_uuid_nil(const locker_uuid_t *uuid)
{
    return uuid == NULL ? true : uuid_is_nil(uuid);
}

bool locker_wire_cursor_valid(const locker_wire_cursor_t *cursor)
{
    if (cursor == NULL) {
        return false;
    }
    if (cursor->boot_id == 0u) {
        return cursor->event_seq == 0u;
    }
    return cursor->event_seq != 0u;
}

size_t locker_wire_request_payload_size(uint8_t type)
{
    switch (type) {
    case 1u:
        return REQUEST_PREFIX_SIZE + HELLO_REQ_BODY;
    case 2u:
        return REQUEST_PREFIX_SIZE + SNAPSHOT_REQ_BODY;
    case 3u:
        return REQUEST_PREFIX_SIZE + FETCH_REQ_BODY;
    case 4u:
        return REQUEST_PREFIX_SIZE + ACK_REQ_BODY;
    case 5u:
        return REQUEST_PREFIX_SIZE + RESUME_REQ_BODY;
    case 6u:
        return REQUEST_PREFIX_SIZE + DISARM_REQ_BODY;
    case 7u:
        return REQUEST_PREFIX_SIZE + EXECUTE_REQ_BODY;
    case 8u:
        return REQUEST_PREFIX_SIZE + QUERY_REQ_BODY;
    case 9u:
        return REQUEST_PREFIX_SIZE + HEALTH_REQ_BODY;
    case 10u:
        return REQUEST_PREFIX_SIZE + ANCHOR_REQ_BODY;
    case 11u:
        return REQUEST_PREFIX_SIZE + RELEASE_REQ_BODY;
    default:
        return 0u;
    }
}

size_t locker_wire_response_payload_size(uint8_t type, uint8_t event_count)
{
    if (event_count > MAX_EVENT_COUNT) {
        return 0u;
    }
    switch (type) {
    case 1u:
        return RESPONSE_PREFIX_SIZE + HELLO_RSP_BODY;
    case 2u:
        return RESPONSE_PREFIX_SIZE + SNAPSHOT_RSP_BODY;
    case 3u:
        return RESPONSE_PREFIX_SIZE + FETCH_RSP_BODY_FIXED + ((size_t)event_count * W_EVENT96);
    case 4u:
        return RESPONSE_PREFIX_SIZE + ACK_RSP_BODY;
    case 5u:
        return RESPONSE_PREFIX_SIZE + RESUME_RSP_BODY;
    case 6u:
        return RESPONSE_PREFIX_SIZE + DISARM_RSP_BODY;
    case 7u:
    case 8u:
        return RESPONSE_PREFIX_SIZE + COMMAND_RSP_BODY;
    case 9u:
        return RESPONSE_PREFIX_SIZE + HEALTH_RSP_BODY;
    case 10u:
        return RESPONSE_PREFIX_SIZE + ANCHOR_RSP_BODY;
    case 11u:
        return RESPONSE_PREFIX_SIZE + RELEASE_RSP_BODY;
    default:
        return 0u;
    }
}

/* ---- field validators ---- */

static bool enum_le(uint8_t value, uint8_t max_value)
{
    return value <= max_value;
}

uint16_t locker_wire_validate_event96(const locker_wire_event96_t *record)
{
    size_t index;

    if (record == NULL) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (record->boot_id == 0u || record->event_seq == 0u) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (record->event_type < 1u || record->event_type > 15u) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (record->reason > LOCKER_WIRE_RESULT_UNSUPPORTED) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (!enum_le(record->door, 2u) || !enum_le(record->item, 2u)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if ((record->flags & (uint16_t)~LOCKER_WIRE_FLAG_DEFINED_MASK) != 0u) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    switch (record->card_uid_len) {
    case 0u:
        break;
    case 4u:
    case 7u:
    case 10u:
        if (record->event_type != 8u) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        break;
    default:
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    for (index = record->card_uid_len; index < W_BYTES10; ++index) {
        if (record->card_uid[index] != 0u) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
    }
    if (record->reserved != 0u) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if ((record->flags & LOCKER_WIRE_FLAG_SUPPLY_VALID) != 0u &&
        record->supply_mv == LOCKER_WIRE_SUPPLY_UNKNOWN) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    return LOCKER_WIRE_RESULT_OK;
}

static uint16_t validate_snapshot_response(const locker_wire_get_snapshot_response_t *body)
{
    if (!enum_le(body->door, 2u) || !enum_le(body->item, 2u)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if ((body->flags & (uint16_t)~LOCKER_WIRE_FLAG_DEFINED_MASK) != 0u) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if ((body->flags & LOCKER_WIRE_FLAG_SUPPLY_VALID) != 0u &&
        body->supply_mv == LOCKER_WIRE_SUPPLY_UNKNOWN) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (!locker_wire_cursor_valid(&body->first_available) ||
        !locker_wire_cursor_valid(&body->last_committed) ||
        !locker_wire_cursor_valid(&body->ack_persisted)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (!enum_le(body->active_command_state, 5u)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (!enum_le(body->device_mode, 2u) || !enum_le(body->journal_state, 4u)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    return LOCKER_WIRE_RESULT_OK;
}

/* ---- encoders ---- */

static uint16_t encode_request_body(writer_t *w, uint8_t type,
                                    const locker_wire_request_union_t *request)
{
    switch (type) {
    case 1u:
        put_uuid(w, &request->hello.new_session_id);
        put_uuid(w, &request->hello.host_instance_id);
        put_u8(w, request->hello.requested_version);
        return LOCKER_WIRE_RESULT_OK;
    case 2u:
        return LOCKER_WIRE_RESULT_OK;
    case 3u:
        put_uuid(w, &request->fetch_events.journal_epoch);
        put_cursor(w, &request->fetch_events.after);
        put_u8(w, request->fetch_events.max_count);
        return LOCKER_WIRE_RESULT_OK;
    case 4u:
        put_uuid(w, &request->ack_persisted.journal_epoch);
        put_cursor(w, &request->ack_persisted.through);
        return LOCKER_WIRE_RESULT_OK;
    case 5u:
        put_uuid(w, &request->resume_ready.journal_epoch);
        put_u64(w, request->resume_ready.expected_boot_id);
        put_u64(w, request->resume_ready.expected_physical_revision);
        put_cursor(w, &request->resume_ready.persisted_through);
        return LOCKER_WIRE_RESULT_OK;
    case 6u:
        put_u16(w, request->disarm.reason);
        return LOCKER_WIRE_RESULT_OK;
    case 7u:
        put_uuid(w, &request->execute_unlock.journal_epoch);
        put_u64(w, request->execute_unlock.expected_boot_id);
        put_u64(w, request->execute_unlock.expected_physical_revision);
        put_u64(w, request->execute_unlock.action_no);
        put_u64(w, request->execute_unlock.not_after_mcu_ms);
        put_uuid(w, &request->execute_unlock.operation_id);
        put_uuid(w, &request->execute_unlock.command_id);
        put_u16(w, request->execute_unlock.slot_id);
        put_u8(w, request->execute_unlock.kind);
        return LOCKER_WIRE_RESULT_OK;
    case 8u:
        put_uuid(w, &request->query_command.journal_epoch);
        put_uuid(w, &request->query_command.command_id);
        return LOCKER_WIRE_RESULT_OK;
    case 9u:
        put_uuid(w, &request->host_health.host_instance_id);
        put_uuid(w, &request->host_health.ui_instance_id);
        put_u64(w, request->host_health.core_progress);
        put_u64(w, request->host_health.ui_progress);
        put_u32(w, request->host_health.core_observed_age_ms);
        put_u32(w, request->host_health.ui_observed_age_ms);
        put_u8(w, request->host_health.supervisor_state);
        return LOCKER_WIRE_RESULT_OK;
    case 10u:
        put_u32(w, request->time_anchor.anchor_id);
        put_u64(w, request->time_anchor.host_utc_ms);
        put_u64(w, request->time_anchor.host_monotonic_ms);
        return LOCKER_WIRE_RESULT_OK;
    case 11u:
        put_uuid(w, &request->release_context.journal_epoch);
        put_u64(w, request->release_context.expected_boot_id);
        put_u64(w, request->release_context.expected_physical_revision);
        put_uuid(w, &request->release_context.operation_id);
        put_uuid(w, &request->release_context.command_id);
        return LOCKER_WIRE_RESULT_OK;
    default:
        return LOCKER_WIRE_RESULT_UNSUPPORTED;
    }
}

static void put_event96(writer_t *w, const locker_wire_event96_t *record)
{
    put_u64(w, record->boot_id);
    put_u64(w, record->event_seq);
    put_u64(w, record->mono_ms);
    put_u16(w, record->event_type);
    put_u16(w, record->reason);
    put_uuid(w, &record->operation_id);
    put_uuid(w, &record->command_id);
    put_u8(w, record->door);
    put_u8(w, record->item);
    put_u16(w, record->flags);
    put_u32(w, record->supply_mv);
    put_u64(w, record->physical_revision);
    put_u64(w, record->detail);
    put_u8(w, record->card_uid_len);
    put_bytes(w, record->card_uid, W_BYTES10);
    put_u8(w, record->reserved);
}

static void get_event96(reader_t *r, locker_wire_event96_t *record)
{
    (void)get_u64(r, &record->boot_id);
    (void)get_u64(r, &record->event_seq);
    (void)get_u64(r, &record->mono_ms);
    (void)get_u16(r, &record->event_type);
    (void)get_u16(r, &record->reason);
    (void)get_uuid(r, &record->operation_id);
    (void)get_uuid(r, &record->command_id);
    (void)get_u8(r, &record->door);
    (void)get_u8(r, &record->item);
    (void)get_u16(r, &record->flags);
    (void)get_u32(r, &record->supply_mv);
    (void)get_u64(r, &record->physical_revision);
    (void)get_u64(r, &record->detail);
    (void)get_u8(r, &record->card_uid_len);
    (void)get_bytes(r, record->card_uid, W_BYTES10);
    (void)get_u8(r, &record->reserved);
}

static uint16_t encode_response_body(writer_t *w, uint8_t type,
                                     const locker_wire_response_union_t *response)
{
    switch (type) {
    case 1u:
        put_bytes(w, response->hello.device_uid, W_BYTES12);
        put_uuid(w, &response->hello.journal_epoch);
        put_u64(w, response->hello.boot_id);
        put_u8(w, response->hello.firmware_major);
        put_u8(w, response->hello.firmware_minor);
        put_u8(w, response->hello.firmware_patch);
        put_u32(w, response->hello.capabilities);
        put_u8(w, response->hello.device_mode);
        put_u8(w, response->hello.journal_state);
        put_u64(w, response->hello.action_watermark);
        return LOCKER_WIRE_RESULT_OK;
    case 2u:
        put_uuid(w, &response->get_snapshot.journal_epoch);
        put_u64(w, response->get_snapshot.boot_id);
        put_u64(w, response->get_snapshot.sampled_mono_ms);
        put_u64(w, response->get_snapshot.physical_revision);
        put_u8(w, response->get_snapshot.door);
        put_u8(w, response->get_snapshot.item);
        put_u32(w, response->get_snapshot.supply_mv);
        put_u16(w, response->get_snapshot.flags);
        put_u64(w, response->get_snapshot.dropped_events);
        put_cursor(w, &response->get_snapshot.first_available);
        put_cursor(w, &response->get_snapshot.last_committed);
        put_cursor(w, &response->get_snapshot.ack_persisted);
        put_uuid(w, &response->get_snapshot.active_operation_id);
        put_uuid(w, &response->get_snapshot.active_command_id);
        put_u8(w, response->get_snapshot.active_command_state);
        put_uuid(w, &response->get_snapshot.active_session_id);
        put_u64(w, response->get_snapshot.active_action_no);
        put_u64(w, response->get_snapshot.action_watermark);
        put_u8(w, response->get_snapshot.device_mode);
        put_u8(w, response->get_snapshot.journal_state);
        return LOCKER_WIRE_RESULT_OK;
    case 3u: {
        uint8_t index;
        if (response->fetch_events.event_count > MAX_EVENT_COUNT) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        put_uuid(w, &response->fetch_events.journal_epoch);
        put_cursor(w, &response->fetch_events.first_available);
        put_cursor(w, &response->fetch_events.last_committed);
        put_cursor(w, &response->fetch_events.next_after);
        put_u8(w, response->fetch_events.event_count);
        put_u8(w, response->fetch_events.has_more);
        put_u8(w, response->fetch_events.gap_before_page);
        for (index = 0u; index < response->fetch_events.event_count; ++index) {
            put_event96(w, &response->fetch_events.events[index]);
        }
        return LOCKER_WIRE_RESULT_OK;
    }
    case 4u:
        put_cursor(w, &response->ack_persisted.ack_persisted);
        return LOCKER_WIRE_RESULT_OK;
    case 5u:
        put_u64(w, response->resume_ready.physical_revision);
        put_u8(w, response->resume_ready.device_mode);
        return LOCKER_WIRE_RESULT_OK;
    case 6u:
        put_u8(w, response->disarm.device_mode);
        put_u64(w, response->disarm.physical_revision);
        return LOCKER_WIRE_RESULT_OK;
    case 7u:
        put_uuid(w, &response->execute_unlock.operation_id);
        put_uuid(w, &response->execute_unlock.command_id);
        put_u8(w, response->execute_unlock.command_state);
        put_u64(w, response->execute_unlock.action_no);
        put_uuid(w, &response->execute_unlock.command_session_id);
        put_u64(w, response->execute_unlock.command_boot_id);
        put_u64(w, response->execute_unlock.physical_revision);
        put_u16(w, response->execute_unlock.reason);
        return LOCKER_WIRE_RESULT_OK;
    case 8u:
        put_uuid(w, &response->query_command.operation_id);
        put_uuid(w, &response->query_command.command_id);
        put_u8(w, response->query_command.command_state);
        put_u64(w, response->query_command.action_no);
        put_uuid(w, &response->query_command.command_session_id);
        put_u64(w, response->query_command.command_boot_id);
        put_u64(w, response->query_command.physical_revision);
        put_u16(w, response->query_command.reason);
        return LOCKER_WIRE_RESULT_OK;
    case 9u:
        put_u64(w, response->host_health.received_mono_ms);
        put_u8(w, response->host_health.device_mode);
        return LOCKER_WIRE_RESULT_OK;
    case 10u:
        put_u32(w, response->time_anchor.anchor_id);
        put_u64(w, response->time_anchor.boot_id);
        put_u64(w, response->time_anchor.mcu_receive_mono_ms);
        put_u64(w, response->time_anchor.mcu_send_mono_ms);
        return LOCKER_WIRE_RESULT_OK;
    case 11u:
        put_u8(w, response->release_context.already_released);
        put_u64(w, response->release_context.physical_revision);
        return LOCKER_WIRE_RESULT_OK;
    default:
        return LOCKER_WIRE_RESULT_UNSUPPORTED;
    }
}

/* Writes prefix+body into a temporary payload buffer, then frames it. */
static locker_status_t emit_payload(uint8_t type, uint32_t request_no, const writer_t *payload,
                                    uint8_t *out, size_t out_size, size_t *out_length)
{
    if (payload->failed) {
        return LOCKER_STATUS_INVALID;
    }
    return locker_wire_encode_frame(type, request_no, payload->data, payload->length, out, out_size,
                                    out_length);
}

locker_status_t locker_wire_encode_request(uint8_t type, uint32_t request_no,
                                           const locker_wire_request_union_t *request,
                                           uint8_t *out, size_t out_size, size_t *out_length)
{
    uint8_t buffer[LOCKER_WIRE_MAX_PAYLOAD];
    writer_t w;
    uint16_t result;

    if (request == NULL || !locker_wire_request_type_valid(type)) {
        return LOCKER_STATUS_INVALID;
    }
    w.data = buffer;
    w.capacity = sizeof(buffer);
    w.length = 0u;
    w.failed = false;

    if (type == 1u) {
        locker_uuid_t nil;
        memset(&nil, 0, sizeof(nil));
        put_uuid(&w, &nil);
    } else {
        put_uuid(&w, &request->hello.session_id);
    }
    result = encode_request_body(&w, type, request);
    if (result != LOCKER_WIRE_RESULT_OK) {
        return LOCKER_STATUS_INVALID;
    }
    if (w.length != locker_wire_request_payload_size(type)) {
        return LOCKER_STATUS_INVALID;
    }
    return emit_payload(type, request_no, &w, out, out_size, out_length);
}

uint16_t locker_wire_response_result(uint8_t type,
                                      const locker_wire_response_union_t *response)
{
    if (response == NULL) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    switch (type) {
    case 1u:
        return response->hello.result;
    case 2u:
        return response->get_snapshot.result;
    case 3u:
        return response->fetch_events.result;
    case 4u:
        return response->ack_persisted.result;
    case 5u:
        return response->resume_ready.result;
    case 6u:
        return response->disarm.result;
    case 7u:
        return response->execute_unlock.result;
    case 8u:
        return response->query_command.result;
    case 9u:
        return response->host_health.result;
    case 10u:
        return response->time_anchor.result;
    case 11u:
        return response->release_context.result;
    default:
        return LOCKER_WIRE_RESULT_UNSUPPORTED;
    }
}

static void set_response_result(uint8_t type, locker_wire_response_union_t *response,
                                uint16_t result)
{
    switch (type) {
    case 1u:
        response->hello.result = result;
        break;
    case 2u:
        response->get_snapshot.result = result;
        break;
    case 3u:
        response->fetch_events.result = result;
        break;
    case 4u:
        response->ack_persisted.result = result;
        break;
    case 5u:
        response->resume_ready.result = result;
        break;
    case 6u:
        response->disarm.result = result;
        break;
    case 7u:
        response->execute_unlock.result = result;
        break;
    case 8u:
        response->query_command.result = result;
        break;
    case 9u:
        response->host_health.result = result;
        break;
    case 10u:
        response->time_anchor.result = result;
        break;
    case 11u:
        response->release_context.result = result;
        break;
    default:
        break;
    }
}

locker_status_t locker_wire_encode_response(uint8_t type, uint32_t request_no,
                                            const locker_wire_response_union_t *response,
                                            uint8_t *out, size_t out_size, size_t *out_length)
{
    uint8_t buffer[LOCKER_WIRE_MAX_PAYLOAD];
    writer_t w;
    uint16_t result;
    uint8_t event_count = 0u;
    uint16_t dto_result;

    if (response == NULL || !locker_wire_request_type_valid(type)) {
        return LOCKER_STATUS_INVALID;
    }
    if (type == 3u) {
        event_count = response->fetch_events.event_count;
    }
    /* A non-zero result means the body must not be sent at all (uart-v1.md:28). */
    dto_result = locker_wire_response_result(type, response);
    if (dto_result != LOCKER_WIRE_RESULT_OK) {
        return locker_wire_encode_error_response(type, request_no, &response->hello.session_id,
                                                 dto_result, out, out_size, out_length);
    }
    w.data = buffer;
    w.capacity = sizeof(buffer);
    w.length = 0u;
    w.failed = false;

    put_uuid(&w, &response->hello.session_id);
    put_u16(&w, 0u);
    result = encode_response_body(&w, type, response);
    if (result != LOCKER_WIRE_RESULT_OK) {
        return LOCKER_STATUS_INVALID;
    }
    if (w.length != locker_wire_response_payload_size(type, event_count)) {
        return LOCKER_STATUS_INVALID;
    }
    return emit_payload((uint8_t)(type | LOCKER_WIRE_RESPONSE_FLAG), request_no, &w, out, out_size,
                        out_length);
}

locker_status_t locker_wire_encode_error_response(uint8_t type, uint32_t request_no,
                                                  const locker_uuid_t *session_id, uint16_t result,
                                                  uint8_t *out, size_t out_size,
                                                  size_t *out_length)
{
    uint8_t buffer[RESPONSE_PREFIX_SIZE];
    writer_t w;

    if (session_id == NULL || type == 0u || type == LOCKER_WIRE_DEVICE_EVENT ||
        (type & LOCKER_WIRE_RESPONSE_FLAG) != 0u || request_no == 0u ||
        result == 0u) {
        return LOCKER_STATUS_INVALID;
    }
    w.data = buffer;
    w.capacity = sizeof(buffer);
    w.length = 0u;
    w.failed = false;
    put_uuid(&w, session_id);
    put_u16(&w, result);
    if (!locker_wire_request_type_valid(type)) {
        uint16_t crc;
        writer_t frame;
        if (out == NULL || out_length == NULL) {
            return LOCKER_STATUS_INVALID;
        }
        if (out_size < locker_wire_frame_size(w.length)) {
            return LOCKER_STATUS_NO_SPACE;
        }
        /* Only errors may echo an unknown request type; normal encoders stay strict. */
        frame.data = out;
        frame.capacity = out_size;
        frame.length = 0u;
        frame.failed = false;
        put_u8(&frame, LOCKER_WIRE_MAGIC_FIRST);
        put_u8(&frame, LOCKER_WIRE_MAGIC_SECOND);
        put_u8(&frame, LOCKER_WIRE_VERSION);
        put_u8(&frame, (uint8_t)(type | LOCKER_WIRE_RESPONSE_FLAG));
        put_u16(&frame, (uint16_t)w.length);
        put_u32(&frame, request_no);
        put_bytes(&frame, buffer, w.length);
        crc = locker_wire_crc16(out + 2u, frame.length - 2u);
        put_u16(&frame, crc);
        *out_length = frame.length;
        return LOCKER_STATUS_OK;
    }
    return emit_payload((uint8_t)(type | LOCKER_WIRE_RESPONSE_FLAG), request_no, &w, out, out_size,
                        out_length);
}

locker_status_t locker_wire_encode_device_event(const locker_uuid_t *session_id,
                                                const locker_uuid_t *journal_epoch,
                                                const locker_wire_event96_t *record, uint8_t *out,
                                                size_t out_size, size_t *out_length)
{
    uint8_t buffer[LOCKER_WIRE_DEVICE_EVENT_PAYLOAD_SIZE];
    writer_t w;
    uint16_t result;

    if (session_id == NULL || journal_epoch == NULL || record == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    result = locker_wire_validate_event96(record);
    if (result != LOCKER_WIRE_RESULT_OK) {
        return LOCKER_STATUS_INVALID;
    }
    w.data = buffer;
    w.capacity = sizeof(buffer);
    w.length = 0u;
    w.failed = false;
    put_uuid(&w, session_id);
    put_uuid(&w, journal_epoch);
    put_event96(&w, record);
    if (w.length != LOCKER_WIRE_DEVICE_EVENT_PAYLOAD_SIZE) {
        return LOCKER_STATUS_INVALID;
    }
    return locker_wire_encode_frame(LOCKER_WIRE_DEVICE_EVENT, 0u, buffer, w.length, out, out_size,
                                    out_length);
}

/* ---- decoders ---- */

static uint16_t decode_request_body(reader_t *r, uint8_t type,
                                    locker_wire_request_union_t *request)
{
    switch (type) {
    case 1u: {
        if (!get_uuid(r, &request->hello.new_session_id) ||
            !get_uuid(r, &request->hello.host_instance_id) ||
            !get_u8(r, &request->hello.requested_version)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        /* HELLO carries the all-zero prefix; the body names the new session. */
        if (!uuid_is_nil(&request->hello.session_id)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (uuid_is_nil(&request->hello.new_session_id) ||
            uuid_is_nil(&request->hello.host_instance_id)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (request->hello.requested_version != LOCKER_WIRE_VERSION) {
            return LOCKER_WIRE_RESULT_BAD_VERSION;
        }
        return LOCKER_WIRE_RESULT_OK;
    }
    case 2u:
        return LOCKER_WIRE_RESULT_OK;
    case 3u:
        if (!get_uuid(r, &request->fetch_events.journal_epoch) ||
            !get_cursor(r, &request->fetch_events.after) ||
            !get_u8(r, &request->fetch_events.max_count)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (uuid_is_nil(&request->fetch_events.journal_epoch) ||
            !locker_wire_cursor_valid(&request->fetch_events.after)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (request->fetch_events.max_count < 1u || request->fetch_events.max_count > MAX_EVENT_COUNT) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    case 4u:
        if (!get_uuid(r, &request->ack_persisted.journal_epoch) ||
            !get_cursor(r, &request->ack_persisted.through)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (uuid_is_nil(&request->ack_persisted.journal_epoch) ||
            !locker_wire_cursor_valid(&request->ack_persisted.through)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    case 5u:
        if (!get_uuid(r, &request->resume_ready.journal_epoch) ||
            !get_u64(r, &request->resume_ready.expected_boot_id) ||
            !get_u64(r, &request->resume_ready.expected_physical_revision) ||
            !get_cursor(r, &request->resume_ready.persisted_through)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (uuid_is_nil(&request->resume_ready.journal_epoch) ||
            !locker_wire_cursor_valid(&request->resume_ready.persisted_through)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    case 6u:
        if (!get_u16(r, &request->disarm.reason)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (request->disarm.reason > LOCKER_WIRE_RESULT_UNSUPPORTED) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    case 7u:
        if (!get_uuid(r, &request->execute_unlock.journal_epoch) ||
            !get_u64(r, &request->execute_unlock.expected_boot_id) ||
            !get_u64(r, &request->execute_unlock.expected_physical_revision) ||
            !get_u64(r, &request->execute_unlock.action_no) ||
            !get_u64(r, &request->execute_unlock.not_after_mcu_ms) ||
            !get_uuid(r, &request->execute_unlock.operation_id) ||
            !get_uuid(r, &request->execute_unlock.command_id) ||
            !get_u16(r, &request->execute_unlock.slot_id) ||
            !get_u8(r, &request->execute_unlock.kind)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (uuid_is_nil(&request->execute_unlock.journal_epoch) ||
            uuid_is_nil(&request->execute_unlock.operation_id) ||
            uuid_is_nil(&request->execute_unlock.command_id)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (request->execute_unlock.slot_id != 1u) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (request->execute_unlock.kind != 1u && request->execute_unlock.kind != 2u) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (request->execute_unlock.action_no == 0u) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    case 8u:
        if (!get_uuid(r, &request->query_command.journal_epoch) ||
            !get_uuid(r, &request->query_command.command_id)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (uuid_is_nil(&request->query_command.journal_epoch) ||
            uuid_is_nil(&request->query_command.command_id)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    case 9u:
        if (!get_uuid(r, &request->host_health.host_instance_id) ||
            !get_uuid(r, &request->host_health.ui_instance_id) ||
            !get_u64(r, &request->host_health.core_progress) ||
            !get_u64(r, &request->host_health.ui_progress) ||
            !get_u32(r, &request->host_health.core_observed_age_ms) ||
            !get_u32(r, &request->host_health.ui_observed_age_ms) ||
            !get_u8(r, &request->host_health.supervisor_state)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (uuid_is_nil(&request->host_health.host_instance_id)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (request->host_health.supervisor_state > 3u) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (uuid_is_nil(&request->host_health.ui_instance_id) &&
            request->host_health.ui_observed_age_ms != LOCKER_WIRE_AGE_UNKNOWN) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    case 10u:
        if (!get_u32(r, &request->time_anchor.anchor_id) ||
            !get_u64(r, &request->time_anchor.host_utc_ms) ||
            !get_u64(r, &request->time_anchor.host_monotonic_ms)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    case 11u:
        if (!get_uuid(r, &request->release_context.journal_epoch) ||
            !get_u64(r, &request->release_context.expected_boot_id) ||
            !get_u64(r, &request->release_context.expected_physical_revision) ||
            !get_uuid(r, &request->release_context.operation_id) ||
            !get_uuid(r, &request->release_context.command_id)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (uuid_is_nil(&request->release_context.journal_epoch) ||
            uuid_is_nil(&request->release_context.operation_id) ||
            uuid_is_nil(&request->release_context.command_id)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    default:
        return LOCKER_WIRE_RESULT_UNSUPPORTED;
    }
}

static uint16_t decode_response_body(reader_t *r, uint8_t type,
                                     locker_wire_response_union_t *response)
{
    switch (type) {
    case 1u:
        if (!get_bytes(r, response->hello.device_uid, W_BYTES12) ||
            !get_uuid(r, &response->hello.journal_epoch) ||
            !get_u64(r, &response->hello.boot_id) ||
            !get_u8(r, &response->hello.firmware_major) ||
            !get_u8(r, &response->hello.firmware_minor) ||
            !get_u8(r, &response->hello.firmware_patch) ||
            !get_u32(r, &response->hello.capabilities) ||
            !get_u8(r, &response->hello.device_mode) ||
            !get_u8(r, &response->hello.journal_state) ||
            !get_u64(r, &response->hello.action_watermark)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (uuid_is_nil(&response->hello.session_id)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if ((response->hello.capabilities & ~0x1fu) != 0u) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (!enum_le(response->hello.device_mode, 2u) ||
            !enum_le(response->hello.journal_state, 4u)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    case 2u: {
        locker_wire_get_snapshot_response_t *body = &response->get_snapshot;
        if (!get_uuid(r, &body->journal_epoch) || !get_u64(r, &body->boot_id) ||
            !get_u64(r, &body->sampled_mono_ms) || !get_u64(r, &body->physical_revision) ||
            !get_u8(r, &body->door) || !get_u8(r, &body->item) ||
            !get_u32(r, &body->supply_mv) || !get_u16(r, &body->flags) ||
            !get_u64(r, &body->dropped_events) || !get_cursor(r, &body->first_available) ||
            !get_cursor(r, &body->last_committed) || !get_cursor(r, &body->ack_persisted) ||
            !get_uuid(r, &body->active_operation_id) ||
            !get_uuid(r, &body->active_command_id) || !get_u8(r, &body->active_command_state) ||
            !get_uuid(r, &body->active_session_id) || !get_u64(r, &body->active_action_no) ||
            !get_u64(r, &body->action_watermark) || !get_u8(r, &body->device_mode) ||
            !get_u8(r, &body->journal_state)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return validate_snapshot_response(body);
    }
    case 3u: {
        locker_wire_fetch_events_response_t *body = &response->fetch_events;
        uint8_t index;
        if (!get_uuid(r, &body->journal_epoch) || !get_cursor(r, &body->first_available) ||
            !get_cursor(r, &body->last_committed) || !get_cursor(r, &body->next_after) ||
            !get_u8(r, &body->event_count) || !get_u8(r, &body->has_more) ||
            !get_u8(r, &body->gap_before_page)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (body->event_count > MAX_EVENT_COUNT || body->has_more > 1u ||
            body->gap_before_page > 1u) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (!locker_wire_cursor_valid(&body->first_available) ||
            !locker_wire_cursor_valid(&body->last_committed) ||
            !locker_wire_cursor_valid(&body->next_after)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        for (index = 0u; index < body->event_count; ++index) {
            uint16_t result;
            get_event96(r, &body->events[index]);
            result = locker_wire_validate_event96(&body->events[index]);
            if (result != LOCKER_WIRE_RESULT_OK) {
                return result;
            }
        }
        return LOCKER_WIRE_RESULT_OK;
    }
    case 4u:
        if (!get_cursor(r, &response->ack_persisted.ack_persisted)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (!locker_wire_cursor_valid(&response->ack_persisted.ack_persisted)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    case 5u:
        if (!get_u64(r, &response->resume_ready.physical_revision) ||
            !get_u8(r, &response->resume_ready.device_mode)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (!enum_le(response->resume_ready.device_mode, 2u)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    case 6u:
        if (!get_u8(r, &response->disarm.device_mode) ||
            !get_u64(r, &response->disarm.physical_revision)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (!enum_le(response->disarm.device_mode, 2u)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    case 7u: {
        locker_wire_execute_unlock_response_t *body = &response->execute_unlock;
        if (!get_uuid(r, &body->operation_id) || !get_uuid(r, &body->command_id) ||
            !get_u8(r, &body->command_state) || !get_u64(r, &body->action_no) ||
            !get_uuid(r, &body->command_session_id) || !get_u64(r, &body->command_boot_id) ||
            !get_u64(r, &body->physical_revision) || !get_u16(r, &body->reason)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (uuid_is_nil(&body->command_id) || body->command_state > 5u ||
            body->reason > LOCKER_WIRE_RESULT_UNSUPPORTED) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    }
    case 8u: {
        locker_wire_query_command_response_t *body = &response->query_command;
        if (!get_uuid(r, &body->operation_id) || !get_uuid(r, &body->command_id) ||
            !get_u8(r, &body->command_state) || !get_u64(r, &body->action_no) ||
            !get_uuid(r, &body->command_session_id) || !get_u64(r, &body->command_boot_id) ||
            !get_u64(r, &body->physical_revision) || !get_u16(r, &body->reason)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (uuid_is_nil(&body->command_id) || body->command_state > 5u ||
            body->reason > LOCKER_WIRE_RESULT_UNSUPPORTED) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    }
    case 9u:
        if (!get_u64(r, &response->host_health.received_mono_ms) ||
            !get_u8(r, &response->host_health.device_mode)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (!enum_le(response->host_health.device_mode, 2u)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    case 10u:
        if (!get_u32(r, &response->time_anchor.anchor_id) ||
            !get_u64(r, &response->time_anchor.boot_id) ||
            !get_u64(r, &response->time_anchor.mcu_receive_mono_ms) ||
            !get_u64(r, &response->time_anchor.mcu_send_mono_ms)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    case 11u:
        if (!get_u8(r, &response->release_context.already_released) ||
            !get_u64(r, &response->release_context.physical_revision)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (response->release_context.already_released > 1u) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        return LOCKER_WIRE_RESULT_OK;
    default:
        return LOCKER_WIRE_RESULT_UNSUPPORTED;
    }
}

uint16_t locker_wire_decode_request(const locker_wire_frame_t *frame,
                                    locker_wire_request_union_t *out_request)
{
    reader_t r;
    uint8_t type;

    if (frame == NULL || out_request == NULL || frame->request_no == 0u) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (frame->version != LOCKER_WIRE_VERSION) {
        return LOCKER_WIRE_RESULT_BAD_VERSION;
    }
    type = frame->type;
    if (!locker_wire_request_type_valid(type)) {
        return LOCKER_WIRE_RESULT_UNSUPPORTED;
    }
    if (frame->payload_len != locker_wire_request_payload_size(type)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    memset(out_request, 0, sizeof(*out_request));
    r.data = frame->payload;
    r.length = frame->payload_len;
    r.offset = 0u;
    if (!get_uuid(&r, &out_request->hello.session_id)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    return decode_request_body(&r, type, out_request);
}

uint16_t locker_wire_decode_response(const locker_wire_frame_t *frame,
                                     locker_wire_response_union_t *out_response)
{
    reader_t r;
    uint8_t type;
    uint16_t result;
    size_t body_size;

    if (frame == NULL || out_response == NULL) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (frame->version != LOCKER_WIRE_VERSION) {
        return LOCKER_WIRE_RESULT_BAD_VERSION;
    }
    if ((frame->type & LOCKER_WIRE_RESPONSE_FLAG) == 0u) {
        return LOCKER_WIRE_RESULT_UNSUPPORTED;
    }
    type = (uint8_t)(frame->type & (uint8_t)~LOCKER_WIRE_RESPONSE_FLAG);
    if (!locker_wire_request_type_valid(type)) {
        return LOCKER_WIRE_RESULT_UNSUPPORTED;
    }
    if (frame->payload_len < RESPONSE_PREFIX_SIZE) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    memset(out_response, 0, sizeof(*out_response));
    r.data = frame->payload;
    r.length = frame->payload_len;
    r.offset = 0u;
    if (!get_uuid(&r, &out_response->hello.session_id) || !get_u16(&r, &result)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (result != 0u) {
        /* A failed response is exactly the 18-byte prefix and nothing else. */
        if (frame->payload_len != RESPONSE_PREFIX_SIZE) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        if (result > LOCKER_WIRE_RESULT_UNSUPPORTED) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
        /* Keep the failure visible to the caller; no body is present. */
        set_response_result(type, out_response, result);
        return LOCKER_WIRE_RESULT_OK;
    }
    if (type == 3u) {
        /* FETCH length depends on event_count inside the body; the exact length is
         * re-checked after decoding. Reject anything below the fixed part here so
         * the reader cannot run past the payload. */
        if (frame->payload_len < RESPONSE_PREFIX_SIZE + FETCH_RSP_BODY_FIXED) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
    } else {
        body_size = locker_wire_response_payload_size(type, 0u);
        if (body_size == 0u || frame->payload_len != body_size) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
    }
    {
        uint16_t body_result = decode_response_body(&r, type, out_response);
        if (body_result != LOCKER_WIRE_RESULT_OK) {
            return body_result;
        }
    }
    if (type == 3u) {
        if (frame->payload_len !=
            locker_wire_response_payload_size(type, out_response->fetch_events.event_count)) {
            return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
        }
    }
    return LOCKER_WIRE_RESULT_OK;
}

uint16_t locker_wire_decode_device_event(const locker_wire_frame_t *frame,
                                         locker_uuid_t *out_session_id,
                                         locker_uuid_t *out_journal_epoch,
                                         locker_wire_event96_t *out_record)
{
    reader_t r;
    uint16_t result;

    if (frame == NULL || out_session_id == NULL || out_journal_epoch == NULL ||
        out_record == NULL) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (frame->version != LOCKER_WIRE_VERSION) {
        return LOCKER_WIRE_RESULT_BAD_VERSION;
    }
    if (frame->type != LOCKER_WIRE_DEVICE_EVENT) {
        return LOCKER_WIRE_RESULT_UNSUPPORTED;
    }
    if (frame->payload_len != LOCKER_WIRE_DEVICE_EVENT_PAYLOAD_SIZE) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (frame->request_no != 0u) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    r.data = frame->payload;
    r.length = frame->payload_len;
    r.offset = 0u;
    if (!get_uuid(&r, out_session_id) || !get_uuid(&r, out_journal_epoch)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    memset(out_record, 0, sizeof(*out_record));
    get_event96(&r, out_record);
    if (uuid_is_nil(out_session_id) || uuid_is_nil(out_journal_epoch)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    result = locker_wire_validate_event96(out_record);
    return result;
}
