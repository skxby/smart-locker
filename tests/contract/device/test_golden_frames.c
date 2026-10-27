/*
 * S1-03 contract test: the golden UART-v1 vectors from
 * contracts/examples/uart-golden.json must be reproduced byte for byte.
 *
 * The vectors are the machine-readable contract between the host codec and the
 * device firmware. Encoding one byte differently here would silently break the
 * wire protocol, so these checks compare the whole frame, not just fields.
 */

#include <stdio.h>
#include <string.h>

#include "device_test_support.h"
#include "test_support.h"

/* contracts/examples/uart-golden.json: hello_request. */
static const char GOLDEN_HELLO_FRAME_HEX[] =
    "4c4b0101310001000000000000000000000000000000000000000000000000004000800000000000000a"
    "0000000000004000800000000000000b01d8bd";
/* contracts/examples/uart-golden.json: snapshot_request. */
static const char GOLDEN_SNAPSHOT_FRAME_HEX[] =
    "4c4b01021000020000000000000000004000800000000000000a6eea";
/* contracts/examples/uart-golden.json: crc_error (last byte corrupted). */
static const char GOLDEN_CRC_ERROR_FRAME_HEX[] =
    "4c4b01021000020000000000000000004000800000000000000a6eeb";
/* contracts/examples/uart-golden.json: oversized_header (payload_len = 513). */
static const char GOLDEN_OVERSIZED_FRAME_HEX[] = "4c4b0102010203000000";

static int hex_nibble(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

/* Decode a hex string into bytes; returns the byte count or 0 on bad input. */
static size_t decode_hex(const char *text, uint8_t *out, size_t out_size)
{
    size_t length;
    size_t index;

    if (text == NULL || out == NULL) {
        return 0u;
    }
    length = strlen(text);
    if (length == 0u || (length % 2u) != 0u || length / 2u > out_size) {
        return 0u;
    }
    for (index = 0u; index < length; index += 2u) {
        int high = hex_nibble(text[index]);
        int low = hex_nibble(text[index + 1u]);

        if (high < 0 || low < 0) {
            return 0u;
        }
        out[index / 2u] = (uint8_t)((high << 4) | low);
    }
    return length / 2u;
}

static void set_uuid_bytes(locker_uuid_t *uuid, const uint8_t *bytes)
{
    memcpy(uuid->bytes, bytes, sizeof(uuid->bytes));
}

/* The canonical CRC check from the contract: "123456789" -> 0x29b1. */
static void test_crc_canonical_check(void)
{
    static const char canonical[] = "123456789";

    TEST_CHECK_U64(0x29b1u, locker_wire_crc16((const uint8_t *)canonical, 9u));
    /* Empty input yields the initial value. */
    TEST_CHECK_U64(0xffffu, locker_wire_crc16(NULL, 0u));
}

static void test_hello_request_golden_frame(void)
{
    static const uint8_t new_session_bytes[16] = {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                                  0x40u, 0x00u, 0x80u, 0x00u, 0x00u, 0x00u,
                                                  0x00u, 0x00u, 0x00u, 0x0au};
    static const uint8_t host_instance_bytes[16] = {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                                    0x40u, 0x00u, 0x80u, 0x00u, 0x00u, 0x00u,
                                                    0x00u, 0x00u, 0x00u, 0x0bu};
    uint8_t golden[LOCKER_WIRE_MAX_FRAME];
    uint8_t encoded[LOCKER_WIRE_MAX_FRAME];
    size_t golden_length;
    size_t encoded_length = 0u;
    locker_wire_request_union_t request;

    golden_length = decode_hex(GOLDEN_HELLO_FRAME_HEX, golden, sizeof(golden));
    TEST_CHECK_U64(61u, golden_length);
    TEST_CHECK_U64(49u, locker_wire_request_payload_size(1u));

    memset(&request, 0, sizeof(request));
    set_uuid_bytes(&request.hello.new_session_id, new_session_bytes);
    set_uuid_bytes(&request.hello.host_instance_id, host_instance_bytes);
    request.hello.requested_version = LOCKER_WIRE_VERSION;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_request(1u, 1u, &request, encoded, sizeof(encoded),
                                                 &encoded_length));
    TEST_CHECK_U64(golden_length, encoded_length);
    TEST_CHECK(memcmp(golden, encoded, golden_length) == 0);

    /* The encoded frame also parses back into the same request. */
    {
        locker_wire_stream_t stream;
        frame_capture_t capture;
        locker_wire_request_union_t decoded;

        test_capture_reset(&capture);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, test_capture_feed(&capture, &stream, encoded,
                                                              encoded_length));
        TEST_CHECK_U64(1u, capture.count);
        TEST_CHECK_U64(0u, stream.stats.crc_errors);
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                       locker_wire_decode_request(&capture.frames[0], &decoded));
        TEST_CHECK(locker_uuid_equal(&request.hello.new_session_id, &decoded.hello.new_session_id));
        TEST_CHECK(
            locker_uuid_equal(&request.hello.host_instance_id, &decoded.hello.host_instance_id));
        TEST_CHECK_U64(1u, decoded.hello.requested_version);
        /* HELLO carries an all-zero session prefix. */
        TEST_CHECK(locker_uuid_is_nil(&decoded.hello.session_id));
    }
}

