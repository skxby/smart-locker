#define _POSIX_C_SOURCE 200809L
#include "journal.h"
#include "nor_fixture.h"
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum { PRODUCERS = 2, EVENTS_PER_PRODUCER = 8, FIRST_TOKEN = 100 };
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t ready_condition;
    pthread_t owner;
    bool ready, stop;
    unsigned committed, notified;
    bool seen[PRODUCERS * EVENTS_PER_PRODUCER];
    mcu_journal_state_t state;
} runtime_t;
static runtime_t runtime;
static nor_fixture_t nor;
static mcu_journal_t *journal;
static locker_uuid_t epoch = {{0x73}};
static const mcu_journal_config_t geometry = {
    true, 0xef4018, 16U * 1024U * 1024U, 4096, 256, 0
};

static void lock(void *user) { assert(pthread_mutex_lock(&((runtime_t *)user)->mutex) == 0); }
static void unlock(void *user) { assert(pthread_mutex_unlock(&((runtime_t *)user)->mutex) == 0); }
static bool is_owner(void *user) { return pthread_equal(pthread_self(), ((runtime_t *)user)->owner) != 0; }
static void pause_caller(void *user)
{
    const struct timespec duration = {0, 1000000};
    (void)user;
    assert(nanosleep(&duration, NULL) == 0);
}
static locker_status_t on_commit(void *user, uint32_t token, locker_status_t result)
{
    runtime_t *ctx = user;
    unsigned index = token - FIRST_TOKEN;
    assert(is_owner(user) && result == LOCKER_STATUS_OK);
    assert(token >= FIRST_TOKEN && index < PRODUCERS * EVENTS_PER_PRODUCER);
    assert(!ctx->seen[index]);
    ctx->seen[index] = true;
    ++ctx->committed;
    return LOCKER_STATUS_OK;
}
static locker_status_t on_event(void *user, const locker_device_event_t *event)
{
    runtime_t *ctx = user;
    assert(is_owner(user));
    assert(event->event_type == LOCKER_WIRE_DEVICE_EVENT_TYPE_HEALTH_CHANGED);
    assert(event->live_transport && event->key.boot_id == 1);
    ++ctx->notified;
    return LOCKER_STATUS_OK;
}
static locker_status_t on_control(void *user, uint32_t token,
                                  locker_status_t result, const mcu_journal_state_t *state)
{
    runtime_t *ctx = user;
    assert(is_owner(user) && token == 999 && result == LOCKER_STATUS_OK && state);
    ctx->state = *state;
    return LOCKER_STATUS_OK;
}

static void *storage_owner(void *argument)
{
    runtime_t *ctx = argument;
    lock(ctx);
    ctx->owner = pthread_self();
    ctx->ready = true;
    assert(pthread_cond_signal(&ctx->ready_condition) == 0);
    unlock(ctx);
    for (;;) {
        bool stop;
        locker_status_t result = mcu_journal_process_one(journal);
        assert(result == LOCKER_STATUS_OK || result == LOCKER_STATUS_NOT_READY);
        lock(ctx); stop = ctx->stop; unlock(ctx);
        if (stop) break;
        if (result == LOCKER_STATUS_NOT_READY) pause_caller(ctx);
    }
    return NULL;
}

static void *producer(void *argument)
{
    unsigned producer_index = *(const unsigned *)argument;
    unsigned round;
    for (round = 0; round < EVENTS_PER_PRODUCER; ++round) {
        locker_device_event_t event = {0};
        locker_event_key_t after = {0};
        locker_event_batch_t page;
        locker_status_t result = LOCKER_STATUS_BUSY;
        unsigned retries = 0;
        uint32_t token = FIRST_TOKEN + producer_index * EVENTS_PER_PRODUCER + round;
        event.event_type = LOCKER_WIRE_DEVICE_EVENT_TYPE_HEALTH_CHANGED;
        event.detail = token;
        while (result == LOCKER_STATUS_BUSY && retries++ < 5000) {
            result = mcu_journal_append(journal, &event, token);
            if (result == LOCKER_STATUS_BUSY) pause_caller(&runtime);
        }
        assert(result == LOCKER_STATUS_QUEUED);
        event.detail = 0; /* Caller storage may be reused immediately. */
        after.journal_epoch = epoch;
        strcpy(after.device_id, "concurrent-journal");
        assert(mcu_journal_read_page(journal, &after, 4, &page) == LOCKER_STATUS_OK);
        assert(!page.gap_before_page && page.count > 0);
        /* Its own preceding append must have passed the Storage FIFO before
           this read reply, regardless of the other producer's interleaving. */
        assert(page.last_committed.boot_id == 1 && page.last_committed.event_seq >= round + 2);
        assert(mcu_journal_ack(journal, &page.next_after) == LOCKER_STATUS_OK);
    }
    return NULL;
}

