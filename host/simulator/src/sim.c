/*
 * S1-03 simulated locker device: UART v1 device side plus its own SQLite journal.
 *
 * Scope reminder (docs/stages/stage-01-task-cards.md:58-65):
 *   - the unlock pulse is a simulated counter, not a GPIO pulse;
 *   - "restart" re-opens the journal and advances boot_id inside this process,
 *     which is NOT an MCU/littlefs power-fail acceptance;
 *   - the journal file is independent of the business database;
 *   - the only control surface is the private 0600 AF_UNIX socket owned by
 *     host/simulator/src/sim_main.c.
 */

#include "locker/simulator/sim.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "locker/common/clock.h"
#include "locker/common/uuid.h"

#define SIM_META_EPOCH "journal_epoch"
#define SIM_META_BOOT "boot_id"
#define SIM_META_ACTION_WATERMARK "action_watermark"
#define SIM_META_ACK "ack_persisted"
#define SIM_META_DROPPED "dropped_events"
#define SIM_META_CONTINUITY "continuity_ok"
#define SIM_META_EVENT_SEQ "event_seq"

/* Simulated pulse duration. It is a counter in the journal, never a real timer. */
#define SIM_PULSE_MS 150u
/* uart-v1.md:394-396: five seconds without HOST_HEALTH stops new unlocks. */
#define SIM_HEALTH_TIMEOUT_MS 5000u
/* uart-v1.md:388: not_after_mcu_ms may be at most receive time + 15000. */
#define SIM_NOT_AFTER_HORIZON_MS 15000u
/* uart-v1.md:386: preparation deadline is 2000 ms. */
#define SIM_PREPARE_DEADLINE_MS 2000u
#define SIM_CAPABILITIES 0x001fu
#define SIM_FIRMWARE_MAJOR 0u
#define SIM_FIRMWARE_MINOR 1u
#define SIM_FIRMWARE_PATCH 0u
#define SIM_DEVICE_EVENT_QUEUE 32u

typedef struct sim_queued_event {
    locker_uuid_t session_id;
    locker_uuid_t journal_epoch;
    locker_wire_event96_t record;
} sim_queued_event_t;

struct locker_sim {
    bool created;
    bool opened;
    locker_sim_config_t config;

    sqlite3 *db;
    bool fail_journal;
    bool journal_fault_latched;
    bool meta_missing;
    locker_sim_failure_stage_t fail_next;

    /* Persisted journal state. */
    locker_uuid_t journal_epoch;
    uint64_t boot_id;
    uint64_t action_watermark;
    uint64_t dropped_events;
    uint64_t event_seq;
    locker_wire_cursor_t first_available;
    locker_wire_cursor_t last_committed;
    locker_wire_cursor_t ack_persisted;
    bool continuity_ok;

    /* Session state (never persisted: a restart invalidates the old session). */
    bool has_session;
    locker_uuid_t session_id;
    locker_uuid_t host_instance_id;
    bool hello_complete;

    uint8_t device_mode;
    uint8_t journal_state;
    uint8_t door;
    uint8_t item;
    uint64_t physical_revision;

    bool has_active;
    locker_unlock_intent_t active_intent;
    uint8_t command_state;

    bool health_seen;
    uint64_t last_health_ms;
    uint64_t core_progress;
    uint32_t core_observed_age_ms;
    bool snapshot_seen;
    locker_wire_cursor_t recovery_boundary;
    locker_wire_cursor_t fetched_through;
    uint64_t pulse_started_ms;
    locker_wire_event96_t pulse_checkpoint;
    bool pulse_active;

    /* Fault injection switches (control socket only). */
    bool reject_next;
    bool drop_ack;
    bool drop_execute_ack;
    bool suppress_pulse;

    sim_queued_event_t queue[SIM_DEVICE_EVENT_QUEUE];
    size_t queue_head;
    size_t queue_count;
};

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static void sim_mark_fault(locker_sim_t *sim)
{
    sim->journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_FAULT;
    sim->journal_fault_latched = true;
    sim->device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING;
}

static uint64_t sim_now_ms(const locker_sim_t *sim)
{
    uint64_t now_ms = 0u;

    if (sim == NULL || !locker_clock_port_is_valid(&sim->config.clock)) {
        return 0u;
    }
    if (locker_clock_port_monotonic_ms(&sim->config.clock, &now_ms) != LOCKER_STATUS_OK) {
        return 0u;
    }
    return now_ms;
}

/* Observation flags depend on journal state; declared here because event
 * emission (below) needs them before the definition. */
static uint16_t sim_continuity_flags(const locker_sim_t *sim);

static bool sim_uuid_nil(const locker_uuid_t *uuid)
{
    return uuid == NULL || locker_uuid_is_nil(uuid);
}

static void sim_uuid_set(locker_uuid_t *out, const locker_uuid_t *in)
{
    if (out == NULL || in == NULL) {
        return;
    }
    *out = *in;
}

static void sim_uuid_clear(locker_uuid_t *uuid)
{
    if (uuid != NULL) {
        memset(uuid, 0, sizeof(*uuid));
    }
}

static int sim_cursor_compare(locker_wire_cursor_t left, locker_wire_cursor_t right)
{
    if (left.boot_id != right.boot_id) {
        return left.boot_id < right.boot_id ? -1 : 1;
    }
    if (left.event_seq != right.event_seq) {
        return left.event_seq < right.event_seq ? -1 : 1;
    }
    return 0;
}

/* uart-v1.md:35-46: (0,0) is the only legal "no cursor" value; one zero field
 * with a non-zero other field is malformed. */
static bool sim_cursor_structure_ok(locker_wire_cursor_t cursor)
{
    return (cursor.boot_id == 0u) == (cursor.event_seq == 0u);
}

static void sim_u64_text(uint64_t value, char out[21])
{
    (void)snprintf(out, 21u, "%020llu", (unsigned long long)value);
}

static bool sim_u64_from_text(const char *text, uint64_t *out)
{
    uint64_t value = 0u;
    size_t index;

    if (text == NULL || out == NULL || strlen(text) != 20u) {
        return false;
    }
    for (index = 0u; index < 20u; index += 1u) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        if (value > (UINT64_MAX - (uint64_t)(text[index] - '0')) / 10u) {
            return false;
        }
        value = (value * 10u) + (uint64_t)(text[index] - '0');
    }
    *out = value;
    return true;
}

static void sim_cursor_text(locker_wire_cursor_t cursor, char out[42])
{
    char boot[21];
    char seq[21];

    sim_u64_text(cursor.boot_id, boot);
    sim_u64_text(cursor.event_seq, seq);
    (void)snprintf(out, 42u, "%s:%s", boot, seq);
}

static bool sim_cursor_from_text(const char *text, locker_wire_cursor_t *out)
{
    char boot[21];
    char seq[21];

    if (text == NULL || out == NULL || strlen(text) != 41u || text[20] != ':') {
        return false;
    }
    memcpy(boot, text, 20u);
    boot[20] = '\0';
    memcpy(seq, &text[21], 20u);
    seq[20] = '\0';
    if (!sim_u64_from_text(boot, &out->boot_id) || !sim_u64_from_text(seq, &out->event_seq)) {
        return false;
    }
    return true;
}

static void sim_uuid_hex(const locker_uuid_t *uuid, char out[33])
{
    size_t index;

    for (index = 0u; index < 16u; index += 1u) {
        (void)snprintf(&out[index * 2u], 3u, "%02x", uuid->bytes[index]);
    }
    out[32] = '\0';
}