static void test_snapshot_request_golden_frame(void)
{
    static const uint8_t session_bytes[16] = {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                              0x40u, 0x00u, 0x80u, 0x00u, 0x00u, 0x00u,
                                              0x00u, 0x00u, 0x00u, 0x0au};
    uint8_t golden[LOCKER_WIRE_MAX_FRAME];
    uint8_t encoded[LOCKER_WIRE_MAX_FRAME];
    size_t golden_length;
    size_t encoded_length = 0u;
    locker_wire_request_union_t request;

    golden_length = decode_hex(GOLDEN_SNAPSHOT_FRAME_HEX, golden, sizeof(golden));
    TEST_CHECK_U64(28u, golden_length);
    TEST_CHECK_U64(16u, locker_wire_request_payload_size(2u));

    memset(&request, 0, sizeof(request));
    set_uuid_bytes(&request.get_snapshot.session_id, session_bytes);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_request(2u, 2u, &request, encoded, sizeof(encoded),
                                                 &encoded_length));
    TEST_CHECK_U64(golden_length, encoded_length);
    TEST_CHECK(memcmp(golden, encoded, golden_length) == 0);

    /* Header fields are exactly the documented little-endian layout. */
    TEST_CHECK_U64(0x4cu, golden[0]);
    TEST_CHECK_U64(0x4bu, golden[1]);
    TEST_CHECK_U64(1u, golden[2]);
    TEST_CHECK_U64(2u, golden[3]);
    TEST_CHECK_U64(16u, (uint64_t)golden[4] | ((uint64_t)golden[5] << 8));
    TEST_CHECK_U64(2u, (uint64_t)golden[6]);
    TEST_CHECK_U64(0u, (uint64_t)golden[7]);
    TEST_CHECK_U64(0u, (uint64_t)golden[8]);
    TEST_CHECK_U64(0u, (uint64_t)golden[9]);
}

