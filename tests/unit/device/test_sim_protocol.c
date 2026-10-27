/*
 * S1-03: UART-v1 protocol semantics on the simulator engine.
 *
 * Everything here goes through locker_sim_handle_frame, so it exercises the
 * same framing, validation and state machine the daemon uses. The frame bytes
 * come from the codec, never from hand-built structs (uart-v1.md:7 forbids
 * putting C structs on the wire).
 */

#include <stdio.h>
#include <string.h>

#include "device_test_support.h"
#include "test_support.h"

static uint16_t sim_exchange(sim_fixture_t *fixture, uint8_t type, uint32_t request_no,
                             const locker_wire_request_union_t *request,
                             locker_wire_response_union_t *response)
{
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    size_t frame_length = 0u;
    uint8_t reply_type = 0u;
    uint32_t reply_request_no = 0u;
    uint16_t decoded;

    if (test_build_request(type, request_no, request, frame, sizeof(frame), &frame_length) !=
        LOCKER_STATUS_OK) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (sim_fixture_send(fixture, frame, frame_length) != LOCKER_STATUS_OK) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    decoded = sim_fixture_reply(fixture, response, &reply_type, &reply_request_no);
    if (decoded != LOCKER_WIRE_RESULT_OK) {
        return decoded;
    }
    if (reply_type != (uint8_t)(type | LOCKER_WIRE_RESPONSE_FLAG) || reply_request_no != request_no) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    return locker_wire_response_result(type, response);
}

static void sim_hello_ok(sim_fixture_t *fixture, const locker_uuid_t *new_session,
                         locker_wire_hello_response_t *out)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;

    memset(&request, 0, sizeof(request));
    request.hello.new_session_id = *new_session;
    test_uuid_fill(&request.hello.host_instance_id, 0x40u);
    request.hello.requested_version = LOCKER_WIRE_VERSION;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_exchange(fixture, 1u, 1u, &request, &response));
    if (out != NULL) {
        *out = response.hello;
    }
}

static void sim_snapshot_ok(sim_fixture_t *fixture, const locker_uuid_t *session,
                            locker_wire_get_snapshot_response_t *out)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;

    memset(&request, 0, sizeof(request));
    request.get_snapshot.session_id = *session;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_exchange(fixture, 2u, 2u, &request, &response));
    *out = response.get_snapshot;
}

static uint16_t sim_execute_at(sim_fixture_t *fixture, const locker_uuid_t *session,
                               const locker_wire_get_snapshot_response_t *snapshot,
                               uint64_t action_no, const locker_uuid_t *command_id, uint8_t kind,
                               uint64_t not_after_ms, locker_wire_execute_unlock_response_t *out)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint16_t result;

    memset(&request, 0, sizeof(request));
    request.execute_unlock.session_id = *session;
    request.execute_unlock.journal_epoch = snapshot->journal_epoch;
    request.execute_unlock.expected_boot_id = snapshot->boot_id;
    request.execute_unlock.expected_physical_revision = snapshot->physical_revision;
    request.execute_unlock.action_no = action_no;
    request.execute_unlock.not_after_mcu_ms = not_after_ms;
    test_uuid_fill(&request.execute_unlock.operation_id, 0x60u);
    request.execute_unlock.command_id = *command_id;
    request.execute_unlock.slot_id = 1u;
    request.execute_unlock.kind = kind;
    result = sim_exchange(fixture, 7u, 20u + (uint32_t)action_no, &request, &response);
    if (result == LOCKER_WIRE_RESULT_OK && out != NULL) {
        *out = response.execute_unlock;
    }
    return result;
}

static uint16_t sim_fetch_at(sim_fixture_t *fixture, const locker_uuid_t *session,
                             const locker_uuid_t *epoch, locker_wire_cursor_t after, uint8_t limit,
                             locker_wire_fetch_events_response_t *out)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint16_t result;

    memset(&request, 0, sizeof(request));
    request.fetch_events.session_id = *session;
    request.fetch_events.journal_epoch = *epoch;
    request.fetch_events.after = after;
    request.fetch_events.max_count = limit;
    result = sim_exchange(fixture, 3u, 40u, &request, &response);
    if (result == LOCKER_WIRE_RESULT_OK && out != NULL) {
        *out = response.fetch_events;
    }
    return result;
}

static uint16_t sim_ack_at(sim_fixture_t *fixture, const locker_uuid_t *session,
                           const locker_uuid_t *epoch, locker_wire_cursor_t through,
                           locker_wire_ack_persisted_response_t *out)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint16_t result;

    memset(&request, 0, sizeof(request));
    request.ack_persisted.session_id = *session;
    request.ack_persisted.journal_epoch = *epoch;
    request.ack_persisted.through = through;
    result = sim_exchange(fixture, 4u, 42u, &request, &response);
    if (result == LOCKER_WIRE_RESULT_OK && out != NULL) {
        *out = response.ack_persisted;
    }
    return result;
}

static uint16_t sim_resume_at(sim_fixture_t *fixture, const locker_uuid_t *session,
                              const locker_uuid_t *epoch, uint64_t boot_id, uint64_t revision,
                              locker_wire_cursor_t through,
                              locker_wire_resume_ready_response_t *out)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint16_t result;

    memset(&request, 0, sizeof(request));
    request.resume_ready.session_id = *session;
    request.resume_ready.journal_epoch = *epoch;
    request.resume_ready.expected_boot_id = boot_id;
    request.resume_ready.expected_physical_revision = revision;
    request.resume_ready.persisted_through = through;
    result = sim_exchange(fixture, 5u, 43u, &request, &response);
    if (result == LOCKER_WIRE_RESULT_OK && out != NULL) {
        *out = response.resume_ready;
    }
    return result;
}

static uint16_t sim_disarm_at(sim_fixture_t *fixture, const locker_uuid_t *session,
                              locker_wire_disarm_response_t *out)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint16_t result;

    memset(&request, 0, sizeof(request));
    request.disarm.session_id = *session;
    request.disarm.reason = 1u;
    result = sim_exchange(fixture, 6u, 44u, &request, &response);
    if (result == LOCKER_WIRE_RESULT_OK && out != NULL) {
        *out = response.disarm;
    }
    return result;
}

static uint16_t sim_query_at(sim_fixture_t *fixture, const locker_uuid_t *session,
                              const locker_uuid_t *epoch, const locker_uuid_t *command,
                              locker_wire_query_command_response_t *out)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint16_t result;
    memset(&request, 0, sizeof(request));
    request.query_command.session_id = *session;
    request.query_command.journal_epoch = *epoch;
    request.query_command.command_id = *command;
    result = sim_exchange(fixture, 8u, 46u, &request, &response);
    if (result == LOCKER_WIRE_RESULT_OK && out != NULL) {
        *out = response.query_command;
    }
    return result;
}

static uint16_t sim_health_at(sim_fixture_t *fixture, const locker_uuid_t *session,
                              const locker_uuid_t *host_instance,
                              locker_wire_host_health_response_t *out)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint16_t result;

    memset(&request, 0, sizeof(request));
    request.host_health.session_id = *session;
    request.host_health.host_instance_id = *host_instance;
    request.host_health.core_progress = fixture->clock.monotonic_ms + 1u;
    request.host_health.core_observed_age_ms = 0u;
    request.host_health.ui_observed_age_ms = 0u;
    test_uuid_fill(&request.host_health.ui_instance_id, 0x80u);
    result = sim_exchange(fixture, 9u, 45u, &request, &response);
    if (result == LOCKER_WIRE_RESULT_OK && out != NULL) {
        *out = response.host_health;
    }
    return result;
}

