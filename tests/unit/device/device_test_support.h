#ifndef LOCKER_DEVICE_TEST_SUPPORT_H
#define LOCKER_DEVICE_TEST_SUPPORT_H

/*
 * Shared helpers for the S1-03 device tests.
 *
 * Same rules as S1-01/S1-02: no third-party framework, no network, no daemon.
 * Everything here builds real wire bytes through the production codec and drives
 * the production simulator engine in-process.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "locker/common/clock.h"
#include "locker/common/uuid.h"
#include "locker/device/codec.h"
#include "locker/device/wire.h"
#include "locker/simulator/sim.h"

/*
 * Header-only helpers are compiled into every test binary, so any helper a
 * given test does not call must not trigger -Wunused-function under -Werror.
 */
#if defined(__GNUC__)
#define LOCKER_TEST_HELPER __attribute__((unused))
#else
#define LOCKER_TEST_HELPER
#endif

/*
 * Upper bound for every "keep fetching while has_more" helper loop. A page
 * sequence that never terminates is a defect in the engine, so the loop must
 * fail the run instead of spinning: an unbounded loop under a broken
 * has_more/next_after pair once produced eleven million assertion lines.
 */
#define LOCKER_TEST_PAGE_LIMIT 64u

/* ---- deterministic fake clock (tests only; never reachable from the library) ---- */

typedef struct test_clock {
    uint64_t monotonic_ms;
    uint64_t wall_ms;
    uint32_t uuid_counter;
} test_clock_t;

LOCKER_TEST_HELPER static uint64_t test_clock_monotonic(void *user)
{
    return ((test_clock_t *)user)->monotonic_ms;
}

LOCKER_TEST_HELPER static bool test_clock_wall(void *user, uint64_t *out_wall_ms)
{
    *out_wall_ms = ((test_clock_t *)user)->wall_ms;
    return true;
}

LOCKER_TEST_HELPER static locker_status_t test_clock_uuid(void *user, locker_uuid_t *out_uuid)
{
    test_clock_t *clock = (test_clock_t *)user;
    uint32_t value = ++clock->uuid_counter;
    size_t index;

    memset(out_uuid, 0, sizeof(*out_uuid));
    /* Version 4, variant 10xx so the value is a real RFC 4122 v4 UUID. */
    out_uuid->bytes[0] = (uint8_t)(value >> 24);
    out_uuid->bytes[1] = (uint8_t)(value >> 16);
    out_uuid->bytes[2] = (uint8_t)(value >> 8);
    out_uuid->bytes[3] = (uint8_t)value;
    out_uuid->bytes[4] = (uint8_t)(value ^ 0x5au);
    out_uuid->bytes[5] = (uint8_t)(value ^ 0xa5u);
    out_uuid->bytes[6] = (uint8_t)((value >> 8) ^ 0x3cu);
    out_uuid->bytes[7] = (uint8_t)(value ^ 0xc3u);
    out_uuid->bytes[8] = 0x80u | (uint8_t)(value & 0x3fu);
    for (index = 9u; index < 16u; index += 1u) {
        out_uuid->bytes[index] = (uint8_t)(value + index);
    }
    return LOCKER_STATUS_OK;
}

LOCKER_TEST_HELPER static locker_clock_port_t test_clock_port(test_clock_t *clock)
{
    locker_clock_port_t port;

    memset(&port, 0, sizeof(port));
    port.user = clock;
    port.monotonic_ms = test_clock_monotonic;
    port.wall_utc_ms = test_clock_wall;
    port.new_uuid = test_clock_uuid;
    return port;
}

LOCKER_TEST_HELPER static void test_uuid_fill(locker_uuid_t *uuid, uint8_t seed)
{
    size_t index;

    for (index = 0u; index < sizeof(uuid->bytes); index += 1u) {
        uuid->bytes[index] = (uint8_t)(seed + index);
    }
    uuid->bytes[6] = (uint8_t)((uuid->bytes[6] & 0x0fu) | 0x40u);
    uuid->bytes[8] = (uint8_t)((uuid->bytes[8] & 0x3fu) | 0x80u);
}

