#include "journal.h"
#include "nor_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Uses the locked littlefs implementation and a software NOR model.
   Board power interruption remains a separate hardware acceptance test. */
static nor_fixture_t nor;
static mcu_journal_t *journal;
static int owner = 1;
static unsigned commits, controls, events;
static uint32_t last_token;
static locker_status_t last_status;
static mcu_journal_state_t state;
static locker_device_event_t last_event;
static const mcu_journal_config_t geometry = {
    true, 0xef4018, 16U * 1024U * 1024U, 4096, 256, 0
};
static locker_uuid_t epoch;

static bool is_owner(void *user) { (void)user; return owner != 0; }
static void no_lock(void *user) { (void)user; }
static void pump(void *user)
{
    int saved = owner;
    (void)user;
    owner = 1;
    (void)mcu_journal_process_one(journal);
    owner = saved;
}
static locker_status_t committed(void *user, uint32_t token, locker_status_t result)
{
    (void)user;
    ++commits;
    last_token = token;
    last_status = result;
    return LOCKER_STATUS_OK;
}
static locker_status_t event_committed(void *user, const locker_device_event_t *event)
{
    (void)user;
    ++events;
    last_event = *event;
    return LOCKER_STATUS_OK;
}
static locker_status_t control_result(void *user, uint32_t token,
                                     locker_status_t result, const mcu_journal_state_t *value)
{
    (void)user;
    ++controls;
    last_token = token;
    last_status = result;
    if (value) state = *value;
    return LOCKER_STATUS_OK;
}
static void attach(void)
{
    mcu_journal_port_t port = {&nor.config, NULL, no_lock, no_lock, pump, is_owner};
    assert(mcu_journal_attach(&port) == LOCKER_STATUS_OK);
}
static void bind(void)
{
    mcu_journal_sink_t sink = {NULL, committed, event_committed, control_result};
    assert(mcu_journal_bind_sink(journal, &sink) == LOCKER_STATUS_OK);
}
static void recover(uint32_t token)
{
    unsigned before = controls;
    mcu_journal_control_request_t request = {0};
    request.kind = MCU_JOURNAL_RECOVER;
    request.token = token;
    assert(mcu_journal_control(journal, &request) == LOCKER_STATUS_QUEUED);
    (void)mcu_journal_process_one(journal);
    assert(controls == before + 1 && last_token == token);
    assert(last_status == LOCKER_STATUS_OK);
}
static void fresh(void)
{
    nor_fixture_init(&nor, 4096);
    epoch.bytes[0] = 0x42;
    epoch.bytes[15] = 0x99;
    attach();
    assert(mcu_journal_provision(&geometry, &epoch, "journal-test-device") == LOCKER_STATUS_OK);
    assert(mcu_journal_init(&geometry, &journal) == LOCKER_STATUS_OK);
    bind();
    recover(1000);
    assert(state.boot_id == 1 && state.action_watermark == 0);
    assert(!state.has_active && state.continuity_ok);
    assert(state.journal_state == LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY);
    assert(!memcmp(state.journal_epoch.bytes, epoch.bytes, 16));
}
static void stop(void)
{
    (void)mcu_journal_shutdown(journal);
    journal = NULL;
    nor_fixture_destroy(&nor);
}
static locker_unlock_intent_t intent(void)
{
    locker_unlock_intent_t value = {0};
    value.journal_epoch = epoch;
    value.session_id.bytes[0] = 1;
    value.operation_id.bytes[0] = 2;
    value.command_id.bytes[0] = 3;
    value.expected_boot_id = state.boot_id;
    value.expected_physical_revision = 4;
    value.action_no = 1;
    value.not_after_mcu_ms = 2000;
    value.slot_id = 1;
    value.kind = LOCKER_OPERATION_KIND_BORROW;
    return value;
}
static void submit_intent(const locker_unlock_intent_t *value, uint32_t token)
{
    unsigned before = commits;
    assert(mcu_journal_commit_intent(journal, value, token) == LOCKER_STATUS_QUEUED);
    assert(commits == before); /* Queue acceptance is not a durable receipt. */
    (void)mcu_journal_process_one(journal);
    assert(commits == before + 1 && last_token == token);
}
static void test_lifecycle(void)
{
    locker_unlock_intent_t value;
    locker_event_batch_t page;
    locker_event_key_t after = {0}, ack;
    locker_device_event_t fact = {0};
    mcu_journal_control_request_t request = {0};
    uint64_t boot;
    fresh();
    value = intent();
    submit_intent(&value, 1);
    assert(last_status == LOCKER_STATUS_OK);
    recover(1001);
    assert(state.action_watermark == 1 && state.has_active);
    assert(state.command_state == LOCKER_COMMAND_STATE_ACCEPTED);
    assert(!memcmp(&state.active_intent, &value, sizeof(value)));
    {
        unsigned before = events;
        submit_intent(&value, 2);
        assert(events == before); /* Exact retry must not republish an old fact as LIVE. */
    }
    assert(last_status == LOCKER_STATUS_OK); /* Exact retry does not increment watermarks. */
    recover(1002);
    assert(state.action_watermark == 1);

    fact.event_type = LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_OPENED;
    fact.door = LOCKER_DOOR_STATE_OPEN;
    fact.item = LOCKER_ITEM_STATE_PRESENT;
    fact.physical_revision = 5;
    fact.mono_ms = 100;
    fact.operation_id = value.operation_id;
    fact.command_id = value.command_id;
    assert(mcu_journal_append(journal, &fact, 3) == LOCKER_STATUS_QUEUED);
    memset(&fact, 0, sizeof(fact)); /* Queue owns a copy, not the caller pointer. */
    (void)mcu_journal_process_one(journal);
    assert(last_status == LOCKER_STATUS_OK && last_token == 3);
    assert(last_event.event_type == LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_OPENED);
    assert(last_event.key.boot_id == 1 && last_event.key.event_seq > 0);
    assert(!memcmp(last_event.key.journal_epoch.bytes, epoch.bytes, 16));

    request.kind = MCU_JOURNAL_BARRIER;
    request.token = 4;
    request.barrier_after_token = 3;
    assert(mcu_journal_control(journal, &request) == LOCKER_STATUS_QUEUED);
    (void)mcu_journal_process_one(journal);
    assert(last_status == LOCKER_STATUS_OK && last_token == 4);
    after.journal_epoch = epoch;
    strcpy(after.device_id, "journal-test-device");
    owner = 0; /* Synchronous API is marshalled through the Storage FIFO. */
    assert(mcu_journal_read_page(journal, &after, 4, &page) == LOCKER_STATUS_OK);
    owner = 1;
    assert(page.count >= 2 && !page.gap_before_page);
    assert(page.events[0].event_type == LOCKER_WIRE_DEVICE_EVENT_TYPE_BOOT);
    ack = page.events[page.count - 1].key;
    owner = 0;
    assert(mcu_journal_ack(journal, &ack) == LOCKER_STATUS_OK);
    owner = 1;
    recover(1003);
    assert(state.ack_persisted.boot_id == ack.boot_id && state.ack_persisted.event_seq == ack.event_seq);
    {
        locker_event_key_t invalid = ack;
        invalid.boot_id = 0;
        assert(mcu_journal_ack(journal, &invalid) == LOCKER_STATUS_CONFLICT);
        invalid = ack; invalid.event_seq = 0;
        assert(mcu_journal_ack(journal, &invalid) == LOCKER_STATUS_CONFLICT);
    }
    boot = state.boot_id;
    (void)mcu_journal_shutdown(journal);
    nor_fixture_restart(&nor);
    attach();
    assert(mcu_journal_init(&geometry, &journal) == LOCKER_STATUS_OK);
    bind();
    recover(1004);
    assert(state.boot_id == boot + 1 && state.action_watermark == 1 && state.has_active);
    assert(state.ack_persisted.boot_id == ack.boot_id && state.ack_persisted.event_seq == ack.event_seq);

    request.kind = MCU_JOURNAL_FINALIZE;
    request.token = 5;
    request.journal_epoch = epoch;
    request.session_id = value.session_id;
    request.operation_id = value.operation_id;
    request.command_id = value.command_id;
    request.expected_boot_id = state.boot_id;
    request.action_no = 1;
    request.terminal_state = LOCKER_COMMAND_STATE_UNKNOWN_RESULT;
    assert(mcu_journal_control(journal, &request) == LOCKER_STATUS_QUEUED);
    (void)mcu_journal_process_one(journal);
    assert(last_status == LOCKER_STATUS_OK);
    request.kind = MCU_JOURNAL_RELEASE;
    request.token = 6;
    assert(mcu_journal_control(journal, &request) == LOCKER_STATUS_QUEUED);
    (void)mcu_journal_process_one(journal);
    assert(last_status == LOCKER_STATUS_OK);
    recover(1005);
    assert(!state.has_active && state.action_watermark == 1);
    stop();
}

