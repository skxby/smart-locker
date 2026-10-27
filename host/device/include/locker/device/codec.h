#ifndef LOCKER_DEVICE_CODEC_H
#define LOCKER_DEVICE_CODEC_H

/*
 * S1-03 UART v1 payload codec (docs/architecture/uart-v1.md:25-355,
 * contracts/uart-layouts.json). Field order and widths come from the layout
 * source, never from a hand-copied table; the DTOs are the generated
 * locker_wire_*_t structs in contracts/include/locker_contracts.h:157-181.
 *
 * Encoding writes every field explicitly in little-endian order. Decoding
 * validates structure before it hands a DTO to a handler:
 *   - version != 1                                  -> BAD_VERSION
 *   - unknown request/reply type                    -> UNSUPPORTED
 *   - payload_len not exactly the layout length     -> BAD_PAYLOAD
 *   - enum out of range, undefined flag bit set,
 *     reserved != 0, illegal cursor, bad card_uid,
 *     result != 0 with a body, HELLO prefix != 0    -> BAD_PAYLOAD
 * Semantic state checks (session currency, action watermarks, readiness,
 * deadlines) belong to the device state machine, not to this file.
 *
 * Decoders never allocate and never trust payload_len: they only read inside
 * frame->payload[0..payload_len).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "locker_contracts.h"
#include "locker/device/wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/* wire_result values (uart-v1.md:406, contract-source.json wire_enums). */
enum {
    LOCKER_WIRE_RESULT_OK = 0,
    LOCKER_WIRE_RESULT_BAD_VERSION = 1,
    LOCKER_WIRE_RESULT_BAD_SESSION = 2,
    LOCKER_WIRE_RESULT_NOT_READY = 3,
    LOCKER_WIRE_RESULT_BAD_PRECONDITION = 4,
    LOCKER_WIRE_RESULT_REPLAY_BLOCKED = 5,
    LOCKER_WIRE_RESULT_ACTION_GAP = 6,
    LOCKER_WIRE_RESULT_BUSY = 7,
    LOCKER_WIRE_RESULT_LOG_UNAVAILABLE = 8,
    LOCKER_WIRE_RESULT_UNKNOWN_RESULT = 9,
    LOCKER_WIRE_RESULT_BAD_PAYLOAD = 10,
    LOCKER_WIRE_RESULT_CURSOR_GAP = 11,
    LOCKER_WIRE_RESULT_PHYSICAL_CHANGED = 12,
    LOCKER_WIRE_RESULT_JOURNAL_CHANGED = 13,
    LOCKER_WIRE_RESULT_UNSUPPORTED = 14
};

/* observation_flags bit numbers (uart-layouts.json bitmaps.observation_flags). */
enum {
    LOCKER_WIRE_FLAG_SUPPLY_VALID = 1u << 0,
    LOCKER_WIRE_FLAG_PHYSICAL_STABLE = 1u << 1,
    LOCKER_WIRE_FLAG_HISTORY_INCOMPLETE = 1u << 2,
    LOCKER_WIRE_FLAG_JOURNAL_DEGRADED = 1u << 3,
    LOCKER_WIRE_FLAG_PULSE_ACTIVE = 1u << 4,
    LOCKER_WIRE_FLAG_DEFINED_MASK = 0x001fu
};

/* capabilities bit numbers (uart-layouts.json bitmaps.capabilities). */
enum {
    LOCKER_WIRE_CAP_DOOR_AND_ITEM = 1u << 0,
    LOCKER_WIRE_CAP_PERSISTENT_JOURNAL = 1u << 1,
    LOCKER_WIRE_CAP_CARD_READER = 1u << 2,
    LOCKER_WIRE_CAP_SUPPLY_ADC = 1u << 3,
    LOCKER_WIRE_CAP_BOUNDED_PULSE = 1u << 4
};

#define LOCKER_WIRE_SUPPLY_UNKNOWN 0xffffffffu
#define LOCKER_WIRE_AGE_UNKNOWN 0xffffffffu