/* The golden frames must be accepted by the production stream parser. */
static void test_golden_frames_parse(void)
{
    uint8_t hello[LOCKER_WIRE_MAX_FRAME];
    uint8_t snapshot[LOCKER_WIRE_MAX_FRAME];
    size_t hello_length;
    size_t snapshot_length;

    hello_length = decode_hex(GOLDEN_HELLO_FRAME_HEX, hello, sizeof(hello));
    snapshot_length = decode_hex(GOLDEN_SNAPSHOT_FRAME_HEX, snapshot, sizeof(snapshot));
    TEST_CHECK(hello_length != 0u);
    TEST_CHECK(snapshot_length != 0u);
    {
        locker_wire_stream_t stream;
        frame_capture_t capture;

        test_capture_reset(&capture);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                          test_capture_feed(&capture, &stream, hello, hello_length));
        TEST_CHECK_U64(1u, capture.count);
        TEST_CHECK_U64(1u, capture.frames[0].type);
        TEST_CHECK_U64(1u, capture.frames[0].request_no);
        TEST_CHECK_U64(49u, capture.frames[0].payload_len);
        TEST_CHECK_U64(0u, stream.stats.resyncs);
        TEST_CHECK_U64(0u, stream.stats.crc_errors);
        TEST_CHECK_U64(0u, stream.stats.length_errors);
    }
    {
        locker_wire_stream_t stream;
        frame_capture_t capture;

        test_capture_reset(&capture);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                          test_capture_feed(&capture, &stream, snapshot, snapshot_length));
        TEST_CHECK_U64(1u, capture.count);
        TEST_CHECK_U64(2u, capture.frames[0].type);
        TEST_CHECK_U64(2u, capture.frames[0].request_no);
        TEST_CHECK_U64(16u, capture.frames[0].payload_len);
        TEST_CHECK_U64(0u, stream.stats.crc_errors);
    }
    /* Both frames concatenated parse as two frames (the stream is not framed by reads). */
    {
        uint8_t both[LOCKER_WIRE_MAX_FRAME * 2u];
        locker_wire_stream_t stream;
        frame_capture_t capture;

        memcpy(both, hello, hello_length);
        memcpy(both + hello_length, snapshot, snapshot_length);
        test_capture_reset(&capture);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                          test_capture_feed(&capture, &stream, both, hello_length + snapshot_length));
        TEST_CHECK_U64(2u, capture.count);
        TEST_CHECK_U64(1u, capture.frames[0].type);
        TEST_CHECK_U64(2u, capture.frames[1].type);
    }
    /* Fed one byte at a time the parser still assembles both frames. */
    {
        uint8_t both[LOCKER_WIRE_MAX_FRAME * 2u];
        size_t total;
        size_t index;
        locker_wire_stream_t stream;
        frame_capture_t capture;

        memcpy(both, hello, hello_length);
        memcpy(both + hello_length, snapshot, snapshot_length);
        total = hello_length + snapshot_length;
        locker_wire_stream_init(&stream, &capture, test_capture_handler);
        test_capture_reset(&capture);
        for (index = 0u; index < total; ++index) {
            TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                              locker_wire_stream_feed(&stream, &both[index], 1u));
        }
        TEST_CHECK_U64(2u, capture.count);
        TEST_CHECK_U64(1u, capture.frames[0].type);
        TEST_CHECK_U64(2u, capture.frames[1].type);
    }
}