static void test_mount_failure_does_not_format(void)
{
    unsigned programs, erases;
    nor_fixture_init(&nor, 4096);
    attach();
    memset(nor.bytes, 0x5a, nor.capacity);
    programs = nor.programs; erases = nor.erases;
    journal = NULL;
    assert(mcu_journal_init(&geometry, &journal) != LOCKER_STATUS_OK);
    assert(nor.programs == programs && nor.erases == erases);
    assert(nor.bytes[0] == 0x5a && nor.bytes[nor.capacity - 1] == 0x5a);
    nor_fixture_destroy(&nor);
}

static void test_intent_power_cuts(void)
{
    uint8_t *baseline;
    unsigned operation_count, cut_index;
    locker_unlock_intent_t value;
    fresh();
    (void)mcu_journal_shutdown(journal);
    baseline = malloc(nor.capacity);
    assert(baseline);
    memcpy(baseline, nor.bytes, nor.capacity);

    attach();
    assert(mcu_journal_init(&geometry, &journal) == LOCKER_STATUS_OK);
    bind(); recover(1100);
    value = intent();
    nor_fixture_cut_at(&nor, 0);
    submit_intent(&value, 11);
    assert(last_status == LOCKER_STATUS_OK);
    operation_count = nor.operations;
    assert(operation_count > 0);
    (void)mcu_journal_shutdown(journal);

    for (cut_index = 1; cut_index <= operation_count; ++cut_index) {
        memcpy(nor.bytes, baseline, nor.capacity);
        nor_fixture_restart(&nor);
        attach();
        assert(mcu_journal_init(&geometry, &journal) == LOCKER_STATUS_OK);
        bind(); recover(1101);
        value = intent();
        nor_fixture_cut_at(&nor, cut_index);
        submit_intent(&value, 12);
        assert(nor.failed && last_status != LOCKER_STATUS_OK);
        (void)mcu_journal_shutdown(journal);
        nor_fixture_restart(&nor);
        attach();
        assert(mcu_journal_init(&geometry, &journal) == LOCKER_STATUS_OK);
        bind(); recover(1102);
        assert(state.action_watermark == 0 || state.action_watermark == 1);
        assert(state.dropped_events == UINT64_MAX && !state.continuity_ok);
        assert(state.journal_state == LOCKER_WIRE_WIRE_JOURNAL_STATE_DEGRADED);
        assert(state.has_active == (state.action_watermark == 1));
        if (state.has_active) {
            assert(state.active_intent.action_no == 1);
            assert(!memcmp(state.active_intent.command_id.bytes, value.command_id.bytes, 16));
            assert(!memcmp(state.active_intent.operation_id.bytes, value.operation_id.bytes, 16));
            assert(state.command_state == LOCKER_COMMAND_STATE_ACCEPTED ||
                   state.command_state == LOCKER_COMMAND_STATE_UNKNOWN_RESULT);
        }
        (void)mcu_journal_shutdown(journal);
    }
    printf("intent power-cut positions checked: %u\n", operation_count);
    free(baseline);
    nor_fixture_destroy(&nor);
}