static uint16_t sim_anchor_at(sim_fixture_t *fixture, const locker_uuid_t *session, uint32_t anchor_id,
                              locker_wire_time_anchor_response_t *out)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint16_t result;

    memset(&request, 0, sizeof(request));
    request.time_anchor.session_id = *session;
    request.time_anchor.anchor_id = anchor_id;
    request.time_anchor.host_utc_ms = fixture->clock.wall_ms;
    request.time_anchor.host_monotonic_ms = fixture->clock.monotonic_ms;
    result = sim_exchange(fixture, 10u, 46u, &request, &response);
    if (result == LOCKER_WIRE_RESULT_OK && out != NULL) {
        *out = response.time_anchor;
    }
    return result;
}

static uint16_t sim_release_at(sim_fixture_t *fixture, const locker_uuid_t *session,
                               const locker_uuid_t *epoch, uint64_t boot_id, uint64_t revision,
                               const locker_uuid_t *operation_id, const locker_uuid_t *command_id,
                               locker_wire_release_context_response_t *out)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint16_t result;

    memset(&request, 0, sizeof(request));
    request.release_context.session_id = *session;
    request.release_context.journal_epoch = *epoch;
    request.release_context.expected_boot_id = boot_id;
    request.release_context.expected_physical_revision = revision;
    request.release_context.operation_id = *operation_id;
    request.release_context.command_id = *command_id;
    result = sim_exchange(fixture, 11u, 47u, &request, &response);
    if (result == LOCKER_WIRE_RESULT_OK && out != NULL) {
        *out = response.release_context;
    }
    return result;
}

/* ---------------------------------------------------------------- framing */

/* Recover the real journal, confirm its snapshot boundary, then qualify Core.
 * Callers opt in explicitly so negative tests still exercise incomplete setup. */
static void sim_prepare_ready(sim_fixture_t *fixture, const locker_uuid_t *session,
                               locker_wire_get_snapshot_response_t *snapshot)
{
    locker_wire_fetch_events_response_t page;
    locker_wire_cursor_t after = locker_sim_ack_persisted(fixture->sim);
    locker_uuid_t host;
    unsigned guard;

    sim_snapshot_ok(fixture, session, snapshot);
    for (guard = 0u; guard < LOCKER_TEST_PAGE_LIMIT; ++guard) {
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                       sim_fetch_at(fixture, session, &snapshot->journal_epoch, after, 4u, &page));
        after = page.next_after;
        if (page.has_more == 0u) {
            break;
        }
    }
    /* A page sequence that never terminates is an engine defect, not a slow test. */
    TEST_CHECK(guard < LOCKER_TEST_PAGE_LIMIT);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_ack_at(fixture, session, &snapshot->journal_epoch, snapshot->last_committed, NULL));
    test_uuid_fill(&host, 0x40u);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_health_at(fixture, session, &host, NULL));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_resume_at(fixture, session, &snapshot->journal_epoch, snapshot->boot_id,
                                 snapshot->physical_revision, snapshot->last_committed, NULL));
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_READY, locker_sim_device_mode(fixture->sim));
}

static void test_frame_framing_rules(void)
{
    sim_fixture_t fixture;
    locker_wire_request_union_t request;
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    uint8_t doubled[LOCKER_WIRE_MAX_FRAME * 2u];
    size_t frame_length = 0u;
    size_t out_length = 1u;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_framing"));
    memset(&request, 0, sizeof(request));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      test_build_request(1u, 1u, &request, frame, sizeof(frame), &frame_length));

    /* Half a frame is not a frame: rejected, not guessed. */
    TEST_CHECK_U64(LOCKER_STATUS_INVALID,
                   locker_sim_handle_frame(fixture.sim, frame, frame_length / 2u, fixture.reply,
                                           sizeof(fixture.reply), &out_length));
    /* Two frames in one read is a caller error here; the daemon splits them. */
    memcpy(doubled, frame, frame_length);
    memcpy(doubled + frame_length, frame, frame_length);
    TEST_CHECK_U64(LOCKER_STATUS_INVALID,
                   locker_sim_handle_frame(fixture.sim, doubled, frame_length * 2u, fixture.reply,
                                           sizeof(fixture.reply), &out_length));
    /* Corrupted CRC is dropped. */
    memcpy(doubled, frame, frame_length);
    doubled[frame_length - 1u] ^= 0xffu;
    TEST_CHECK_U64(LOCKER_STATUS_INVALID,
                   locker_sim_handle_frame(fixture.sim, doubled, frame_length, fixture.reply,
                                           sizeof(fixture.reply), &out_length));
    /* Oversized payload length is rejected before any allocation. */
    {
        static const uint8_t oversized[10] = {0x4cu, 0x4bu, 0x01u, 0x01u, 0x01u, 0x02u,
                                              0x03u, 0x00u, 0x00u, 0x00u};
        TEST_CHECK_U64(LOCKER_STATUS_INVALID,
                       locker_sim_handle_frame(fixture.sim, oversized, sizeof(oversized),
                                               fixture.reply, sizeof(fixture.reply), &out_length));
    }
    /* Unknown request type is refused, not dispatched. The encoder rejects type
     * 12 by design, so the frame is assembled by hand to prove the device side
     * refuses an unrecognised type that is otherwise well framed. */
    {
        uint8_t unknown[12] = {0x4cu, 0x4bu, 0x01u, 12u, 0x00u, 0x00u,
                               0x05u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
        uint16_t unknown_crc = locker_wire_crc16(&unknown[2], 8u);

        unknown[10] = (uint8_t)(unknown_crc & 0xffu);
        unknown[11] = (uint8_t)(unknown_crc >> 8);
        TEST_CHECK_U64(LOCKER_STATUS_OK,
                       locker_sim_handle_frame(fixture.sim, unknown, sizeof(unknown), fixture.reply,
                                               sizeof(fixture.reply), &out_length));
        TEST_CHECK_U64(30u, out_length);
        TEST_CHECK_U64(0x8cu, fixture.reply[3]);
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_UNSUPPORTED, fixture.reply[26]);
    }
    /* Nothing above changed the journal: only the boot record exists. */
    TEST_CHECK_U64(1u, locker_sim_event_count(fixture.sim));
    sim_fixture_close(&fixture);
}

/* ----------------------------------------------------------------- session */