/* The invalid golden vectors must be rejected, not reinterpreted. */
static void test_golden_invalid_frames(void)
{
    uint8_t crc_error[LOCKER_WIRE_MAX_FRAME];
    uint8_t oversized[LOCKER_WIRE_MAX_FRAME];
    size_t crc_error_length;
    size_t oversized_length;

    crc_error_length = decode_hex(GOLDEN_CRC_ERROR_FRAME_HEX, crc_error, sizeof(crc_error));
    oversized_length = decode_hex(GOLDEN_OVERSIZED_FRAME_HEX, oversized, sizeof(oversized));
    TEST_CHECK_U64(28u, crc_error_length);
    TEST_CHECK_U64(10u, oversized_length);
    /* The corrupted frame differs from the valid one only in the CRC byte. */
    TEST_CHECK_U64(0xebu, crc_error[27]);
    TEST_CHECK_U64(513u, (uint64_t)oversized[4] | ((uint64_t)oversized[5] << 8));

    {
        locker_wire_stream_t stream;
        frame_capture_t capture;

        test_capture_reset(&capture);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                          test_capture_feed(&capture, &stream, crc_error, crc_error_length));
        TEST_CHECK_U64(0u, capture.count);
        TEST_CHECK_U64(1u, stream.stats.crc_errors);
    }
    {
        locker_wire_stream_t stream;
        frame_capture_t capture;

        test_capture_reset(&capture);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                          test_capture_feed(&capture, &stream, oversized, oversized_length));
        TEST_CHECK_U64(0u, capture.count);
        TEST_CHECK_U64(1u, stream.stats.length_errors);
    }
    /* A corrupted frame followed by a valid one still delivers the valid one. */
    {
        uint8_t mixed[LOCKER_WIRE_MAX_FRAME * 2u];
        uint8_t snapshot[LOCKER_WIRE_MAX_FRAME];
        size_t snapshot_length;
        locker_wire_stream_t stream;
        frame_capture_t capture;

        snapshot_length = decode_hex(GOLDEN_SNAPSHOT_FRAME_HEX, snapshot, sizeof(snapshot));
        memcpy(mixed, crc_error, crc_error_length);
        memcpy(mixed + crc_error_length, snapshot, snapshot_length);
        test_capture_reset(&capture);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                          test_capture_feed(&capture, &stream, mixed,
                                            crc_error_length + snapshot_length));
        TEST_CHECK_U64(1u, capture.count);
        TEST_CHECK_U64(2u, capture.frames[0].type);
        TEST_CHECK_U64(1u, stream.stats.crc_errors);
    }
}

/* End to end on the simulator: the golden HELLO opens a session, then the golden
 * snapshot request is accepted with the session it returned. */
static void test_golden_frames_on_simulator(void)
{
    sim_fixture_t fixture;
    uint8_t hello[LOCKER_WIRE_MAX_FRAME];
    uint8_t snapshot[LOCKER_WIRE_MAX_FRAME];
    size_t hello_length;
    size_t snapshot_length;
    locker_wire_response_union_t response;
    uint8_t reply_type = 0u;
    uint32_t reply_request_no = 0u;

    hello_length = decode_hex(GOLDEN_HELLO_FRAME_HEX, hello, sizeof(hello));
    snapshot_length = decode_hex(GOLDEN_SNAPSHOT_FRAME_HEX, snapshot, sizeof(snapshot));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "golden_frames"));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_send(&fixture, hello, hello_length));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_fixture_reply(&fixture, &response, &reply_type, &reply_request_no));
    TEST_CHECK_U64((uint64_t)(1u | LOCKER_WIRE_RESPONSE_FLAG), reply_type);
    TEST_CHECK_U64(1u, reply_request_no);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, response.hello.result);
    /* The session the device echoes is the one the golden frame proposed. */
    {
        static const uint8_t expected_session[16] = {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                                     0x40u, 0x00u, 0x80u, 0x00u, 0x00u, 0x00u,
                                                     0x00u, 0x00u, 0x00u, 0x0au};

        TEST_CHECK(memcmp(expected_session, response.hello.session_id.bytes, 16u) == 0);
    }
    /* The golden snapshot request reuses that session and is accepted. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_send(&fixture, snapshot, snapshot_length));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_fixture_reply(&fixture, &response, &reply_type, &reply_request_no));
    TEST_CHECK_U64((uint64_t)(2u | LOCKER_WIRE_RESPONSE_FLAG), reply_type);
    TEST_CHECK_U64(2u, reply_request_no);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, response.get_snapshot.result);
    TEST_CHECK_U64(1u, response.get_snapshot.boot_id);
    sim_fixture_close(&fixture);
}

int main(void)
{
    test_crc_canonical_check();
    test_hello_request_golden_frame();
    test_snapshot_request_golden_frame();
    test_golden_frames_parse();
    test_golden_invalid_frames();
    test_golden_frames_on_simulator();
    TEST_REPORT("contract.device.test_golden_frames");
}
