/*
 * S1-03: independent SQLite device journal (host/simulator/src/sim.c).
 *
 * The simulator keeps its own journal file. These tests drive the production
 * engine directly (no socket, no daemon) and prove the durability rules the card
 * requires: boot epochs, event keys, action watermark, gap latching and the
 * "journal commit failure refuses new actions" rule.
 *
 * This is a process-local restart, never an MCU/littlefs power-loss proof.
 */

#include <stdio.h>
#include <string.h>

#include "../../../host/storage/third_party/sqlite/sqlite3.h"

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
    if (reply_type != (uint8_t)(type | LOCKER_WIRE_RESPONSE_FLAG)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (reply_request_no != request_no) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    return locker_wire_response_result(type, response);
}

static bool sim_hello(sim_fixture_t *fixture, const locker_uuid_t *new_session,
                      locker_wire_hello_response_t *out)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint16_t result;

    memset(&request, 0, sizeof(request));
    request.hello.new_session_id = *new_session;
    test_uuid_fill(&request.hello.host_instance_id, 0x40u);
    request.hello.requested_version = LOCKER_WIRE_VERSION;
    result = sim_exchange(fixture, 1u, 1u, &request, &response);
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (result != LOCKER_WIRE_RESULT_OK) {
        return false;
    }
    if (out != NULL) {
        *out = response.hello;
    }
    return true;
}

static uint16_t sim_snapshot(sim_fixture_t *fixture, const locker_uuid_t *session,
                             uint32_t request_no, locker_wire_get_snapshot_response_t *out)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint16_t result;

    memset(&request, 0, sizeof(request));
    request.get_snapshot.session_id = *session;
    result = sim_exchange(fixture, 2u, request_no, &request, &response);
    if (out != NULL) {
        /* Defined on failure too: callers must never read garbage. */
        memset(out, 0, sizeof(*out));
        if (result == LOCKER_WIRE_RESULT_OK) {
            *out = response.get_snapshot;
        }
    }
    return result;
}

static uint16_t sim_execute(sim_fixture_t *fixture, const locker_uuid_t *session,
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
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        if (result == LOCKER_WIRE_RESULT_OK) {
            *out = response.execute_unlock;
        }
    }
    return result;
}

static uint16_t sim_fetch(sim_fixture_t *fixture, const locker_uuid_t *session,
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
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        if (result == LOCKER_WIRE_RESULT_OK) {
            *out = response.fetch_events;
        }
    }
    return result;
}

static uint16_t sim_query(sim_fixture_t *fixture, const locker_uuid_t *session,
                          const locker_uuid_t *epoch, const locker_uuid_t *command_id,
                          locker_wire_query_command_response_t *out)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    uint16_t result;

    memset(&request, 0, sizeof(request));
    request.query_command.session_id = *session;
    request.query_command.journal_epoch = *epoch;
    request.query_command.command_id = *command_id;
    result = sim_exchange(fixture, 8u, 41u, &request, &response);
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        if (result == LOCKER_WIRE_RESULT_OK) {
            *out = response.query_command;
        }
    }
    return result;
}

static void sim_prepare_ready(sim_fixture_t *fixture, const locker_uuid_t *session,
                               locker_wire_get_snapshot_response_t *snapshot)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    locker_wire_fetch_events_response_t page;
    locker_wire_cursor_t after = locker_sim_ack_persisted(fixture->sim);
    unsigned guard;

    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_snapshot(fixture, session, 2u, snapshot));
    for (guard = 0u; guard < LOCKER_TEST_PAGE_LIMIT; ++guard) {
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                       sim_fetch(fixture, session, &snapshot->journal_epoch, after, 4u, &page));
        after = page.next_after;
        if (page.has_more == 0u) {
            break;
        }
    }
    /* A page sequence that never terminates is an engine defect, not a slow test. */
    TEST_CHECK(guard < LOCKER_TEST_PAGE_LIMIT);
    memset(&request, 0, sizeof(request));
    request.ack_persisted.session_id = *session;
    request.ack_persisted.journal_epoch = snapshot->journal_epoch;
    request.ack_persisted.through = snapshot->last_committed;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_exchange(fixture, 4u, 3u, &request, &response));
    memset(&request, 0, sizeof(request));
    request.host_health.session_id = *session;
    test_uuid_fill(&request.host_health.host_instance_id, 0x40u);
    request.host_health.core_progress = fixture->clock.monotonic_ms + 1u;
    /* uart-v1.md:43: a nil UI instance must carry an unknown UI age. */
    request.host_health.ui_observed_age_ms = LOCKER_WIRE_AGE_UNKNOWN;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_exchange(fixture, 9u, 4u, &request, &response));
    memset(&request, 0, sizeof(request));
    request.resume_ready.session_id = *session;
    request.resume_ready.journal_epoch = snapshot->journal_epoch;
    request.resume_ready.expected_boot_id = snapshot->boot_id;
    request.resume_ready.expected_physical_revision = snapshot->physical_revision;
    request.resume_ready.persisted_through = snapshot->last_committed;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_exchange(fixture, 5u, 5u, &request, &response));
}