static void test_session_rules(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t other;
    locker_wire_hello_response_t hello;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    locker_wire_disarm_response_t disarmed;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_session"));
    /* No session yet: every non-HELLO message is refused. */
    test_uuid_fill(&session, 0x20u);
    memset(&request, 0, sizeof(request));
    request.get_snapshot.session_id = session;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_SESSION,
                   sim_exchange(&fixture, 2u, 2u, &request, &response));
    TEST_CHECK(sim_session_is_nil(fixture.sim));

    /* HELLO with version 0 is refused and does not establish a session. */
    memset(&request, 0, sizeof(request));
    request.hello.new_session_id = session;
    test_uuid_fill(&request.hello.host_instance_id, 0x40u);
    request.hello.requested_version = 0u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_VERSION,
                   sim_exchange(&fixture, 1u, 1u, &request, &response));
    TEST_CHECK(sim_session_is_nil(fixture.sim));

    /* A nil new_session_id is a payload error. */
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.hello.host_instance_id, 0x40u);
    request.hello.requested_version = LOCKER_WIRE_VERSION;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   sim_exchange(&fixture, 1u, 1u, &request, &response));

    /* A valid HELLO echoes the new session and revokes action eligibility. */
    sim_hello_ok(&fixture, &session, &hello);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, hello.result);
    TEST_CHECK(locker_uuid_equal(&session, &hello.session_id));
    TEST_CHECK(sim_session_equals(fixture.sim, &session));
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING, hello.device_mode);
    TEST_CHECK_U64(LOCKER_WIRE_CAP_DOOR_AND_ITEM | LOCKER_WIRE_CAP_PERSISTENT_JOURNAL |
                       LOCKER_WIRE_CAP_CARD_READER | LOCKER_WIRE_CAP_SUPPLY_ADC |
                       LOCKER_WIRE_CAP_BOUNDED_PULSE,
                   hello.capabilities);
    TEST_CHECK_U64(0u, hello.firmware_major);
    TEST_CHECK_U64(1u, hello.firmware_minor);
    TEST_CHECK_U64(1u, hello.boot_id);
    TEST_CHECK_U64(0u, hello.action_watermark);
    TEST_CHECK_U64(0x01u, hello.device_uid[0]);
    TEST_CHECK_U64(0x0cu, hello.device_uid[11]);

    /* The same session+host re-HELLO is idempotent. */
    sim_hello_ok(&fixture, &session, NULL);
    TEST_CHECK(sim_session_equals(fixture.sim, &session));

    /* A different session replaces it. */
    test_uuid_fill(&other, 0x28u);
    sim_hello_ok(&fixture, &other, NULL);
    TEST_CHECK(sim_session_equals(fixture.sim, &other));
    sim_snapshot_ok(&fixture, &other, &snapshot);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, snapshot.result);
    TEST_CHECK(locker_uuid_equal(&other, &snapshot.session_id));

    /* DISARM is idempotent and keeps the session. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_disarm_at(&fixture, &other, &disarmed));
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING, disarmed.device_mode);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_disarm_at(&fixture, &other, &disarmed));
    TEST_CHECK(sim_session_equals(fixture.sim, &other));
    sim_fixture_close(&fixture);
}

/* ------------------------------------------------------------------- fetch */

static void test_fetch_pagination(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t epoch;
    locker_wire_hello_response_t hello;
    locker_wire_fetch_events_response_t page;
    locker_wire_cursor_t after;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_fetch"));
    test_uuid_fill(&session, 0x30u);
    sim_hello_ok(&fixture, &session, &hello);
    epoch = hello.journal_epoch;
    /* Two more events, so the journal holds boot + 2 = 3 records. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_sim_control_set_door(fixture.sim, LOCKER_WIRE_DOOR_STATE_OPEN));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_sim_control_set_item(fixture.sim, LOCKER_WIRE_ITEM_STATE_ABSENT));
    TEST_CHECK_U64(3u, locker_sim_event_count(fixture.sim));

    /* A wrong epoch is a journal change, not an empty page. */
    {
        locker_uuid_t wrong;

        test_uuid_fill(&wrong, 0x90u);
        after.boot_id = 0u;
        after.event_seq = 0u;
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_JOURNAL_CHANGED,
                       sim_fetch_at(&fixture, &session, &wrong, after, 4u, NULL));
    }
    /* max_count outside 1..4 is a payload error. */
    after.boot_id = 0u;
    after.event_seq = 0u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   sim_fetch_at(&fixture, &session, &epoch, after, 0u, NULL));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   sim_fetch_at(&fixture, &session, &epoch, after, 5u, NULL));
    /* Half-zero cursors are structurally illegal. */
    after.boot_id = 1u;
    after.event_seq = 0u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   sim_fetch_at(&fixture, &session, &epoch, after, 4u, NULL));
    /* A cursor past last_committed is a payload error, not an empty page. */
    after.boot_id = 9u;
    after.event_seq = 9u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   sim_fetch_at(&fixture, &session, &epoch, after, 4u, NULL));

    /* limit 1 paginates with has_more and a next_after that advances. */
    after.boot_id = 0u;
    after.event_seq = 0u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_fetch_at(&fixture, &session, &epoch, after, 1u, &page));
    TEST_CHECK_U64(1u, page.event_count);
    TEST_CHECK_U64(1u, page.has_more);
    TEST_CHECK_U64(0u, page.gap_before_page);
    TEST_CHECK_U64(1u, page.events[0].boot_id);
    TEST_CHECK_U64(1u, page.events[0].event_seq);
    TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT_TYPE_BOOT, page.events[0].event_type);
    TEST_CHECK_U64(1u, page.next_after.boot_id);
    TEST_CHECK_U64(1u, page.next_after.event_seq);
    TEST_CHECK_U64(1u, page.first_available.boot_id);
    TEST_CHECK_U64(1u, page.last_committed.boot_id);
    TEST_CHECK_U64(3u, page.last_committed.event_seq);

    /* Continue after the returned record. */
    after = page.next_after;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_fetch_at(&fixture, &session, &epoch, after, 4u, &page));
    TEST_CHECK_U64(2u, page.event_count);
    TEST_CHECK_U64(0u, page.has_more);
    TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_OPENED, page.events[0].event_type);
    TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT_TYPE_ITEM_REMOVED, page.events[1].event_type);

    /* Fetching at last_committed yields an empty page that echoes the cursor. */
    after = locker_sim_last_committed(fixture.sim);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_fetch_at(&fixture, &session, &epoch, after, 4u, &page));
    TEST_CHECK_U64(0u, page.event_count);
    TEST_CHECK_U64(0u, page.has_more);
    TEST_CHECK_U64(after.boot_id, page.next_after.boot_id);
    TEST_CHECK_U64(after.event_seq, page.next_after.event_seq);

    /* A continuity fault does not retroactively discard the records already
     * available, but the range now has a known hole, so every page served while
     * the latch is set reports gap_before_page=1 together with the retained
     * records (uart-v1.md:366-372 grants no exemption for a marked hole). */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_inject_gap(fixture.sim, 2u));
    TEST_CHECK(!locker_sim_continuity_ok(fixture.sim));
    after.boot_id = 1u;
    after.event_seq = 1u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_fetch_at(&fixture, &session, &epoch, after, 4u, &page));
    TEST_CHECK_U64(1u, page.gap_before_page);
    TEST_CHECK_U64(3u, page.event_count);
    TEST_CHECK_U64(1u, page.first_available.boot_id);
    TEST_CHECK_U64(1u, page.first_available.event_seq);
    sim_fixture_close(&fixture);
}

/* --------------------------------------------------------------------- ack */