static void queued_control(mcu_journal_control_request_t *request, uint32_t token,
                           locker_status_t expected)
{
    request->token = token;
    assert(mcu_journal_control(journal, request) == LOCKER_STATUS_QUEUED);
    (void)mcu_journal_process_one(journal);
    assert(last_token == token && last_status == expected);
}

static unsigned count_events(void)
{
    locker_event_batch_t page;
    locker_event_key_t after = {0};
    unsigned count = 0;
    after.journal_epoch = epoch;
    strcpy(after.device_id, "journal-test-device");
    do {
        unsigned i;
        assert(mcu_journal_read_page(journal, &after, 4, &page) == LOCKER_STATUS_OK);
        assert(!page.gap_before_page);
        for (i = 0; i < page.count; ++i) {
            if (after.boot_id == page.events[i].key.boot_id)
                assert(page.events[i].key.event_seq == after.event_seq + 1);
            else
                assert(page.events[i].key.event_seq == 1 &&
                       page.events[i].event_type == LOCKER_WIRE_DEVICE_EVENT_TYPE_BOOT);
            assert(!page.events[i].live_transport);
            after = page.events[i].key;
            ++count;
        }
    } while (page.has_more);
    return count;
}

static void append_fact(uint32_t token)
{
    locker_device_event_t fact = {0};
    fact.event_type = LOCKER_WIRE_DEVICE_EVENT_TYPE_HEALTH_CHANGED;
    fact.detail = token;
    assert(mcu_journal_append(journal, &fact, token) == LOCKER_STATUS_QUEUED);
    (void)mcu_journal_process_one(journal);
    assert(last_token == token);
}