static bool sim_uuid_from_hex(const char *text, locker_uuid_t *out)
{
    size_t index;
    unsigned int value;

    if (text == NULL || out == NULL || strlen(text) != 32u) {
        return false;
    }
    for (index = 0u; index < 16u; index += 1u) {
        if (!((text[index * 2u] >= '0' && text[index * 2u] <= '9') ||
              (text[index * 2u] >= 'a' && text[index * 2u] <= 'f')) ||
            !((text[index * 2u + 1u] >= '0' && text[index * 2u + 1u] <= '9') ||
              (text[index * 2u + 1u] >= 'a' && text[index * 2u + 1u] <= 'f')) ||
            sscanf(&text[index * 2u], "%2x", &value) != 1) {
            return false;
        }
        out->bytes[index] = (uint8_t)value;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* journal                                                             */
/* ------------------------------------------------------------------ */

static const char *const SIM_SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS sim_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID;"
    "CREATE TABLE IF NOT EXISTS sim_events("
    "boot_id TEXT NOT NULL, event_seq TEXT NOT NULL, mono_ms TEXT NOT NULL,"
    "event_type INTEGER NOT NULL, reason INTEGER NOT NULL,"
    "operation_id BLOB NOT NULL, command_id BLOB NOT NULL,"
    "door INTEGER NOT NULL, item INTEGER NOT NULL, flags INTEGER NOT NULL,"
    "supply_mv TEXT NOT NULL, physical_revision TEXT NOT NULL, detail TEXT NOT NULL,"
    "card_uid_len INTEGER NOT NULL, card_uid BLOB NOT NULL,"
    "PRIMARY KEY(boot_id, event_seq)) WITHOUT ROWID;"
    "CREATE TABLE IF NOT EXISTS sim_commands("
    "action_no TEXT PRIMARY KEY, session_id BLOB NOT NULL,"
    "operation_id BLOB NOT NULL, command_id BLOB NOT NULL,"
    "state INTEGER NOT NULL, boot_id TEXT NOT NULL, physical_revision TEXT NOT NULL,"
    "slot_id INTEGER NOT NULL, kind INTEGER NOT NULL,"
    "not_after_mcu_ms TEXT NOT NULL, reason INTEGER NOT NULL) WITHOUT ROWID;"
    "CREATE TABLE IF NOT EXISTS sim_commands_v2("
    "action_no TEXT NOT NULL, session_id BLOB NOT NULL,"
    "operation_id BLOB NOT NULL, command_id BLOB NOT NULL,"
    "state INTEGER NOT NULL, boot_id TEXT NOT NULL, physical_revision TEXT NOT NULL,"
    "slot_id INTEGER NOT NULL, kind INTEGER NOT NULL,"
    "not_after_mcu_ms TEXT NOT NULL, reason INTEGER NOT NULL,"
    "PRIMARY KEY(session_id,action_no)) WITHOUT ROWID;"
    "INSERT OR IGNORE INTO sim_commands_v2 SELECT * FROM sim_commands;";

static bool sim_sql_ok(int rc)
{
    return rc == SQLITE_DONE;
}

static bool sim_exec(sqlite3 *db, const char *sql)
{
    char *error = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &error);

    if (error != NULL) {
        sqlite3_free(error);
    }
    return rc == SQLITE_OK;
}

static bool sim_meta_set(locker_sim_t *sim, const char *key, const char *value)
{
    sqlite3_stmt *stmt = NULL;
    bool ok;

    if (sim->db == NULL) {
        return false;
    }
    if (sqlite3_prepare_v2(sim->db, "INSERT OR REPLACE INTO sim_meta(key,value) VALUES(?,?)", -1,
                           &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    ok = sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC) == SQLITE_OK &&
         sim_sql_ok(sqlite3_step(stmt));
    (void)sqlite3_finalize(stmt);
    return ok;
}

static bool sim_meta_get(locker_sim_t *sim, const char *key, char *out, size_t out_size)
{
    sqlite3_stmt *stmt = NULL;
    bool found = false;
    int step;

    sim->meta_missing = false;
    if (sim->db == NULL || out == NULL || out_size == 0u ||
        sqlite3_prepare_v2(sim->db, "SELECT value FROM sim_meta WHERE key=?", -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    if (sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC) != SQLITE_OK) {
        (void)sqlite3_finalize(stmt);
        return false;
    }
    step = sqlite3_step(stmt);
    if (step == SQLITE_ROW) {
        const unsigned char *value = sqlite3_column_text(stmt, 0);
        int bytes = sqlite3_column_bytes(stmt, 0);
        if (value != NULL && bytes >= 0 && (size_t)bytes < out_size &&
            strlen((const char *)value) == (size_t)bytes) {
            memcpy(out, value, (size_t)bytes + 1u);
            found = true;
        }
    } else if (step == SQLITE_DONE) {
        sim->meta_missing = true;
    }
    (void)sqlite3_finalize(stmt);
    return found;
}

static bool sim_meta_set_u64(locker_sim_t *sim, const char *key, uint64_t value)
{
    char text[21];

    sim_u64_text(value, text);
    return sim_meta_set(sim, key, text);
}

static bool sim_meta_get_u64(locker_sim_t *sim, const char *key, uint64_t *out)
{
    char text[32];

    if (!sim_meta_get(sim, key, text, sizeof(text))) {
        return false;
    }
    return sim_u64_from_text(text, out);
}

static bool sim_meta_set_cursor(locker_sim_t *sim, const char *key, locker_wire_cursor_t cursor)
{
    char text[42];

    sim_cursor_text(cursor, text);
    return sim_meta_set(sim, key, text);
}

static bool sim_meta_get_cursor(locker_sim_t *sim, const char *key, locker_wire_cursor_t *out)
{
    char text[64];

    if (!sim_meta_get(sim, key, text, sizeof(text))) {
        return false;
    }
    return sim_cursor_from_text(text, out);
}

static bool sim_failure(locker_sim_t *sim, locker_sim_failure_stage_t stage)
{
    if (sim->fail_next != stage) {
        return false;
    }
    sim->fail_next = LOCKER_SIM_FAILURE_NONE;
    return true;
}

static bool sim_journal_begin(locker_sim_t *sim)
{
    if (sim_failure(sim, LOCKER_SIM_FAILURE_BEGIN)) {
        return false;
    }
    return sim->db != NULL && sim_exec(sim->db, "BEGIN IMMEDIATE");
}

static bool sim_journal_commit(locker_sim_t *sim)
{
    if (sim->db == NULL) {
        return false;
    }
    if (sim->fail_journal || sim_failure(sim, LOCKER_SIM_FAILURE_COMMIT)) {
        (void)sim_exec(sim->db, "ROLLBACK");
        return false;
    }
    return sim_exec(sim->db, "COMMIT");
}

static void sim_journal_rollback(locker_sim_t *sim)
{
    if (sim->db != NULL) {
        (void)sim_exec(sim->db, "ROLLBACK");
    }
}

static bool sim_journal_store_event(locker_sim_t *sim, const locker_wire_event96_t *record)
{
    sqlite3_stmt *stmt = NULL;
    char boot[21];
    char seq[21];
    char mono[21];
    char supply[21];
    char revision[21];
    char detail[21];
    bool ok;

    if (sim->db == NULL || sim_failure(sim, LOCKER_SIM_FAILURE_EVENT)) {
        return false;
    }
    if (sqlite3_prepare_v2(sim->db,
                           "INSERT INTO sim_events(boot_id,event_seq,mono_ms,event_type,reason,"
                           "operation_id,command_id,door,item,flags,supply_mv,physical_revision,"
                           "detail,card_uid_len,card_uid) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    sim_u64_text(record->boot_id, boot);
    sim_u64_text(record->event_seq, seq);
    sim_u64_text(record->mono_ms, mono);
    sim_u64_text(record->supply_mv, supply);
    sim_u64_text(record->physical_revision, revision);
    sim_u64_text(record->detail, detail);
    ok = sqlite3_bind_text(stmt, 1, boot, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_text(stmt, 2, seq, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_text(stmt, 3, mono, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_int(stmt, 4, (int)record->event_type) == SQLITE_OK &&
         sqlite3_bind_int(stmt, 5, (int)record->reason) == SQLITE_OK &&
         sqlite3_bind_blob(stmt, 6, record->operation_id.bytes, 16, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_blob(stmt, 7, record->command_id.bytes, 16, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_int(stmt, 8, (int)record->door) == SQLITE_OK &&
         sqlite3_bind_int(stmt, 9, (int)record->item) == SQLITE_OK &&
         sqlite3_bind_int(stmt, 10, (int)record->flags) == SQLITE_OK &&
         sqlite3_bind_text(stmt, 11, supply, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_text(stmt, 12, revision, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_text(stmt, 13, detail, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_int(stmt, 14, (int)record->card_uid_len) == SQLITE_OK &&
         sqlite3_bind_blob(stmt, 15, record->card_uid, 10, SQLITE_STATIC) == SQLITE_OK &&
         sim_sql_ok(sqlite3_step(stmt));
    (void)sqlite3_finalize(stmt);
    return ok;
}

static bool sim_journal_store_command(locker_sim_t *sim, const locker_unlock_intent_t *intent,
                                      uint64_t boot_id, uint64_t physical_revision,
                                      uint8_t state, uint16_t reason)
{
    sqlite3_stmt *stmt = NULL;
    char action[21];
    char boot[21];
    char revision[21];
    char not_after[21];
    bool ok;

    if (sim->db == NULL || sim_failure(sim, LOCKER_SIM_FAILURE_INTENT)) {
        return false;
    }
    if (sqlite3_prepare_v2(sim->db,
                           "INSERT OR REPLACE INTO sim_commands_v2(action_no,session_id,operation_id,"
                           "command_id,state,boot_id,physical_revision,slot_id,kind,"
                           "not_after_mcu_ms,reason) VALUES(?,?,?,?,?,?,?,?,?,?,?)",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    sim_u64_text(intent->action_no, action);
    sim_u64_text(boot_id, boot);
    sim_u64_text(physical_revision, revision);
    sim_u64_text(intent->not_after_mcu_ms, not_after);
    ok = sqlite3_bind_text(stmt, 1, action, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_blob(stmt, 2, intent->session_id.bytes, 16, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_blob(stmt, 3, intent->operation_id.bytes, 16, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_blob(stmt, 4, intent->command_id.bytes, 16, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_int(stmt, 5, (int)state) == SQLITE_OK &&
         sqlite3_bind_text(stmt, 6, boot, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_text(stmt, 7, revision, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_int(stmt, 8, (int)intent->slot_id) == SQLITE_OK &&
         sqlite3_bind_int(stmt, 9, (int)intent->kind) == SQLITE_OK &&
         sqlite3_bind_text(stmt, 10, not_after, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_int(stmt, 11, (int)reason) == SQLITE_OK &&
         sim_sql_ok(sqlite3_step(stmt));
    (void)sqlite3_finalize(stmt);
    return ok;
}

typedef struct sim_command_row {
    bool found;
    locker_uuid_t session_id;
    locker_uuid_t operation_id;
    locker_uuid_t command_id;
    uint8_t state;
    uint64_t boot_id;
    uint64_t physical_revision;
    uint64_t action_no;
    uint16_t slot_id;
    uint8_t kind;
    uint64_t not_after_mcu_ms;
    uint16_t reason;
} sim_command_row_t;

static bool sim_command_row_valid(const sim_command_row_t *row)
{
    return !sim_uuid_nil(&row->session_id) && !sim_uuid_nil(&row->operation_id) &&
           !sim_uuid_nil(&row->command_id) && row->action_no != 0u && row->boot_id != 0u &&
           row->physical_revision != 0u && row->state <= LOCKER_WIRE_COMMAND_STATE_UNKNOWN_RESULT &&
           row->slot_id == 1u && (row->kind == LOCKER_WIRE_OPERATION_KIND_BORROW ||
                                  row->kind == LOCKER_WIRE_OPERATION_KIND_RETURN);
}

static bool sim_journal_load_command(locker_sim_t *sim, uint64_t action_no, sim_command_row_t *out)
{
    sqlite3_stmt *stmt = NULL;
    char action[21];
    bool ok = true;
    int step;

    memset(out, 0, sizeof(*out));
    if (sim->db == NULL) {
        return false;
    }
    sim_u64_text(action_no, action);
    if (sqlite3_prepare_v2(sim->db,
                           "SELECT session_id,operation_id,command_id,state,boot_id,"
                           "physical_revision,slot_id,kind,not_after_mcu_ms,reason FROM sim_commands_v2 "
                           "WHERE action_no=? AND session_id=?",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    if (sqlite3_bind_text(stmt, 1, action, -1, SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_blob(stmt, 2, sim->session_id.bytes, 16, SQLITE_STATIC) != SQLITE_OK) {
        (void)sqlite3_finalize(stmt);
        return false;
    }
    step = sqlite3_step(stmt);
    if (step == SQLITE_ROW) {
        const void *session = sqlite3_column_blob(stmt, 0);
        const void *operation = sqlite3_column_blob(stmt, 1);
        const void *command = sqlite3_column_blob(stmt, 2);
        const unsigned char *boot = sqlite3_column_text(stmt, 4);
        const unsigned char *revision = sqlite3_column_text(stmt, 5);
        const unsigned char *not_after = sqlite3_column_text(stmt, 8);

        if (sqlite3_column_bytes(stmt, 0) != 16 || sqlite3_column_bytes(stmt, 1) != 16 ||
            sqlite3_column_bytes(stmt, 2) != 16 || boot == NULL || revision == NULL || not_after == NULL) {
            (void)sqlite3_finalize(stmt);
            return false;
        }
        if (sqlite3_column_int64(stmt, 3) < LOCKER_WIRE_COMMAND_STATE_NONE ||
            sqlite3_column_int64(stmt, 3) > LOCKER_WIRE_COMMAND_STATE_UNKNOWN_RESULT ||
            sqlite3_column_int64(stmt, 6) != 1 ||
            (sqlite3_column_int64(stmt, 7) != LOCKER_WIRE_OPERATION_KIND_BORROW &&
             sqlite3_column_int64(stmt, 7) != LOCKER_WIRE_OPERATION_KIND_RETURN) ||
            sqlite3_column_int64(stmt, 9) < 0 || sqlite3_column_int64(stmt, 9) > UINT16_MAX) {
            (void)sqlite3_finalize(stmt);
            return false;
        }
        out->found = true;
        out->action_no = action_no;
        if (session != NULL) {
            memcpy(out->session_id.bytes, session, 16u);
        }
        if (operation != NULL) {
            memcpy(out->operation_id.bytes, operation, 16u);
        }
        if (command != NULL) {
            memcpy(out->command_id.bytes, command, 16u);
        }
        out->state = (uint8_t)sqlite3_column_int(stmt, 3);
        out->slot_id = (uint16_t)sqlite3_column_int(stmt, 6);
        out->kind = (uint8_t)sqlite3_column_int(stmt, 7);
        out->reason = (uint16_t)sqlite3_column_int(stmt, 9);
        if (boot != NULL) {
            ok = ok && sim_u64_from_text((const char *)boot, &out->boot_id);
        }
        if (revision != NULL) {
            ok = ok && sim_u64_from_text((const char *)revision, &out->physical_revision);
        }
        if (not_after != NULL) {
            ok = ok && sim_u64_from_text((const char *)not_after, &out->not_after_mcu_ms);
        }
    }
    if (step != SQLITE_ROW && step != SQLITE_DONE) {
        ok = false;
    }
    (void)sqlite3_finalize(stmt);
    return ok && (!out->found || sim_command_row_valid(out));
}

static bool sim_journal_load_command_by_id(locker_sim_t *sim, const locker_uuid_t *command_id,
                                           sim_command_row_t *out)
{
    sqlite3_stmt *stmt = NULL;
    bool ok = true;
    int step;

    memset(out, 0, sizeof(*out));
    if (sim->db == NULL) {
        return false;
    }
    if (sqlite3_prepare_v2(sim->db,
                           "SELECT action_no,session_id,operation_id,state,boot_id,"
                           "physical_revision,slot_id,kind,not_after_mcu_ms,reason FROM sim_commands_v2 "
                           "WHERE command_id=? ORDER BY action_no DESC LIMIT 1",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    if (sqlite3_bind_blob(stmt, 1, command_id->bytes, 16, SQLITE_STATIC) != SQLITE_OK) {
        (void)sqlite3_finalize(stmt);
        return false;
    }
    step = sqlite3_step(stmt);
    if (step == SQLITE_ROW) {
        const unsigned char *action = sqlite3_column_text(stmt, 0);
        const void *session = sqlite3_column_blob(stmt, 1);
        const void *operation = sqlite3_column_blob(stmt, 2);
        const unsigned char *boot = sqlite3_column_text(stmt, 4);
        const unsigned char *revision = sqlite3_column_text(stmt, 5);
        const unsigned char *not_after = sqlite3_column_text(stmt, 8);

        if (sqlite3_column_bytes(stmt, 1) != 16 || sqlite3_column_bytes(stmt, 2) != 16 ||
            action == NULL || boot == NULL || revision == NULL || not_after == NULL) {
            (void)sqlite3_finalize(stmt);
            return false;
        }
        if (sqlite3_column_int64(stmt, 3) < LOCKER_WIRE_COMMAND_STATE_NONE ||
            sqlite3_column_int64(stmt, 3) > LOCKER_WIRE_COMMAND_STATE_UNKNOWN_RESULT ||
            sqlite3_column_int64(stmt, 6) != 1 ||
            (sqlite3_column_int64(stmt, 7) != LOCKER_WIRE_OPERATION_KIND_BORROW &&
             sqlite3_column_int64(stmt, 7) != LOCKER_WIRE_OPERATION_KIND_RETURN) ||
            sqlite3_column_int64(stmt, 9) < 0 || sqlite3_column_int64(stmt, 9) > UINT16_MAX) {
            (void)sqlite3_finalize(stmt);
            return false;
        }
        out->found = true;
        out->state = (uint8_t)sqlite3_column_int(stmt, 3);
        out->slot_id = (uint16_t)sqlite3_column_int(stmt, 6);
        out->kind = (uint8_t)sqlite3_column_int(stmt, 7);
        out->reason = (uint16_t)sqlite3_column_int(stmt, 9);
        out->command_id = *command_id;
        if (session != NULL) {
            memcpy(out->session_id.bytes, session, 16u);
        }
        if (operation != NULL) {
            memcpy(out->operation_id.bytes, operation, 16u);
        }
        if (action != NULL) {
            ok = ok && sim_u64_from_text((const char *)action, &out->action_no);
        }
        if (boot != NULL) {
            ok = ok && sim_u64_from_text((const char *)boot, &out->boot_id);
        }
        if (revision != NULL) {
            ok = ok && sim_u64_from_text((const char *)revision, &out->physical_revision);
        }
        if (not_after != NULL) {
            ok = ok && sim_u64_from_text((const char *)not_after, &out->not_after_mcu_ms);
        }
    }
    if (step != SQLITE_ROW && step != SQLITE_DONE) {
        ok = false;
    }
    (void)sqlite3_finalize(stmt);
    return ok && (!out->found || sim_command_row_valid(out));
}

static bool sim_journal_update_command_state(locker_sim_t *sim, uint64_t action_no, uint8_t state,
                                             uint16_t reason)
{
    sqlite3_stmt *stmt = NULL;
    char action[21];
    bool ok;

    if (sim->db == NULL || sim_failure(sim, LOCKER_SIM_FAILURE_RESULT)) {
        return false;
    }
    if (sqlite3_prepare_v2(sim->db,
                           "UPDATE sim_commands_v2 SET state=?,reason=? WHERE action_no=? AND session_id=?", -1, &stmt,
                           NULL) != SQLITE_OK) {
        return false;
    }
    sim_u64_text(action_no, action);
    ok = sqlite3_bind_int(stmt, 1, (int)state) == SQLITE_OK &&
         sqlite3_bind_int(stmt, 2, (int)reason) == SQLITE_OK &&
         sqlite3_bind_text(stmt, 3, action, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_blob(stmt, 4, sim->active_intent.session_id.bytes, 16, SQLITE_STATIC) == SQLITE_OK &&
         sim_sql_ok(sqlite3_step(stmt)) && sqlite3_changes(sim->db) == 1;
    (void)sqlite3_finalize(stmt);
    return ok;
}

static uint64_t sim_journal_event_count(const locker_sim_t *sim)
{
    sqlite3_stmt *stmt = NULL;
    uint64_t count = 0u;

    if (sim->db == NULL) {
        return 0u;
    }
    if (sqlite3_prepare_v2(sim->db, "SELECT COUNT(*) FROM sim_events", -1, &stmt, NULL) !=
        SQLITE_OK) {
        return 0u;
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = (uint64_t)sqlite3_column_int64(stmt, 0);
    }
    (void)sqlite3_finalize(stmt);
    return count;
}

/* Loads the persisted journal state. A fresh file gets a new epoch and boot 0. */
static bool sim_journal_load_meta(locker_sim_t *sim)
{
    char text[64];
    bool fresh;

    fresh = !sim_meta_get(sim, SIM_META_EPOCH, text, sizeof(text));
    if (fresh && !sim->meta_missing) {
        return false;
    }
    if (fresh) {
        locker_uuid_t epoch;
        if (locker_clock_port_new_uuid(&sim->config.clock, &epoch) != LOCKER_STATUS_OK ||
            sim_uuid_nil(&epoch)) {
            return false;
        }
        sim_uuid_set(&sim->journal_epoch, &epoch);
        if (!sim_meta_set(sim, SIM_META_EPOCH, "")) {
            return false;
        }
        sim_uuid_hex(&epoch, text);
        if (!sim_meta_set(sim, SIM_META_EPOCH, text)) {
            return false;
        }
        if (!sim_meta_set_u64(sim, SIM_META_BOOT, 0u) ||
            !sim_meta_set_u64(sim, SIM_META_ACTION_WATERMARK, 0u) ||
            !sim_meta_set_u64(sim, SIM_META_DROPPED, 0u) ||
            !sim_meta_set_u64(sim, SIM_META_EVENT_SEQ, 0u) ||
            !sim_meta_set(sim, SIM_META_CONTINUITY, "1") ||
            !sim_meta_set_cursor(sim, SIM_META_ACK, (locker_wire_cursor_t){0u, 0u})) {
            return false;
        }
    } else if (!sim_uuid_from_hex(text, &sim->journal_epoch)) {
        return false;
    }
    if (!sim_meta_get_u64(sim, SIM_META_BOOT, &sim->boot_id) ||
        !sim_meta_get_u64(sim, SIM_META_ACTION_WATERMARK, &sim->action_watermark) ||
        !sim_meta_get_u64(sim, SIM_META_DROPPED, &sim->dropped_events) ||
        !sim_meta_get_u64(sim, SIM_META_EVENT_SEQ, &sim->event_seq)) {
        return false;
    }
    if (!sim_meta_get(sim, SIM_META_CONTINUITY, text, sizeof(text))) {
        return false;
    }
    sim->continuity_ok = strcmp(text, "1") == 0;
    if (!sim_meta_get_cursor(sim, SIM_META_ACK, &sim->ack_persisted)) {
        return false;
    }
    sim->first_available.boot_id = 0u;
    sim->first_available.event_seq = 0u;
    sim->last_committed.boot_id = 0u;
    sim->last_committed.event_seq = 0u;
    return true;
}

static bool sim_journal_recompute_bounds(locker_sim_t *sim)
{
    sqlite3_stmt *stmt = NULL;
    bool ok = true;
    int step;

    if (sim->db == NULL) {
        return false;
    }
    if (sqlite3_prepare_v2(sim->db,
                           "SELECT boot_id,event_seq FROM sim_events ORDER BY boot_id,event_seq "
                           "LIMIT 1",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    step = sqlite3_step(stmt);
    if (step == SQLITE_ROW) {
        const unsigned char *boot = sqlite3_column_text(stmt, 0);
        const unsigned char *seq = sqlite3_column_text(stmt, 1);
        ok = ok && sim_u64_from_text((const char *)boot, &sim->first_available.boot_id);
        ok = ok && sim_u64_from_text((const char *)seq, &sim->first_available.event_seq);
    }
    ok = ok && (step == SQLITE_ROW || step == SQLITE_DONE);
    (void)sqlite3_finalize(stmt);
    stmt = NULL;
    if (sqlite3_prepare_v2(sim->db,
                           "SELECT boot_id,event_seq FROM sim_events ORDER BY boot_id DESC,"
                           "event_seq DESC LIMIT 1",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    step = sqlite3_step(stmt);
    if (step == SQLITE_ROW) {
        const unsigned char *boot = sqlite3_column_text(stmt, 0);
        const unsigned char *seq = sqlite3_column_text(stmt, 1);
        ok = ok && sim_u64_from_text((const char *)boot, &sim->last_committed.boot_id);
        ok = ok && sim_u64_from_text((const char *)seq, &sim->last_committed.event_seq);
    }
    ok = ok && (step == SQLITE_ROW || step == SQLITE_DONE);
    (void)sqlite3_finalize(stmt);
    return ok;
}

/* ------------------------------------------------------------------ */
/* events                                                              */
/* ------------------------------------------------------------------ */

static void sim_queue_push(locker_sim_t *sim, const locker_wire_event96_t *record)
{
    size_t slot;

    if (!sim->has_session) {
        /* uart-v1.md:356: without a session the record is journaled only. */
        return;
    }
    if (sim->queue_count >= SIM_DEVICE_EVENT_QUEUE) {
        /* The committed record remains available through FETCH. This is live
         * notification backpressure, never a journal continuity fault. */
        return;
    }
    slot = (sim->queue_head + sim->queue_count) % SIM_DEVICE_EVENT_QUEUE;
    sim->queue[slot].session_id = sim->session_id;
    sim->queue[slot].journal_epoch = sim->journal_epoch;
    sim->queue[slot].record = *record;
    sim->queue_count += 1u;
}

/* Builds, journals and queues one event in the current boot. */
static bool sim_emit_event(locker_sim_t *sim, uint16_t event_type, uint16_t reason,
                           uint64_t detail, const locker_uuid_t *operation_id,
                           const locker_uuid_t *command_id, uint8_t card_uid_len,
                           const uint8_t *card_uid)
{
    locker_wire_event96_t record;
    uint64_t next_seq;
    bool ok;

    if (sim->event_seq == UINT64_MAX) {
        sim_mark_fault(sim);
        return false;
    }
    memset(&record, 0, sizeof(record));
    next_seq = sim->event_seq + 1u;
    record.boot_id = sim->boot_id;
    record.event_seq = next_seq;
    record.mono_ms = sim_now_ms(sim);
    record.event_type = event_type;
    record.reason = reason;
    record.door = sim->door;
    record.item = sim->item;
    record.flags = sim_continuity_flags(sim);
    record.supply_mv = 5000u;
    record.physical_revision = sim->physical_revision;
    record.detail = detail;
    record.card_uid_len = card_uid_len;
    if (operation_id != NULL) {
        record.operation_id = *operation_id;
    }
    if (command_id != NULL) {
        record.command_id = *command_id;
    }
    if (card_uid != NULL && card_uid_len > 0u) {
        memcpy(record.card_uid, card_uid, card_uid_len);
    }
    if (!sim_journal_begin(sim)) {
        sim_mark_fault(sim);
        return false;
    }
    ok = sim_journal_store_event(sim, &record) &&
         sim_meta_set_u64(sim, SIM_META_EVENT_SEQ, next_seq) && sim_journal_commit(sim);
    if (!ok) {
        sim_journal_rollback(sim);
        sim_mark_fault(sim);
        return false;
    }
    sim->event_seq = next_seq;
    sim->last_committed = (locker_wire_cursor_t){sim->boot_id, next_seq};
    if (sim->first_available.boot_id == 0u) {
        sim->first_available = sim->last_committed;
    }
    sim_queue_push(sim, &record);
    return true;
}

/* ------------------------------------------------------------------ */
/* session helpers                                                     */
/* ------------------------------------------------------------------ */

static void sim_revoke_actions(locker_sim_t *sim)
{
    /* HELLO/DISARM revoke new-action qualification without clearing the
     * recorded context or the action watermark (uart-v1.md:360, :378). */
    sim->device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING;
}

static bool sim_session_matches(const locker_sim_t *sim, const locker_uuid_t *session_id)
{
    return sim->has_session && session_id != NULL && !sim_uuid_nil(session_id) &&
           locker_uuid_equal(&sim->session_id, session_id);
}

static uint16_t sim_continuity_flags(const locker_sim_t *sim)
{
    uint16_t flags = 0u;

    flags |= LOCKER_WIRE_FLAG_SUPPLY_VALID;
    if (sim->door != LOCKER_WIRE_DOOR_STATE_UNKNOWN && sim->item != LOCKER_WIRE_ITEM_STATE_UNKNOWN) {
        flags |= LOCKER_WIRE_FLAG_PHYSICAL_STABLE;
    }
    if (!sim->continuity_ok) {
        flags |= LOCKER_WIRE_FLAG_HISTORY_INCOMPLETE;
    }
    if (sim->journal_state != LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY) {
        flags |= LOCKER_WIRE_FLAG_JOURNAL_DEGRADED;
    }
    if (sim->pulse_active) {
        flags |= LOCKER_WIRE_FLAG_PULSE_ACTIVE;
    }
    return flags;
}

/* ------------------------------------------------------------------ */
/* journal transaction wrapper                                          */
/* ------------------------------------------------------------------ */

/* Runs a body that must either commit fully or leave the journal untouched.
 * The fail_journal switch makes COMMIT fail, which is how the tests prove the
 * "log commit failure rejects new actions" acceptance item. */
typedef bool (*sim_txn_body_t)(locker_sim_t *sim, void *context);

static bool sim_run_txn(locker_sim_t *sim, sim_txn_body_t body, void *context)
{
    if (sim->db == NULL) {
        return false;
    }
    if (!sim_journal_begin(sim)) {
        sim_mark_fault(sim);
        return false;
    }
    if (!body(sim, context)) {
        sim_mark_fault(sim);
        sim_journal_rollback(sim);
        return false;
    }
    if (!sim_journal_commit(sim)) {
        sim_journal_rollback(sim);
        sim_mark_fault(sim);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* reply helpers                                                       */
/* ------------------------------------------------------------------ */

typedef struct sim_reply_context {
    uint8_t type;
    uint32_t request_no;
    locker_uuid_t session_id;
    uint16_t result;
    locker_wire_response_union_t response;
} sim_reply_context_t;

static locker_status_t sim_emit_reply(sim_reply_context_t *context, uint8_t *out, size_t out_size,
                                      size_t *out_length)
{
    locker_status_t status;

    status = locker_wire_encode_response(context->type, context->request_no, &context->response, out,
                                         out_size, out_length);
    /* A non-zero result is already collapsed to the 18-byte prefix by the codec. */
    return status;
}

/* Every request payload starts with the session the host believes it owns
 * (uart-v1.md:27-31), so a frame that fails before or during decoding can still
 * be correlated: the reply echoes that carried session instead of the device's
 * current one. Only a session-less reply (HELLO, or a payload shorter than the
 * 16-byte prefix) falls back to the nil UUID. */
static void sim_echo_session(const locker_wire_frame_t *frame, locker_uuid_t *out)
{
    memset(out, 0, sizeof(*out));
    if (frame->payload_len >= sizeof(out->bytes)) {
        memcpy(out->bytes, frame->payload, sizeof(out->bytes));
    }
}

static void sim_reply_init(sim_reply_context_t *context, uint8_t type, uint32_t request_no,
                           const locker_uuid_t *session_id)
{
    memset(context, 0, sizeof(*context));
    context->type = type;
    context->request_no = request_no;
    if (session_id != NULL) {
        context->session_id = *session_id;
    }
    context->response.hello.session_id = context->session_id;
}

static void sim_reply_set_result(sim_reply_context_t *context, uint16_t result)
{
    context->result = result;
    switch (context->type) {
    case 1u:
        context->response.hello.result = result;
        break;
    case 2u:
        context->response.get_snapshot.result = result;
        break;
    case 3u:
        context->response.fetch_events.result = result;
        break;
    case 4u:
        context->response.ack_persisted.result = result;
        break;
    case 5u:
        context->response.resume_ready.result = result;
        break;
    case 6u:
        context->response.disarm.result = result;
        break;
    case 7u:
        context->response.execute_unlock.result = result;
        break;
    case 8u:
        context->response.query_command.result = result;
        break;
    case 9u:
        context->response.host_health.result = result;
        break;
    case 10u:
        context->response.time_anchor.result = result;
        break;
    case 11u:
        context->response.release_context.result = result;
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* request handlers                                                    */
/* ------------------------------------------------------------------ */

static bool sim_session_watermark(locker_sim_t *sim, const locker_uuid_t *session,
                                   uint64_t *watermark)
{
    sqlite3_stmt *stmt = NULL;
    int step;
    bool ok;

    *watermark = 0u;
    if (sqlite3_prepare_v2(sim->db,
        "SELECT action_no FROM sim_commands_v2 WHERE session_id=? ORDER BY action_no DESC LIMIT 1",
        -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    if (sqlite3_bind_blob(stmt, 1, session->bytes, 16, SQLITE_STATIC) != SQLITE_OK) {
        (void)sqlite3_finalize(stmt);
        return false;
    }
    step = sqlite3_step(stmt);
    ok = step == SQLITE_DONE ||
         (step == SQLITE_ROW && sim_u64_from_text((const char *)sqlite3_column_text(stmt, 0), watermark));
    (void)sqlite3_finalize(stmt);
    return ok;
}

static uint16_t sim_handle_hello(locker_sim_t *sim, const locker_wire_hello_request_t *request,
                                 sim_reply_context_t *reply)
{
    locker_wire_hello_response_t *out = &reply->response.hello;
    bool same_session;
    uint64_t watermark = 0u;

    out->session_id = request->new_session_id;
    if (request->requested_version != LOCKER_WIRE_VERSION) {
        return LOCKER_WIRE_RESULT_BAD_VERSION;
    }
    if (sim_uuid_nil(&request->new_session_id) || sim_uuid_nil(&request->host_instance_id)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    /* uart-v1.md:360: a retry of the same new_session_id + host_instance_id is
     * idempotent and must not reset the action watermark. */
    same_session = sim->has_session &&
                   locker_uuid_equal(&sim->session_id, &request->new_session_id) &&
                   locker_uuid_equal(&sim->host_instance_id, &request->host_instance_id);
    if (!same_session) {
        if (!sim_session_watermark(sim, &request->new_session_id, &watermark)) {
            return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
        }
        sim->session_id = request->new_session_id;
        sim->host_instance_id = request->host_instance_id;
        sim->has_session = true;
        sim->hello_complete = true;
        sim->action_watermark = watermark;
        sim->health_seen = false;
        sim->core_progress = 0u;
        sim->snapshot_seen = false;
        sim->fetched_through = sim->ack_persisted;
        sim->queue_head = 0u;
        sim->queue_count = 0u;
        sim_revoke_actions(sim);
    }
    out->device_uid[0] = sim->config.device_uid[0];
    memcpy(out->device_uid, sim->config.device_uid, LOCKER_SIM_DEVICE_UID_SIZE);
    out->journal_epoch = sim->journal_epoch;
    out->boot_id = sim->boot_id;
    out->firmware_major = SIM_FIRMWARE_MAJOR;
    out->firmware_minor = SIM_FIRMWARE_MINOR;
    out->firmware_patch = SIM_FIRMWARE_PATCH;
    out->capabilities = SIM_CAPABILITIES;
    out->device_mode = sim->device_mode;
    out->journal_state = sim->journal_state;
    out->action_watermark = sim->action_watermark;
    return LOCKER_WIRE_RESULT_OK;
}

static uint16_t sim_handle_get_snapshot(locker_sim_t *sim, sim_reply_context_t *reply)
{
    locker_wire_get_snapshot_response_t *out = &reply->response.get_snapshot;

    out->journal_epoch = sim->journal_epoch;
    out->boot_id = sim->boot_id;
    out->sampled_mono_ms = sim_now_ms(sim);
    sim->snapshot_seen = true;
    sim->recovery_boundary = sim->last_committed;
    out->physical_revision = sim->physical_revision;
    out->door = sim->door;
    out->item = sim->item;
    out->supply_mv = 5000u;
    out->flags = sim_continuity_flags(sim);
    out->dropped_events = sim->dropped_events;
    out->first_available = sim->first_available;
    out->last_committed = sim->last_committed;
    out->ack_persisted = sim->ack_persisted;
    if (sim->has_active) {
        out->active_operation_id = sim->active_intent.operation_id;
        out->active_command_id = sim->active_intent.command_id;
        out->active_command_state = sim->command_state;
        out->active_session_id = sim->active_intent.session_id;
        out->active_action_no = sim->active_intent.action_no;
    }
    out->action_watermark = sim->action_watermark;
    out->device_mode = sim->device_mode;
    out->journal_state = sim->journal_state;
    return LOCKER_WIRE_RESULT_OK;
}

typedef struct sim_fetch_page {
    locker_wire_event96_t events[4];
    uint8_t count;
    bool has_more;
    bool gap;
    locker_wire_cursor_t next_after;
} sim_fetch_page_t;

static bool sim_fetch_load_page(locker_sim_t *sim, locker_wire_cursor_t after, uint8_t max_count,
                                sim_fetch_page_t *page)
{
    sqlite3_stmt *stmt = NULL;
    char after_boot[21];
    char after_seq[21];
    bool ok = true;
    int step = SQLITE_DONE;

    memset(page, 0, sizeof(*page));
    page->next_after = after;
    if (sim->db == NULL) {
        return false;
    }
    if (sqlite3_prepare_v2(sim->db,
                           "SELECT boot_id,event_seq,mono_ms,event_type,reason,operation_id,"
                           "command_id,door,item,flags,supply_mv,physical_revision,detail,"
                           "card_uid_len,card_uid FROM sim_events "
                           "WHERE (boot_id > ?1) OR (boot_id = ?1 AND event_seq > ?2) "
                           "ORDER BY boot_id,event_seq LIMIT ?3",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    sim_u64_text(after.boot_id, after_boot);
    sim_u64_text(after.event_seq, after_seq);
    ok = sqlite3_bind_text(stmt, 1, after_boot, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_text(stmt, 2, after_seq, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_int(stmt, 3, (int)max_count) == SQLITE_OK;
    while (ok && page->count < max_count && (step = sqlite3_step(stmt)) == SQLITE_ROW) {
        locker_wire_event96_t *record = &page->events[page->count];
        const void *operation = sqlite3_column_blob(stmt, 5);
        const void *command = sqlite3_column_blob(stmt, 6);
        const void *card = sqlite3_column_blob(stmt, 14);
        uint64_t supply_mv = 0u;

        memset(record, 0, sizeof(*record));
        ok = sim_u64_from_text((const char *)sqlite3_column_text(stmt, 0), &record->boot_id) &&
             sim_u64_from_text((const char *)sqlite3_column_text(stmt, 1), &record->event_seq) &&
             sim_u64_from_text((const char *)sqlite3_column_text(stmt, 2), &record->mono_ms) &&
             sim_u64_from_text((const char *)sqlite3_column_text(stmt, 10), &supply_mv) &&
             sim_u64_from_text((const char *)sqlite3_column_text(stmt, 11),
                               &record->physical_revision) &&
             sim_u64_from_text((const char *)sqlite3_column_text(stmt, 12), &record->detail);
        if (sqlite3_column_bytes(stmt, 5) != 16 || sqlite3_column_bytes(stmt, 6) != 16 ||
            sqlite3_column_bytes(stmt, 14) != 10 || supply_mv > UINT32_MAX ||
            sqlite3_column_int64(stmt, 3) < LOCKER_WIRE_DEVICE_EVENT_TYPE_BOOT ||
            sqlite3_column_int64(stmt, 3) > LOCKER_WIRE_DEVICE_EVENT_TYPE_UI_RESTART_REQUEST ||
            sqlite3_column_int64(stmt, 4) < 0 || sqlite3_column_int64(stmt, 4) > UINT16_MAX ||
            sqlite3_column_int64(stmt, 7) < LOCKER_WIRE_DOOR_STATE_UNKNOWN ||
            sqlite3_column_int64(stmt, 7) > LOCKER_WIRE_DOOR_STATE_OPEN ||
            sqlite3_column_int64(stmt, 8) < LOCKER_WIRE_ITEM_STATE_UNKNOWN ||
            sqlite3_column_int64(stmt, 8) > LOCKER_WIRE_ITEM_STATE_PRESENT ||
            sqlite3_column_int64(stmt, 9) < 0 || sqlite3_column_int64(stmt, 9) > UINT16_MAX ||
            (sqlite3_column_int(stmt, 13) != 0 && sqlite3_column_int(stmt, 13) != 4 &&
             sqlite3_column_int(stmt, 13) != 7 && sqlite3_column_int(stmt, 13) != 10)) {
            ok = false;
            break;
        }
        record->supply_mv = (uint32_t)supply_mv;
        record->event_type = (uint16_t)sqlite3_column_int(stmt, 3);
        record->reason = (uint16_t)sqlite3_column_int(stmt, 4);
        record->door = (uint8_t)sqlite3_column_int(stmt, 7);
        record->item = (uint8_t)sqlite3_column_int(stmt, 8);
        record->flags = (uint16_t)sqlite3_column_int(stmt, 9);
        record->card_uid_len = (uint8_t)sqlite3_column_int(stmt, 13);
        if (operation != NULL) {
            memcpy(record->operation_id.bytes, operation, 16u);
        }
        if (command != NULL) {
            memcpy(record->command_id.bytes, command, 16u);
        }
        if (card != NULL) {
            memcpy(record->card_uid, card, 10u);
        }
        page->count += 1u;
        page->next_after = (locker_wire_cursor_t){record->boot_id, record->event_seq};
    }
    (void)sqlite3_finalize(stmt);
    if (!ok || (step != SQLITE_DONE && step != SQLITE_ROW)) {
        return false;
    }
    page->has_more = sim_cursor_compare(page->next_after, sim->last_committed) < 0;
    return true;
}

static uint16_t sim_handle_fetch_events(locker_sim_t *sim,
                                        const locker_wire_fetch_events_request_t *request,
                                        sim_reply_context_t *reply)
{
    locker_wire_fetch_events_response_t *out = &reply->response.fetch_events;
    sim_fetch_page_t page;
    size_t index;

    if (!locker_uuid_equal(&request->journal_epoch, &sim->journal_epoch)) {
        return LOCKER_WIRE_RESULT_JOURNAL_CHANGED;
    }
    if (request->max_count < 1u || request->max_count > 4u) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (!sim_cursor_structure_ok(request->after)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (sim_cursor_compare(request->after, sim->last_committed) > 0) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (!sim_fetch_load_page(sim, request->after, request->max_count, &page)) {
        return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
    }
    out->journal_epoch = sim->journal_epoch;
    out->first_available = sim->first_available;
    out->last_committed = sim->last_committed;
    out->next_after = page.next_after;
    out->event_count = page.count;
    out->has_more = page.has_more ? 1u : 0u;
    /* uart-v1.md:366-372: a page whose "after" was recycled, or a range with a
     * known hole, reports gap_before_page=1 together with first_available and the
     * records that are still retained; it never claims a complete history.
     *
     * A latched continuity fault IS a known hole in the range, whether or not the
     * loss also left an in-band CONTINUITY_LOST record: the specification grants
     * no exemption for a marked hole, so every page served while the latch is set
     * reports the gap. Only a pristine journal (latch clear and "after" not yet
     * recycled) reports 0. */
    out->gap_before_page =
        ((!sim->continuity_ok || (sim->first_available.boot_id != 0u &&
          sim_cursor_compare(request->after, sim->first_available) < 0 &&
          !(request->after.boot_id == 0u && request->after.event_seq == 0u)))
            ? 1u
            : 0u);
    if (sim_cursor_compare(request->after, sim->fetched_through) <= 0 &&
        sim_cursor_compare(page.next_after, sim->fetched_through) > 0) {
        sim->fetched_through = page.next_after;
    }
    for (index = 0u; index < page.count; index += 1u) {
        out->events[index] = page.events[index];
    }
    return LOCKER_WIRE_RESULT_OK;
}

static uint16_t sim_handle_ack_persisted(locker_sim_t *sim,
                                         const locker_wire_ack_persisted_request_t *request,
                                         sim_reply_context_t *reply)
{
    locker_wire_ack_persisted_response_t *out = &reply->response.ack_persisted;
    char text[42];

    if (!locker_uuid_equal(&request->journal_epoch, &sim->journal_epoch)) {
        return LOCKER_WIRE_RESULT_JOURNAL_CHANGED;
    }
    if (!sim_cursor_structure_ok(request->through)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (sim_cursor_compare(request->through, sim->ack_persisted) < 0) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (sim_cursor_compare(request->through, sim->last_committed) > 0) {
        return LOCKER_WIRE_RESULT_CURSOR_GAP;
    }
    if (!sim_cursor_structure_ok(request->through)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (!sim->continuity_ok || sim_cursor_compare(request->through, sim->fetched_through) > 0) {
        return LOCKER_WIRE_RESULT_CURSOR_GAP;
    }
    sim_cursor_text(request->through, text);
    if (!sim_journal_begin(sim) || !sim_meta_set(sim, SIM_META_ACK, text) ||
        !sim_journal_commit(sim)) {
        sim_journal_rollback(sim);
        sim_mark_fault(sim);
        return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
    }
    sim->ack_persisted = request->through;
    out->ack_persisted = sim->ack_persisted;
    return LOCKER_WIRE_RESULT_OK;
}

static bool sim_health_fresh(const locker_sim_t *sim, uint64_t now_ms)
{
    return sim->health_seen && sim->core_progress != 0u &&
           sim->core_observed_age_ms <= SIM_HEALTH_TIMEOUT_MS && now_ms >= sim->last_health_ms &&
           now_ms - sim->last_health_ms <= SIM_HEALTH_TIMEOUT_MS - sim->core_observed_age_ms;
}

static uint16_t sim_handle_resume_ready(locker_sim_t *sim,
                                        const locker_wire_resume_ready_request_t *request,
                                        sim_reply_context_t *reply)
{
    locker_wire_resume_ready_response_t *out = &reply->response.resume_ready;

    if (!locker_uuid_equal(&request->journal_epoch, &sim->journal_epoch)) {
        return LOCKER_WIRE_RESULT_JOURNAL_CHANGED;
    }
    if (request->expected_boot_id != sim->boot_id) {
        return LOCKER_WIRE_RESULT_BAD_SESSION;
    }
    if (request->expected_physical_revision != sim->physical_revision) {
        return LOCKER_WIRE_RESULT_PHYSICAL_CHANGED;
    }
    if (!sim_cursor_structure_ok(request->persisted_through)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (sim_cursor_compare(request->persisted_through, sim->ack_persisted) > 0) {
        return LOCKER_WIRE_RESULT_CURSOR_GAP;
    }
    if (sim->continuity_ok &&
        (!sim->snapshot_seen ||
         sim_cursor_compare(request->persisted_through, sim->recovery_boundary) < 0 ||
         sim_cursor_compare(sim->fetched_through, sim->recovery_boundary) < 0)) {
        return LOCKER_WIRE_RESULT_CURSOR_GAP;
    }
    if (sim->has_active || sim->pulse_active ||
        sim->journal_state != LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY ||
        sim->door != LOCKER_WIRE_DOOR_STATE_CLOSED || sim->item == LOCKER_WIRE_ITEM_STATE_UNKNOWN ||
        !sim_health_fresh(sim, sim_now_ms(sim))) {
        return LOCKER_WIRE_RESULT_NOT_READY;
    }
    /* RESUME_READY never clears HISTORY_INCOMPLETE (uart-v1.md:50). */
    sim->device_mode = sim->continuity_ok ? LOCKER_WIRE_WIRE_DEVICE_MODE_READY
                                          : LOCKER_WIRE_WIRE_DEVICE_MODE_DEGRADED;
    out->physical_revision = sim->physical_revision;
    out->device_mode = sim->device_mode;
    return LOCKER_WIRE_RESULT_OK;
}

static uint16_t sim_handle_disarm(locker_sim_t *sim, sim_reply_context_t *reply)
{
    locker_wire_disarm_response_t *out = &reply->response.disarm;

    sim_revoke_actions(sim);
    if (!sim->continuity_ok && sim->device_mode != LOCKER_WIRE_WIRE_DEVICE_MODE_DEGRADED) {
        sim->device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_DEGRADED;
    }
    out->device_mode = sim->device_mode;
    out->physical_revision = sim->physical_revision;
    return LOCKER_WIRE_RESULT_OK;
}

/* Every command transition and its event share one transaction. The live pulse
 * counter changes only after that transaction commits. */
typedef struct sim_command_transition {
    locker_wire_event96_t event;
    uint8_t state;
    uint16_t reason;
    bool release;
} sim_command_transition_t;

static void sim_make_command_event(locker_sim_t *sim, locker_wire_event96_t *record,
                                    uint16_t type, uint64_t detail)
{
    memset(record, 0, sizeof(*record));
    record->boot_id = sim->boot_id;
    record->event_seq = sim->event_seq + 1u;
    record->mono_ms = sim_now_ms(sim);
    record->event_type = type;
    record->door = sim->door;
    record->item = sim->item;
    record->flags = sim_continuity_flags(sim);
    record->supply_mv = 5000u;
    record->physical_revision = sim->physical_revision;
    record->detail = detail;
    record->operation_id = sim->active_intent.operation_id;
    record->command_id = sim->active_intent.command_id;
}

static void sim_event_committed(locker_sim_t *sim, const locker_wire_event96_t *event)
{
    sim->event_seq = event->event_seq;
    sim->last_committed = (locker_wire_cursor_t){event->boot_id, event->event_seq};
    if (sim->first_available.boot_id == 0u) {
        sim->first_available = sim->last_committed;
    }
    sim_queue_push(sim, event);
}

static bool sim_transition_body(locker_sim_t *sim, void *context)
{
    sim_command_transition_t *transition = (sim_command_transition_t *)context;

    return sim_journal_update_command_state(sim, sim->active_intent.action_no,
                                             transition->state, transition->reason) &&
           sim_journal_store_event(sim, &transition->event) &&
           sim_meta_set_u64(sim, SIM_META_EVENT_SEQ, transition->event.event_seq) &&
           (!transition->release || sim_meta_set(sim, "active_command", ""));
}

static bool sim_transition(locker_sim_t *sim, uint8_t state, uint16_t event_type,
                            uint16_t reason, uint64_t detail, bool release)
{
    sim_command_transition_t transition;

    if (sim->event_seq == UINT64_MAX) {
        sim_mark_fault(sim);
        return false;
    }
    memset(&transition, 0, sizeof(transition));
    sim_make_command_event(sim, &transition.event, event_type, detail);
    transition.event.reason = reason;
    if (state == LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED) {
        transition.event.flags |= LOCKER_WIRE_FLAG_PULSE_ACTIVE;
    } else {
        transition.event.flags &= (uint16_t)~LOCKER_WIRE_FLAG_PULSE_ACTIVE;
    }
    transition.state = state;
    transition.reason = reason;
    transition.release = release;
    if (!sim_run_txn(sim, sim_transition_body, &transition)) {
        return false;
    }
    if (state == LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED) {
        /* Do not publish until the final, post-COMMIT driver guard. */
        sim->pulse_checkpoint = transition.event;
        sim->event_seq = transition.event.event_seq;
        sim->last_committed = (locker_wire_cursor_t){transition.event.boot_id, transition.event.event_seq};
    } else {
        sim_event_committed(sim, &transition.event);
    }
    sim->command_state = state;
    return true;
}

static bool sim_cancel_unstarted_body(locker_sim_t *sim, void *context)
{
    sqlite3_stmt *stmt = NULL;
    char boot[21];
    char seq[21];
    char detail[21];
    bool ok;

    (void)context;
    sim_u64_text(sim->pulse_checkpoint.boot_id, boot);
    sim_u64_text(sim->pulse_checkpoint.event_seq, seq);
    /* uart-v1.md:345 keeps detail=action_no on COMMAND_ACCEPTED even when the
     * pulse never starts, so the rewritten checkpoint matches the original. */
    sim_u64_text(sim->active_intent.action_no, detail);
    if (sqlite3_prepare_v2(sim->db,
        "UPDATE sim_events SET event_type=?,reason=?,flags=?,detail=? WHERE boot_id=? AND event_seq=?",
        -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    ok = sqlite3_bind_int(stmt, 1, LOCKER_WIRE_DEVICE_EVENT_TYPE_COMMAND_ACCEPTED) == SQLITE_OK &&
         sqlite3_bind_int(stmt, 2, LOCKER_WIRE_RESULT_BAD_PRECONDITION) == SQLITE_OK &&
         sqlite3_bind_int(stmt, 3, (int)(sim->pulse_checkpoint.flags &
                                       (uint16_t)~LOCKER_WIRE_FLAG_PULSE_ACTIVE)) == SQLITE_OK &&
         sqlite3_bind_text(stmt, 4, detail, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_text(stmt, 5, boot, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_bind_text(stmt, 6, seq, -1, SQLITE_STATIC) == SQLITE_OK &&
         sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(sim->db) == 1;
    (void)sqlite3_finalize(stmt);
    return ok && sim_journal_update_command_state(sim, sim->active_intent.action_no,
                 LOCKER_WIRE_COMMAND_STATE_REJECTED, LOCKER_WIRE_RESULT_BAD_PRECONDITION);
}

/* Once a physical simulated pulse has ended, journal failure cannot keep the
 * driver active. Persist a conservative loss marker independently of the test
 * fault switch; an actual SQLite failure still leaves the active checkpoint for
 * restart reconciliation and never authorizes a replay. */
static bool sim_latch_pulse_uncertainty(locker_sim_t *sim)
{
    uint64_t dropped = sim->dropped_events;
    bool ok;

    if (dropped != UINT64_MAX) {
        dropped += 1u;
    }
    sim->continuity_ok = false;
    sim->dropped_events = dropped;
    sim->command_state = LOCKER_WIRE_COMMAND_STATE_UNKNOWN_RESULT;
    ok = sim_exec(sim->db, "BEGIN IMMEDIATE") &&
         sim_meta_set_u64(sim, SIM_META_DROPPED, dropped) &&
         sim_meta_set(sim, SIM_META_CONTINUITY, "0") &&
         sim_journal_update_command_state(sim, sim->active_intent.action_no,
                                          LOCKER_WIRE_COMMAND_STATE_UNKNOWN_RESULT, 0u) &&
         sim_exec(sim->db, "COMMIT");
    if (!ok) {
        sim_journal_rollback(sim);
    }
    sim_mark_fault(sim);
    return ok;
}

static bool sim_finish_pulse(locker_sim_t *sim, bool closed_transition)
{
    uint64_t now_ms;

    if (!sim->pulse_active) {
        return true;
    }
    now_ms = sim_now_ms(sim);
    if (!closed_transition && (now_ms < sim->pulse_started_ms ||
        now_ms - sim->pulse_started_ms < SIM_PULSE_MS)) {
        return true;
    }
    /* The simulated timer shuts down before any journal work. */
    sim->pulse_active = false;
    if (!sim_transition(sim, LOCKER_WIRE_COMMAND_STATE_PULSE_FINISHED,
                         LOCKER_WIRE_DEVICE_EVENT_TYPE_PULSE_FINISHED, 0u,
                         now_ms >= sim->pulse_started_ms ? now_ms - sim->pulse_started_ms : 0u,
                         false)) {
        sim->pulse_active = false;
        (void)sim_latch_pulse_uncertainty(sim);
        return false;
    }
    sim->pulse_active = false;
    return true;
}

typedef struct sim_accept_intent {
    locker_unlock_intent_t intent;
    locker_wire_event96_t event;
} sim_accept_intent_t;

static bool sim_accept_body(locker_sim_t *sim, void *context)
{
    sim_accept_intent_t *accept = (sim_accept_intent_t *)context;
    char command_hex[33];

    sim_uuid_hex(&accept->intent.command_id, command_hex);
    return sim_journal_store_command(sim, &accept->intent, sim->boot_id, sim->physical_revision,
                                      LOCKER_WIRE_COMMAND_STATE_ACCEPTED, 0u) &&
           sim_meta_set_u64(sim, SIM_META_ACTION_WATERMARK, accept->intent.action_no) &&
           sim_meta_set(sim, "active_command", command_hex) &&
           sim_journal_store_event(sim, &accept->event) &&
           sim_meta_set_u64(sim, SIM_META_EVENT_SEQ, accept->event.event_seq);
}

static uint16_t sim_handle_execute_unlock(locker_sim_t *sim,
                                          const locker_wire_execute_unlock_request_t *request,
                                          sim_reply_context_t *reply)
{
    locker_wire_execute_unlock_response_t *out = &reply->response.execute_unlock;
    sim_command_row_t existing;
    uint64_t now_ms = sim_now_ms(sim);
    uint64_t deadline_ms;
    sim_accept_intent_t accept;

    out->operation_id = request->operation_id;
    out->command_id = request->command_id;
    if (!locker_uuid_equal(&request->journal_epoch, &sim->journal_epoch)) {
        return LOCKER_WIRE_RESULT_JOURNAL_CHANGED;
    }
    if (request->slot_id != 1u || request->action_no == 0u ||
        (request->kind != LOCKER_WIRE_OPERATION_KIND_BORROW &&
         request->kind != LOCKER_WIRE_OPERATION_KIND_RETURN)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (!sim_journal_load_command(sim, request->action_no, &existing)) {
        return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
    }
    if (existing.found) {
        /* A retry returns the original record even after its deadline. Every
         * immutable intent parameter must agree; no new association is built. */
        if (!locker_uuid_equal(&existing.session_id, &request->session_id) ||
            !locker_uuid_equal(&existing.operation_id, &request->operation_id) ||
            !locker_uuid_equal(&existing.command_id, &request->command_id) ||
            existing.not_after_mcu_ms != request->not_after_mcu_ms ||
            existing.boot_id != request->expected_boot_id ||
            existing.physical_revision != request->expected_physical_revision ||
            existing.kind != request->kind || existing.slot_id != request->slot_id) {
            return LOCKER_WIRE_RESULT_REPLAY_BLOCKED;
        }
        out->operation_id = existing.operation_id;
        out->command_id = existing.command_id;
        out->command_state = existing.state;
        out->action_no = existing.action_no;
        out->command_session_id = existing.session_id;
        out->command_boot_id = existing.boot_id;
        out->physical_revision = existing.physical_revision;
        out->reason = existing.reason;
        return LOCKER_WIRE_RESULT_OK;
    }
    /* command_id is immutable across all sessions, while action_no starts at
     * one for each new session. Historical rows remain queryable. */
    if (!sim_journal_load_command_by_id(sim, &request->command_id, &existing)) {
        return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
    }
    if (existing.found) {
        return LOCKER_WIRE_RESULT_REPLAY_BLOCKED;
    }
    if (request->expected_boot_id != sim->boot_id) {
        return LOCKER_WIRE_RESULT_BAD_SESSION;
    }
    if (request->expected_physical_revision != sim->physical_revision) {
        return LOCKER_WIRE_RESULT_PHYSICAL_CHANGED;
    }
    if (request->not_after_mcu_ms <= now_ms ||
        request->not_after_mcu_ms - now_ms > SIM_NOT_AFTER_HORIZON_MS ||
        UINT64_MAX - now_ms < SIM_PREPARE_DEADLINE_MS) {
        return LOCKER_WIRE_RESULT_BAD_PRECONDITION;
    }
    deadline_ms = request->not_after_mcu_ms < now_ms + SIM_PREPARE_DEADLINE_MS
                      ? request->not_after_mcu_ms : now_ms + SIM_PREPARE_DEADLINE_MS;
    if (sim->action_watermark == UINT64_MAX || request->action_no != sim->action_watermark + 1u) {
        return request->action_no <= sim->action_watermark ? LOCKER_WIRE_RESULT_REPLAY_BLOCKED
                                                          : LOCKER_WIRE_RESULT_ACTION_GAP;
    }
    if (sim->fail_journal || sim->journal_state != LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY) {
        return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
    }
    if (sim->has_active) {
        return LOCKER_WIRE_RESULT_BUSY;
    }
    if (sim->door != LOCKER_WIRE_DOOR_STATE_CLOSED ||
        (request->kind == LOCKER_WIRE_OPERATION_KIND_BORROW &&
         sim->item != LOCKER_WIRE_ITEM_STATE_PRESENT) ||
        (request->kind == LOCKER_WIRE_OPERATION_KIND_RETURN &&
         sim->item != LOCKER_WIRE_ITEM_STATE_ABSENT)) {
        return LOCKER_WIRE_RESULT_BAD_PRECONDITION;
    }
    if (sim->device_mode != LOCKER_WIRE_WIRE_DEVICE_MODE_READY || !sim->continuity_ok ||
        !sim_health_fresh(sim, now_ms)) {
        return LOCKER_WIRE_RESULT_NOT_READY;
    }
    if (sim->reject_next) {
        sim->reject_next = false;
        return LOCKER_WIRE_RESULT_BAD_PRECONDITION;
    }
    if (sim->event_seq >= UINT64_MAX - 1u) {
        sim_mark_fault(sim);
        return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
    }
    memset(&accept, 0, sizeof(accept));
    accept.intent.operation_id = request->operation_id;
    accept.intent.command_id = request->command_id;
    accept.intent.session_id = request->session_id;
    accept.intent.journal_epoch = request->journal_epoch;
    accept.intent.expected_boot_id = request->expected_boot_id;
    accept.intent.expected_physical_revision = request->expected_physical_revision;
    accept.intent.action_no = request->action_no;
    accept.intent.not_after_mcu_ms = request->not_after_mcu_ms;
    accept.intent.slot_id = request->slot_id;
    accept.intent.kind = (locker_operation_kind_t)request->kind;
    sim_make_command_event(sim, &accept.event, LOCKER_WIRE_DEVICE_EVENT_TYPE_COMMAND_ACCEPTED,
                             request->action_no);
    accept.event.operation_id = request->operation_id;
    accept.event.command_id = request->command_id;
    if (!sim_run_txn(sim, sim_accept_body, &accept)) {
        return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
    }
    sim->action_watermark = request->action_no;
    sim->has_active = true;
    sim->active_intent = accept.intent;
    sim->command_state = LOCKER_WIRE_COMMAND_STATE_ACCEPTED;
    sim_event_committed(sim, &accept.event);

    /* SQLite preparation may have crossed either deadline. Re-sample directly
     * before the simulated driver and retain the accepted watermark on expiry. */
    now_ms = sim_now_ms(sim);
    if (now_ms >= deadline_ms || !sim_session_matches(sim, &request->session_id) ||
        sim->device_mode != LOCKER_WIRE_WIRE_DEVICE_MODE_READY || !sim_health_fresh(sim, now_ms) ||
        sim->door != LOCKER_WIRE_DOOR_STATE_CLOSED ||
        sim->physical_revision != request->expected_physical_revision) {
        if (!sim_transition(sim, LOCKER_WIRE_COMMAND_STATE_REJECTED,
                             LOCKER_WIRE_DEVICE_EVENT_TYPE_COMMAND_ACCEPTED,
                             LOCKER_WIRE_RESULT_BAD_PRECONDITION, request->action_no, false)) {
            return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
        }
    } else if (!sim->suppress_pulse) {
        if (!sim_transition(sim, LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED,
                             LOCKER_WIRE_DEVICE_EVENT_TYPE_PULSE_STARTED, 0u, SIM_PULSE_MS, false)) {
            return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
        }
        now_ms = sim_now_ms(sim);
        if (now_ms >= deadline_ms || !sim_health_fresh(sim, now_ms) ||
            !sim_session_matches(sim, &request->session_id) ||
            sim->device_mode != LOCKER_WIRE_WIRE_DEVICE_MODE_READY ||
            sim->door != LOCKER_WIRE_DOOR_STATE_CLOSED ||
            sim->physical_revision != request->expected_physical_revision) {
            if (!sim_run_txn(sim, sim_cancel_unstarted_body, NULL)) {
                return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
            }
            sim->command_state = LOCKER_WIRE_COMMAND_STATE_REJECTED;
            sim->pulse_checkpoint.event_type = LOCKER_WIRE_DEVICE_EVENT_TYPE_COMMAND_ACCEPTED;
            sim->pulse_checkpoint.reason = LOCKER_WIRE_RESULT_BAD_PRECONDITION;
            sim->pulse_checkpoint.flags &= (uint16_t)~LOCKER_WIRE_FLAG_PULSE_ACTIVE;
            sim->pulse_checkpoint.detail = request->action_no;
        } else {
            sim->pulse_active = true;
            sim->pulse_started_ms = now_ms;
        }
        sim_queue_push(sim, &sim->pulse_checkpoint);
    }
    out->command_state = sim->command_state;
    out->action_no = request->action_no;
    out->command_session_id = request->session_id;
    out->command_boot_id = request->expected_boot_id;
    out->physical_revision = request->expected_physical_revision;
    out->reason = sim->command_state == LOCKER_WIRE_COMMAND_STATE_REJECTED
                      ? LOCKER_WIRE_RESULT_BAD_PRECONDITION : 0u;
    return LOCKER_WIRE_RESULT_OK;
}

static uint16_t sim_handle_query_command(locker_sim_t *sim,
                                         const locker_wire_query_command_request_t *request,
                                         sim_reply_context_t *reply)
{
    locker_wire_query_command_response_t *out = &reply->response.query_command;
    sim_command_row_t row;

    out->command_id = request->command_id;
    if (!locker_uuid_equal(&request->journal_epoch, &sim->journal_epoch)) {
        return LOCKER_WIRE_RESULT_JOURNAL_CHANGED;
    }
    if (sim_uuid_nil(&request->command_id)) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (!sim_journal_load_command_by_id(sim, &request->command_id, &row)) {
        return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
    }
    if (!row.found) {
        /* uart-v1.md:390: "not found" must never be reported as "not executed". */
        return LOCKER_WIRE_RESULT_UNKNOWN_RESULT;
    }
    out->operation_id = row.operation_id;
    out->command_state = row.state;
    out->action_no = row.action_no;
    out->command_session_id = row.session_id;
    out->command_boot_id = row.boot_id;
    out->physical_revision = row.physical_revision;
    out->reason = row.reason;
    return LOCKER_WIRE_RESULT_OK;
}

static uint16_t sim_handle_host_health(locker_sim_t *sim,
                                       const locker_wire_host_health_request_t *request,
                                       sim_reply_context_t *reply)
{
    locker_wire_host_health_response_t *out = &reply->response.host_health;

    if (!locker_uuid_equal(&request->host_instance_id, &sim->host_instance_id)) {
        return LOCKER_WIRE_RESULT_BAD_SESSION;
    }
    if (request->core_progress == 0u || request->core_observed_age_ms > SIM_HEALTH_TIMEOUT_MS ||
        (sim->health_seen && request->core_progress < sim->core_progress)) {
        /* An explicit unknown/stale Core observation revokes the previous
         * freshness proof; replaying its counter cannot reinstate it. */
        sim->core_observed_age_ms = UINT32_MAX;
        return LOCKER_WIRE_RESULT_NOT_READY;
    }
    if (!sim->health_seen || request->core_progress > sim->core_progress) {
        sim->last_health_ms = sim_now_ms(sim);
        sim->core_progress = request->core_progress;
        sim->core_observed_age_ms = request->core_observed_age_ms;
        sim->health_seen = true;
    }
    out->received_mono_ms = sim->last_health_ms;
    out->device_mode = sim->device_mode;
    return LOCKER_WIRE_RESULT_OK;
}

static uint16_t sim_handle_time_anchor(locker_sim_t *sim,
                                       const locker_wire_time_anchor_request_t *request,
                                       sim_reply_context_t *reply)
{
    locker_wire_time_anchor_response_t *out = &reply->response.time_anchor;

    if (request->anchor_id == 0u) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    out->anchor_id = request->anchor_id;
    out->boot_id = sim->boot_id;
    out->mcu_receive_mono_ms = sim_now_ms(sim);
    out->mcu_send_mono_ms = sim_now_ms(sim);
    return LOCKER_WIRE_RESULT_OK;
}

static uint16_t sim_handle_release_context(locker_sim_t *sim,
                                           const locker_wire_release_context_request_t *request,
                                           sim_reply_context_t *reply)
{
    locker_wire_release_context_response_t *out = &reply->response.release_context;

    if (!locker_uuid_equal(&request->journal_epoch, &sim->journal_epoch)) {
        return LOCKER_WIRE_RESULT_JOURNAL_CHANGED;
    }
    if (request->expected_boot_id != sim->boot_id) {
        return LOCKER_WIRE_RESULT_BAD_SESSION;
    }
    if (request->expected_physical_revision != sim->physical_revision) {
        return LOCKER_WIRE_RESULT_PHYSICAL_CHANGED;
    }
    if (sim->door != LOCKER_WIRE_DOOR_STATE_CLOSED) {
        return LOCKER_WIRE_RESULT_BAD_PRECONDITION;
    }
    if (sim->pulse_active) {
        return LOCKER_WIRE_RESULT_BAD_PRECONDITION;
    }
    if (sim->journal_state != LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY || sim->fail_journal) {
        return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
    }
    out->physical_revision = sim->physical_revision;
    if (!sim->has_active) {
        /* uart-v1.md:400-402: no active context and safe conditions -> idempotent. */
        out->already_released = 1u;
        return LOCKER_WIRE_RESULT_OK;
    }
    if (!locker_uuid_equal(&request->operation_id, &sim->active_intent.operation_id) ||
        !locker_uuid_equal(&request->command_id, &sim->active_intent.command_id)) {
        return LOCKER_WIRE_RESULT_BAD_PRECONDITION;
    }
    /* uart-v1.md:46 and :352: detail is not applicable to CONTEXT_RELEASED and
     * must be 0. The released context is identified by operation_id/command_id,
     * which sim_make_command_event copies from the active intent. */
    if (!sim_transition(sim, sim->command_state, LOCKER_WIRE_DEVICE_EVENT_TYPE_CONTEXT_RELEASED,
                         0u, 0u, true)) {
        return LOCKER_WIRE_RESULT_LOG_UNAVAILABLE;
    }
    sim->has_active = false;
    memset(&sim->active_intent, 0, sizeof(sim->active_intent));
    sim->command_state = LOCKER_WIRE_COMMAND_STATE_NONE;
    out->already_released = 0u;
    /* The action watermark, committed events, HISTORY_INCOMPLETE and
     * dropped_events are explicitly preserved. */
    return LOCKER_WIRE_RESULT_OK;
}

/* ------------------------------------------------------------------ */
/* frame dispatch                                                      */
/* ------------------------------------------------------------------ */

static uint16_t sim_dispatch(locker_sim_t *sim, const locker_wire_frame_t *frame,
                             locker_wire_request_union_t *request, sim_reply_context_t *reply)
{
    bool session_scoped;

    if (frame->request_no == 0u) {
        return LOCKER_WIRE_RESULT_BAD_PAYLOAD;
    }
    if (frame->version != LOCKER_WIRE_VERSION) {
        return LOCKER_WIRE_RESULT_BAD_VERSION;
    }
    if (!locker_wire_request_type_valid(frame->type)) {
        return LOCKER_WIRE_RESULT_UNSUPPORTED;
    }
    /* Decode failures keep their own wire result: a well-framed request with a
     * recognised type but invalid fields is BAD_PAYLOAD, while an unsupported
     * protocol version stays BAD_VERSION (uart-v1.md:21). */
    {
        uint16_t decode_result = locker_wire_decode_request(frame, request);

        if (decode_result != LOCKER_WIRE_RESULT_OK) {
            return decode_result;
        }
    }
    (void)sim_finish_pulse(sim, false);
    session_scoped = frame->type != 1u;
    if (session_scoped && !sim_session_matches(sim, &request->hello.session_id)) {
        /* The reply echoes the session the request carried (uart-v1.md:27-31). */
        sim_reply_init(reply, frame->type, frame->request_no, &request->hello.session_id);
        return LOCKER_WIRE_RESULT_BAD_SESSION;
    }
    sim_reply_init(reply, frame->type, frame->request_no, &sim->session_id);
    if (frame->type == 1u) {
        return sim_handle_hello(sim, &request->hello, reply);
    }
    if (frame->type == 2u) {
        return sim_handle_get_snapshot(sim, reply);
    }
    if (frame->type == 3u) {
        return sim_handle_fetch_events(sim, &request->fetch_events, reply);
    }
    if (frame->type == 4u) {
        return sim_handle_ack_persisted(sim, &request->ack_persisted, reply);
    }
    if (frame->type == 5u) {
        return sim_handle_resume_ready(sim, &request->resume_ready, reply);
    }
    if (frame->type == 6u) {
        return sim_handle_disarm(sim, reply);
    }
    if (frame->type == 7u) {
        return sim_handle_execute_unlock(sim, &request->execute_unlock, reply);
    }
    if (frame->type == 8u) {
        return sim_handle_query_command(sim, &request->query_command, reply);
    }
    if (frame->type == 9u) {
        return sim_handle_host_health(sim, &request->host_health, reply);
    }
    if (frame->type == 10u) {
        return sim_handle_time_anchor(sim, &request->time_anchor, reply);
    }
    return sim_handle_release_context(sim, &request->release_context, reply);
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

locker_status_t locker_sim_create(locker_sim_t **out, const locker_sim_config_t *config)
{
    locker_sim_t *sim;

    if (out == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    *out = NULL;
    if (config == NULL || config->journal_path[0] == '\0' ||
        !locker_clock_port_is_valid(&config->clock)) {
        return LOCKER_STATUS_INVALID;
    }
    if (strlen(config->journal_path) >= LOCKER_SIM_PATH_SIZE) {
        return LOCKER_STATUS_INVALID;
    }
    sim = (locker_sim_t *)calloc(1u, sizeof(*sim));
    if (sim == NULL) {
        return LOCKER_STATUS_NO_SPACE;
    }
    sim->config = *config;
    sim->created = true;
    sim->device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING;
    sim->journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_UNINITIALIZED;
    sim->door = LOCKER_WIRE_DOOR_STATE_CLOSED;
    sim->item = LOCKER_WIRE_ITEM_STATE_PRESENT;
    sim->continuity_ok = true;
    sim->physical_revision = 1u;
    *out = sim;
    return LOCKER_STATUS_OK;
}

void locker_sim_destroy(locker_sim_t *sim)
{
    if (sim == NULL) {
        return;
    }
    locker_sim_close(sim);
    free(sim);
}

static bool sim_boot_body(locker_sim_t *sim, void *context)
{
    locker_wire_event96_t record;

    (void)context;
    if (!sim_meta_set_u64(sim, SIM_META_BOOT, sim->boot_id)) {
        return false;
    }
    memset(&record, 0, sizeof(record));
    record.boot_id = sim->boot_id;
    record.event_seq = 1u;
    record.mono_ms = sim_now_ms(sim);
    record.event_type = LOCKER_WIRE_DEVICE_EVENT_TYPE_BOOT;
    record.door = sim->door;
    record.item = sim->item;
    record.flags = sim_continuity_flags(sim);
    record.supply_mv = 5000u;
    record.physical_revision = sim->physical_revision;
    /* detail carries the reset reason; 0 is "unspecified" for the simulator. */
    record.detail = 0u;
    if (!sim_journal_store_event(sim, &record)) {
        return false;
    }
    if (!sim_meta_set_u64(sim, SIM_META_EVENT_SEQ, record.event_seq)) {
        return false;
    }
    sim->event_seq = record.event_seq;
    sim->last_committed = (locker_wire_cursor_t){record.boot_id, record.event_seq};
    if (sim->first_available.boot_id == 0u) {
        sim->first_available = sim->last_committed;
    }
    return true;
}

static bool sim_restore_active(locker_sim_t *sim)
{
    char text[64];
    locker_uuid_t command_id;
    sim_command_row_t row;

    sim->has_active = false;
    memset(&sim->active_intent, 0, sizeof(sim->active_intent));
    sim->command_state = LOCKER_WIRE_COMMAND_STATE_NONE;
    if (!sim_meta_get(sim, "active_command", text, sizeof(text))) {
        if (!sim->meta_missing) {
            return false;
        }
        /* A legacy journal with unresolved commands cannot silently lose them. */
        sqlite3_stmt *stmt = NULL;
        int step;
        if (sqlite3_prepare_v2(sim->db,
            "SELECT command_id FROM sim_commands_v2 ORDER BY boot_id DESC,action_no DESC LIMIT 1",
            -1, &stmt, NULL) != SQLITE_OK) {
            return false;
        }
        step = sqlite3_step(stmt);
        if (step == SQLITE_ROW && sqlite3_column_bytes(stmt, 0) == 16) {
            memcpy(command_id.bytes, sqlite3_column_blob(stmt, 0), 16u);
            sim_uuid_hex(&command_id, text);
        } else if (step == SQLITE_DONE) {
            text[0] = '\0';
        } else {
            (void)sqlite3_finalize(stmt);
            return false;
        }
        (void)sqlite3_finalize(stmt);
        if (!sim_meta_set(sim, "active_command", text)) {
            return false;
        }
    }
    if (text[0] == '\0') {
        return true;
    }
    if (!sim_uuid_from_hex(text, &command_id) ||
        !sim_journal_load_command_by_id(sim, &command_id, &row) || !row.found) {
        return false;
    }
    sim->has_active = true;
    sim->active_intent.session_id = row.session_id;
    sim->active_intent.operation_id = row.operation_id;
    sim->active_intent.command_id = row.command_id;
    sim->active_intent.journal_epoch = sim->journal_epoch;
    sim->active_intent.expected_boot_id = row.boot_id;
    sim->active_intent.expected_physical_revision = row.physical_revision;
    sim->active_intent.action_no = row.action_no;
    sim->active_intent.not_after_mcu_ms = row.not_after_mcu_ms;
    sim->active_intent.slot_id = row.slot_id;
    sim->active_intent.kind = (locker_operation_kind_t)row.kind;
    sim->command_state = row.state;
    if (row.state == LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED) {
        if (!sim_latch_pulse_uncertainty(sim)) {
            return false;
        }
    }
    /* Restart never restarts the driver. A previously started pulse remains a
     * recorded uncertainty until Core explicitly reconciles/releases it. */
    return true;
}

locker_status_t locker_sim_open(locker_sim_t *sim)
{
    int rc;

    if (sim == NULL || !sim->created) {
        return LOCKER_STATUS_INVALID;
    }
    if (sim->opened) {
        return LOCKER_STATUS_CONFLICT;
    }
    rc = sqlite3_open_v2(sim->config.journal_path, &sim->db,
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);
    if (rc != SQLITE_OK) {
        if (sim->db != NULL) {
            (void)sqlite3_close(sim->db);
            sim->db = NULL;
        }
        sim_mark_fault(sim);
        return LOCKER_STATUS_IO_ERROR;
    }
    if (!sim_exec(sim->db, "PRAGMA journal_mode=WAL") ||
        !sim_exec(sim->db, "PRAGMA synchronous=FULL") || !sim_exec(sim->db, SIM_SCHEMA_SQL) ||
        !sim_journal_load_meta(sim) || !sim_restore_active(sim)) {
        (void)sqlite3_close(sim->db);
        sim->db = NULL;
        sim_mark_fault(sim);
        return LOCKER_STATUS_IO_ERROR;
    }
    /* A restart invalidates the previous session (uart-v1.md:360). */
    sim->has_session = false;
    sim->hello_complete = false;
    sim_uuid_clear(&sim->session_id);
    sim_uuid_clear(&sim->host_instance_id);
    sim->device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING;
    sim->journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
    sim->journal_fault_latched = false;
    sim->action_watermark = 0u;
    sim->pulse_active = false;
    sim->snapshot_seen = false;
    sim->fetched_through = (locker_wire_cursor_t){0u, 0u};
    sim->health_seen = false;
    sim->queue_head = 0u;
    sim->queue_count = 0u;
    sim->physical_revision = 1u;
    /* Only the brand-new constructor scene is explicitly CLOSED/PRESENT.
     * Reopening a journal has no physical observations and reports UNKNOWN. */
    if (sim->boot_id != 0u) {
        sim->door = LOCKER_WIRE_DOOR_STATE_UNKNOWN;
        sim->item = LOCKER_WIRE_ITEM_STATE_UNKNOWN;
    }

    if (sim->boot_id == UINT64_MAX) {
        (void)sqlite3_close(sim->db);
        sim->db = NULL;
        sim_mark_fault(sim);
        return LOCKER_STATUS_IO_ERROR;
    }
    sim->boot_id += 1u;
    if (!sim_run_txn(sim, sim_boot_body, NULL)) {
        (void)sqlite3_close(sim->db);
        sim->db = NULL;
        sim_mark_fault(sim);
        return LOCKER_STATUS_IO_ERROR;
    }
    if (!sim_journal_recompute_bounds(sim)) {
        (void)sqlite3_close(sim->db);
        sim->db = NULL;
        sim_mark_fault(sim);
        return LOCKER_STATUS_IO_ERROR;
    }
    sim->opened = true;
    return LOCKER_STATUS_OK;
}

void locker_sim_close(locker_sim_t *sim)
{
    if (sim == NULL) {
        return;
    }
    if (sim->db != NULL) {
        (void)sqlite3_close(sim->db);
        sim->db = NULL;
    }
    sim->opened = false;
    sim->has_session = false;
    sim->hello_complete = false;
    sim->queue_head = 0u;
    sim->queue_count = 0u;
}

locker_status_t locker_sim_restart(locker_sim_t *sim)
{
    if (sim == NULL || !sim->created) {
        return LOCKER_STATUS_INVALID;
    }
    locker_sim_close(sim);
    return locker_sim_open(sim);
}

/* ------------------------------------------------------------------ */
/* frame entry                                                         */
/* ------------------------------------------------------------------ */

typedef struct sim_frame_capture {
    bool seen;
    locker_wire_frame_t frame;
} sim_frame_capture_t;

static locker_status_t sim_capture_frame(void *user, const locker_wire_frame_t *frame)
{
    sim_frame_capture_t *capture = (sim_frame_capture_t *)user;

    if (capture->seen) {
        /* handle_frame consumes exactly one frame; a second one means the caller
         * is feeding a stream that must go through locker_wire_stream_t. */
        return LOCKER_STATUS_INVALID;
    }
    capture->frame = *frame;
    capture->seen = true;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_sim_handle_frame(locker_sim_t *sim, const uint8_t *frame, size_t length,
                                        uint8_t *out, size_t out_size, size_t *out_length)
{
    sim_frame_capture_t capture;
    locker_wire_stream_t stream;
    locker_wire_request_union_t request;
    sim_reply_context_t reply;
    locker_uuid_t echo_session;
    uint16_t result;

    if (out_length != NULL) {
        *out_length = 0u;
    }
    if (sim == NULL || !sim->opened || frame == NULL || out == NULL || out_size == 0u ||
        out_length == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    memset(&capture, 0, sizeof(capture));
    locker_wire_stream_init(&stream, &capture, sim_capture_frame);
    if (locker_wire_stream_feed(&stream, frame, length) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_INVALID;
    }
    if (!capture.seen || locker_wire_stream_pending(&stream) != 0u) {
        /* A half frame, a bad CRC or an oversized length is dropped whole. */
        return LOCKER_STATUS_INVALID;
    }
    if (capture.frame.request_no == 0u) {
        return LOCKER_STATUS_INVALID;
    }
    if (!locker_wire_request_type_valid(capture.frame.type)) {
        if (capture.frame.type == 0u || (capture.frame.type & LOCKER_WIRE_RESPONSE_FLAG) != 0u ||
            capture.frame.type == LOCKER_WIRE_DEVICE_EVENT) {
            return LOCKER_STATUS_INVALID;
        }
        sim_echo_session(&capture.frame, &echo_session);
        return locker_wire_encode_error_response(capture.frame.type, capture.frame.request_no,
                    &echo_session, LOCKER_WIRE_RESULT_UNSUPPORTED, out, out_size, out_length);
    }
    memset(&request, 0, sizeof(request));
    /* A frame that is rejected before dispatch (bad header version, bad payload,
     * zero request number) keeps the session the request carried; sim_dispatch
     * replaces it with the live session once the request is accepted. */
    sim_echo_session(&capture.frame, &echo_session);
    sim_reply_init(&reply, capture.frame.type, capture.frame.request_no, &echo_session);
    result = sim_dispatch(sim, &capture.frame, &request, &reply);
    if ((sim->drop_ack && capture.frame.type == 4u) ||
        (sim->drop_execute_ack && capture.frame.type == 7u && result == LOCKER_WIRE_RESULT_OK)) {
        /* Lost-ACK control: the request is applied but its reply is withheld. */
        return LOCKER_STATUS_OK;
    }
    sim_reply_set_result(&reply, result);
    return sim_emit_reply(&reply, out, out_size, out_length);
}

locker_status_t locker_sim_pop_event(locker_sim_t *sim, uint8_t *out, size_t out_size,
                                     size_t *out_length)
{
    sim_queued_event_t *queued;
    locker_status_t status;

    if (out_length != NULL) {
        *out_length = 0u;
    }
    if (sim == NULL || !sim->opened || out == NULL || out_length == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!sim_finish_pulse(sim, false)) {
        return LOCKER_STATUS_IO_ERROR;
    }
    if (sim->queue_count == 0u) {
        return LOCKER_STATUS_NOT_READY;
    }
    queued = &sim->queue[sim->queue_head];
    status = locker_wire_encode_device_event(&queued->session_id, &queued->journal_epoch,
                                             &queued->record, out, out_size, out_length);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    sim->queue_head = (sim->queue_head + 1u) % SIM_DEVICE_EVENT_QUEUE;
    sim->queue_count -= 1u;
    return LOCKER_STATUS_OK;
}

/* ------------------------------------------------------------------ */
/* private control entry                                               */
/* ------------------------------------------------------------------ */

locker_status_t locker_sim_control_set_door(locker_sim_t *sim, uint8_t door)
{
    uint16_t event_type;
    uint8_t previous;

    if (sim == NULL || !sim->opened) {
        return LOCKER_STATUS_NOT_READY;
    }
    if (door != LOCKER_WIRE_DOOR_STATE_UNKNOWN && door != LOCKER_WIRE_DOOR_STATE_CLOSED &&
        door != LOCKER_WIRE_DOOR_STATE_OPEN) {
        return LOCKER_STATUS_INVALID;
    }
    if (sim->door == door) {
        return LOCKER_STATUS_OK;
    }
    if (sim->physical_revision == UINT64_MAX) {
        return LOCKER_STATUS_IO_ERROR;
    }
    previous = sim->door;
    sim->door = door;
    sim->physical_revision += 1u;
    event_type = door == LOCKER_WIRE_DOOR_STATE_UNKNOWN
                     ? LOCKER_WIRE_DEVICE_EVENT_TYPE_HEALTH_CHANGED
                     : (door == LOCKER_WIRE_DOOR_STATE_OPEN
                            ? LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_OPENED
                            : LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_CLOSED);
    if (!sim_emit_event(sim, event_type, 0u, 0u, NULL, NULL, 0u, NULL)) {
        sim->door = previous;
        sim->physical_revision -= 1u;
        return LOCKER_STATUS_IO_ERROR;
    }
    if (!sim_finish_pulse(sim, door == LOCKER_WIRE_DOOR_STATE_CLOSED)) {
        return LOCKER_STATUS_IO_ERROR;
    }
    return LOCKER_STATUS_OK;
}

locker_status_t locker_sim_control_set_item(locker_sim_t *sim, uint8_t item)
{
    uint16_t event_type;
    uint8_t previous;

    if (sim == NULL || !sim->opened) {
        return LOCKER_STATUS_NOT_READY;
    }
    if (item != LOCKER_WIRE_ITEM_STATE_UNKNOWN && item != LOCKER_WIRE_ITEM_STATE_ABSENT &&
        item != LOCKER_WIRE_ITEM_STATE_PRESENT) {
        return LOCKER_STATUS_INVALID;
    }
    if (sim->item == item) {
        return LOCKER_STATUS_OK;
    }
    if (sim->physical_revision == UINT64_MAX) {
        return LOCKER_STATUS_IO_ERROR;
    }
    previous = sim->item;
    sim->item = item;
    sim->physical_revision += 1u;
    event_type = item == LOCKER_WIRE_ITEM_STATE_UNKNOWN
                     ? LOCKER_WIRE_DEVICE_EVENT_TYPE_HEALTH_CHANGED
                     : (item == LOCKER_WIRE_ITEM_STATE_PRESENT
                            ? LOCKER_WIRE_DEVICE_EVENT_TYPE_ITEM_PRESENT
                            : LOCKER_WIRE_DEVICE_EVENT_TYPE_ITEM_REMOVED);
    if (!sim_emit_event(sim, event_type, 0u, 0u, NULL, NULL, 0u, NULL)) {
        sim->item = previous;
        sim->physical_revision -= 1u;
        return LOCKER_STATUS_IO_ERROR;
    }
    return LOCKER_STATUS_OK;
}

locker_status_t locker_sim_control_card(locker_sim_t *sim, const uint8_t *uid, size_t uid_len)
{
    if (sim == NULL || !sim->opened) {
        return LOCKER_STATUS_NOT_READY;
    }
    /* uart-v1.md:35-46: card_uid_len is only 0, 4, 7 or 10. */
    if (uid_len != 0u && uid_len != 4u && uid_len != 7u && uid_len != 10u) {
        return LOCKER_STATUS_INVALID;
    }
    if (uid_len > 0u && uid == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!sim_emit_event(sim, LOCKER_WIRE_DEVICE_EVENT_TYPE_CARD_OBSERVED, 0u, 0u, NULL, NULL,
                        (uint8_t)uid_len, uid)) {
        return LOCKER_STATUS_IO_ERROR;
    }
    return LOCKER_STATUS_OK;
}

locker_status_t locker_sim_control_reject_next(locker_sim_t *sim, bool enabled)
{
    if (sim == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    sim->reject_next = enabled;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_sim_control_drop_ack(locker_sim_t *sim, bool enabled)
{
    if (sim == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    sim->drop_ack = enabled;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_sim_control_drop_execute_ack(locker_sim_t *sim, bool enabled)
{
    if (sim == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    sim->drop_execute_ack = enabled;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_sim_control_fail_next(locker_sim_t *sim, locker_sim_failure_stage_t stage)
{
    if (sim == NULL || (unsigned)stage > (unsigned)LOCKER_SIM_FAILURE_RESULT) {
        return LOCKER_STATUS_INVALID;
    }
    sim->fail_next = stage;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_sim_control_suppress_pulse(locker_sim_t *sim, bool enabled)
{
    if (sim == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    sim->suppress_pulse = enabled;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_sim_control_inject_gap(locker_sim_t *sim, uint64_t dropped)
{
    if (sim == NULL || !sim->opened) {
        return LOCKER_STATUS_NOT_READY;
    }
    if (dropped == 0u) {
        return LOCKER_STATUS_INVALID;
    }
    if (UINT64_MAX - sim->dropped_events < dropped) {
        return LOCKER_STATUS_INVALID;
    }
    if (!sim_journal_begin(sim)) {
        sim_mark_fault(sim);
        return LOCKER_STATUS_IO_ERROR;
    }
    /* HISTORY_INCOMPLETE is a latch: RESUME_READY never clears it. */
    if (!sim_meta_set_u64(sim, SIM_META_DROPPED, sim->dropped_events + dropped) ||
        !sim_meta_set(sim, SIM_META_CONTINUITY, "0") || !sim_journal_commit(sim)) {
        sim_journal_rollback(sim);
        sim_mark_fault(sim);
        return LOCKER_STATUS_IO_ERROR;
    }
    sim->dropped_events += dropped;
    sim->continuity_ok = false;
    sim->device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_DEGRADED;
    if (!sim_emit_event(sim, LOCKER_WIRE_DEVICE_EVENT_TYPE_CONTINUITY_LOST, 0u, dropped, NULL, NULL,
                        0u, NULL)) {
        return LOCKER_STATUS_IO_ERROR;
    }
    return LOCKER_STATUS_OK;
}

locker_status_t locker_sim_control_fail_journal(locker_sim_t *sim, bool enabled)
{
    if (sim == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    sim->fail_journal = enabled;
    sim->journal_state = (enabled || sim->journal_fault_latched)
                             ? LOCKER_WIRE_WIRE_JOURNAL_STATE_FAULT
                             : LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
    if (enabled) {
        sim_revoke_actions(sim);
    }
    return LOCKER_STATUS_OK;
}

/* ------------------------------------------------------------------ */
/* read-only observations                                              */
/* ------------------------------------------------------------------ */

locker_uuid_t locker_sim_session_id(const locker_sim_t *sim)
{
    locker_uuid_t value;

    memset(&value, 0, sizeof(value));
    if (sim != NULL && sim->has_session) {
        value = sim->session_id;
    }
    return value;
}

locker_uuid_t locker_sim_journal_epoch(const locker_sim_t *sim)
{
    locker_uuid_t value;

    memset(&value, 0, sizeof(value));
    if (sim != NULL) {
        value = sim->journal_epoch;
    }
    return value;
}

uint64_t locker_sim_boot_id(const locker_sim_t *sim)
{
    return sim != NULL ? sim->boot_id : 0u;
}

uint64_t locker_sim_action_watermark(const locker_sim_t *sim)
{
    return sim != NULL ? sim->action_watermark : 0u;
}

uint64_t locker_sim_dropped_events(const locker_sim_t *sim)
{
    return sim != NULL ? sim->dropped_events : 0u;
}

uint8_t locker_sim_device_mode(const locker_sim_t *sim)
{
    return sim != NULL ? sim->device_mode : 0u;
}

uint8_t locker_sim_journal_state(const locker_sim_t *sim)
{
    return sim != NULL ? sim->journal_state : 0u;
}

uint8_t locker_sim_door(const locker_sim_t *sim)
{
    return sim != NULL ? sim->door : 0u;
}

uint8_t locker_sim_item(const locker_sim_t *sim)
{
    return sim != NULL ? sim->item : 0u;
}

uint8_t locker_sim_command_state(const locker_sim_t *sim)
{
    return sim != NULL ? sim->command_state : 0u;
}

bool locker_sim_has_active_intent(const locker_sim_t *sim)
{
    return sim != NULL && sim->has_active;
}

bool locker_sim_continuity_ok(const locker_sim_t *sim)
{
    return sim != NULL && sim->continuity_ok;
}

locker_wire_cursor_t locker_sim_first_available(const locker_sim_t *sim)
{
    locker_wire_cursor_t value = {0u, 0u};

    if (sim != NULL) {
        value = sim->first_available;
    }
    return value;
}

locker_wire_cursor_t locker_sim_last_committed(const locker_sim_t *sim)
{
    locker_wire_cursor_t value = {0u, 0u};

    if (sim != NULL) {
        value = sim->last_committed;
    }
    return value;
}

locker_wire_cursor_t locker_sim_ack_persisted(const locker_sim_t *sim)
{
    locker_wire_cursor_t value = {0u, 0u};

    if (sim != NULL) {
        value = sim->ack_persisted;
    }
    return value;
}

uint64_t locker_sim_event_count(const locker_sim_t *sim)
{
    if (sim == NULL) {
        return 0u;
    }
    return sim_journal_event_count(sim);
}

uint64_t locker_sim_pulse_count(const locker_sim_t *sim)
{
    sqlite3_stmt *stmt = NULL;
    uint64_t count = 0u;

    if (sim == NULL || sim->db == NULL ||
        sqlite3_prepare_v2(sim->db, "SELECT COUNT(*) FROM sim_events WHERE event_type=2", -1,
                           &stmt, NULL) != SQLITE_OK) {
        return 0u;
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = (uint64_t)sqlite3_column_int64(stmt, 0);
    }
    (void)sqlite3_finalize(stmt);
    return count;
}