static void test_ack_rules(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t epoch;
    locker_wire_hello_response_t hello;
    locker_wire_ack_persisted_response_t ack;
    locker_wire_cursor_t through;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_ack"));
    test_uuid_fill(&session, 0x38u);
    sim_hello_ok(&fixture, &session, &hello);
    epoch = hello.journal_epoch;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_fetch_at(&fixture, &session, &epoch, (locker_wire_cursor_t){0u, 0u}, 4u, NULL));

    /* Acking the boot record is accepted and echoed. */
    through.boot_id = 1u;
    through.event_seq = 1u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_ack_at(&fixture, &session, &epoch, through, &ack));
    TEST_CHECK_U64(1u, ack.ack_persisted.boot_id);
    TEST_CHECK_U64(1u, ack.ack_persisted.event_seq);
    TEST_CHECK_U64(1u, locker_sim_ack_persisted(fixture.sim).event_seq);

    /* The same ack twice is idempotent. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_ack_at(&fixture, &session, &epoch, through, NULL));

    /* Never backwards. */
    {
        locker_wire_cursor_t zero;

        zero.boot_id = 0u;
        zero.event_seq = 0u;
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                       sim_ack_at(&fixture, &session, &epoch, zero, NULL));
    }
    /* Never past last_committed. */
    through.boot_id = 1u;
    through.event_seq = 2u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_CURSOR_GAP,
                   sim_ack_at(&fixture, &session, &epoch, through, NULL));
    /* Half-zero cursor is structurally illegal. */
    through.boot_id = 1u;
    through.event_seq = 0u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   sim_ack_at(&fixture, &session, &epoch, through, NULL));
    /* Wrong epoch is a journal change. */
    {
        locker_uuid_t wrong;

        test_uuid_fill(&wrong, 0x91u);
        through.boot_id = 1u;
        through.event_seq = 1u;
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_JOURNAL_CHANGED,
                       sim_ack_at(&fixture, &session, &wrong, through, NULL));
    }
    /* Ack state is persisted across a restart. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_restart(fixture.sim));
    TEST_CHECK_U64(1u, locker_sim_ack_persisted(fixture.sim).boot_id);
    TEST_CHECK_U64(1u, locker_sim_ack_persisted(fixture.sim).event_seq);
    sim_fixture_close(&fixture);
}

/* ------------------------------------------------------------ resume/disarm */

static void test_resume_rules(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t epoch;
    locker_wire_hello_response_t hello;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_resume_ready_response_t resumed;
    locker_wire_ack_persisted_response_t ack;
    locker_wire_cursor_t through;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_resume"));
    test_uuid_fill(&session, 0x40u);
    sim_hello_ok(&fixture, &session, &hello);
    epoch = hello.journal_epoch;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_fetch_at(&fixture, &session, &epoch, (locker_wire_cursor_t){0u, 0u}, 4u, NULL));
    sim_snapshot_ok(&fixture, &session, &snapshot);

    through.boot_id = 1u;
    through.event_seq = 1u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_ack_at(&fixture, &session, &epoch, through, &ack));

    /* A wrong boot id means the session belongs to another boot. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_SESSION,
                   sim_resume_at(&fixture, &session, &epoch, snapshot.boot_id + 1u,
                                 snapshot.physical_revision, through, NULL));
    /* A changed physical revision blocks the resume. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_PHYSICAL_CHANGED,
                   sim_resume_at(&fixture, &session, &epoch, snapshot.boot_id,
                                 snapshot.physical_revision + 1u, through, NULL));
    /* Persisted-through beyond the acked watermark is a cursor gap. */
    {
        locker_wire_cursor_t ahead;

        ahead.boot_id = 1u;
        ahead.event_seq = 2u;
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_CURSOR_GAP,
                       sim_resume_at(&fixture, &session, &epoch, snapshot.boot_id,
                                     snapshot.physical_revision, ahead, NULL));
    }
    {
        locker_uuid_t host;
        test_uuid_fill(&host, 0x40u);
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_health_at(&fixture, &session, &host, NULL));
    }
    /* The happy path moves the device to READY. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_resume_at(&fixture, &session, &epoch, snapshot.boot_id,
                                 snapshot.physical_revision, through, &resumed));
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_READY, resumed.device_mode);
    TEST_CHECK_U64(snapshot.physical_revision, resumed.physical_revision);
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_READY, locker_sim_device_mode(fixture.sim));

    /* With a latched gap, resume never returns READY. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_inject_gap(fixture.sim, 1u));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_resume_at(&fixture, &session, &epoch, snapshot.boot_id,
                                 snapshot.physical_revision, through, &resumed));
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_DEGRADED, resumed.device_mode);
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_DEGRADED, locker_sim_device_mode(fixture.sim));
    sim_fixture_close(&fixture);
}

/* ----------------------------------------------------------------- execute */

static void test_execute_rules(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t command_one;
    locker_uuid_t command_two;
    locker_wire_hello_response_t hello;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_execute_unlock_response_t executed;
    locker_wire_query_command_response_t queried;
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    locker_uuid_t operation_one;
    uint64_t not_after;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_execute"));
    test_uuid_fill(&session, 0x48u);
    sim_hello_ok(&fixture, &session, &hello);
    sim_prepare_ready(&fixture, &session, &snapshot);
    test_uuid_fill(&command_one, 0x70u);
    test_uuid_fill(&command_two, 0x71u);
    test_uuid_fill(&operation_one, 0x72u);
    not_after = fixture.clock.monotonic_ms + 5000u;

    /* action_no zero is invalid. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   sim_execute_at(&fixture, &session, &snapshot, 0u, &command_one,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW, not_after, NULL));
    /* slot_id other than 1 is invalid. */
    memset(&request, 0, sizeof(request));
    request.execute_unlock.session_id = session;
    request.execute_unlock.journal_epoch = snapshot.journal_epoch;
    request.execute_unlock.expected_boot_id = snapshot.boot_id;
    request.execute_unlock.expected_physical_revision = snapshot.physical_revision;
    request.execute_unlock.action_no = 1u;
    request.execute_unlock.not_after_mcu_ms = not_after;
    request.execute_unlock.command_id = command_one;
    /* The codec rejects a nil operation/command UUID before the simulator can
     * check slot_id/kind, so fill every identity field: the negative case must
     * fail on slot_id, not on a missing UUID. */
    request.execute_unlock.operation_id = operation_one;
    request.execute_unlock.slot_id = 2u;
    request.execute_unlock.kind = LOCKER_WIRE_OPERATION_KIND_BORROW;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   sim_exchange(&fixture, 7u, 21u, &request, &response));
    /* An unknown operation kind is invalid. */
    request.execute_unlock.slot_id = 1u;
    request.execute_unlock.kind = 9u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                   sim_exchange(&fixture, 7u, 21u, &request, &response));
    /* An expired proof of freshness is refused. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PRECONDITION,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command_one,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW,
                                  fixture.clock.monotonic_ms, NULL));
    /* A horizon beyond 15 s is refused. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PRECONDITION,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command_one,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW,
                                  fixture.clock.monotonic_ms + 15001u, NULL));
    /* An action number that skips ahead is an action gap. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_ACTION_GAP,
                   sim_execute_at(&fixture, &session, &snapshot, 5u, &command_one,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW, not_after, NULL));
    /* A borrowed item must be present. Every physical transition bumps
     * physical_revision, so the CAS snapshot is re-read before each attempt. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_sim_control_set_item(fixture.sim, LOCKER_WIRE_ITEM_STATE_ABSENT));
    sim_snapshot_ok(&fixture, &session, &snapshot);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PRECONDITION,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command_one,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW, not_after, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_sim_control_set_item(fixture.sim, LOCKER_WIRE_ITEM_STATE_PRESENT));
    sim_snapshot_ok(&fixture, &session, &snapshot);
    /* An open door blocks acceptance. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_sim_control_set_door(fixture.sim, LOCKER_WIRE_DOOR_STATE_OPEN));
    sim_snapshot_ok(&fixture, &session, &snapshot);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PRECONDITION,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command_one,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW, not_after, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_sim_control_set_door(fixture.sim, LOCKER_WIRE_DOOR_STATE_CLOSED));
    sim_snapshot_ok(&fixture, &session, &snapshot);
    /* reject_next refuses exactly one attempt without consuming the number. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_reject_next(fixture.sim, true));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PRECONDITION,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command_one,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW, not_after, NULL));
    TEST_CHECK_U64(0u, locker_sim_action_watermark(fixture.sim));

    /* The accepted action returns ACCEPTED and then PULSE_STARTED. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command_one,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW, not_after, &executed));
    TEST_CHECK_U64(1u, executed.action_no);
    TEST_CHECK(locker_uuid_equal(&command_one, &executed.command_id));
    TEST_CHECK(locker_uuid_equal(&session, &executed.command_session_id));
    TEST_CHECK_U64(snapshot.boot_id, executed.command_boot_id);
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED, executed.command_state);
    TEST_CHECK_U64(1u, locker_sim_action_watermark(fixture.sim));
    TEST_CHECK(locker_sim_has_active_intent(fixture.sim));
    /* Read the flag from a real GET_SNAPSHOT: asserting it through
     * locker_sim_continuity_ok() would be a tautology and would not prove the
     * wire-level PULSE_ACTIVE bit is set. */
    sim_snapshot_ok(&fixture, &session, &snapshot);
    TEST_CHECK_U64(LOCKER_WIRE_FLAG_PULSE_ACTIVE,
                   snapshot.flags & LOCKER_WIRE_FLAG_PULSE_ACTIVE);

    /* A second accepted action while one is active is BUSY. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BUSY,
                   sim_execute_at(&fixture, &session, &snapshot, 2u, &command_two,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW, not_after, NULL));
    TEST_CHECK_U64(1u, locker_sim_action_watermark(fixture.sim));

    /* Replaying the same action number with the same command returns the recorded state. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command_one,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW, not_after, &executed));
    TEST_CHECK_U64(1u, executed.action_no);
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED, executed.command_state);

    /* Reusing an already accepted action number with a different command is a
     * replay of the low watermark, never a fresh action (uart-v1.md:388). */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_REPLAY_BLOCKED,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command_two,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW, not_after, NULL));
    TEST_CHECK_U64(1u, locker_sim_action_watermark(fixture.sim));

    /* Query finds the persisted command; an unknown command is UNKNOWN_RESULT. */
    memset(&request, 0, sizeof(request));
    request.query_command.session_id = session;
    request.query_command.journal_epoch = snapshot.journal_epoch;
    request.query_command.command_id = command_one;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_exchange(&fixture, 8u, 48u, &request, &response));
    queried = response.query_command;
    TEST_CHECK_U64(1u, queried.action_no);
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED, queried.command_state);
    request.query_command.command_id = command_two;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_UNKNOWN_RESULT,
                   sim_exchange(&fixture, 8u, 49u, &request, &response));
    /* A nil command id is a payload error. */
    {
        locker_uuid_t nil;

        memset(&nil, 0, sizeof(nil));
        request.query_command.command_id = nil;
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD,
                       sim_exchange(&fixture, 8u, 50u, &request, &response));
    }

    sim_fixture_close(&fixture);
}

