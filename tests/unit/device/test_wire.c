/*
 * S1-03 unit tests: UART v1 byte framing (host/device/src/wire.c).
 *
 * Everything here is byte level: CRC, frame layout, and the incremental parser
 * that must never trust a peer-supplied length.
 */

#include "device_test_support.h"
#include "test_support.h"

static void test_crc_golden(void)
{
    static const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

    /* uart-v1.md:19 canonical check value. */
    TEST_CHECK_U64(0x29b1u, locker_wire_crc16(check, sizeof(check)));
    TEST_CHECK_U64(0xffffu, locker_wire_crc16(NULL, 0u));
}

static void test_frame_size(void)
{
    TEST_CHECK_U64(12u, locker_wire_frame_size(0u));
    TEST_CHECK_U64(524u, locker_wire_frame_size(LOCKER_WIRE_MAX_PAYLOAD));
    TEST_CHECK_U64(16u, locker_wire_frame_size(4u));
}

static void test_request_type_bounds(void)
{
    TEST_CHECK(!locker_wire_request_type_valid(0u));
    TEST_CHECK(locker_wire_request_type_valid(1u));
    TEST_CHECK(locker_wire_request_type_valid(11u));
    TEST_CHECK(!locker_wire_request_type_valid(12u));
    TEST_CHECK(!locker_wire_request_type_valid((uint8_t)(1u | LOCKER_WIRE_RESPONSE_FLAG)));
}

static void test_encode_layout(void)
{
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    uint8_t payload[3] = {0xaau, 0xbbu, 0xccu};
    size_t length = 0u;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_frame(1u, 0x11223344u, payload, sizeof(payload), frame,
                                               sizeof(frame), &length));
    TEST_CHECK_U64(15u, length);
    TEST_CHECK_U64(0x4cu, frame[0]);
    TEST_CHECK_U64(0x4bu, frame[1]);
    TEST_CHECK_U64(1u, frame[2]);
    TEST_CHECK_U64(1u, frame[3]);
    /* payload_len little-endian */
    TEST_CHECK_U64(3u, frame[4]);
    TEST_CHECK_U64(0u, frame[5]);
    /* request_no little-endian */
    TEST_CHECK_U64(0x44u, frame[6]);
    TEST_CHECK_U64(0x33u, frame[7]);
    TEST_CHECK_U64(0x22u, frame[8]);
    TEST_CHECK_U64(0x11u, frame[9]);
    TEST_CHECK_U64(0xaau, frame[10]);
    TEST_CHECK_U64(0xbbu, frame[11]);
    TEST_CHECK_U64(0xccu, frame[12]);
    /* CRC covers offset 2 .. end of payload, little-endian on the wire. */
    {
        uint16_t expected = locker_wire_crc16(&frame[2], 1u + 1u + 2u + 4u + 3u);
        TEST_CHECK_U64(expected & 0xffu, frame[13]);
        TEST_CHECK_U64((expected >> 8) & 0xffu, frame[14]);
    }
}

static void test_encode_rejects_bad_arguments(void)
{
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    uint8_t payload[LOCKER_WIRE_MAX_PAYLOAD + 1u];

    memset(payload, 0, sizeof(payload));
    /* Request number 0 is reserved for asynchronous events. */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_wire_encode_frame(1u, 0u, payload, 4u, frame, sizeof(frame), &length));
    /* Device event must carry request_no 0. */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_wire_encode_frame(LOCKER_WIRE_DEVICE_EVENT, 7u, payload, 4u, frame,
                                               sizeof(frame), &length));
    /* Unknown type. */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_wire_encode_frame(12u, 1u, payload, 4u, frame, sizeof(frame),
                                               &length));
    /* DEVICE_EVENT has no response form: 0x40|0x80 is not an encodable type,
     * even though the CRC/parser layer would still accept the bytes. */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_wire_encode_frame((uint8_t)(LOCKER_WIRE_DEVICE_EVENT | LOCKER_WIRE_RESPONSE_FLAG),
                                               1u, payload, 4u, frame, sizeof(frame), &length));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_wire_encode_frame((uint8_t)(LOCKER_WIRE_DEVICE_EVENT | LOCKER_WIRE_RESPONSE_FLAG),
                                               0u, payload, 4u, frame, sizeof(frame), &length));
    /* Payload above the maximum must be refused, not truncated. */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_wire_encode_frame(1u, 1u, payload, sizeof(payload), frame,
                                               sizeof(frame), &length));
    /* Too small an output buffer is NO_SPACE. */
    TEST_CHECK_STATUS(LOCKER_STATUS_NO_SPACE,
                      locker_wire_encode_frame(1u, 1u, payload, 4u, frame, 8u, &length));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_wire_encode_frame(1u, 1u, NULL, 4u, frame, sizeof(frame), &length));
}