/* The journal is a private SQLite database, not the business store. */
static bool journal_header_is_sqlite(const char *path)
{
    static const char expected[16] = {'S', 'Q', 'L', 'i', 't', 'e', ' ', 'f',
                                      'o', 'r', 'm', 'a', 't', ' ', '3', '\0'};
    unsigned char header[16];
    FILE *handle;
    size_t read_bytes;

    handle = fopen(path, "rb");
    if (handle == NULL) {
        return false;
    }
    read_bytes = fread(header, 1u, sizeof(header), handle);
    (void)fclose(handle);
    if (read_bytes != sizeof(header)) {
        return false;
    }
    return memcmp(header, expected, sizeof(expected)) == 0;
}

static void test_open_creates_boot(void)
{
    sim_fixture_t fixture;
    locker_wire_cursor_t first;
    locker_wire_cursor_t last;
    locker_wire_cursor_t ack;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "journal_open"));
    TEST_CHECK_U64(1u, locker_sim_boot_id(fixture.sim));
    TEST_CHECK(!sim_epoch_is_nil(fixture.sim));
    TEST_CHECK_U64(1u, locker_sim_event_count(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY, locker_sim_journal_state(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING, locker_sim_device_mode(fixture.sim));
    TEST_CHECK(locker_sim_continuity_ok(fixture.sim));
    TEST_CHECK_U64(0u, locker_sim_action_watermark(fixture.sim));
    TEST_CHECK_U64(0u, locker_sim_dropped_events(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_DOOR_STATE_CLOSED, locker_sim_door(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_ITEM_STATE_PRESENT, locker_sim_item(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_NONE, locker_sim_command_state(fixture.sim));
    TEST_CHECK(!locker_sim_has_active_intent(fixture.sim));
    TEST_CHECK(sim_session_is_nil(fixture.sim));
    first = locker_sim_first_available(fixture.sim);
    last = locker_sim_last_committed(fixture.sim);
    ack = locker_sim_ack_persisted(fixture.sim);
    TEST_CHECK_U64(1u, first.boot_id);
    TEST_CHECK_U64(1u, first.event_seq);
    TEST_CHECK_U64(1u, last.boot_id);
    TEST_CHECK_U64(1u, last.event_seq);
    TEST_CHECK_U64(0u, ack.boot_id);
    TEST_CHECK_U64(0u, ack.event_seq);
    /* A private SQLite file, never the business repository database. */
    TEST_CHECK(journal_header_is_sqlite(fixture.journal_path));
    sim_fixture_close(&fixture);
}

static void test_restart_bumps_boot_and_keeps_epoch(void)
{
    sim_fixture_t fixture;
    locker_uuid_t epoch_before;
    locker_uuid_t session;
    locker_wire_hello_response_t hello;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_fetch_events_response_t page;
    locker_wire_cursor_t after;
    uint64_t boot_before;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "journal_restart"));
    test_uuid_fill(&session, 0x20u);
    TEST_CHECK(sim_hello(&fixture, &session, &hello));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, hello.result);
    epoch_before = locker_sim_journal_epoch(fixture.sim);
    boot_before = locker_sim_boot_id(fixture.sim);
    TEST_CHECK_U64(1u, boot_before);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_restart(fixture.sim));
    TEST_CHECK_U64(boot_before + 1u, locker_sim_boot_id(fixture.sim));
    TEST_CHECK(sim_epoch_equals(fixture.sim, &epoch_before));
    /* The session dies with the restart (uart-v1.md:360). */
    TEST_CHECK(sim_session_is_nil(fixture.sim));
    TEST_CHECK_U64(2u, locker_sim_event_count(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING, locker_sim_device_mode(fixture.sim));

    /* Old session is rejected. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_SESSION, sim_snapshot(&fixture, &session, 2u, NULL));
    test_uuid_fill(&session, 0x30u);
    TEST_CHECK(sim_hello(&fixture, &session, NULL));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_snapshot(&fixture, &session, 3u, &snapshot));
    TEST_CHECK_U64(2u, snapshot.boot_id);

    /* Every retained record is addressable by (boot_id, event_seq). */
    after.boot_id = 0u;
    after.event_seq = 0u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_fetch(&fixture, &session, &snapshot.journal_epoch, after, 4u, &page));
    TEST_CHECK_U64(2u, page.event_count);
    TEST_CHECK_U64(1u, page.events[0].boot_id);
    TEST_CHECK_U64(1u, page.events[0].event_seq);
    TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT_TYPE_BOOT, page.events[0].event_type);
    TEST_CHECK_U64(2u, page.events[1].boot_id);
    TEST_CHECK_U64(1u, page.events[1].event_seq);
    TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT_TYPE_BOOT, page.events[1].event_type);
    TEST_CHECK_U64(0u, page.has_more);
    TEST_CHECK_U64(1u, page.first_available.boot_id);
    TEST_CHECK_U64(2u, page.last_committed.boot_id);
    TEST_CHECK_U64(1u, page.last_committed.event_seq);
    sim_fixture_close(&fixture);
}

