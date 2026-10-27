/*
 * S1-03 unit tests: UART v1 payload codec (host/device/src/codec.c).
 *
 * The codec is the only place that knows field order and widths. These tests
 * pin the layout lengths from contracts/uart-layouts.json, exercise a full
 * encode/decode round trip for every message, and prove that structurally
 * invalid payloads are refused with the exact wire_result the device must echo.
 */

#include "device_test_support.h"
#include "test_support.h"

/* Payload lengths from docs/architecture/uart-v1.md:54-355. */
static void test_payload_sizes(void)
{
    TEST_CHECK_U64(49u, locker_wire_request_payload_size(1u));  /* HELLO */
    TEST_CHECK_U64(16u, locker_wire_request_payload_size(2u));  /* GET_SNAPSHOT */
    TEST_CHECK_U64(49u, locker_wire_request_payload_size(3u));  /* FETCH_EVENTS */
    TEST_CHECK_U64(48u, locker_wire_request_payload_size(4u));  /* ACK_PERSISTED */
    TEST_CHECK_U64(64u, locker_wire_request_payload_size(5u));  /* RESUME_READY */
    TEST_CHECK_U64(18u, locker_wire_request_payload_size(6u));  /* DISARM */
    TEST_CHECK_U64(99u, locker_wire_request_payload_size(7u));  /* EXECUTE_UNLOCK */
    TEST_CHECK_U64(48u, locker_wire_request_payload_size(8u));  /* QUERY_COMMAND */
    TEST_CHECK_U64(73u, locker_wire_request_payload_size(9u));  /* HOST_HEALTH */
    TEST_CHECK_U64(36u, locker_wire_request_payload_size(10u)); /* TIME_ANCHOR */
    TEST_CHECK_U64(80u, locker_wire_request_payload_size(11u)); /* RELEASE_CONTEXT */
    TEST_CHECK_U64(0u, locker_wire_request_payload_size(0u));
    TEST_CHECK_U64(0u, locker_wire_request_payload_size(12u));

    TEST_CHECK_U64(71u, locker_wire_response_payload_size(1u, 0u));
    TEST_CHECK_U64(189u, locker_wire_response_payload_size(2u, 0u));
    TEST_CHECK_U64(85u, locker_wire_response_payload_size(3u, 0u));
    TEST_CHECK_U64(181u, locker_wire_response_payload_size(3u, 1u));
    TEST_CHECK_U64(469u, locker_wire_response_payload_size(3u, 4u));
    TEST_CHECK_U64(34u, locker_wire_response_payload_size(4u, 0u));
    TEST_CHECK_U64(27u, locker_wire_response_payload_size(5u, 0u));
    TEST_CHECK_U64(27u, locker_wire_response_payload_size(6u, 0u));
    TEST_CHECK_U64(93u, locker_wire_response_payload_size(7u, 0u));
    TEST_CHECK_U64(93u, locker_wire_response_payload_size(8u, 0u));
    TEST_CHECK_U64(27u, locker_wire_response_payload_size(9u, 0u));
    TEST_CHECK_U64(46u, locker_wire_response_payload_size(10u, 0u));
    TEST_CHECK_U64(27u, locker_wire_response_payload_size(11u, 0u));
    TEST_CHECK_U64(0u, locker_wire_response_payload_size(3u, 5u));
    TEST_CHECK_U64(0u, locker_wire_response_payload_size(12u, 0u));
}