typedef union locker_wire_request_union {
    locker_wire_hello_request_t hello;
    locker_wire_get_snapshot_request_t get_snapshot;
    locker_wire_fetch_events_request_t fetch_events;
    locker_wire_ack_persisted_request_t ack_persisted;
    locker_wire_resume_ready_request_t resume_ready;
    locker_wire_disarm_request_t disarm;
    locker_wire_execute_unlock_request_t execute_unlock;
    locker_wire_query_command_request_t query_command;
    locker_wire_host_health_request_t host_health;
    locker_wire_time_anchor_request_t time_anchor;
    locker_wire_release_context_request_t release_context;
} locker_wire_request_union_t;

typedef union locker_wire_response_union {
    locker_wire_hello_response_t hello;
    locker_wire_get_snapshot_response_t get_snapshot;
    locker_wire_fetch_events_response_t fetch_events;
    locker_wire_ack_persisted_response_t ack_persisted;
    locker_wire_resume_ready_response_t resume_ready;
    locker_wire_disarm_response_t disarm;
    locker_wire_execute_unlock_response_t execute_unlock;
    locker_wire_query_command_response_t query_command;
    locker_wire_host_health_response_t host_health;
    locker_wire_time_anchor_response_t time_anchor;
    locker_wire_release_context_response_t release_context;
} locker_wire_response_union_t;

/* Payload length implied by the layout. 0 means "unknown type". */
size_t locker_wire_request_payload_size(uint8_t type);
/* FETCH_EVENTS depends on event_count (0..4); other types ignore the argument. */
size_t locker_wire_response_payload_size(uint8_t type, uint8_t event_count);

bool locker_wire_cursor_valid(const locker_wire_cursor_t *cursor);
bool locker_wire_uuid_nil(const locker_uuid_t *uuid);

/* Encode helpers: out receives one complete frame (header+payload+CRC). */
locker_status_t locker_wire_encode_request(uint8_t type, uint32_t request_no,
                                           const locker_wire_request_union_t *request,
                                           uint8_t *out, size_t out_size, size_t *out_length);
locker_status_t locker_wire_encode_response(uint8_t type, uint32_t request_no,
                                            const locker_wire_response_union_t *response,
                                            uint8_t *out, size_t out_size, size_t *out_length);
/* Reads the result field of a response DTO without knowing the message type. */
uint16_t locker_wire_response_result(uint8_t type,
                                     const locker_wire_response_union_t *response);
/* result != 0 responses carry exactly the 18-byte prefix and no body. */
locker_status_t locker_wire_encode_error_response(uint8_t type, uint32_t request_no,
                                                  const locker_uuid_t *session_id,
                                                  uint16_t result, uint8_t *out,
                                                  size_t out_size, size_t *out_length);
locker_status_t locker_wire_encode_device_event(const locker_uuid_t *session_id,
                                                const locker_uuid_t *journal_epoch,
                                                const locker_wire_event96_t *record,
                                                uint8_t *out, size_t out_size,
                                                size_t *out_length);

/* Decoders return a wire_result value: OK on success, otherwise the result the
 * device must echo. The DTO is only written when the result is OK. */
uint16_t locker_wire_decode_request(const locker_wire_frame_t *frame,
                                    locker_wire_request_union_t *out_request);
uint16_t locker_wire_decode_response(const locker_wire_frame_t *frame,
                                     locker_wire_response_union_t *out_response);
uint16_t locker_wire_decode_device_event(const locker_wire_frame_t *frame,
                                         locker_uuid_t *out_session_id,
                                         locker_uuid_t *out_journal_epoch,
                                         locker_wire_event96_t *out_record);

/* Field-level validators shared by encoder and decoder. */
uint16_t locker_wire_validate_event96(const locker_wire_event96_t *record);

#ifdef __cplusplus
}
#endif

#endif /* LOCKER_DEVICE_CODEC_H */