static void test_parser_roundtrip(void)
{
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    frame_capture_t capture;
    locker_wire_stream_t stream;
    uint8_t payload[5] = {1u, 2u, 3u, 4u, 5u};

    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_frame(3u, 99u, payload, sizeof(payload), frame,
                                               sizeof(frame), &length));
    test_capture_reset(&capture);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      test_capture_feed(&capture, &stream, frame, length));
    TEST_CHECK_U64(1u, capture.count);
    TEST_CHECK_U64(3u, capture.frames[0].type);
    TEST_CHECK_U64(1u, capture.frames[0].version);
    TEST_CHECK_U64(99u, capture.frames[0].request_no);
    TEST_CHECK_U64(sizeof(payload), capture.frames[0].payload_len);
    TEST_CHECK_U64(1u, capture.frames[0].payload[0]);
    TEST_CHECK_U64(5u, capture.frames[0].payload[4]);
    TEST_CHECK_U64(1u, stream.stats.frames_accepted);
    TEST_CHECK_U64(0u, stream.stats.crc_errors);
}

/* One byte at a time: the parser must not depend on read boundaries. */
static void test_parser_byte_at_a_time(void)
{
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    frame_capture_t capture;
    locker_wire_stream_t stream;
    uint8_t payload[7] = {9u, 8u, 7u, 6u, 5u, 4u, 3u};
    size_t index;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_frame(2u, 5u, payload, sizeof(payload), frame,
                                               sizeof(frame), &length));
    test_capture_reset(&capture);
    locker_wire_stream_init(&stream, &capture, test_capture_handler);
    for (index = 0u; index < length; index += 1u) {
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_wire_stream_feed(&stream, &frame[index], 1u));
        TEST_CHECK_U64(index + 1u < length ? index + 1u : 0u,
                       index + 1u < length ? locker_wire_stream_pending(&stream) : 0u);
    }
    TEST_CHECK_U64(1u, capture.count);
    TEST_CHECK_U64(2u, capture.frames[0].type);
    TEST_CHECK_U64(0u, locker_wire_stream_pending(&stream));
}

/* Two frames in one write, then a third split across two writes. */
static void test_parser_sticky_frames(void)
{
    uint8_t first[LOCKER_WIRE_MAX_FRAME];
    uint8_t second[LOCKER_WIRE_MAX_FRAME];
    uint8_t combined[LOCKER_WIRE_MAX_FRAME * 2u];
    size_t first_length = 0u;
    size_t second_length = 0u;
    frame_capture_t capture;
    locker_wire_stream_t stream;
    uint8_t payload[2] = {0x11u, 0x22u};

    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_frame(1u, 1u, payload, sizeof(payload), first,
                                               sizeof(first), &first_length));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_frame(2u, 2u, payload, sizeof(payload), second,
                                               sizeof(second), &second_length));
    memcpy(combined, first, first_length);
    memcpy(combined + first_length, second, second_length);
    test_capture_reset(&capture);
    locker_wire_stream_init(&stream, &capture, test_capture_handler);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_stream_feed(&stream, combined, first_length + second_length));
    TEST_CHECK_U64(2u, capture.count);
    TEST_CHECK_U64(1u, capture.frames[0].request_no);
    TEST_CHECK_U64(2u, capture.frames[1].request_no);

    test_capture_reset(&capture);
    locker_wire_stream_init(&stream, &capture, test_capture_handler);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_stream_feed(&stream, combined, 4u));
    TEST_CHECK_U64(0u, capture.count);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_stream_feed(&stream, combined + 4u,
                                              first_length + second_length - 4u));
    TEST_CHECK_U64(2u, capture.count);
}

/* Leading garbage before a valid frame is skipped one byte at a time. */
static void test_parser_resync(void)
{
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    uint8_t noisy[LOCKER_WIRE_MAX_FRAME + 5u];
    size_t length = 0u;
    frame_capture_t capture;
    locker_wire_stream_t stream;
    uint8_t payload[1] = {0x5au};

    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_frame(1u, 42u, payload, sizeof(payload), frame,
                                               sizeof(frame), &length));
    memset(noisy, 0x00, 5u);
    memcpy(noisy + 5u, frame, length);
    test_capture_reset(&capture);
    locker_wire_stream_init(&stream, &capture, test_capture_handler);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_wire_stream_feed(&stream, noisy, 5u + length));
    TEST_CHECK_U64(1u, capture.count);
    TEST_CHECK_U64(42u, capture.frames[0].request_no);
    TEST_CHECK_U64(5u, stream.stats.resyncs);
}

/* A corrupted CRC drops the frame and re-searches for magic. */
static void test_parser_crc_error(void)
{
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    frame_capture_t capture;
    locker_wire_stream_t stream;
    uint8_t payload[2] = {0x01u, 0x02u};

    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_frame(1u, 7u, payload, sizeof(payload), frame,
                                               sizeof(frame), &length));
    frame[length - 1u] ^= 0xffu;
    test_capture_reset(&capture);
    locker_wire_stream_init(&stream, &capture, test_capture_handler);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_wire_stream_feed(&stream, frame, length));
    TEST_CHECK_U64(0u, capture.count);
    TEST_CHECK(stream.stats.crc_errors > 0u);
    TEST_CHECK(stream.stats.resyncs > 0u);
}