static void test_events_survive_reopen_without_session(void)
{
    sim_fixture_t fixture;
    locker_wire_cursor_t first;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "journal_reopen"));
    /* No HELLO yet: events are committed to the journal but not queued. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_sim_control_set_door(fixture.sim, LOCKER_WIRE_DOOR_STATE_OPEN));
    TEST_CHECK_U64(2u, locker_sim_event_count(fixture.sim));
    {
        uint8_t frame[LOCKER_WIRE_MAX_FRAME];
        size_t frame_length = 0u;

        TEST_CHECK_U64(LOCKER_STATUS_NOT_READY,
                       locker_sim_pop_event(fixture.sim, frame, sizeof(frame), &frame_length));
    }
    first = locker_sim_first_available(fixture.sim);
    TEST_CHECK_U64(1u, first.boot_id);
    TEST_CHECK_U64(1u, first.event_seq);
    locker_sim_close(fixture.sim);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_open(fixture.sim));
    TEST_CHECK_U64(3u, locker_sim_event_count(fixture.sim));
    /* Restart has no new physical observation, so it reports UNKNOWN. */
    TEST_CHECK_U64(LOCKER_WIRE_DOOR_STATE_UNKNOWN, locker_sim_door(fixture.sim));
    first = locker_sim_first_available(fixture.sim);
    TEST_CHECK_U64(1u, first.boot_id);
    TEST_CHECK_U64(1u, first.event_seq);
    sim_fixture_close(&fixture);
}

static void test_action_watermark_and_query_survive_restart(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t epoch;
    locker_uuid_t command_id;
    locker_wire_hello_response_t hello;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_execute_unlock_response_t executed;
    locker_wire_query_command_response_t queried;
    uint64_t not_after;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "journal_watermark"));
    test_uuid_fill(&session, 0x50u);
    TEST_CHECK(sim_hello(&fixture, &session, &hello));
    epoch = hello.journal_epoch;
    sim_prepare_ready(&fixture, &session, &snapshot);
    test_uuid_fill(&command_id, 0x70u);
    not_after = fixture.clock.monotonic_ms + 5000u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute(&fixture, &session, &snapshot, 1u, &command_id,
                               LOCKER_WIRE_OPERATION_KIND_BORROW, not_after, &executed));
    TEST_CHECK_U64(1u, executed.action_no);
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED, executed.command_state);
    TEST_CHECK_U64(1u, locker_sim_action_watermark(fixture.sim));
    TEST_CHECK(locker_sim_has_active_intent(fixture.sim));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_restart(fixture.sim));
    TEST_CHECK_U64(0u, locker_sim_action_watermark(fixture.sim));
    TEST_CHECK(locker_sim_has_active_intent(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_UNKNOWN_RESULT, locker_sim_command_state(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_DOOR_STATE_UNKNOWN, locker_sim_door(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_ITEM_STATE_UNKNOWN, locker_sim_item(fixture.sim));
    TEST_CHECK(!locker_sim_continuity_ok(fixture.sim));

    test_uuid_fill(&session, 0x51u);
    TEST_CHECK(sim_hello(&fixture, &session, NULL));
    /* The command record outlives the session: query returns the recorded state. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_query(&fixture, &session, &epoch, &command_id, &queried));
    TEST_CHECK_U64(1u, queried.action_no);
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_UNKNOWN_RESULT, queried.command_state);
    TEST_CHECK(locker_uuid_equal(&command_id, &queried.command_id));
    /* Replaying the accepted action number is blocked, never re-executed. */
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_snapshot(&fixture, &session, 3u, &snapshot));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_REPLAY_BLOCKED,
                   sim_execute(&fixture, &session, &snapshot, 1u, &command_id,
                               LOCKER_WIRE_OPERATION_KIND_BORROW,
                               fixture.clock.monotonic_ms + 5000u, NULL));
    TEST_CHECK_U64(0u, locker_sim_action_watermark(fixture.sim));
    sim_fixture_close(&fixture);
}