static void test_queue_and_guards(void)
{
    locker_device_event_t fact = {0};
    locker_unlock_intent_t value, invalid;
    locker_event_batch_t page;
    locker_event_key_t cursor = {0};
    mcu_journal_control_request_t request = {0};
    unsigned i, before;
    fresh();
    fact.event_type = LOCKER_WIRE_DEVICE_EVENT_TYPE_HEALTH_CHANGED;
    before = commits;
    assert(mcu_journal_append(journal, &fact, 20) == LOCKER_STATUS_QUEUED);
    assert(mcu_journal_append(journal, &fact, 20) == LOCKER_STATUS_CONFLICT);
    for (i = 1; i < MCU_JOURNAL_QUEUE_CAPACITY; ++i)
        assert(mcu_journal_append(journal, &fact, 20 + i) == LOCKER_STATUS_QUEUED);
    assert(mcu_journal_append(journal, &fact, 50) == LOCKER_STATUS_BUSY);
    assert(mcu_journal_shutdown(journal) == LOCKER_STATUS_BUSY);
    owner = 0;
    assert(mcu_journal_process_one(journal) == LOCKER_STATUS_NOT_READY);
    owner = 1;
    assert(commits == before);
    for (i = 0; i < MCU_JOURNAL_QUEUE_CAPACITY; ++i) {
        assert(mcu_journal_process_one(journal) == LOCKER_STATUS_OK);
        assert(last_token == 20 + i);
    }
    assert(commits == before + MCU_JOURNAL_QUEUE_CAPACITY);
    assert(mcu_journal_process_one(journal) == LOCKER_STATUS_NOT_READY);
    assert(count_events() == 1 + MCU_JOURNAL_QUEUE_CAPACITY);
    assert(mcu_journal_append(journal, &fact, 0) == LOCKER_STATUS_INVALID);
    fact.card_uid_len = 5;
    assert(mcu_journal_append(journal, &fact, 50) == LOCKER_STATUS_INVALID);

    recover(1200);
    value = intent();
    invalid = value; invalid.journal_epoch.bytes[0] ^= 1;
    submit_intent(&invalid, 51); assert(last_status == LOCKER_STATUS_CONFLICT);
    invalid = value; ++invalid.expected_boot_id;
    submit_intent(&invalid, 52); assert(last_status == LOCKER_STATUS_CONFLICT);
    invalid = value; invalid.action_no = 0;
    submit_intent(&invalid, 53); assert(last_status == LOCKER_STATUS_REPLAY_BLOCKED);
    invalid = value; invalid.action_no = 2;
    submit_intent(&invalid, 54); assert(last_status == LOCKER_STATUS_CONFLICT);
    invalid = value; invalid.slot_id = 2;
    submit_intent(&invalid, 55); assert(last_status == LOCKER_STATUS_CONFLICT);
    invalid = value; memset(&invalid.command_id, 0, sizeof(invalid.command_id));
    submit_intent(&invalid, 56); assert(last_status == LOCKER_STATUS_CONFLICT);
    recover(1201);
    assert(!state.has_active && state.action_watermark == 0 && state.continuity_ok);
    submit_intent(&value, 57); assert(last_status == LOCKER_STATUS_OK);
    invalid = value; ++invalid.not_after_mcu_ms;
    submit_intent(&invalid, 58); assert(last_status == LOCKER_STATUS_BUSY);
    request.kind = MCU_JOURNAL_RELEASE;
    request.journal_epoch = epoch;
    request.session_id = value.session_id;
    request.operation_id = value.operation_id;
    request.command_id = value.command_id;
    request.expected_boot_id = value.expected_boot_id;
    request.action_no = 1;
    queued_control(&request, 59, LOCKER_STATUS_CONFLICT); /* Not terminal yet. */
    request.kind = MCU_JOURNAL_FINALIZE;
    request.terminal_state = LOCKER_COMMAND_STATE_REJECTED;
    request.command_id.bytes[0] ^= 1;
    queued_control(&request, 60, LOCKER_STATUS_CONFLICT);
    request.command_id = value.command_id;
    request.terminal_state = LOCKER_COMMAND_STATE_PULSE_STARTED;
    queued_control(&request, 61, LOCKER_STATUS_INVALID);
    request.terminal_state = LOCKER_COMMAND_STATE_REJECTED;
    queued_control(&request, 62, LOCKER_STATUS_OK);
    request.kind = MCU_JOURNAL_RELEASE;
    ++request.expected_boot_id;
    queued_control(&request, 63, LOCKER_STATUS_CONFLICT);
    request.kind = MCU_JOURNAL_BARRIER;
    request.barrier_after_token = 9999;
    queued_control(&request, 64, LOCKER_STATUS_CONFLICT);
    recover(1202);
    assert(state.action_watermark == 1 && state.has_active &&
           state.command_state == LOCKER_COMMAND_STATE_REJECTED);
    cursor.journal_epoch = epoch;
    strcpy(cursor.device_id, "journal-test-device");
    assert(mcu_journal_read_page(journal, &cursor, 0, &page) == LOCKER_STATUS_INVALID);
    assert(mcu_journal_read_page(journal, &cursor, 5, &page) == LOCKER_STATUS_INVALID);
    cursor.journal_epoch.bytes[0] ^= 1;
    assert(mcu_journal_read_page(journal, &cursor, 4, &page) == LOCKER_STATUS_CONFLICT);
    cursor = state.last_committed;
    ++cursor.event_seq;
    assert(mcu_journal_ack(journal, &cursor) == LOCKER_STATUS_CONFLICT);
    stop();
}