/* Round trip helper: encode a request, decode the frame, compare fields. */
static uint16_t roundtrip_request(uint8_t type, uint32_t request_no,
                                  const locker_wire_request_union_t *request,
                                  locker_wire_request_union_t *decoded)
{
    uint8_t frame_bytes[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    locker_wire_frame_t frame;
    locker_status_t status;

    status = locker_wire_encode_request(type, request_no, request, frame_bytes, sizeof(frame_bytes),
                                        &length);
    if (status != LOCKER_STATUS_OK) {
        return 0xffffu;
    }
    if (length != locker_wire_frame_size(locker_wire_request_payload_size(type))) {
        return 0xfffeu;
    }
    memset(&frame, 0, sizeof(frame));
    frame.version = frame_bytes[2];
    frame.type = frame_bytes[3];
    frame.payload_len = (uint16_t)((uint16_t)frame_bytes[4] | ((uint16_t)frame_bytes[5] << 8));
    frame.request_no = (uint32_t)frame_bytes[6] | ((uint32_t)frame_bytes[7] << 8) |
                       ((uint32_t)frame_bytes[8] << 16) | ((uint32_t)frame_bytes[9] << 24);
    if (frame.payload_len > LOCKER_WIRE_MAX_PAYLOAD) {
        return 0xfffdu;
    }
    memcpy(frame.payload, frame_bytes + LOCKER_WIRE_HEADER_SIZE, frame.payload_len);
    return locker_wire_decode_request(&frame, decoded);
}

static void test_request_roundtrip(void)
{
    locker_wire_request_union_t request;
    locker_wire_request_union_t decoded;

    /* HELLO: the 16-byte prefix is all zero, the body names the new session. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.hello.new_session_id, 0x21u);
    test_uuid_fill(&request.hello.host_instance_id, 0x31u);
    request.hello.requested_version = 1u;
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   roundtrip_request(1u, 1u, &request, &decoded));
    TEST_CHECK(locker_wire_uuid_nil(&decoded.hello.session_id));
    TEST_CHECK(locker_uuid_equal(&request.hello.new_session_id, &decoded.hello.new_session_id));
    TEST_CHECK(
        locker_uuid_equal(&request.hello.host_instance_id, &decoded.hello.host_instance_id));
    TEST_CHECK_U64(1u, decoded.hello.requested_version);

    /* GET_SNAPSHOT: session only. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.get_snapshot.session_id, 0x41u);
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, roundtrip_request(2u, 2u, &request, &decoded));
    TEST_CHECK(locker_uuid_equal(&request.get_snapshot.session_id, &decoded.get_snapshot.session_id));

    /* FETCH_EVENTS. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.fetch_events.session_id, 0x42u);
    test_uuid_fill(&request.fetch_events.journal_epoch, 0x43u);
    request.fetch_events.after.boot_id = 7u;
    request.fetch_events.after.event_seq = 9u;
    request.fetch_events.max_count = 4u;
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, roundtrip_request(3u, 3u, &request, &decoded));
    TEST_CHECK_U64(7u, decoded.fetch_events.after.boot_id);
    TEST_CHECK_U64(9u, decoded.fetch_events.after.event_seq);
    TEST_CHECK_U64(4u, decoded.fetch_events.max_count);

    /* ACK_PERSISTED. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.ack_persisted.session_id, 0x44u);
    test_uuid_fill(&request.ack_persisted.journal_epoch, 0x45u);
    request.ack_persisted.through.boot_id = 2u;
    request.ack_persisted.through.event_seq = 3u;
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, roundtrip_request(4u, 4u, &request, &decoded));
    TEST_CHECK_U64(2u, decoded.ack_persisted.through.boot_id);
    TEST_CHECK_U64(3u, decoded.ack_persisted.through.event_seq);

    /* RESUME_READY. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.resume_ready.session_id, 0x46u);
    test_uuid_fill(&request.resume_ready.journal_epoch, 0x47u);
    request.resume_ready.expected_boot_id = 11u;
    request.resume_ready.expected_physical_revision = 12u;
    request.resume_ready.persisted_through.boot_id = 11u;
    request.resume_ready.persisted_through.event_seq = 13u;
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, roundtrip_request(5u, 5u, &request, &decoded));
    TEST_CHECK_U64(11u, decoded.resume_ready.expected_boot_id);
    TEST_CHECK_U64(12u, decoded.resume_ready.expected_physical_revision);
    TEST_CHECK_U64(13u, decoded.resume_ready.persisted_through.event_seq);

    /* DISARM. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.disarm.session_id, 0x48u);
    request.disarm.reason = LOCKER_WIRE_RESULT_BAD_SESSION;
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, roundtrip_request(6u, 6u, &request, &decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_SESSION, decoded.disarm.reason);

    /* EXECUTE_UNLOCK. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.execute_unlock.session_id, 0x49u);
    test_uuid_fill(&request.execute_unlock.journal_epoch, 0x4au);
    test_uuid_fill(&request.execute_unlock.operation_id, 0x4bu);
    test_uuid_fill(&request.execute_unlock.command_id, 0x4cu);
    request.execute_unlock.expected_boot_id = 21u;
    request.execute_unlock.expected_physical_revision = 22u;
    request.execute_unlock.action_no = 23u;
    request.execute_unlock.not_after_mcu_ms = 24000u;
    request.execute_unlock.slot_id = 1u;
    request.execute_unlock.kind = (uint8_t)LOCKER_OPERATION_KIND_RETURN;
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, roundtrip_request(7u, 7u, &request, &decoded));
    TEST_CHECK_U64(21u, decoded.execute_unlock.expected_boot_id);
    TEST_CHECK_U64(22u, decoded.execute_unlock.expected_physical_revision);
    TEST_CHECK_U64(23u, decoded.execute_unlock.action_no);
    TEST_CHECK_U64(24000u, decoded.execute_unlock.not_after_mcu_ms);
    TEST_CHECK_U64(1u, decoded.execute_unlock.slot_id);
    TEST_CHECK_U64(LOCKER_OPERATION_KIND_RETURN, decoded.execute_unlock.kind);
    TEST_CHECK(
        locker_uuid_equal(&request.execute_unlock.command_id, &decoded.execute_unlock.command_id));

    /* QUERY_COMMAND. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.query_command.session_id, 0x4du);
    test_uuid_fill(&request.query_command.journal_epoch, 0x4eu);
    test_uuid_fill(&request.query_command.command_id, 0x4fu);
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, roundtrip_request(8u, 8u, &request, &decoded));
    TEST_CHECK(
        locker_uuid_equal(&request.query_command.command_id, &decoded.query_command.command_id));

    /* HOST_HEALTH: a nil UI instance forces an unknown UI age. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.host_health.session_id, 0x50u);
    test_uuid_fill(&request.host_health.host_instance_id, 0x51u);
    request.host_health.core_progress = 5u;
    request.host_health.ui_progress = 6u;
    request.host_health.core_observed_age_ms = 7u;
    request.host_health.ui_observed_age_ms = LOCKER_WIRE_AGE_UNKNOWN;
    request.host_health.supervisor_state = 2u;
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, roundtrip_request(9u, 9u, &request, &decoded));
    TEST_CHECK_U64(5u, decoded.host_health.core_progress);
    TEST_CHECK_U64(LOCKER_WIRE_AGE_UNKNOWN, decoded.host_health.ui_observed_age_ms);
    TEST_CHECK_U64(2u, decoded.host_health.supervisor_state);

    /* TIME_ANCHOR. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.time_anchor.session_id, 0x52u);
    request.time_anchor.anchor_id = 3u;
    request.time_anchor.host_utc_ms = 1700000000000u;
    request.time_anchor.host_monotonic_ms = 1000u;
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, roundtrip_request(10u, 10u, &request, &decoded));
    TEST_CHECK_U64(3u, decoded.time_anchor.anchor_id);
    TEST_CHECK_U64(1700000000000u, decoded.time_anchor.host_utc_ms);

    /* RELEASE_CONTEXT. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.release_context.session_id, 0x53u);
    test_uuid_fill(&request.release_context.journal_epoch, 0x54u);
    test_uuid_fill(&request.release_context.operation_id, 0x55u);
    test_uuid_fill(&request.release_context.command_id, 0x56u);
    request.release_context.expected_boot_id = 31u;
    request.release_context.expected_physical_revision = 32u;
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, roundtrip_request(11u, 11u, &request, &decoded));
    TEST_CHECK_U64(31u, decoded.release_context.expected_boot_id);
    TEST_CHECK(locker_uuid_equal(&request.release_context.operation_id,
                                 &decoded.release_context.operation_id));
}

/* Structural violations are refused with the exact result the device echoes. */
static void test_request_validation(void)
{
    locker_wire_request_union_t request;
    locker_wire_request_union_t decoded;
    uint8_t frame_bytes[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    locker_wire_frame_t frame;

    /* A HELLO body with a nil new_session_id is invalid. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.hello.host_instance_id, 0x31u);
    request.hello.requested_version = 1u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   roundtrip_request(1u, 1u, &request, &decoded));

    /* Unsupported version in HELLO body. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.hello.new_session_id, 0x21u);
    test_uuid_fill(&request.hello.host_instance_id, 0x31u);
    request.hello.requested_version = 2u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_VERSION,
                   roundtrip_request(1u, 1u, &request, &decoded));

    /* FETCH with max_count 0 and 5. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.fetch_events.session_id, 0x42u);
    test_uuid_fill(&request.fetch_events.journal_epoch, 0x43u);
    request.fetch_events.after.boot_id = 1u;
    request.fetch_events.after.event_seq = 1u;
    request.fetch_events.max_count = 0u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   roundtrip_request(3u, 3u, &request, &decoded));
    request.fetch_events.max_count = 5u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   roundtrip_request(3u, 3u, &request, &decoded));

    /* Half-zero cursors are forbidden. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.fetch_events.session_id, 0x42u);
    test_uuid_fill(&request.fetch_events.journal_epoch, 0x43u);
    request.fetch_events.after.boot_id = 1u;
    request.fetch_events.after.event_seq = 0u;
    request.fetch_events.max_count = 1u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   roundtrip_request(3u, 3u, &request, &decoded));

    /* EXECUTE with slot_id 2, kind 3 and action_no 0. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.execute_unlock.session_id, 0x49u);
    test_uuid_fill(&request.execute_unlock.journal_epoch, 0x4au);
    test_uuid_fill(&request.execute_unlock.operation_id, 0x4bu);
    test_uuid_fill(&request.execute_unlock.command_id, 0x4cu);
    request.execute_unlock.action_no = 1u;
    request.execute_unlock.slot_id = 2u;
    request.execute_unlock.kind = (uint8_t)LOCKER_OPERATION_KIND_BORROW;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   roundtrip_request(7u, 7u, &request, &decoded));
    request.execute_unlock.slot_id = 1u;
    request.execute_unlock.kind = 3u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   roundtrip_request(7u, 7u, &request, &decoded));
    request.execute_unlock.kind = (uint8_t)LOCKER_OPERATION_KIND_BORROW;
    request.execute_unlock.action_no = 0u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   roundtrip_request(7u, 7u, &request, &decoded));

    /* HOST_HEALTH: a nil UI instance is only legal when the UI age is unknown
     * (uart-v1.md:40); a nil UI instance with a concrete age is BAD_PAYLOAD. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.host_health.session_id, 0x50u);
    test_uuid_fill(&request.host_health.host_instance_id, 0x51u);
    request.host_health.ui_observed_age_ms = LOCKER_WIRE_AGE_UNKNOWN;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, roundtrip_request(9u, 9u, &request, &decoded));
    TEST_CHECK(locker_uuid_is_nil(&decoded.host_health.ui_instance_id));
    TEST_CHECK_U64(LOCKER_WIRE_AGE_UNKNOWN, decoded.host_health.ui_observed_age_ms);
    request.host_health.ui_observed_age_ms = 250u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   roundtrip_request(9u, 9u, &request, &decoded));
    test_uuid_fill(&request.host_health.ui_instance_id, 0x52u);
    request.host_health.ui_observed_age_ms = 250u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, roundtrip_request(9u, 9u, &request, &decoded));

    /* A wrong payload_len for the type is BAD_PAYLOAD, not a silent truncation. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.get_snapshot.session_id, 0x41u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_request(2u, 2u, &request, frame_bytes, sizeof(frame_bytes),
                                                 &length));
    memset(&frame, 0, sizeof(frame));
    frame.version = LOCKER_WIRE_VERSION;
    frame.type = 2u;
    frame.payload_len = 15u; /* one byte short */
    frame.request_no = 2u;
    memcpy(frame.payload, frame_bytes + LOCKER_WIRE_HEADER_SIZE, frame.payload_len);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_decode_request(&frame, &decoded));

    /* A frame that claims a response type is not a request. */
    frame.payload_len = (uint16_t)locker_wire_request_payload_size(2u);
    memcpy(frame.payload, frame_bytes + LOCKER_WIRE_HEADER_SIZE, frame.payload_len);
    frame.type = (uint8_t)(2u | LOCKER_WIRE_RESPONSE_FLAG);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_UNSUPPORTED, locker_wire_decode_request(&frame, &decoded));

    /* A future version is refused before any field is trusted. */
    frame.type = 2u;
    frame.version = 2u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_VERSION, locker_wire_decode_request(&frame, &decoded));
}