static void test_gap_latches_history_incomplete(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_wire_get_snapshot_response_t snapshot;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "journal_gap"));
    test_uuid_fill(&session, 0x58u);
    TEST_CHECK(sim_hello(&fixture, &session, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_sim_control_inject_gap(fixture.sim, 0u));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_inject_gap(fixture.sim, 3u));
    TEST_CHECK_U64(3u, locker_sim_dropped_events(fixture.sim));
    TEST_CHECK(!locker_sim_continuity_ok(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_snapshot(&fixture, &session, 2u, &snapshot));
    TEST_CHECK_U64(LOCKER_WIRE_FLAG_HISTORY_INCOMPLETE,
                   snapshot.flags & LOCKER_WIRE_FLAG_HISTORY_INCOMPLETE);
    TEST_CHECK_U64(3u, snapshot.dropped_events);
    /* The latch survives a restart: continuity is persisted. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_restart(fixture.sim));
    TEST_CHECK(!locker_sim_continuity_ok(fixture.sim));
    TEST_CHECK_U64(3u, locker_sim_dropped_events(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY, locker_sim_journal_state(fixture.sim));
    sim_fixture_close(&fixture);
}

static void test_journal_failure_refuses_new_action(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t command_id;
    locker_wire_hello_response_t hello;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_execute_unlock_response_t executed;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "journal_fail"));
    test_uuid_fill(&session, 0x60u);
    TEST_CHECK(sim_hello(&fixture, &session, &hello));
    sim_prepare_ready(&fixture, &session, &snapshot);
    test_uuid_fill(&command_id, 0x72u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_fail_journal(fixture.sim, true));
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_JOURNAL_STATE_FAULT, locker_sim_journal_state(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_LOG_UNAVAILABLE,
                   sim_execute(&fixture, &session, &snapshot, 1u, &command_id,
                               LOCKER_WIRE_OPERATION_KIND_BORROW,
                               fixture.clock.monotonic_ms + 5000u, &executed));
    TEST_CHECK_U64(0u, locker_sim_action_watermark(fixture.sim));
    TEST_CHECK(!locker_sim_has_active_intent(fixture.sim));
    /* Recovery re-enables acceptance; nothing was acknowledged while failing. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_fail_journal(fixture.sim, false));
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY, locker_sim_journal_state(fixture.sim));
    sim_prepare_ready(&fixture, &session, &snapshot);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute(&fixture, &session, &snapshot, 1u, &command_id,
                               LOCKER_WIRE_OPERATION_KIND_BORROW,
                               fixture.clock.monotonic_ms + 5000u, &executed));
    TEST_CHECK_U64(1u, locker_sim_action_watermark(fixture.sim));
    sim_fixture_close(&fixture);
}

static uint64_t journal_scalar(sim_fixture_t *fixture, const char *sql)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    uint64_t value = UINT64_MAX;
    TEST_CHECK_U64(SQLITE_OK, sqlite3_open_v2(fixture->journal_path, &db, SQLITE_OPEN_READONLY, NULL));
    if (db != NULL && sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            value = (uint64_t)sqlite3_column_int64(stmt, 0);
        }
    }
    TEST_CHECK(value != UINT64_MAX);
    (void)sqlite3_finalize(stmt);
    (void)sqlite3_close(db);
    return value;
}

static void journal_write(sim_fixture_t *fixture, const char *sql)
{
    sqlite3 *db = NULL;
    TEST_CHECK_U64(SQLITE_OK, sqlite3_open_v2(fixture->journal_path, &db, SQLITE_OPEN_READWRITE, NULL));
    if (db != NULL) {
        TEST_CHECK_U64(SQLITE_OK, sqlite3_exec(db, sql, NULL, NULL, NULL));
    }
    (void)sqlite3_close(db);
}

static void test_transaction_failures_preserve_atomic_checkpoints(void)
{
    static const locker_sim_failure_stage_t stages[] = {
        LOCKER_SIM_FAILURE_BEGIN, LOCKER_SIM_FAILURE_INTENT, LOCKER_SIM_FAILURE_EVENT,
        LOCKER_SIM_FAILURE_COMMIT, LOCKER_SIM_FAILURE_RESULT
    };
    size_t index;
    for (index = 0u; index < sizeof(stages) / sizeof(stages[0]); index += 1u) {
        sim_fixture_t fixture;
        locker_uuid_t session;
        locker_uuid_t command;
        locker_wire_get_snapshot_response_t snapshot;
        locker_wire_query_command_response_t queried;
        bool accepted = stages[index] == LOCKER_SIM_FAILURE_RESULT;

        TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "journal_stage_fail"));
        test_uuid_fill(&session, 0x71u);
        test_uuid_fill(&command, 0x72u);
        TEST_CHECK(sim_hello(&fixture, &session, NULL));
        sim_prepare_ready(&fixture, &session, &snapshot);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_fail_next(fixture.sim, stages[index]));
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_LOG_UNAVAILABLE,
                       sim_execute(&fixture, &session, &snapshot, 1u, &command,
                                   LOCKER_WIRE_OPERATION_KIND_BORROW, 6000u, NULL));
        TEST_CHECK_U64(accepted ? 1u : 0u, locker_sim_action_watermark(fixture.sim));
        TEST_CHECK_U64(accepted ? 2u : 1u, locker_sim_event_count(fixture.sim));
        TEST_CHECK_U64(accepted ? 1u : 0u,
                       journal_scalar(&fixture, "SELECT COUNT(*) FROM sim_commands_v2"));
        TEST_CHECK_U64(accepted ? 2u : 1u,
                       journal_scalar(&fixture, "SELECT COUNT(*) FROM sim_events"));
        TEST_CHECK_U64(0u, journal_scalar(&fixture, "SELECT COUNT(*) FROM sim_events WHERE event_type=2"));
        TEST_CHECK_U64(accepted ? 1u : 0u,
                       journal_scalar(&fixture, "SELECT CAST(value AS INTEGER) FROM sim_meta WHERE key='action_watermark'"));
        TEST_CHECK_U64(accepted ? LOCKER_WIRE_RESULT_OK : LOCKER_WIRE_RESULT_UNKNOWN_RESULT,
                       sim_query(&fixture, &session, &snapshot.journal_epoch, &command, &queried));
        if (accepted) {
            TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_ACCEPTED, queried.command_state);
            TEST_CHECK(locker_sim_has_active_intent(fixture.sim));
        } else {
            TEST_CHECK(!locker_sim_has_active_intent(fixture.sim));
        }
        /* This is a real transaction FAULT with fail_journal=false. OFF cannot
         * hide it; this negative input distinguishes the old m32 mutant. */
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_fail_journal(fixture.sim, false));
        TEST_CHECK_U64(LOCKER_WIRE_WIRE_JOURNAL_STATE_FAULT, locker_sim_journal_state(fixture.sim));
        test_uuid_fill(&command, 0x73u);
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_LOG_UNAVAILABLE,
                       sim_execute(&fixture, &session, &snapshot, accepted ? 2u : 1u, &command,
                                   LOCKER_WIRE_OPERATION_KIND_BORROW, 6000u, NULL));
        sim_fixture_close(&fixture);
    }
}