/* -------------------------------------------------------- health heartbeat */

static void test_health_timeout_rules(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t command_id;
    locker_uuid_t operation_id;
    locker_uuid_t host_instance;
    locker_wire_hello_response_t hello;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_execute_unlock_response_t executed;
    locker_wire_release_context_response_t released;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_health"));
    test_uuid_fill(&session, 0x4cu);
    sim_hello_ok(&fixture, &session, &hello);
    sim_prepare_ready(&fixture, &session, &snapshot);
    test_uuid_fill(&command_id, 0x78u);
    test_uuid_fill(&operation_id, 0x60u);
    test_uuid_fill(&host_instance, 0x40u);

    /* Complete a real simulated pulse before checking the health gate. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command_id,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW,
                                  fixture.clock.monotonic_ms + 5000u, &executed));
    fixture.clock.monotonic_ms += 150u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_release_at(&fixture, &session, &snapshot.journal_epoch, snapshot.boot_id,
                                  snapshot.physical_revision, &operation_id, &command_id,
                                  &released));
    TEST_CHECK(!locker_sim_has_active_intent(fixture.sim));
    test_uuid_fill(&command_id, 0x79u);

    /* The host heartbeat starts the 5 s window (uart-v1.md:394). */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_health_at(&fixture, &session, &host_instance, NULL));
    /* A heartbeat older than 5 s stops new unlocks. */
    fixture.clock.monotonic_ms += 5001u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_NOT_READY,
                   sim_execute_at(&fixture, &session, &snapshot, 2u, &command_id,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW,
                                  fixture.clock.monotonic_ms + 5000u, NULL));
    /* A fresh heartbeat restores eligibility. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_health_at(&fixture, &session, &host_instance, NULL));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute_at(&fixture, &session, &snapshot, 2u, &command_id,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW,
                                  fixture.clock.monotonic_ms + 5000u, NULL));
    sim_fixture_close(&fixture);
}

/* ---------------------------------------------------------- release context */

static void test_release_context_rules(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t epoch;
    locker_uuid_t command_id;
    locker_uuid_t operation_id;
    locker_uuid_t other_command;
    locker_wire_hello_response_t hello;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_execute_unlock_response_t executed;
    locker_wire_release_context_response_t released;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_release"));
    test_uuid_fill(&session, 0x50u);
    sim_hello_ok(&fixture, &session, &hello);
    epoch = hello.journal_epoch;
    sim_prepare_ready(&fixture, &session, &snapshot);
    test_uuid_fill(&command_id, 0x74u);
    test_uuid_fill(&other_command, 0x75u);
    test_uuid_fill(&operation_id, 0x60u);

    /* No active context: idempotent already_released=1. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_release_at(&fixture, &session, &epoch, snapshot.boot_id,
                                  snapshot.physical_revision, &operation_id, &command_id, &released));
    TEST_CHECK_U64(1u, released.already_released);

    /* Accept a normal pulse; advance its deterministic timer before release. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command_id,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW,
                                  fixture.clock.monotonic_ms + 5000u, &executed));
    fixture.clock.monotonic_ms += 150u;
    sim_snapshot_ok(&fixture, &session, &snapshot);
    TEST_CHECK_U64(1u, snapshot.active_action_no);
    /* A different command does not match the active context. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PRECONDITION,
                   sim_release_at(&fixture, &session, &epoch, snapshot.boot_id,
                                  snapshot.physical_revision, &operation_id, &other_command, NULL));
    /* An open door blocks the release. The door transition bumps
     * physical_revision, so the guard snapshot is re-read first. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_sim_control_set_door(fixture.sim, LOCKER_WIRE_DOOR_STATE_OPEN));
    sim_snapshot_ok(&fixture, &session, &snapshot);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PRECONDITION,
                   sim_release_at(&fixture, &session, &epoch, snapshot.boot_id,
                                  snapshot.physical_revision, &operation_id, &command_id, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_sim_control_set_door(fixture.sim, LOCKER_WIRE_DOOR_STATE_CLOSED));
    sim_snapshot_ok(&fixture, &session, &snapshot);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_release_at(&fixture, &session, &epoch, snapshot.boot_id,
                                  snapshot.physical_revision, &operation_id, &command_id, &released));
    TEST_CHECK_U64(0u, released.already_released);
    TEST_CHECK(!locker_sim_has_active_intent(fixture.sim));
    /* The watermark and the event log survive the release. */
    TEST_CHECK_U64(1u, locker_sim_action_watermark(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_NONE, locker_sim_command_state(fixture.sim));
    sim_fixture_close(&fixture);
}

/* ------------------------------------------------------- pulse activity */

static void test_pulse_activity_rules(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t command_id;
    locker_uuid_t operation_id;
    locker_wire_hello_response_t hello;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_execute_unlock_response_t executed;
    locker_wire_release_context_response_t released;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_pulse"));
    test_uuid_fill(&session, 0x54u);
    sim_hello_ok(&fixture, &session, &hello);
    sim_prepare_ready(&fixture, &session, &snapshot);
    test_uuid_fill(&command_id, 0x7cu);
    test_uuid_fill(&operation_id, 0x60u);

    /* An accepted action starts a bounded pulse; while it is active the
     * snapshot advertises PULSE_ACTIVE and a release is refused because the
     * cabinet is not known to be safe yet (uart-v1.md:400-402). */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command_id,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW,
                                  fixture.clock.monotonic_ms + 5000u, &executed));
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED, executed.command_state);
    sim_snapshot_ok(&fixture, &session, &snapshot);
    TEST_CHECK_U64(LOCKER_WIRE_FLAG_PULSE_ACTIVE,
                   snapshot.flags & LOCKER_WIRE_FLAG_PULSE_ACTIVE);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PRECONDITION,
                   sim_release_at(&fixture, &session, &snapshot.journal_epoch, snapshot.boot_id,
                                  snapshot.physical_revision, &operation_id, &command_id, &released));
    TEST_CHECK(locker_sim_has_active_intent(fixture.sim));
    /* The active context survives the refused release: nothing was released. */
    sim_snapshot_ok(&fixture, &session, &snapshot);
    TEST_CHECK_U64(1u, snapshot.active_action_no);
    TEST_CHECK_U64(LOCKER_WIRE_FLAG_PULSE_ACTIVE,
                   snapshot.flags & LOCKER_WIRE_FLAG_PULSE_ACTIVE);
    fixture.clock.monotonic_ms += 150u;
    sim_snapshot_ok(&fixture, &session, &snapshot);
    TEST_CHECK_U64(0u, snapshot.flags & LOCKER_WIRE_FLAG_PULSE_ACTIVE);
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_PULSE_FINISHED, snapshot.active_command_state);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_release_at(&fixture, &session, &snapshot.journal_epoch, snapshot.boot_id,
                                  snapshot.physical_revision, &operation_id, &command_id, &released));
    TEST_CHECK(!locker_sim_has_active_intent(fixture.sim));
    test_uuid_fill(&command_id, 0x7du);
    sim_prepare_ready(&fixture, &session, &snapshot);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute_at(&fixture, &session, &snapshot, 2u, &command_id,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW,
                                  fixture.clock.monotonic_ms + 5000u, &executed));
    TEST_CHECK_U64(2u, locker_sim_pulse_count(fixture.sim));
    sim_fixture_close(&fixture);
}