static void test_response_roundtrip(void)
{
    locker_wire_response_union_t response;
    locker_wire_response_union_t decoded;
    uint8_t frame_bytes[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    locker_wire_frame_t frame;
    uint8_t index;

    /* HELLO response. */
    memset(&response, 0, sizeof(response));
    test_uuid_fill(&response.hello.session_id, 0x61u);
    test_uuid_fill(&response.hello.journal_epoch, 0x62u);
    response.hello.device_uid[0] = 0x01u;
    response.hello.device_uid[11] = 0x0cu;
    response.hello.boot_id = 4u;
    response.hello.firmware_major = 0u;
    response.hello.firmware_minor = 1u;
    response.hello.firmware_patch = 2u;
    response.hello.capabilities = LOCKER_WIRE_CAP_DOOR_AND_ITEM | LOCKER_WIRE_CAP_PERSISTENT_JOURNAL;
    response.hello.device_mode = 1u;
    response.hello.journal_state = 1u;
    response.hello.action_watermark = 9u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_response(1u, 1u, &response, frame_bytes, sizeof(frame_bytes),
                                                  &length));
    TEST_CHECK_U64(locker_wire_frame_size(71u), length);
    memset(&frame, 0, sizeof(frame));
    frame.version = frame_bytes[2];
    frame.type = frame_bytes[3];
    frame.payload_len = (uint16_t)((uint16_t)frame_bytes[4] | ((uint16_t)frame_bytes[5] << 8));
    frame.request_no = 1u;
    memcpy(frame.payload, frame_bytes + LOCKER_WIRE_HEADER_SIZE, frame.payload_len);
    TEST_CHECK_U64(LOCKER_WIRE_RESPONSE_FLAG | 1u, frame.type);
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, locker_wire_decode_response(&frame, &decoded));
    TEST_CHECK_U64(0u, decoded.hello.result);
    TEST_CHECK_U64(4u, decoded.hello.boot_id);
    TEST_CHECK_U64(2u, decoded.hello.firmware_patch);
    TEST_CHECK_U64(9u, decoded.hello.action_watermark);
    TEST_CHECK(locker_uuid_equal(&response.hello.journal_epoch, &decoded.hello.journal_epoch));

    /* A frame carrying another protocol version is BAD_VERSION, not a body
     * decode failure (uart-v1.md:21). */
    frame.version = 0u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_VERSION, locker_wire_decode_response(&frame, &decoded));
    frame.version = LOCKER_WIRE_VERSION;

    /* Snapshot response with a complete event boundary set. */
    memset(&response, 0, sizeof(response));
    test_uuid_fill(&response.get_snapshot.session_id, 0x63u);
    test_uuid_fill(&response.get_snapshot.journal_epoch, 0x64u);
    test_uuid_fill(&response.get_snapshot.active_operation_id, 0x65u);
    test_uuid_fill(&response.get_snapshot.active_command_id, 0x66u);
    test_uuid_fill(&response.get_snapshot.active_session_id, 0x67u);
    response.get_snapshot.boot_id = 5u;
    response.get_snapshot.sampled_mono_ms = 1000u;
    response.get_snapshot.physical_revision = 6u;
    response.get_snapshot.door = (uint8_t)LOCKER_DOOR_STATE_CLOSED;
    response.get_snapshot.item = (uint8_t)LOCKER_ITEM_STATE_PRESENT;
    response.get_snapshot.supply_mv = 12000u;
    response.get_snapshot.flags = LOCKER_WIRE_FLAG_SUPPLY_VALID | LOCKER_WIRE_FLAG_PHYSICAL_STABLE;
    response.get_snapshot.dropped_events = 0u;
    response.get_snapshot.first_available.boot_id = 5u;
    response.get_snapshot.first_available.event_seq = 1u;
    response.get_snapshot.last_committed.boot_id = 5u;
    response.get_snapshot.last_committed.event_seq = 2u;
    response.get_snapshot.ack_persisted.boot_id = 5u;
    response.get_snapshot.ack_persisted.event_seq = 1u;
    response.get_snapshot.active_command_state = (uint8_t)LOCKER_COMMAND_STATE_PULSE_STARTED;
    response.get_snapshot.active_action_no = 3u;
    response.get_snapshot.action_watermark = 3u;
    response.get_snapshot.device_mode = 1u;
    response.get_snapshot.journal_state = 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_response(2u, 2u, &response, frame_bytes, sizeof(frame_bytes),
                                                  &length));
    TEST_CHECK_U64(locker_wire_frame_size(189u), length);
    frame.type = frame_bytes[3];
    frame.payload_len = (uint16_t)((uint16_t)frame_bytes[4] | ((uint16_t)frame_bytes[5] << 8));
    memcpy(frame.payload, frame_bytes + LOCKER_WIRE_HEADER_SIZE, frame.payload_len);
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, locker_wire_decode_response(&frame, &decoded));
    TEST_CHECK_U64(12000u, decoded.get_snapshot.supply_mv);
    TEST_CHECK_U64(6u, decoded.get_snapshot.physical_revision);
    TEST_CHECK_U64(2u, decoded.get_snapshot.last_committed.event_seq);
    TEST_CHECK_U64(3u, decoded.get_snapshot.active_action_no);

    /* FETCH response with four events: the only variable-length response. */
    memset(&response, 0, sizeof(response));
    test_uuid_fill(&response.fetch_events.session_id, 0x68u);
    test_uuid_fill(&response.fetch_events.journal_epoch, 0x69u);
    response.fetch_events.first_available.boot_id = 5u;
    response.fetch_events.first_available.event_seq = 1u;
    response.fetch_events.last_committed.boot_id = 5u;
    response.fetch_events.last_committed.event_seq = 4u;
    response.fetch_events.next_after.boot_id = 5u;
    response.fetch_events.next_after.event_seq = 4u;
    response.fetch_events.event_count = 4u;
    response.fetch_events.has_more = 0u;
    response.fetch_events.gap_before_page = 0u;
    for (index = 0u; index < 4u; ++index) {
        locker_wire_event96_t *record = &response.fetch_events.events[index];
        memset(record, 0, sizeof(*record));
        record->boot_id = 5u;
        record->event_seq = (uint64_t)(index + 1u);
        record->mono_ms = 1000u + index;
        record->event_type = 1u;
        record->reason = 0u;
        test_uuid_fill(&record->operation_id, 0x70u);
        test_uuid_fill(&record->command_id, 0x71u);
        record->door = (uint8_t)LOCKER_DOOR_STATE_CLOSED;
        record->item = (uint8_t)LOCKER_ITEM_STATE_PRESENT;
        record->flags = LOCKER_WIRE_FLAG_PHYSICAL_STABLE;
        record->supply_mv = 12000u;
        record->physical_revision = 6u;
        record->detail = 0u;
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_response(3u, 3u, &response, frame_bytes, sizeof(frame_bytes),
                                                  &length));
    TEST_CHECK_U64(locker_wire_frame_size(469u), length);
    frame.type = frame_bytes[3];
    frame.payload_len = (uint16_t)((uint16_t)frame_bytes[4] | ((uint16_t)frame_bytes[5] << 8));
    memcpy(frame.payload, frame_bytes + LOCKER_WIRE_HEADER_SIZE, frame.payload_len);
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, locker_wire_decode_response(&frame, &decoded));
    TEST_CHECK_U64(4u, decoded.fetch_events.event_count);
    TEST_CHECK_U64(4u, decoded.fetch_events.events[3].event_seq);
    TEST_CHECK_U64(1003u, decoded.fetch_events.events[3].mono_ms);

    /* The FETCH frame length must agree with the event_count carried in the
     * body; a page claiming fewer events than the frame holds is BAD_PAYLOAD
     * (uart-v1.md:21).  payload offset 82 is event_count: 18 prefix bytes,
     * then journal_epoch, first_available, last_committed and next_after. */
    frame.payload[82] = 1u;
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_decode_response(&frame, &decoded));
    frame.payload[82] = 4u;

    /* Command responses (execute and query share one layout). */
    memset(&response, 0, sizeof(response));
    test_uuid_fill(&response.execute_unlock.session_id, 0x72u);
    test_uuid_fill(&response.execute_unlock.operation_id, 0x73u);
    test_uuid_fill(&response.execute_unlock.command_id, 0x74u);
    test_uuid_fill(&response.execute_unlock.command_session_id, 0x75u);
    response.execute_unlock.command_state = (uint8_t)LOCKER_COMMAND_STATE_PULSE_FINISHED;
    response.execute_unlock.action_no = 8u;
    response.execute_unlock.command_boot_id = 5u;
    response.execute_unlock.physical_revision = 6u;
    response.execute_unlock.reason = 0u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_response(7u, 7u, &response, frame_bytes, sizeof(frame_bytes),
                                                  &length));
    TEST_CHECK_U64(locker_wire_frame_size(93u), length);
    frame.type = frame_bytes[3];
    frame.payload_len = (uint16_t)((uint16_t)frame_bytes[4] | ((uint16_t)frame_bytes[5] << 8));
    memcpy(frame.payload, frame_bytes + LOCKER_WIRE_HEADER_SIZE, frame.payload_len);
    memset(&decoded, 0, sizeof(decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, locker_wire_decode_response(&frame, &decoded));
    TEST_CHECK_U64(LOCKER_COMMAND_STATE_PULSE_FINISHED, decoded.execute_unlock.command_state);
    TEST_CHECK_U64(8u, decoded.execute_unlock.action_no);

    /* A response type without the response flag is UNSUPPORTED. */
    frame.type = 2u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_UNSUPPORTED, locker_wire_decode_response(&frame, &decoded));
}