static void test_capacity_and_unacked_protection(void)
{
    locker_unlock_intent_t value;
    locker_event_key_t first;
    unsigned i, before;
    fresh();
    first = state.first_available;
    for (i = 1; i < MCU_JOURNAL_MAX_EVENTS - MCU_JOURNAL_RESERVE_EVENTS + 1; ++i) {
        append_fact(2000 + i);
        assert(last_status == LOCKER_STATUS_OK);
    }
    value = intent();
    before = events;
    submit_intent(&value, 2100);
    assert(last_status == LOCKER_STATUS_NO_SPACE && events == before);
    recover(2101);
    assert(!state.has_active && state.continuity_ok && state.action_watermark == 0);
    assert(state.journal_state == LOCKER_WIRE_WIRE_JOURNAL_STATE_FULL);
    assert(state.dropped_events == 0 && state.ack_persisted.boot_id == 0);
    assert(count_events() == MCU_JOURNAL_MAX_EVENTS - MCU_JOURNAL_RESERVE_EVENTS + 1);
    assert(state.first_available.boot_id == first.boot_id &&
           state.first_available.event_seq == first.event_seq);
    /* Refusing a new action keeps its reserved space usable for facts. */
    for (i = MCU_JOURNAL_MAX_EVENTS - MCU_JOURNAL_RESERVE_EVENTS + 1;
         i < MCU_JOURNAL_MAX_EVENTS; ++i) {
        append_fact(2110 + i);
        assert(last_status == LOCKER_STATUS_OK);
    }
    assert(count_events() == MCU_JOURNAL_MAX_EVENTS);
    stop();

    fresh();
    for (i = 1; i < MCU_JOURNAL_MAX_EVENTS; ++i) {
        append_fact(2200 + i);
        assert(last_status == LOCKER_STATUS_OK);
    }
    assert(count_events() == MCU_JOURNAL_MAX_EVENTS);
    before = events;
    append_fact(2300);
    assert(last_status == LOCKER_STATUS_NO_SPACE && events == before);
    recover(2301);
    assert(!state.continuity_ok && state.dropped_events == 1);
    assert(state.journal_state == LOCKER_WIRE_WIRE_JOURNAL_STATE_FULL);
    assert(state.first_available.boot_id == 1 && state.first_available.event_seq == 1);
    assert(count_events() == MCU_JOURNAL_MAX_EVENTS); /* No unACKed eviction. */
    assert(mcu_journal_shutdown(journal) == LOCKER_STATUS_OK);
    nor_fixture_restart(&nor);
    attach();
    journal = NULL;
    assert(mcu_journal_init(&geometry, &journal) == LOCKER_STATUS_NO_SPACE);
    assert(journal == NULL); /* A full journal cannot be READY after reboot. */
    nor_fixture_destroy(&nor);
}

static uint32_t fixture_crc(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_MAX;
    size_t i;
    unsigned bit;
    for (i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (bit = 0; bit < 8; ++bit)
            crc = crc & 1 ? (crc >> 1) ^ 0xedb88320U : crc >> 1;
    }
    return ~crc;
}