/* payload_len above 512 must never be used to size an allocation or a wait. */
static void test_parser_oversized_length(void)
{
    uint8_t header[LOCKER_WIRE_HEADER_SIZE];
    frame_capture_t capture;
    locker_wire_stream_t stream;

    header[0] = LOCKER_WIRE_MAGIC_FIRST;
    header[1] = LOCKER_WIRE_MAGIC_SECOND;
    header[2] = LOCKER_WIRE_VERSION;
    header[3] = 1u;
    header[4] = 0x01u; /* 513 = 0x0201 */
    header[5] = 0x02u;
    header[6] = 1u;
    header[7] = 0u;
    header[8] = 0u;
    header[9] = 0u;
    test_capture_reset(&capture);
    locker_wire_stream_init(&stream, &capture, test_capture_handler);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_stream_feed(&stream, header, sizeof(header)));
    TEST_CHECK_U64(0u, capture.count);
    TEST_CHECK(stream.stats.length_errors > 0u);
    TEST_CHECK(locker_wire_stream_pending(&stream) < LOCKER_WIRE_MAX_FRAME);
}

/* A maximum-size frame is accepted and leaves the buffer empty. */
static void test_parser_maximum_frame(void)
{
    uint8_t payload[LOCKER_WIRE_MAX_PAYLOAD];
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    frame_capture_t capture;
    locker_wire_stream_t stream;

    memset(payload, 0xa5, sizeof(payload));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_frame(1u, 1u, payload, sizeof(payload), frame,
                                               sizeof(frame), &length));
    TEST_CHECK_U64(LOCKER_WIRE_MAX_FRAME, length);
    test_capture_reset(&capture);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, test_capture_feed(&capture, &stream, frame, length));
    TEST_CHECK_U64(1u, capture.count);
    TEST_CHECK_U64(LOCKER_WIRE_MAX_PAYLOAD, capture.frames[0].payload_len);
    TEST_CHECK_U64(0u, locker_wire_stream_pending(&stream));
}

static locker_status_t reject_frame(void *user, const locker_wire_frame_t *frame)
{
    unsigned *calls = (unsigned *)user;
    (void)frame;
    ++*calls;
    return LOCKER_STATUS_BUSY;
}

/* A real rejecting handler is called once; buffered trailing bytes remain. */
static void test_parser_handler_abort(void)
{
    uint8_t frames[LOCKER_WIRE_MAX_FRAME * 2u];
    size_t length = 0u;
    locker_wire_stream_t stream;
    uint8_t payload[1] = {1u};
    unsigned calls = 0u;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_frame(1u, 1u, payload, sizeof(payload), frames,
                                               sizeof(frames), &length));
    locker_wire_stream_init(&stream, NULL, NULL);
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_wire_stream_feed(&stream, frames, length));
    TEST_CHECK_U64(0u, calls);
    memcpy(frames + length, frames, length);
    locker_wire_stream_init(&stream, &calls, reject_frame);
    TEST_CHECK_STATUS(LOCKER_STATUS_BUSY, locker_wire_stream_feed(&stream, frames, length * 2u));
    TEST_CHECK_U64(1u, calls);
    TEST_CHECK_U64(1u, stream.stats.handler_errors);
    TEST_CHECK_U64(length, locker_wire_stream_pending(&stream));
}

static void test_cursor_validity(void)
{
    locker_wire_cursor_t cursor;

    cursor.boot_id = 0u;
    cursor.event_seq = 0u;
    TEST_CHECK(locker_wire_cursor_valid(&cursor));
    cursor.boot_id = 1u;
    cursor.event_seq = 0u;
    TEST_CHECK(!locker_wire_cursor_valid(&cursor));
    cursor.boot_id = 0u;
    cursor.event_seq = 1u;
    TEST_CHECK(!locker_wire_cursor_valid(&cursor));
    cursor.boot_id = 3u;
    cursor.event_seq = 9u;
    TEST_CHECK(locker_wire_cursor_valid(&cursor));
    TEST_CHECK(!locker_wire_cursor_valid(NULL));
}

int main(void)
{
    test_crc_golden();
    test_frame_size();
    test_request_type_bounds();
    test_encode_layout();
    test_encode_rejects_bad_arguments();
    test_parser_roundtrip();
    test_parser_byte_at_a_time();
    test_parser_sticky_frames();
    test_parser_resync();
    test_parser_crc_error();
    test_parser_oversized_length();
    test_parser_maximum_frame();
    test_parser_handler_abort();
    test_cursor_validity();
    TEST_REPORT("device.test_wire");
}