/* result != 0 responses are exactly the 18-byte prefix, never a body. */
static void test_error_response_shape(void)
{
    locker_uuid_t session;
    uint8_t frame_bytes[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    locker_wire_frame_t frame;
    locker_wire_response_union_t decoded;
    locker_wire_response_union_t response;

    test_uuid_fill(&session, 0x80u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_error_response(7u, 5u, &session,
                                                        LOCKER_WIRE_RESULT_ACTION_GAP, frame_bytes,
                                                        sizeof(frame_bytes), &length));
    TEST_CHECK_U64(locker_wire_frame_size(18u), length);
    TEST_CHECK_U64((uint8_t)(7u | LOCKER_WIRE_RESPONSE_FLAG), frame_bytes[3]);
    TEST_CHECK_U64(18u, frame_bytes[4]);
    memset(&frame, 0, sizeof(frame));
    frame.version = frame_bytes[2];
    frame.type = frame_bytes[3];
    frame.payload_len = (uint16_t)((uint16_t)frame_bytes[4] | ((uint16_t)frame_bytes[5] << 8));
    frame.request_no = 5u;
    memcpy(frame.payload, frame_bytes + LOCKER_WIRE_HEADER_SIZE, frame.payload_len);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, locker_wire_decode_response(&frame, &decoded));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_ACTION_GAP, locker_wire_response_result(7u, &decoded));
    TEST_CHECK_U64(7u, frame_bytes[3] & 0x7fu);

    /* An encoder given a non-zero result in the DTO must not emit a body. */
    memset(&response, 0, sizeof(response));
    test_uuid_fill(&response.execute_unlock.session_id, 0x81u);
    test_uuid_fill(&response.execute_unlock.operation_id, 0x82u);
    test_uuid_fill(&response.execute_unlock.command_id, 0x83u);
    response.execute_unlock.result = LOCKER_WIRE_RESULT_REPLAY_BLOCKED;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_response(7u, 6u, &response, frame_bytes, sizeof(frame_bytes),
                                                  &length));
    TEST_CHECK_U64(locker_wire_frame_size(18u), length);

    /* result 0 must not be accepted by the error encoder. */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_wire_encode_error_response(7u, 5u, &session, 0u, frame_bytes,
                                                        sizeof(frame_bytes), &length));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_wire_encode_error_response(0u, 5u, &session, 1u, frame_bytes,
                                                        sizeof(frame_bytes), &length));

    /* A failed response carrying extra bytes is a protocol violation. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_error_response(2u, 3u, &session, LOCKER_WIRE_RESULT_BUSY,
                                                        frame_bytes, sizeof(frame_bytes), &length));
    frame.type = frame_bytes[3];
    frame.payload_len = (uint16_t)(frame_bytes[4] + 1u);
    memcpy(frame.payload, frame_bytes + LOCKER_WIRE_HEADER_SIZE, 18u);
    frame.payload[18] = 0x00u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_decode_response(&frame, &decoded));

    /* result above UNSUPPORTED is not a wire value. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_error_response(2u, 3u, &session, 15u, frame_bytes,
                                                        sizeof(frame_bytes), &length));
    frame.type = frame_bytes[3];
    frame.payload_len = 18u;
    memcpy(frame.payload, frame_bytes + LOCKER_WIRE_HEADER_SIZE, 18u);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_decode_response(&frame, &decoded));
}

static void test_event96_validation(void)
{
    locker_wire_event96_t record;

    memset(&record, 0, sizeof(record));
    record.boot_id = 1u;
    record.event_seq = 1u;
    record.event_type = 1u;
    record.door = 1u;
    record.item = 2u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, locker_wire_validate_event96(&record));

    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_validate_event96(NULL));

    /* Zero boot or seq is never a real record. */
    record.boot_id = 0u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_validate_event96(&record));
    record.boot_id = 1u;
    record.event_seq = 0u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_validate_event96(&record));
    record.event_seq = 1u;

    /* event_type range and reason range. */
    record.event_type = 0u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_validate_event96(&record));
    record.event_type = 16u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_validate_event96(&record));
    record.event_type = 1u;
    record.reason = 15u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_validate_event96(&record));
    record.reason = 0u;

    /* door/item enumerations. */
    record.door = 3u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_validate_event96(&record));
    record.door = 1u;
    record.item = 3u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_validate_event96(&record));
    record.item = 2u;

    /* Undefined flag bits are refused. */
    record.flags = 0x0020u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_validate_event96(&record));
    record.flags = LOCKER_WIRE_FLAG_DEFINED_MASK;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, locker_wire_validate_event96(&record));
    record.flags = 0u;

    /* card_uid_len is limited to 0/4/7/10 and only CARD_OBSERVED may carry it. */
    record.card_uid_len = 5u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_validate_event96(&record));
    record.card_uid_len = 4u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_validate_event96(&record));
    record.event_type = 8u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, locker_wire_validate_event96(&record));
    /* Trailing card_uid bytes must be zero. */
    record.card_uid[5] = 0x01u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_validate_event96(&record));
    record.card_uid[5] = 0x00u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, locker_wire_validate_event96(&record));

    /* reserved must be zero. */
    record.reserved = 1u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_validate_event96(&record));
    record.reserved = 0u;

    /* SUPPLY_VALID with an unknown supply is contradictory. */
    record.event_type = 1u;
    record.card_uid_len = 0u;
    record.flags = LOCKER_WIRE_FLAG_SUPPLY_VALID;
    record.supply_mv = LOCKER_WIRE_SUPPLY_UNKNOWN;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_validate_event96(&record));
    record.supply_mv = 12000u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, locker_wire_validate_event96(&record));
}