static void test_checkpoint_corruption(void)
{
    unsigned variant;
    for (variant = 0; variant < 11; ++variant) {
        lfs_t fs;
        lfs_file_t file;
        struct lfs_file_config file_config = {0};
        uint8_t cache[256];
        uint8_t image[384 + 96 * MCU_JOURNAL_MAX_EVENTS + 4];
        unsigned programs, erases;
        fresh();
        if (variant == 6 || variant == 7) {
            locker_unlock_intent_t value = intent();
            submit_intent(&value, 2999);
            assert(last_status == LOCKER_STATUS_OK);
        }
        append_fact(3000); assert(last_status == LOCKER_STATUS_OK);
        append_fact(3001); assert(last_status == LOCKER_STATUS_OK);
        assert(mcu_journal_shutdown(journal) == LOCKER_STATUS_OK);
        assert(lfs_mount(&fs, &nor.config) == 0);
        file_config.buffer = cache;
        assert(lfs_file_opencfg(&fs, &file, "journal", LFS_O_RDWR, &file_config) == 0);
        assert(lfs_file_read(&fs, &file, image, sizeof(image)) == (lfs_ssize_t)sizeof(image));
        if (variant == 0) {
            image[60] ^= 1; /* A committed littlefs file with invalid Journal CRC. */
        } else {
            uint32_t crc;
            unsigned i;
            /* Version-1 offsets intentionally exercise malformed, CRC-valid
               persisted checkpoints rather than only littlefs mount errors. */
            if (variant == 1) image[384 + 96 + 8] = 9; /* Sequence gap. */
            if (variant == 2) image[53] = 2; /* Header boot exceeds event tail. */
            if (variant == 3) image[77] = 1; /* Half-zero ACK: boot=1, seq=0. */
            if (variant == 4) { image[77] = 1; image[85] = 99; } /* ACK absent from retained facts. */
            if (variant == 5) image[94] = LOCKER_COMMAND_STATE_ACCEPTED; /* Inactive has command state. */
            if (variant == 6) memset(image + 129, 0, 16); /* Active intent has zero session. */
            if (variant == 7) image[161] = 2; /* Active intent refers to a future boot. */
            if (variant == 8) image[93] = 2; /* Non-boolean has_active encoding. */
            if (variant == 9) image[303] = 2; /* Non-boolean persisted continuity. */
            if (variant == 10) image[304] = 2; /* Non-boolean boot_open. */
            crc = fixture_crc(image, sizeof(image) - 4);
            for (i = 0; i < 4; ++i) image[sizeof(image) - 4 + i] = (uint8_t)(crc >> (8 * i));
        }
        assert(lfs_file_seek(&fs, &file, 0, LFS_SEEK_SET) == 0);
        assert(lfs_file_write(&fs, &file, image, sizeof(image)) == (lfs_ssize_t)sizeof(image));
        assert(lfs_file_sync(&fs, &file) == 0);
        assert(lfs_file_close(&fs, &file) == 0);
        assert(lfs_unmount(&fs) == 0);
        programs = nor.programs; erases = nor.erases;
        attach();
        journal = NULL;
        assert(mcu_journal_init(&geometry, &journal) == LOCKER_STATUS_IO_ERROR);
        assert(journal == NULL && nor.programs == programs && nor.erases == erases);
        nor_fixture_destroy(&nor);
    }
}

static mcu_journal_control_request_t active_request(uint8_t kind);
static void test_pulse_state_management(void)
{
    locker_unlock_intent_t value;
    locker_device_event_t fact = {0};
    mcu_journal_control_request_t request;
    fresh();
    value = intent();
    submit_intent(&value, 3900);
    assert(last_status == LOCKER_STATUS_OK);
    fact.event_type = LOCKER_WIRE_DEVICE_EVENT_TYPE_PULSE_STARTED;
    fact.operation_id = value.operation_id;
    fact.command_id = value.command_id;
    assert(mcu_journal_append(journal, &fact, 3901) == LOCKER_STATUS_QUEUED);
    (void)mcu_journal_process_one(journal);
    assert(last_status == LOCKER_STATUS_OK);
    recover(3902);
    assert(state.command_state == LOCKER_COMMAND_STATE_PULSE_STARTED);
    request = active_request(MCU_JOURNAL_RELEASE);
    queued_control(&request, 3903, LOCKER_STATUS_CONFLICT);
    request.kind = MCU_JOURNAL_FINALIZE;
    request.terminal_state = LOCKER_COMMAND_STATE_UNKNOWN_RESULT;
    queued_control(&request, 3904, LOCKER_STATUS_OK);
    recover(3905);
    assert(state.command_state == LOCKER_COMMAND_STATE_UNKNOWN_RESULT && state.has_active);
    stop();
}

static mcu_journal_control_request_t active_request(uint8_t kind)
{
    mcu_journal_control_request_t request = {0};
    request.kind = kind;
    request.journal_epoch = epoch;
    request.session_id = state.active_intent.session_id;
    request.operation_id = state.active_intent.operation_id;
    request.command_id = state.active_intent.command_id;
    request.expected_boot_id = state.boot_id;
    request.action_no = state.active_intent.action_no;
    return request;
}

static void reboot_and_recover(uint32_t token)
{
    assert(mcu_journal_shutdown(journal) == LOCKER_STATUS_OK);
    nor_fixture_restart(&nor);
    attach();
    assert(mcu_journal_init(&geometry, &journal) == LOCKER_STATUS_OK);
    bind();
    recover(token);
}