static void recover(void)
{
    mcu_journal_control_request_t request = {0};
    request.kind = MCU_JOURNAL_RECOVER;
    request.token = 999;
    assert(mcu_journal_control(journal, &request) == LOCKER_STATUS_QUEUED);
    assert(mcu_journal_process_one(journal) == LOCKER_STATUS_OK);
}

int main(void)
{
    pthread_t storage, producers[PRODUCERS];
    unsigned indices[PRODUCERS], i, count = 0;
    bool records[PRODUCERS * EVENTS_PER_PRODUCER] = {false};
    locker_event_key_t after = {0};
    locker_event_batch_t page;
    mcu_journal_sink_t sink = {&runtime, on_commit, on_event, on_control};
    mcu_journal_port_t port = {&nor.config, &runtime, lock, unlock, pause_caller, is_owner};
    memset(&runtime, 0, sizeof(runtime));
    assert(pthread_mutex_init(&runtime.mutex, NULL) == 0);
    assert(pthread_cond_init(&runtime.ready_condition, NULL) == 0);
    runtime.owner = pthread_self();
    nor_fixture_init(&nor, 4096);
    assert(mcu_journal_attach(&port) == LOCKER_STATUS_OK);
    assert(mcu_journal_provision(&geometry, &epoch, "concurrent-journal") == LOCKER_STATUS_OK);
    assert(mcu_journal_init(&geometry, &journal) == LOCKER_STATUS_OK);
    assert(mcu_journal_bind_sink(journal, &sink) == LOCKER_STATUS_OK);
    assert(pthread_create(&storage, NULL, storage_owner, &runtime) == 0);
    lock(&runtime);
    while (!runtime.ready)
        assert(pthread_cond_wait(&runtime.ready_condition, &runtime.mutex) == 0);
    unlock(&runtime);
    for (i = 0; i < PRODUCERS; ++i) {
        indices[i] = i;
        assert(pthread_create(&producers[i], NULL, producer, &indices[i]) == 0);
    }
    for (i = 0; i < PRODUCERS; ++i) assert(pthread_join(producers[i], NULL) == 0);
    lock(&runtime); runtime.stop = true; unlock(&runtime);
    assert(pthread_join(storage, NULL) == 0);
    runtime.owner = pthread_self();
    assert(runtime.committed == PRODUCERS * EVENTS_PER_PRODUCER);
    assert(runtime.notified == runtime.committed);
    for (i = 0; i < runtime.committed; ++i) assert(runtime.seen[i]);
    recover();
    assert(runtime.state.continuity_ok && !runtime.state.has_active);
    assert(runtime.state.last_committed.event_seq == 1 + PRODUCERS * EVENTS_PER_PRODUCER);
    assert(runtime.state.ack_persisted.event_seq >= 1);
    after.journal_epoch = epoch;
    strcpy(after.device_id, "concurrent-journal");
    do {
        assert(mcu_journal_read_page(journal, &after, 4, &page) == LOCKER_STATUS_OK);
        for (i = 0; i < page.count; ++i) {
            const locker_device_event_t *event = &page.events[i];
            assert(event->key.event_seq == count + 1 && !event->live_transport);
            if (count == 0) assert(event->event_type == LOCKER_WIRE_DEVICE_EVENT_TYPE_BOOT);
            else {
                unsigned index = (unsigned)event->detail - FIRST_TOKEN;
                assert(event->event_type == LOCKER_WIRE_DEVICE_EVENT_TYPE_HEALTH_CHANGED);
                assert(index < PRODUCERS * EVENTS_PER_PRODUCER && !records[index]);
                records[index] = true;
            }
            ++count;
        }
        after = page.next_after;
    } while (page.has_more);
    assert(count == 1 + PRODUCERS * EVENTS_PER_PRODUCER);
    assert(mcu_journal_ack(journal, &after) == LOCKER_STATUS_OK);
    assert(mcu_journal_shutdown(journal) == LOCKER_STATUS_OK);
    nor_fixture_restart(&nor);
    assert(mcu_journal_attach(&port) == LOCKER_STATUS_OK);
    assert(mcu_journal_init(&geometry, &journal) == LOCKER_STATUS_OK);
    assert(mcu_journal_bind_sink(journal, &sink) == LOCKER_STATUS_OK);
    recover();
    assert(runtime.state.boot_id == 2 && runtime.state.ack_persisted.event_seq == after.event_seq);
    assert(mcu_journal_shutdown(journal) == LOCKER_STATUS_OK);
    nor_fixture_destroy(&nor);
    assert(pthread_cond_destroy(&runtime.ready_condition) == 0);
    assert(pthread_mutex_destroy(&runtime.mutex) == 0);
    puts("Two pthread producers, FIFO read/ACK, and one Storage owner passed; hardware NOT_RUN");
    return 0;
}