static uint16_t journal_release(sim_fixture_t *fixture, const locker_uuid_t *session,
                                 const locker_wire_get_snapshot_response_t *snapshot,
                                 const locker_uuid_t *command)
{
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    memset(&request, 0, sizeof(request));
    request.release_context.session_id = *session;
    request.release_context.journal_epoch = snapshot->journal_epoch;
    request.release_context.expected_boot_id = snapshot->boot_id;
    request.release_context.expected_physical_revision = snapshot->physical_revision;
    test_uuid_fill(&request.release_context.operation_id, 0x60u);
    request.release_context.command_id = *command;
    return sim_exchange(fixture, 11u, 80u, &request, &response);
}

static void test_release_checkpoint_survives_process_reopen(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t command;
    locker_wire_get_snapshot_response_t snapshot;
    locker_sim_config_t config;
    uint64_t events;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "journal_release"));
    test_uuid_fill(&session, 0x77u);
    test_uuid_fill(&command, 0x78u);
    TEST_CHECK(sim_hello(&fixture, &session, NULL));
    sim_prepare_ready(&fixture, &session, &snapshot);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute(&fixture, &session, &snapshot, 1u, &command,
                               LOCKER_WIRE_OPERATION_KIND_BORROW, 6000u, NULL));
    fixture.clock.monotonic_ms += 150u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_snapshot(&fixture, &session, 9u, &snapshot));
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_PULSE_FINISHED, snapshot.active_command_state);
    events = locker_sim_event_count(fixture.sim);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_fail_next(fixture.sim, LOCKER_SIM_FAILURE_COMMIT));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_LOG_UNAVAILABLE, journal_release(&fixture, &session, &snapshot, &command));
    TEST_CHECK(locker_sim_has_active_intent(fixture.sim));
    TEST_CHECK_U64(events, locker_sim_event_count(fixture.sim));
    TEST_CHECK_U64(0u, journal_scalar(&fixture, "SELECT COUNT(*) FROM sim_events WHERE event_type=13"));
    /* Simulated process recreation reads the same checkpoint; it never invents
     * CLOSED/PRESENT observations and does not restart a pulse. */
    locker_sim_destroy(fixture.sim);
    fixture.sim = NULL;
    memset(&config, 0, sizeof(config));
    (void)snprintf(config.journal_path, sizeof(config.journal_path), "%s", fixture.journal_path);
    config.clock = test_clock_port(&fixture.clock);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_create(&fixture.sim, &config));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_open(fixture.sim));
    TEST_CHECK(locker_sim_has_active_intent(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_PULSE_FINISHED, locker_sim_command_state(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_DOOR_STATE_UNKNOWN, locker_sim_door(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_ITEM_STATE_UNKNOWN, locker_sim_item(fixture.sim));
    test_uuid_fill(&session, 0x79u);
    TEST_CHECK(sim_hello(&fixture, &session, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                       locker_sim_control_set_door(fixture.sim, LOCKER_WIRE_DOOR_STATE_CLOSED));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                       locker_sim_control_set_item(fixture.sim, LOCKER_WIRE_ITEM_STATE_ABSENT));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_snapshot(&fixture, &session, 10u, &snapshot));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, journal_release(&fixture, &session, &snapshot, &command));
    TEST_CHECK(!locker_sim_has_active_intent(fixture.sim));
    TEST_CHECK_U64(1u, journal_scalar(&fixture, "SELECT COUNT(*) FROM sim_events WHERE event_type=13"));
    /* uart-v1.md:46 and :352: detail is not applicable to CONTEXT_RELEASED, so the
     * released context is identified by operation_id/command_id alone and detail
     * must be 0. A non-zero detail here would smuggle the action number into a
     * field the contract reserves as "not applicable". */
    /* detail is stored as 20-digit zero-padded decimal TEXT, so comparing the
     * column against the integer 0 would always be true (SQLite orders TEXT above
     * INTEGER). Compare against the canonical zero text and additionally cast. */
    TEST_CHECK_U64(0u, journal_scalar(&fixture,
                    "SELECT COUNT(*) FROM sim_events WHERE event_type=13 "
                    "AND detail<>'00000000000000000000'"));
    TEST_CHECK_U64(0u, journal_scalar(&fixture,
                    "SELECT CAST(detail AS INTEGER) FROM sim_events WHERE event_type=13 LIMIT 1"));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_restart(fixture.sim));
    TEST_CHECK(!locker_sim_has_active_intent(fixture.sim));
    TEST_CHECK_U64(1u, locker_sim_pulse_count(fixture.sim));
    sim_fixture_close(&fixture);
}