static void test_terminal_reboot_and_release_retry(void)
{
    locker_unlock_intent_t value;
    mcu_journal_control_request_t request, release;
    unsigned terminal;
    const locker_command_state_t terminal_states[] = {
        LOCKER_COMMAND_STATE_REJECTED, LOCKER_COMMAND_STATE_PULSE_FINISHED
    };
    for (terminal = 0; terminal < sizeof(terminal_states) / sizeof(terminal_states[0]); ++terminal) {
        unsigned before_events;
        fresh();
        value = intent();
        submit_intent(&value, 4000);
        assert(last_status == LOCKER_STATUS_OK);
        recover(4001);
        request = active_request(MCU_JOURNAL_FINALIZE);
        request.terminal_state = terminal_states[terminal];
        queued_control(&request, 4002, LOCKER_STATUS_OK);
        reboot_and_recover(4003);
        assert(state.has_active && state.action_watermark == 1 &&
               state.command_state == terminal_states[terminal]);
        release = active_request(MCU_JOURNAL_RELEASE);
        queued_control(&release, 4004, LOCKER_STATUS_OK);
        before_events = events;
        queued_control(&release, 4005, LOCKER_STATUS_OK);
        assert(events == before_events); /* Retry neither emits nor consumes another event. */
        reboot_and_recover(4006);
        assert(!state.has_active && state.action_watermark == 1);
        /* The tombstone retains the original successful release guard across reboot. */
        queued_control(&release, 4007, LOCKER_STATUS_OK);
        assert(events == before_events);
        release.command_id.bytes[0] ^= 1;
        queued_control(&release, 4008, LOCKER_STATUS_CONFLICT);
        stop();
    }
}

/* 0=ACK, 1=FINALIZE, 2=RELEASE. All traverse the same littlefs commit path. */
static locker_status_t fault_operation(unsigned kind, const locker_event_key_t *ack)
{
    mcu_journal_control_request_t request;
    if (kind == 0) return mcu_journal_ack(journal, ack);
    request = active_request(kind == 1 ? MCU_JOURNAL_FINALIZE : MCU_JOURNAL_RELEASE);
    request.token = 4100;
    request.terminal_state = LOCKER_COMMAND_STATE_REJECTED;
    assert(mcu_journal_control(journal, &request) == LOCKER_STATUS_QUEUED);
    (void)mcu_journal_process_one(journal);
    assert(last_token == request.token);
    return last_status;
}

static void test_management_power_cuts(void)
{
    unsigned kind;
    for (kind = 0; kind < 3; ++kind) {
        uint8_t *baseline;
        unsigned cut_index, operation_count;
        locker_unlock_intent_t value;
        locker_event_key_t ack;
        fresh();
        value = intent();
        submit_intent(&value, 4101);
        assert(last_status == LOCKER_STATUS_OK);
        recover(4102);
        if (kind == 2) {
            mcu_journal_control_request_t request = active_request(MCU_JOURNAL_FINALIZE);
            request.terminal_state = LOCKER_COMMAND_STATE_REJECTED;
            queued_control(&request, 4103, LOCKER_STATUS_OK);
            recover(4104);
        }
        ack = state.last_committed;
        assert(mcu_journal_shutdown(journal) == LOCKER_STATUS_OK);
        baseline = malloc(nor.capacity);
        assert(baseline);
        memcpy(baseline, nor.bytes, nor.capacity);
        attach();
        assert(mcu_journal_init(&geometry, &journal) == LOCKER_STATUS_OK);
        bind(); recover(4105);
        nor_fixture_cut_at(&nor, 0);
        assert(fault_operation(kind, &ack) == LOCKER_STATUS_OK);
        operation_count = nor.operations;
        assert(operation_count > 0);
        assert(mcu_journal_shutdown(journal) == LOCKER_STATUS_OK);

        for (cut_index = 1; cut_index <= operation_count; ++cut_index) {
            memcpy(nor.bytes, baseline, nor.capacity);
            nor_fixture_restart(&nor);
            attach();
            assert(mcu_journal_init(&geometry, &journal) == LOCKER_STATUS_OK);
            bind(); recover(4106);
            nor_fixture_cut_at(&nor, cut_index);
            assert(fault_operation(kind, &ack) != LOCKER_STATUS_OK);
            assert(nor.failed);
            reboot_and_recover(4107);
            assert(state.action_watermark == 1 && !state.continuity_ok);
            assert(state.dropped_events == UINT64_MAX);
            assert(state.journal_state == LOCKER_WIRE_WIRE_JOURNAL_STATE_DEGRADED);
            if (kind == 0) {
                assert(state.has_active);
                assert((state.ack_persisted.boot_id == 0 && state.ack_persisted.event_seq == 0) ||
                       (state.ack_persisted.boot_id == ack.boot_id &&
                        state.ack_persisted.event_seq == ack.event_seq));
            } else if (kind == 1) {
                assert(state.has_active);
                assert(state.command_state == LOCKER_COMMAND_STATE_UNKNOWN_RESULT ||
                       state.command_state == LOCKER_COMMAND_STATE_REJECTED);
            } else {
                assert((state.has_active && state.command_state == LOCKER_COMMAND_STATE_REJECTED) ||
                       (!state.has_active && state.command_state == LOCKER_COMMAND_STATE_NONE));
            }
            assert(mcu_journal_shutdown(journal) == LOCKER_STATUS_OK);
        }
        printf("management kind %u power-cut positions checked: %u\n", kind, operation_count);
        free(baseline);
        nor_fixture_destroy(&nor);
    }
}