/* ---- UUID observation helpers (simulator accessors return by value) ---- */

LOCKER_TEST_HELPER static bool sim_session_is_nil(const locker_sim_t *sim)
{
    locker_uuid_t session = locker_sim_session_id(sim);

    return locker_uuid_is_nil(&session);
}

LOCKER_TEST_HELPER static bool sim_session_equals(const locker_sim_t *sim, const locker_uuid_t *expected)
{
    locker_uuid_t session = locker_sim_session_id(sim);

    return locker_uuid_equal(&session, expected);
}

LOCKER_TEST_HELPER static bool sim_epoch_is_nil(const locker_sim_t *sim)
{
    locker_uuid_t epoch = locker_sim_journal_epoch(sim);

    return locker_uuid_is_nil(&epoch);
}

LOCKER_TEST_HELPER static bool sim_epoch_equals(const locker_sim_t *sim, const locker_uuid_t *expected)
{
    locker_uuid_t epoch = locker_sim_journal_epoch(sim);

    return locker_uuid_equal(&epoch, expected);
}

/* ---- frame capture through the production stream parser ---- */

typedef struct frame_capture {
    locker_wire_frame_t frames[8];
    size_t count;
    size_t dropped;
} frame_capture_t;

LOCKER_TEST_HELPER static locker_status_t test_capture_handler(void *user, const locker_wire_frame_t *frame)
{
    frame_capture_t *capture = (frame_capture_t *)user;

    if (capture->count >= (sizeof(capture->frames) / sizeof(capture->frames[0]))) {
        capture->dropped += 1u;
        return LOCKER_STATUS_OK;
    }
    capture->frames[capture->count] = *frame;
    capture->count += 1u;
    return LOCKER_STATUS_OK;
}

LOCKER_TEST_HELPER static void test_capture_reset(frame_capture_t *capture)
{
    memset(capture, 0, sizeof(*capture));
}

/* Feeds bytes through the real parser and collects every complete frame. */
LOCKER_TEST_HELPER static locker_status_t test_capture_feed(frame_capture_t *capture, locker_wire_stream_t *stream,
                                         const uint8_t *data, size_t length)
{
    locker_wire_stream_init(stream, capture, test_capture_handler);
    return locker_wire_stream_feed(stream, data, length);
}

/* ---- request frame builders ---- */

LOCKER_TEST_HELPER static locker_status_t test_build_request(uint8_t type, uint32_t request_no,
                                          const locker_wire_request_union_t *request,
                                          uint8_t *out, size_t out_size, size_t *out_length)
{
    return locker_wire_encode_request(type, request_no, request, out, out_size, out_length);
}

/* ---- simulator fixture ---- */

typedef struct sim_fixture {
    test_clock_t clock;
    locker_sim_t *sim;
    char journal_path[LOCKER_SIM_PATH_SIZE];
    uint8_t reply[LOCKER_WIRE_MAX_FRAME];
    size_t reply_length;
    uint8_t events[LOCKER_WIRE_MAX_FRAME];
    size_t events_length;
} sim_fixture_t;

LOCKER_TEST_HELPER static void sim_fixture_path(sim_fixture_t *fixture, const char *tag)
{
    static unsigned int counter;

    counter += 1u;
    (void)snprintf(fixture->journal_path, sizeof(fixture->journal_path), "/tmp/locker_s103_%s_%u_%u.db",
                   tag, (unsigned int)getpid(), counter);
    (void)unlink(fixture->journal_path);
}