static void test_physical_input_commit_failure_is_atomic(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_wire_get_snapshot_response_t before;
    locker_wire_get_snapshot_response_t after;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "journal_physical"));
    test_uuid_fill(&session, 0x7bu);
    TEST_CHECK(sim_hello(&fixture, &session, NULL));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_snapshot(&fixture, &session, 2u, &before));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_fail_next(fixture.sim, LOCKER_SIM_FAILURE_COMMIT));
    TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR,
                       locker_sim_control_set_door(fixture.sim, LOCKER_WIRE_DOOR_STATE_OPEN));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_snapshot(&fixture, &session, 3u, &after));
    TEST_CHECK_U64(before.door, after.door);
    TEST_CHECK_U64(before.physical_revision, after.physical_revision);
    TEST_CHECK_U64(1u, locker_sim_event_count(fixture.sim));
    TEST_CHECK(locker_sim_continuity_ok(fixture.sim));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_fail_journal(fixture.sim, false));
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_JOURNAL_STATE_FAULT, locker_sim_journal_state(fixture.sim));
    sim_fixture_close(&fixture);
}

static void test_failed_pulse_finish_latches_durable_gap(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t command;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_fetch_events_response_t page;
    locker_wire_request_union_t request;
    locker_wire_response_union_t response;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "journal_finish_fail"));
    test_uuid_fill(&session, 0x81u);
    test_uuid_fill(&command, 0x82u);
    TEST_CHECK(sim_hello(&fixture, &session, NULL));
    sim_prepare_ready(&fixture, &session, &snapshot);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute(&fixture, &session, &snapshot, 1u, &command,
                               LOCKER_WIRE_OPERATION_KIND_BORROW, 6000u, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_fail_next(fixture.sim, LOCKER_SIM_FAILURE_EVENT));
    fixture.clock.monotonic_ms += 150u;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, sim_snapshot(&fixture, &session, 8u, &snapshot));
    TEST_CHECK_U64(0u, snapshot.flags & LOCKER_WIRE_FLAG_PULSE_ACTIVE);
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_UNKNOWN_RESULT, snapshot.active_command_state);
    TEST_CHECK(!locker_sim_continuity_ok(fixture.sim));
    TEST_CHECK_U64(1u, snapshot.dropped_events);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_fetch(&fixture, &session, &snapshot.journal_epoch,
                             (locker_wire_cursor_t){0u, 0u}, 4u, &page));
    TEST_CHECK_U64(1u, page.gap_before_page);
    memset(&request, 0, sizeof(request));
    request.ack_persisted.session_id = session;
    request.ack_persisted.journal_epoch = snapshot.journal_epoch;
    request.ack_persisted.through = snapshot.last_committed;
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_CURSOR_GAP, sim_exchange(&fixture, 4u, 9u, &request, &response));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_restart(fixture.sim));
    TEST_CHECK(!locker_sim_continuity_ok(fixture.sim));
    TEST_CHECK_U64(1u, locker_sim_dropped_events(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_UNKNOWN_RESULT, locker_sim_command_state(fixture.sim));
    sim_fixture_close(&fixture);
}