static void test_loss_survives_unclean_and_clean_reboots(void)
{
    locker_unlock_intent_t value;
    locker_event_batch_t page;
    locker_event_key_t after = {0};
    unsigned count;
    fresh();
    /* The first failed fact has a known runtime loss count. A subsequent
       unclean boot cannot bound all missing observations and uses the
       epoch-wide unknown sentinel, never a fresh session's zero counter. */
    nor_fixture_cut_at(&nor, 1);
    append_fact(5000);
    assert(last_status == LOCKER_STATUS_IO_ERROR && nor.failed);
    recover(5001);
    assert(state.dropped_events == 1 && !state.continuity_ok);
    assert(state.journal_state == LOCKER_WIRE_WIRE_JOURNAL_STATE_FAULT);
    assert(mcu_journal_shutdown(journal) == LOCKER_STATUS_OK);
    nor_fixture_restart(&nor);
    attach();
    assert(mcu_journal_init(&geometry, &journal) == LOCKER_STATUS_OK);
    bind(); recover(5002);
    assert(state.boot_id == 2 && !state.has_active && !state.continuity_ok);
    assert(state.dropped_events == UINT64_MAX);
    assert(state.journal_state == LOCKER_WIRE_WIRE_JOURNAL_STATE_DEGRADED);
    assert(!memcmp(state.journal_epoch.bytes, epoch.bytes, sizeof(epoch.bytes)));
    value = intent();
    submit_intent(&value, 5003);
    assert(last_status == LOCKER_STATUS_NOT_READY);
    value.session_id.bytes[0] = 77;
    submit_intent(&value, 5004);
    assert(last_status == LOCKER_STATUS_NOT_READY);
    append_fact(5005);
    assert(last_status == LOCKER_STATUS_OK); /* Keep recording evidence while degraded. */
    after.journal_epoch = epoch;
    strcpy(after.device_id, "journal-test-device");
    assert(mcu_journal_read_page(journal, &after, 4, &page) == LOCKER_STATUS_OK);
    assert(page.count >= 2 && !page.gap_before_page);
    assert(mcu_journal_ack(journal, &page.next_after) == LOCKER_STATUS_OK);
    recover(5006);
    assert(state.dropped_events == UINT64_MAX && !state.continuity_ok);
    count = count_events();
    reboot_and_recover(5007);
    assert(state.boot_id == 3 && state.dropped_events == UINT64_MAX && !state.continuity_ok);
    assert(state.journal_state == LOCKER_WIRE_WIRE_JOURNAL_STATE_DEGRADED);
    assert(count_events() == count + 1); /* Clean close contributes no fact of its own. */
    assert(state.ack_persisted.boot_id == page.next_after.boot_id &&
           state.ack_persisted.event_seq == page.next_after.event_seq);
    value = intent();
    submit_intent(&value, 5008);
    assert(last_status == LOCKER_STATUS_NOT_READY);
    stop();
}

static void test_unclean_close_without_prior_business_fault(void)
{
    locker_unlock_intent_t value;
    unsigned count;
    fresh();
    count = count_events();
    assert(count == 1 && state.dropped_events == 0 && state.continuity_ok);
    /* Power disappears before an orderly close can clear boot_open. There
       was no prior Journal job failure to set the in-memory fault flag. */
    nor.failed = 1;
    assert(mcu_journal_shutdown(journal) == LOCKER_STATUS_IO_ERROR);
    nor_fixture_restart(&nor);
    attach();
    assert(mcu_journal_init(&geometry, &journal) == LOCKER_STATUS_OK);
    bind(); recover(5100);
    assert(state.boot_id == 2 && state.dropped_events == UINT64_MAX && !state.continuity_ok);
    assert(state.journal_state == LOCKER_WIRE_WIRE_JOURNAL_STATE_DEGRADED);
    assert(count_events() == count + 1);
    count = count_events();
    reboot_and_recover(5101);
    assert(state.boot_id == 3 && state.dropped_events == UINT64_MAX && !state.continuity_ok);
    assert(count_events() == count + 1); /* Clean-close metadata adds no event. */
    value = intent();
    submit_intent(&value, 5102);
    assert(last_status == LOCKER_STATUS_NOT_READY);
    stop();
}

int main(void)
{
    test_mount_failure_does_not_format();
    test_lifecycle();
    test_queue_and_guards();
    test_capacity_and_unacked_protection();
    test_checkpoint_corruption();
    test_pulse_state_management();
    test_intent_power_cuts();
    test_terminal_reboot_and_release_retry();
    test_management_power_cuts();
    test_loss_survives_unclean_and_clean_reboots();
    test_unclean_close_without_prior_business_fault();
    puts("Journal littlefs software fixture checks passed; hardware NOT_RUN");
    return 0;
}