static void test_device_event_codec(void)
{
    locker_uuid_t session;
    locker_uuid_t epoch;
    locker_wire_event96_t record;
    locker_wire_event96_t decoded_record;
    locker_uuid_t decoded_session;
    locker_uuid_t decoded_epoch;
    uint8_t frame_bytes[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    locker_wire_frame_t frame;

    test_uuid_fill(&session, 0x90u);
    test_uuid_fill(&epoch, 0x91u);
    memset(&record, 0, sizeof(record));
    record.boot_id = 3u;
    record.event_seq = 4u;
    record.mono_ms = 500u;
    record.event_type = 2u; /* PULSE_STARTED */
    record.reason = 0u;
    test_uuid_fill(&record.operation_id, 0x92u);
    test_uuid_fill(&record.command_id, 0x93u);
    record.door = 1u;
    record.item = 2u;
    record.flags = LOCKER_WIRE_FLAG_PHYSICAL_STABLE | LOCKER_WIRE_FLAG_PULSE_ACTIVE;
    record.supply_mv = 12000u;
    record.physical_revision = 6u;
    record.detail = 300u;
    record.card_uid_len = 0u;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_device_event(&session, &epoch, &record, frame_bytes,
                                                      sizeof(frame_bytes), &length));
    TEST_CHECK_U64(locker_wire_frame_size(128u), length);
    TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT, frame_bytes[3]);
    TEST_CHECK_U64(128u, frame_bytes[4]);
    TEST_CHECK_U64(0u, frame_bytes[5]);
    TEST_CHECK_U64(0u, frame_bytes[6]); /* asynchronous frames carry request_no 0 */

    memset(&frame, 0, sizeof(frame));
    frame.version = frame_bytes[2];
    frame.type = frame_bytes[3];
    frame.payload_len = (uint16_t)((uint16_t)frame_bytes[4] | ((uint16_t)frame_bytes[5] << 8));
    frame.request_no = 0u;
    memcpy(frame.payload, frame_bytes + LOCKER_WIRE_HEADER_SIZE, frame.payload_len);
    memset(&decoded_record, 0, sizeof(decoded_record));
    memset(&decoded_session, 0, sizeof(decoded_session));
    memset(&decoded_epoch, 0, sizeof(decoded_epoch));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   locker_wire_decode_device_event(&frame, &decoded_session, &decoded_epoch,
                                                   &decoded_record));
    TEST_CHECK(locker_uuid_equal(&session, &decoded_session));
    TEST_CHECK(locker_uuid_equal(&epoch, &decoded_epoch));
    TEST_CHECK_U64(3u, decoded_record.boot_id);
    TEST_CHECK_U64(4u, decoded_record.event_seq);
    TEST_CHECK_U64(2u, decoded_record.event_type);
    TEST_CHECK_U64(300u, decoded_record.detail);
    TEST_CHECK_U64(6u, decoded_record.physical_revision);

    /* A device event with a non-zero request_no is malformed. */
    frame.request_no = 1u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   locker_wire_decode_device_event(&frame, &decoded_session, &decoded_epoch,
                                                   &decoded_record));
    frame.request_no = 0u;

    /* A nil session or epoch is never valid on the wire. */
    memset(&frame.payload[0], 0, 16u);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   locker_wire_decode_device_event(&frame, &decoded_session, &decoded_epoch,
                                                   &decoded_record));
    memcpy(frame.payload, session.bytes, 16u);
    memset(&frame.payload[16], 0, 16u);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   locker_wire_decode_device_event(&frame, &decoded_session, &decoded_epoch,
                                                   &decoded_record));

    /* The encoder refuses an invalid record instead of writing it out. */
    record.reserved = 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_wire_encode_device_event(&session, &epoch, &record, frame_bytes,
                                                      sizeof(frame_bytes), &length));
    record.reserved = 0u;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_wire_encode_device_event(NULL, &epoch, &record, frame_bytes,
                                                      sizeof(frame_bytes), &length));
}