static void test_corrupt_journal_rows_fail_closed(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t command;
    locker_wire_get_snapshot_response_t snapshot;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "journal_corrupt"));
    test_uuid_fill(&session, 0x85u);
    test_uuid_fill(&command, 0x86u);
    TEST_CHECK(sim_hello(&fixture, &session, NULL));
    sim_prepare_ready(&fixture, &session, &snapshot);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute(&fixture, &session, &snapshot, 1u, &command,
                               LOCKER_WIRE_OPERATION_KIND_BORROW, 6000u, NULL));
    journal_write(&fixture, "UPDATE sim_commands_v2 SET operation_id=x'01'");
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_LOG_UNAVAILABLE,
                   sim_query(&fixture, &session, &snapshot.journal_epoch, &command, NULL));
    journal_write(&fixture, "UPDATE sim_events SET operation_id=x'01' WHERE event_type=1");
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_LOG_UNAVAILABLE,
                   sim_fetch(&fixture, &session, &snapshot.journal_epoch,
                             (locker_wire_cursor_t){0u, 0u}, 4u, NULL));
    journal_write(&fixture, "UPDATE sim_events SET operation_id=zeroblob(16),mono_ms='99999999999999999999' WHERE event_type=1");
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_LOG_UNAVAILABLE,
                   sim_fetch(&fixture, &session, &snapshot.journal_epoch,
                             (locker_wire_cursor_t){0u, 0u}, 4u, NULL));
    journal_write(&fixture, "DROP TABLE sim_commands_v2");
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_LOG_UNAVAILABLE,
                   sim_query(&fixture, &session, &snapshot.journal_epoch, &command, NULL));
    sim_fixture_close(&fixture);
}

typedef struct delayed_clock {
    test_clock_t *base;
    unsigned calls;
    unsigned jump_on_call;
} delayed_clock_t;

static uint64_t delayed_monotonic(void *user)
{
    delayed_clock_t *clock = (delayed_clock_t *)user;
    clock->calls += 1u;
    if (clock->jump_on_call != 0u && clock->calls == clock->jump_on_call) {
        clock->base->monotonic_ms += 2001u;
    }
    return clock->base->monotonic_ms;
}

static bool delayed_wall(void *user, uint64_t *out)
{
    return test_clock_wall(((delayed_clock_t *)user)->base, out);
}

static locker_status_t delayed_uuid(void *user, locker_uuid_t *out)
{
    return test_clock_uuid(((delayed_clock_t *)user)->base, out);
}

static void test_deadline_rechecked_after_prepare_and_result(void)
{
    unsigned jump;
    /* EXECUTE samples receive, accepted-event, after accept-COMMIT,
     * start-event, after start-COMMIT. Advance only at either COMMIT return. */
    for (jump = 3u; jump <= 5u; jump += 2u) {
        sim_fixture_t fixture;
        locker_sim_config_t config;
        delayed_clock_t clock;
        locker_uuid_t session;
        locker_uuid_t command;
        locker_wire_get_snapshot_response_t snapshot;
        locker_wire_execute_unlock_response_t accepted;
        locker_wire_query_command_response_t queried;
        memset(&fixture, 0, sizeof(fixture));
        fixture.clock.monotonic_ms = 1000u;
        fixture.clock.wall_ms = 1700000000000u;
        sim_fixture_path(&fixture, "journal_deadline");
        memset(&clock, 0, sizeof(clock));
        clock.base = &fixture.clock;
        memset(&config, 0, sizeof(config));
        (void)snprintf(config.journal_path, sizeof(config.journal_path), "%s", fixture.journal_path);
        config.clock.user = &clock;
        config.clock.monotonic_ms = delayed_monotonic;
        config.clock.wall_utc_ms = delayed_wall;
        config.clock.new_uuid = delayed_uuid;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_create(&fixture.sim, &config));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_open(fixture.sim));
        test_uuid_fill(&session, 0x88u);
        test_uuid_fill(&command, 0x89u);
        TEST_CHECK(sim_hello(&fixture, &session, NULL));
        sim_prepare_ready(&fixture, &session, &snapshot);
        clock.calls = 0u;
        clock.jump_on_call = jump;
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                       sim_execute(&fixture, &session, &snapshot, 1u, &command,
                                   LOCKER_WIRE_OPERATION_KIND_BORROW, 6000u, &accepted));
        clock.jump_on_call = 0u;
        TEST_CHECK_U64(1u, locker_sim_action_watermark(fixture.sim));
        TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_REJECTED, accepted.command_state);
        TEST_CHECK_U64(0u, locker_sim_pulse_count(fixture.sim));
        TEST_CHECK_U64(0u, journal_scalar(&fixture, "SELECT COUNT(*) FROM sim_events WHERE event_type=2"));
        /* uart-v1.md:345 keeps detail=action_no on COMMAND_ACCEPTED. The accepted
         * checkpoint is rewritten from PULSE_STARTED to COMMAND_ACCEPTED when the
         * post-COMMIT guard expires, so both rows must still carry action_no=1 and
         * none may be zeroed. */
        TEST_CHECK(journal_scalar(&fixture,
                       "SELECT COUNT(*) FROM sim_events WHERE event_type=12") >= 1u);
        TEST_CHECK(journal_scalar(&fixture,
                       "SELECT COUNT(*) FROM sim_events WHERE event_type=12 "
                       "AND detail<>'00000000000000000001'") == 0u);
        TEST_CHECK(journal_scalar(&fixture,
                       "SELECT COUNT(*) FROM sim_events WHERE event_type=12 "
                       "AND detail='00000000000000000001'") >= 1u);
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                       sim_query(&fixture, &session, &snapshot.journal_epoch, &command, &queried));
        TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_REJECTED, queried.command_state);
        TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, journal_release(&fixture, &session, &snapshot, &command));
        sim_fixture_close(&fixture);
    }
}