/* ------------------------------------------------------- event queue bound */

static void test_event_queue_saturation(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_wire_hello_response_t hello;
    unsigned index;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_queue"));
    test_uuid_fill(&session, 0x58u);
    sim_hello_ok(&fixture, &session, &hello);

    /* Queue overflow drops notifications only; FETCH retains every fact. */
    TEST_CHECK(locker_sim_continuity_ok(fixture.sim));
    for (index = 0u; index < LOCKER_SIM_EVENT_QUEUE; ++index) {
        uint8_t door = ((index % 2u) == 0u) ? LOCKER_WIRE_DOOR_STATE_OPEN
                                            : LOCKER_WIRE_DOOR_STATE_CLOSED;

        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_set_door(fixture.sim, door));
        TEST_CHECK(locker_sim_continuity_ok(fixture.sim));
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_sim_control_set_door(fixture.sim, LOCKER_WIRE_DOOR_STATE_OPEN));
    TEST_CHECK(locker_sim_continuity_ok(fixture.sim));
    TEST_CHECK_U64(0u, locker_sim_dropped_events(fixture.sim));
    /* The journal keeps every event: saturation costs notifications, not rows. */
    TEST_CHECK_U64((uint64_t)LOCKER_SIM_EVENT_QUEUE + 2u, locker_sim_event_count(fixture.sim));
    /* The queue hands out exactly its capacity; the overflow notification was
     * discarded on push, so the next pop must report an empty queue. */
    {
        uint8_t frame[LOCKER_WIRE_MAX_FRAME];
        size_t frame_length = 0u;
        unsigned popped;

        for (popped = 0u; popped < LOCKER_SIM_EVENT_QUEUE; ++popped) {
            TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                              locker_sim_pop_event(fixture.sim, frame, sizeof(frame), &frame_length));
        }
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY,
                          locker_sim_pop_event(fixture.sim, frame, sizeof(frame), &frame_length));
    }
    {
        locker_wire_get_snapshot_response_t snapshot;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                           locker_sim_control_set_door(fixture.sim, LOCKER_WIRE_DOOR_STATE_CLOSED));
        sim_prepare_ready(&fixture, &session, &snapshot);
        TEST_CHECK_U64(snapshot.last_committed.event_seq,
                       locker_sim_ack_persisted(fixture.sim).event_seq);
    }
    sim_fixture_close(&fixture);
}

/* ------------------------------------------------------- async event queue */

static void test_device_event_queue(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t out_session;
    locker_uuid_t out_epoch;
    locker_uuid_t epoch;
    locker_wire_hello_response_t hello;
    locker_wire_event96_t records[8];
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    size_t frame_length = 0u;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_events"));
    /* Without a session, nothing is queued for transport. */
    TEST_CHECK_U64(LOCKER_STATUS_NOT_READY,
                   locker_sim_pop_event(fixture.sim, frame, sizeof(frame), &frame_length));
    test_uuid_fill(&session, 0x58u);
    sim_hello_ok(&fixture, &session, &hello);
    epoch = hello.journal_epoch;
    /* Door and item transitions become DEVICE_EVENT frames carrying the session. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_sim_control_set_door(fixture.sim, LOCKER_WIRE_DOOR_STATE_OPEN));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_card(fixture.sim, (const uint8_t *)"\x04\x11\x22\x33", 4u));
    {
        size_t count = sim_fixture_events(&fixture, records, 8u, &out_session, &out_epoch);

        TEST_CHECK_U64(2u, count);
        TEST_CHECK(locker_uuid_equal(&session, &out_session));
        TEST_CHECK(locker_uuid_equal(&epoch, &out_epoch));
        TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_OPENED, records[0].event_type);
        TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT_TYPE_CARD_OBSERVED, records[1].event_type);
        TEST_CHECK_U64(4u, records[1].card_uid_len);
        TEST_CHECK_U64(0x04u, records[1].card_uid[0]);
    }
    /* An invalid card length is refused. */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_sim_control_card(fixture.sim, NULL, 3u));
    /* A door value that does not change emits nothing. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_sim_control_set_door(fixture.sim, LOCKER_WIRE_DOOR_STATE_OPEN));
    TEST_CHECK_U64(LOCKER_STATUS_NOT_READY,
                   locker_sim_pop_event(fixture.sim, frame, sizeof(frame), &frame_length));

    /* The async event frame is a DEVICE_EVENT with request_no 0 and no result prefix. */
    memset(&request, 0, sizeof(request));
    request.get_snapshot.session_id = session;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_exchange(&fixture, 2u, 2u, &request, &response));
    {
        locker_wire_stream_t stream;
        frame_capture_t capture;

        fixture.reply_length = 0u;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                          locker_sim_control_set_item(fixture.sim, LOCKER_WIRE_ITEM_STATE_ABSENT));
        TEST_CHECK_U64(0u, fixture.reply_length);
        frame_length = 0u;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                          locker_sim_pop_event(fixture.sim, frame, sizeof(frame), &frame_length));
        test_capture_reset(&capture);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, test_capture_feed(&capture, &stream, frame, frame_length));
        TEST_CHECK_U64(1u, capture.count);
        TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT, capture.frames[0].type);
        TEST_CHECK_U64(0u, capture.frames[0].request_no);
        TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT_PAYLOAD_SIZE, capture.frames[0].payload_len);
    }
    sim_fixture_close(&fixture);
}