LOCKER_TEST_HELPER static locker_status_t sim_fixture_open(sim_fixture_t *fixture, const char *tag)
{
    locker_sim_config_t config;

    memset(fixture, 0, sizeof(*fixture));
    fixture->clock.monotonic_ms = 1000u;
    fixture->clock.wall_ms = 1700000000000u;
    sim_fixture_path(fixture, tag);
    memset(&config, 0, sizeof(config));
    (void)snprintf(config.journal_path, sizeof(config.journal_path), "%s", fixture->journal_path);
    config.device_uid[0] = 0x01u;
    config.device_uid[11] = 0x0cu;
    config.clock = test_clock_port(&fixture->clock);
    if (locker_sim_create(&fixture->sim, &config) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_IO_ERROR;
    }
    return locker_sim_open(fixture->sim);
}

LOCKER_TEST_HELPER static void sim_fixture_close(sim_fixture_t *fixture)
{
    if (fixture->sim != NULL) {
        locker_sim_close(fixture->sim);
        locker_sim_destroy(fixture->sim);
        fixture->sim = NULL;
    }
    (void)unlink(fixture->journal_path);
    {
        char wal[LOCKER_SIM_PATH_SIZE + 8u];
        (void)snprintf(wal, sizeof(wal), "%s-wal", fixture->journal_path);
        (void)unlink(wal);
        (void)snprintf(wal, sizeof(wal), "%s-shm", fixture->journal_path);
        (void)unlink(wal);
    }
}

/* Sends one request frame to the simulator and keeps the reply bytes. */
LOCKER_TEST_HELPER static locker_status_t sim_fixture_send(sim_fixture_t *fixture, const uint8_t *frame,
                                        size_t length)
{
    fixture->reply_length = 0u;
    fixture->events_length = 0u;
    return locker_sim_handle_frame(fixture->sim, frame, length, fixture->reply,
                                   sizeof(fixture->reply), &fixture->reply_length);
}

/* Decodes the stored reply into a response union. */
LOCKER_TEST_HELPER static uint16_t sim_fixture_reply(sim_fixture_t *fixture, locker_wire_response_union_t *response,
                                  uint8_t *out_type, uint32_t *out_request_no)
{
    frame_capture_t capture;
    locker_wire_stream_t stream;
    uint16_t result;

    test_capture_reset(&capture);
    if (test_capture_feed(&capture, &stream, fixture->reply, fixture->reply_length) !=
        LOCKER_STATUS_OK) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (capture.count != 1u) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (out_type != NULL) {
        *out_type = capture.frames[0].type;
    }
    if (out_request_no != NULL) {
        *out_request_no = capture.frames[0].request_no;
    }
    result = locker_wire_decode_response(&capture.frames[0], response);
    return result;
}

/* Drains every queued DEVICE_EVENT frame into decoded records. */
LOCKER_TEST_HELPER static size_t sim_fixture_events(sim_fixture_t *fixture, locker_wire_event96_t *records,
                                 size_t max_records, locker_uuid_t *out_session,
                                 locker_uuid_t *out_epoch)
{
    size_t count = 0u;
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    size_t frame_length = 0u;

    while (count < max_records &&
           locker_sim_pop_event(fixture->sim, frame, sizeof(frame), &frame_length) ==
               LOCKER_STATUS_OK) {
        frame_capture_t capture;
        locker_wire_stream_t stream;
        locker_uuid_t session;
        locker_uuid_t epoch;

        test_capture_reset(&capture);
        if (test_capture_feed(&capture, &stream, frame, frame_length) != LOCKER_STATUS_OK ||
            capture.count != 1u) {
            break;
        }
        if (locker_wire_decode_device_event(&capture.frames[0], &session, &epoch,
                                            &records[count]) != LOCKER_WIRE_RESULT_OK) {
            break;
        }
        if (out_session != NULL) {
            *out_session = session;
        }
        if (out_epoch != NULL) {
            *out_epoch = epoch;
        }
        count += 1u;
    }
    return count;
}

#endif /* LOCKER_DEVICE_TEST_SUPPORT_H */