static void test_zero_request_number_and_unknown_error(void)
{
    locker_wire_frame_t frame;
    locker_wire_request_union_t decoded;
    locker_uuid_t session;
    uint8_t bytes[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    frame_capture_t capture;
    locker_wire_stream_t stream;

    memset(&frame, 0, sizeof(frame));
    frame.version = LOCKER_WIRE_VERSION;
    frame.type = 2u;
    frame.payload_len = 16u;
    test_uuid_fill(&session, 0x44u);
    memcpy(frame.payload, session.bytes, 16u);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, locker_wire_decode_request(&frame, &decoded));
    frame.request_no = 1u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, locker_wire_decode_request(&frame, &decoded));
    frame.type = 12u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_UNSUPPORTED, locker_wire_decode_request(&frame, &decoded));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
        locker_wire_encode_error_response(12u, 9u, &session, LOCKER_WIRE_RESULT_UNSUPPORTED,
                                          bytes, sizeof(bytes), &length));
    TEST_CHECK_U64(30u, length);
    test_capture_reset(&capture);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, test_capture_feed(&capture, &stream, bytes, length));
    TEST_CHECK_U64(1u, capture.count);
    TEST_CHECK_U64(0x8cu, capture.frames[0].type);
    TEST_CHECK_U64(18u, capture.frames[0].payload_len);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_UNSUPPORTED, capture.frames[0].payload[16]);
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
        locker_wire_encode_frame(12u, 9u, frame.payload, 16u, bytes, sizeof(bytes), &length));
}

int main(void)
{
    test_zero_request_number_and_unknown_error();
    test_payload_sizes();
    test_request_roundtrip();
    test_request_validation();
    test_response_roundtrip();
    test_error_response_shape();
    test_event96_validation();
    test_device_event_codec();
    TEST_REPORT("device.test_codec");
}