/* ---------------------------------------------------------- ack suppression */

static void test_drop_ack_suppression(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t epoch;
    locker_wire_hello_response_t hello;
    locker_wire_cursor_t through;
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    size_t frame_length = 0u;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_drop"));
    test_uuid_fill(&session, 0x60u);
    sim_hello_ok(&fixture, &session, &hello);
    epoch = hello.journal_epoch;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_fetch_at(&fixture, &session, &epoch, (locker_wire_cursor_t){0u, 0u}, 4u, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_drop_ack(fixture.sim, true));

    through.boot_id = 1u;
    through.event_seq = 1u;
    memset(&request, 0, sizeof(request));
    request.ack_persisted.session_id = session;
    request.ack_persisted.journal_epoch = epoch;
    request.ack_persisted.through = through;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      test_build_request(4u, 60u, &request, frame, sizeof(frame), &frame_length));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_send(&fixture, frame, frame_length));
    /* The ACK was applied but the reply is deliberately withheld. */
    TEST_CHECK_U64(0u, fixture.reply_length);
    TEST_CHECK_U64(1u, locker_sim_ack_persisted(fixture.sim).event_seq);

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_drop_ack(fixture.sim, false));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_exchange(&fixture, 4u, 61u, &request, &response));
    sim_fixture_close(&fixture);
}

/* ---------------------------------------------------------- time anchoring */

static void test_time_anchor_rules(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_wire_hello_response_t hello;
    locker_wire_time_anchor_response_t anchor;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_anchor"));
    test_uuid_fill(&session, 0x68u);
    sim_hello_ok(&fixture, &session, &hello);
    /* anchor_id 0 is reserved. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, sim_anchor_at(&fixture, &session, 0u, NULL));
    fixture.clock.monotonic_ms = 4321u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_anchor_at(&fixture, &session, 7u, &anchor));
    TEST_CHECK_U64(7u, anchor.anchor_id);
    TEST_CHECK_U64(hello.boot_id, anchor.boot_id);
    TEST_CHECK_U64(4321u, anchor.mcu_receive_mono_ms);
    /* The anchor never rewrites an event timestamp: the journal is untouched. */
    TEST_CHECK_U64(1u, locker_sim_event_count(fixture.sim));
    sim_fixture_close(&fixture);
}