static void test_real_sql_fault_cannot_be_cleared_by_injection_switch(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t command;
    locker_wire_get_snapshot_response_t snapshot;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "journal_sql_fault"));
    test_uuid_fill(&session, 0x8bu);
    test_uuid_fill(&command, 0x8cu);
    TEST_CHECK(sim_hello(&fixture, &session, NULL));
    sim_prepare_ready(&fixture, &session, &snapshot);
    journal_write(&fixture,
        "CREATE TRIGGER reject_result BEFORE UPDATE ON sim_commands_v2 WHEN NEW.state=2 "
        "BEGIN SELECT RAISE(ABORT,'formal result-write failure'); END");
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_LOG_UNAVAILABLE,
                   sim_execute(&fixture, &session, &snapshot, 1u, &command,
                               LOCKER_WIRE_OPERATION_KIND_BORROW, 6000u, NULL));
    TEST_CHECK_U64(1u, locker_sim_action_watermark(fixture.sim));
    TEST_CHECK_U64(1u, journal_scalar(&fixture, "SELECT COUNT(*) FROM sim_commands_v2 WHERE state=1"));
    TEST_CHECK_U64(0u, locker_sim_pulse_count(fixture.sim));
    journal_write(&fixture, "DROP TRIGGER reject_result");
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_fail_journal(fixture.sim, false));
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_JOURNAL_STATE_FAULT, locker_sim_journal_state(fixture.sim));
    test_uuid_fill(&command, 0x8du);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_LOG_UNAVAILABLE,
                   sim_execute(&fixture, &session, &snapshot, 2u, &command,
                               LOCKER_WIRE_OPERATION_KIND_BORROW, 6000u, NULL));
    sim_fixture_close(&fixture);
}

static void test_suppressed_intent_restores_without_replay(void)
{
    sim_fixture_t fixture;
    locker_uuid_t session;
    locker_uuid_t command;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_query_command_response_t queried;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, sim_fixture_open(&fixture, "journal_accepted"));
    test_uuid_fill(&session, 0x91u);
    test_uuid_fill(&command, 0x92u);
    TEST_CHECK(sim_hello(&fixture, &session, NULL));
    sim_prepare_ready(&fixture, &session, &snapshot);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_suppress_pulse(fixture.sim, true));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_execute(&fixture, &session, &snapshot, 1u, &command,
                               LOCKER_WIRE_OPERATION_KIND_BORROW, 6000u, NULL));
    TEST_CHECK_U64(0u, locker_sim_pulse_count(fixture.sim));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_restart(fixture.sim));
    TEST_CHECK(locker_sim_has_active_intent(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_ACCEPTED, locker_sim_command_state(fixture.sim));
    TEST_CHECK_U64(LOCKER_WIRE_ITEM_STATE_UNKNOWN, locker_sim_item(fixture.sim));
    test_uuid_fill(&session, 0x93u);
    TEST_CHECK(sim_hello(&fixture, &session, NULL));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK,
                   sim_query(&fixture, &session, &snapshot.journal_epoch, &command, &queried));
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_ACCEPTED, queried.command_state);
    TEST_CHECK_U64(0u, locker_sim_pulse_count(fixture.sim));
    sim_fixture_close(&fixture);
}

int main(void)
{
    test_open_creates_boot();
    test_transaction_failures_preserve_atomic_checkpoints();
    test_real_sql_fault_cannot_be_cleared_by_injection_switch();
    test_suppressed_intent_restores_without_replay();
    test_release_checkpoint_survives_process_reopen();
    test_physical_input_commit_failure_is_atomic();
    test_failed_pulse_finish_latches_durable_gap();
    test_corrupt_journal_rows_fail_closed();
    test_deadline_rechecked_after_prepare_and_result();
    test_restart_bumps_boot_and_keeps_epoch();
    test_events_survive_reopen_without_session();
    test_action_watermark_and_query_survive_restart();
    test_gap_latches_history_incomplete();
    test_journal_failure_refuses_new_action();
    TEST_REPORT("device.test_sim_journal");
}