static void test_recovery_requires_contiguous_fetch_and_fresh_core(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t command;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    locker_wire_cursor_t last;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_barriers"));
    test_uuid_fill(&session, 0x23u);
    test_uuid_fill(&command, 0x25u);
    sim_hello_ok(&fixture, &session, NULL);
    sim_snapshot_ok(&fixture, &session, &snapshot);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_NOT_READY,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW, 6000u, NULL));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_CURSOR_GAP,
                   sim_resume_at(&fixture, &session, &snapshot.journal_epoch, snapshot.boot_id,
                                 snapshot.physical_revision, (locker_wire_cursor_t){0u, 0u}, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_card(fixture.sim, NULL, 0u));
    last = locker_sim_last_committed(fixture.sim);
    /* Fetching only after BOOT skips a known required record and cannot qualify ACK. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_fetch_at(&fixture, &session, &snapshot.journal_epoch,
                                snapshot.last_committed, 4u, NULL));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_CURSOR_GAP,
                   sim_ack_at(&fixture, &session, &snapshot.journal_epoch, last, NULL));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_fetch_at(&fixture, &session, &snapshot.journal_epoch,
                                (locker_wire_cursor_t){0u, 0u}, 4u, NULL));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_ack_at(&fixture, &session, &snapshot.journal_epoch, last, NULL));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_NOT_READY,
                   sim_resume_at(&fixture, &session, &snapshot.journal_epoch, snapshot.boot_id,
                                 snapshot.physical_revision, last, NULL));
    memset(&request, 0, sizeof(request));
    request.host_health.session_id = session;
    test_uuid_fill(&request.host_health.host_instance_id, 0x40u);
    request.host_health.core_progress = 10u;
    request.host_health.core_observed_age_ms = UINT32_MAX;
    /* uart-v1.md:43: nil UI instance => unknown UI age, else the frame is invalid. */
    request.host_health.ui_observed_age_ms = LOCKER_WIRE_AGE_UNKNOWN;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_NOT_READY, sim_exchange(&fixture, 9u, 70u, &request, &response));
    request.host_health.core_observed_age_ms = 0u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_exchange(&fixture, 9u, 71u, &request, &response));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_resume_at(&fixture, &session, &snapshot.journal_epoch, snapshot.boot_id,
                                 snapshot.physical_revision, last, NULL));
    fixture.clock.monotonic_ms += 5001u;
    /* A courier replaying the same progress cannot keep a stalled Core alive. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_exchange(&fixture, 9u, 72u, &request, &response));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_NOT_READY,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW,
                                  fixture.clock.monotonic_ms + 1000u, NULL));
    request.host_health.core_progress += 1u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_exchange(&fixture, 9u, 73u, &request, &response));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_disarm_at(&fixture, &session, NULL));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_NOT_READY,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW,
                                  fixture.clock.monotonic_ms + 1000u, NULL));
    TEST_CHECK_U64(0u, locker_sim_pulse_count(fixture.sim));
    sim_fixture_close(&fixture);
}

static void test_full_intent_dedup_and_new_session_watermark(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t old_session;
    locker_uuid_t command;
    locker_uuid_t operation;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    locker_wire_execute_unlock_response_t accepted;
    uint64_t not_after;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_dedup"));
    test_uuid_fill(&session, 0x30u);
    old_session = session;
    test_uuid_fill(&command, 0x31u);
    test_uuid_fill(&operation, 0x60u);
    sim_hello_ok(&fixture, &session, NULL);
    sim_prepare_ready(&fixture, &session, &snapshot);
    not_after = fixture.clock.monotonic_ms + 1000u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command,
                                  LOCKER_WIRE_OPERATION_KIND_BORROW, not_after, &accepted));
    memset(&request, 0, sizeof(request));
    request.execute_unlock.session_id = session;
    request.execute_unlock.journal_epoch = snapshot.journal_epoch;
    request.execute_unlock.operation_id = operation;
    request.execute_unlock.command_id = command;
    request.execute_unlock.expected_boot_id = snapshot.boot_id;
    request.execute_unlock.expected_physical_revision = snapshot.physical_revision;
    request.execute_unlock.action_no = 1u;
    request.execute_unlock.not_after_mcu_ms = not_after;
    request.execute_unlock.slot_id = 1u;
    request.execute_unlock.kind = LOCKER_WIRE_OPERATION_KIND_BORROW;
    request.execute_unlock.operation_id.bytes[0] ^= 1u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_REPLAY_BLOCKED, sim_exchange(&fixture, 7u, 80u, &request, &response));
    request.execute_unlock.operation_id = operation;
    request.execute_unlock.not_after_mcu_ms += 1u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_REPLAY_BLOCKED, sim_exchange(&fixture, 7u, 81u, &request, &response));
    request.execute_unlock.not_after_mcu_ms = not_after;
    request.execute_unlock.expected_physical_revision += 1u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_REPLAY_BLOCKED, sim_exchange(&fixture, 7u, 82u, &request, &response));
    request.execute_unlock.expected_physical_revision = snapshot.physical_revision;
    fixture.clock.monotonic_ms += 1001u;
    /* Exact retry is the durable result, even after its original deadline. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_exchange(&fixture, 7u, 83u, &request, &response));
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_PULSE_FINISHED, response.execute_unlock.command_state);
    TEST_CHECK(locker_uuid_equal(&operation, &response.execute_unlock.operation_id));
    TEST_CHECK_U64(1u, locker_sim_pulse_count(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_release_at(&fixture, &session, &snapshot.journal_epoch, snapshot.boot_id,
                                  snapshot.physical_revision, &operation, &command, NULL));
    test_uuid_fill(&session, 0x33u);
    sim_hello_ok(&fixture, &session, NULL);
    TEST_CHECK_U64(0u, locker_sim_action_watermark(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_query_at(&fixture, &session, &snapshot.journal_epoch, &command, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                       locker_sim_control_set_item(fixture.sim, LOCKER_WIRE_ITEM_STATE_ABSENT));
    sim_prepare_ready(&fixture, &session, &snapshot);
    /* Old sessions remain invalid; a new session can start its own action 1. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_SESSION,
                   sim_execute_at(&fixture, &old_session, &snapshot, 1u, &command,
                                  LOCKER_WIRE_OPERATION_KIND_RETURN,
                                  fixture.clock.monotonic_ms + 1000u, NULL));
    test_uuid_fill(&command, 0x34u);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute_at(&fixture, &session, &snapshot, 1u, &command,
                                  LOCKER_WIRE_OPERATION_KIND_RETURN,
                                  fixture.clock.monotonic_ms + 1000u, &accepted));
    TEST_CHECK_U64(1u, locker_sim_action_watermark(fixture.sim));
    TEST_CHECK_U64(2u, locker_sim_pulse_count(fixture.sim));
    sim_fixture_close(&fixture);
}

static void test_execute_ack_loss_is_independent_of_pulse_suppression(void)
{
    unsigned suppressed;
    for (suppressed = 0u; suppressed < 2u; suppressed += 1u) {
        sim_fixture_t fixture;
        locker_uuid_t session;
        locker_uuid_t command;
        locker_wire_get_snapshot_response_t snapshot;
        locker_wire_request_union_t request;
        locker_wire_response_union_t response;
        locker_wire_query_command_response_t queried;
        uint8_t frame[LOCKER_WIRE_MAX_FRAME];
        size_t length = 0u;

        TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_execute_ack"));
        test_uuid_fill(&session, 0x39u);
        test_uuid_fill(&command, 0x3au);
        sim_hello_ok(&fixture, &session, NULL);
        sim_prepare_ready(&fixture, &session, &snapshot);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_drop_execute_ack(fixture.sim, true));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_suppress_pulse(fixture.sim, suppressed != 0u));
        memset(&request, 0, sizeof(request));
        request.execute_unlock.session_id = session;
        request.execute_unlock.journal_epoch = snapshot.journal_epoch;
        request.execute_unlock.expected_boot_id = snapshot.boot_id;
        request.execute_unlock.expected_physical_revision = snapshot.physical_revision;
        request.execute_unlock.action_no = 1u;
        request.execute_unlock.not_after_mcu_ms = fixture.clock.monotonic_ms + 1000u;
        test_uuid_fill(&request.execute_unlock.operation_id, 0x60u);
        request.execute_unlock.command_id = command;
        request.execute_unlock.slot_id = 1u;
        request.execute_unlock.kind = LOCKER_WIRE_OPERATION_KIND_BORROW;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                           test_build_request(7u, 90u, &request, frame, sizeof(frame), &length));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_send(&fixture, frame, length));
        TEST_CHECK_U64(0u, fixture.reply_length);
        TEST_CHECK_U64(1u, locker_sim_action_watermark(fixture.sim));
        TEST_CHECK_U64(suppressed != 0u ? 0u : 1u, locker_sim_pulse_count(fixture.sim));
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                       sim_query_at(&fixture, &session, &snapshot.journal_epoch, &command, &queried));
        TEST_CHECK_U64(suppressed != 0u ? LOCKER_WIRE_COMMAND_STATE_ACCEPTED :
                       LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED, queried.command_state);
        fixture.clock.monotonic_ms += 150u;
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                       sim_query_at(&fixture, &session, &snapshot.journal_epoch, &command, &queried));
        TEST_CHECK_U64(suppressed != 0u ? LOCKER_WIRE_COMMAND_STATE_ACCEPTED :
                       LOCKER_WIRE_COMMAND_STATE_PULSE_FINISHED, queried.command_state);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_drop_execute_ack(fixture.sim, false));
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_exchange(&fixture, 7u, 91u, &request, &response));
        TEST_CHECK_U64(suppressed != 0u ? 0u : 1u, locker_sim_pulse_count(fixture.sim));
        sim_fixture_close(&fixture);
    }
}

static void test_malformed_hello_prefix_and_zero_number_have_no_side_effect(void)
{
    sim_fixture_t fixture;
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    uint16_t crc;
    uint8_t type;
    uint32_t request_no;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "proto_bad_prefix"));
    memset(&request, 0, sizeof(request));
    test_uuid_fill(&request.hello.new_session_id, 0x44u);
    test_uuid_fill(&request.hello.host_instance_id, 0x40u);
    request.hello.requested_version = LOCKER_WIRE_VERSION;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                       test_build_request(1u, 100u, &request, frame, sizeof(frame), &length));
    frame[10] = 1u; /* HELLO's prefix session must be nil. This catches m39. */
    crc = locker_wire_crc16(&frame[2], length - 4u);
    frame[length - 2u] = (uint8_t)crc;
    frame[length - 1u] = (uint8_t)(crc >> 8);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_send(&fixture, frame, length));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_fixture_reply(&fixture, &response, &type, &request_no));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PAYLOAD, response.hello.result);
    /* The body decoder rejects the non-nil HELLO prefix, so the reply must still
     * echo the session carried by the rejected request (uart-v1.md:29) instead of
     * the daemon's (nil) current session. */
    TEST_CHECK(memcmp(&response.hello.session_id, &frame[LOCKER_WIRE_HEADER_SIZE],
                      sizeof(locker_uuid_t)) == 0);
    TEST_CHECK(sim_session_is_nil(fixture.sim));
    frame[10] = 0u;
    memset(&frame[6], 0, 4u);
    crc = locker_wire_crc16(&frame[2], length - 4u);
    frame[length - 2u] = (uint8_t)crc;
    frame[length - 1u] = (uint8_t)(crc >> 8);
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, sim_fixture_send(&fixture, frame, length));
    TEST_CHECK(sim_session_is_nil(fixture.sim));
    TEST_CHECK_U64(1u, locker_sim_event_count(fixture.sim));
    sim_fixture_close(&fixture);
}

int main(void)
{
    test_frame_framing_rules();
    test_recovery_requires_contiguous_fetch_and_fresh_core();
    test_full_intent_dedup_and_new_session_watermark();
    test_execute_ack_loss_is_independent_of_pulse_suppression();
    test_malformed_hello_prefix_and_zero_number_have_no_side_effect();
    test_session_rules();
    test_fetch_pagination();
    test_ack_rules();
    test_resume_rules();
    test_execute_rules();
    test_health_timeout_rules();
    test_release_context_rules();
    test_device_event_queue();
    test_drop_ack_suppression();
    test_time_anchor_rules();
    test_pulse_activity_rules();
    test_event_queue_saturation();
    TEST_REPORT("device.test_sim_protocol");
}
