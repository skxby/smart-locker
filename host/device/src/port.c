/*
 * S1-03 DevicePort over the simulated UART byte stream
 * (docs/stages/stage-01-task-cards.md:58-65, docs/architecture/uart-v1.md).
 *
 * This file owns the host side of the UART v1 link:
 *   - one reader thread per open device, fed by locker_device_transport_read();
 *   - a request table that matches replies by (session, request_no, type) and
 *     hands exactly one locker_device_reply_t to the bound sink;
 *   - DEVICE_EVENT frames delivered to sink->event only while a session is live
 *     (uart-v1.md:356).
 *
 * It holds no business state: sessions, watermarks and action numbers are the
 * device's, and this adapter only mirrors what the device reported so it can
 * populate the next request. No retry loop ever re-sends EXECUTE (uart-v1.md:382).
 *
 * Stage 1 had no physical UART and refused config->simulated == false. S2-06
 * drives the real board UART through the transport's /dev/ backend; the path
 * selects the backend, so a "real" configuration must name a device node and is
 * rejected if it still points at the simulator socket.
 */

#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "locker/common/clock.h"
#include "locker/common/uuid.h"
#include "locker/device/codec.h"
#include "locker/device/recovery.h"
#include "locker/device/transport.h"
#include "locker/device/wire.h"
#include "locker_contracts.h"

#define LOCKER_DEVICE_PENDING_MAX 16u
#define LOCKER_DEVICE_CONNECT_TIMEOUT_MS 5000u
#define LOCKER_DEVICE_READ_TIMEOUT_MS 200
#define LOCKER_DEVICE_PENDING_TIMEOUT_MS 5000u
/* CR-S206-1: SUPPLY_ADC (bit 3) is deliberately not required yet. The MCU cannot
 * advertise it honestly while PC0 calibration is deferred and supply_valid stays
 * false (S2-02 debt, due before the S2-08 foundation gate). Restore 0x1f here in
 * the same change that lands the calibration; requiring a capability the device
 * does not have would only push every real cabinet into BAD_PRECONDITION. */
#define LOCKER_DEVICE_REQUIRED_CAPS 0x17u

typedef struct pending_request {
    bool used;
    uint32_t request_no;
    uint8_t request_type;
    locker_uuid_t session_id;
    uint64_t deadline_ms;
    uint64_t started_host_mono_ms;
} pending_request_t;

typedef struct transmit_frame {
    uint8_t bytes[LOCKER_WIRE_MAX_FRAME];
    size_t length;
    uint32_t request_no;
} transmit_frame_t;

struct locker_device {
    bool created;
    bool opened;
    bool sink_bound;
    bool closed;
    locker_device_config_t config;
    locker_device_sink_t sink;
    locker_device_transport_t transport;
    locker_wire_stream_t stream;

    pthread_t reader_thread;
    bool reader_started;
    bool reader_stop;

    pthread_mutex_t lock;
    pthread_mutex_t write_lock; /* lifecycle/shutdown only; never held during IO */
    pthread_cond_t writer_ready;
    pthread_t writer_thread;
    bool writer_started;
    bool writer_stop;
    transmit_frame_t transmit[LOCKER_DEVICE_PENDING_MAX];
    size_t transmit_head;
    size_t transmit_count;

    pending_request_t pending[LOCKER_DEVICE_PENDING_MAX];
    uint32_t next_request_no;
    uint32_t expired_request_floor;

    /* Mirror of the last HELLO answer; never authoritative, only used to fill
     * the next request's expected values (uart-v1.md:360-378). */
    locker_uuid_t session_id;
    locker_uuid_t journal_epoch;
    locker_uuid_t pending_session_id;
    locker_uuid_t host_instance_id;
    uint64_t boot_id;
    uint64_t physical_revision;
    uint64_t action_watermark;
    uint8_t device_mode;
    bool hello_complete;
    bool reconcile_required;
    bool request_numbers_exhausted;
    /* A pulled UART wire is invisible to the tty, so the only evidence of a dead
     * link is a run of unanswered requests. Three consecutive expiries mark the
     * link lost; any matched reply clears the streak. */
    bool link_lost;
    uint32_t expired_streak;
    /* Liveness of the two workers: if either dies the adapter keeps accepting
     * frames but nothing expires or answers them, so "silent" would otherwise be
     * indistinguishable from "healthy". */
    bool reader_alive;
    bool writer_alive;
};

/* Monotonic milliseconds from the injected clock; 0 when the port is unusable
 * (the contract's monotonic callback has no error channel, see clock.h). */
static uint64_t device_now_ms(const locker_device_t *device)
{
    uint64_t now_ms = 0u;

    if (!locker_clock_port_is_valid(&device->config.clock)) {
        return 0u;
    }
    if (locker_clock_port_monotonic_ms(&device->config.clock, &now_ms) != LOCKER_STATUS_OK) {
        return 0u;
    }
    return now_ms;
}

static void pending_clear(locker_device_t *device)
{
    memset(device->pending, 0, sizeof(device->pending));
}

/* A timeout makes the outcome unknown, never retries an action. Expired slots
 * are reclaimed; the same session must still use a new request number.
 *
 * Deadlines run on the injected clock (config.clock), exactly like every other
 * timestamp this port produces: production injects the real monotonic clock and
 * tests inject a controllable one, so the public API never exposes a way to move
 * time and a timeout test never has to sleep for five real seconds. */
static void pending_expire(locker_device_t *device)
{
    size_t index;
    uint64_t now = device_now_ms(device);
    for (index = 0u; index < LOCKER_DEVICE_PENDING_MAX; ++index) {
        if (device->pending[index].used && now >= device->pending[index].deadline_ms) {
            if (device->pending[index].request_no > device->expired_request_floor) {
                device->expired_request_floor = device->pending[index].request_no;
            }
            device->pending[index].used = false;
            ++device->expired_streak;
            if (device->expired_streak >= 3u) {
                device->link_lost = true;
            }
        }
    }
}

/* Caller holds device->lock. Foreign responses cannot consume a live slot. */
static bool pending_take(locker_device_t *device, uint32_t request_no, uint8_t request_type,
                         const locker_uuid_t *session_id, pending_request_t *out)
{
    size_t index;
    for (index = 0u; index < LOCKER_DEVICE_PENDING_MAX; ++index) {
        pending_request_t *entry = &device->pending[index];
        if (entry->used && entry->request_no == request_no &&
            entry->request_type == request_type &&
            locker_uuid_equal(&entry->session_id, session_id)) {
            *out = *entry;
            entry->used = false;
            return true;
        }
    }
    return false;
}

/* Uniqueness and reservation occur under the same lock for every request type. */
static bool pending_add(locker_device_t *device, uint32_t request_no, uint8_t request_type,
                        const locker_uuid_t *session_id, uint64_t started_ms)
{
    size_t index;
    pending_request_t *available = NULL;
    pending_expire(device);
    if (request_no <= device->expired_request_floor) {
        return false;
    }
    for (index = 0u; index < LOCKER_DEVICE_PENDING_MAX; ++index) {
        pending_request_t *entry = &device->pending[index];
        if (entry->used && entry->request_no == request_no) {
            return false;
        }
        if (!entry->used && available == NULL) {
            available = entry;
        }
    }
    if (available == NULL) {
        return false;
    }
    available->used = true;
    available->request_no = request_no;
    available->request_type = request_type;
    available->session_id = *session_id;
    available->started_host_mono_ms = started_ms;
    available->deadline_ms = device_now_ms(device) + LOCKER_DEVICE_PENDING_TIMEOUT_MS;
    return true;
}

/* No wrap in one session: a fresh HELLO is required after UINT32_MAX. */
static uint32_t pending_next_request_no(locker_device_t *device)
{
    if (device->request_numbers_exhausted || device->next_request_no == UINT32_MAX) {
        device->request_numbers_exhausted = true;
        return 0u;
    }
    return ++device->next_request_no;
}

static bool device_identity_matches(const locker_device_t *device,
                                     const locker_wire_hello_response_t *hello)
{
    static const char hex[] = "0123456789abcdef";
    char id[25];
    size_t index;
    for (index = 0u; index < sizeof(hello->device_uid); ++index) {
        id[index * 2u] = hex[hello->device_uid[index] >> 4];
        id[index * 2u + 1u] = hex[hello->device_uid[index] & 15u];
    }
    id[24] = '\0';
    return strcmp(id, device->config.expected_device_id) == 0 &&
           (hello->capabilities & LOCKER_DEVICE_REQUIRED_CAPS) == LOCKER_DEVICE_REQUIRED_CAPS;
}

/* Sink rejection requires an explicit close/open and recovery from the journal.
 * Stop new writes immediately; facts remain available via FETCH after reconnect. */
static locker_status_t device_sink_result(locker_device_t *device, locker_status_t status)
{
    if (status != LOCKER_STATUS_OK) {
        pthread_mutex_lock(&device->write_lock);
        pthread_mutex_lock(&device->lock);
        device->reconcile_required = true;
        device->reader_stop = true;
        device->writer_stop = true;
        device->hello_complete = false;
        device->transmit_count = 0u;
        pending_clear(device);
        pthread_cond_signal(&device->writer_ready);
        pthread_mutex_unlock(&device->lock);
        locker_device_transport_shutdown(&device->transport);
        pthread_mutex_unlock(&device->write_lock);
    }
    return status;
}

static void reply_reset(locker_device_reply_t *reply)
{
    memset(reply, 0, sizeof(*reply));
}

static void copy_uuid(locker_uuid_t *destination, const locker_uuid_t *source)
{
    memcpy(destination->bytes, source->bytes, sizeof(destination->bytes));
}

static void apply_hello(locker_device_t *device, const locker_wire_hello_response_t *body)
{
    device->journal_epoch = body->journal_epoch;
    device->boot_id = body->boot_id;
    device->action_watermark = body->action_watermark;
    device->device_mode = body->device_mode;
    device->session_id = device->pending_session_id;
    device->hello_complete = true;
    device->physical_revision = 0u;
}

static void apply_snapshot(locker_device_t *device,
                           const locker_wire_get_snapshot_response_t *body)
{
    device->journal_epoch = body->journal_epoch;
    device->boot_id = body->boot_id;
    device->physical_revision = body->physical_revision;
    device->action_watermark = body->action_watermark;
    device->device_mode = body->device_mode;
    device->session_id = body->session_id;
}

static void apply_execute_or_query(locker_device_t *device, uint64_t action_no,
                                   uint64_t physical_revision)
{
    if (action_no > device->action_watermark) {
        device->action_watermark = action_no;
    }
    if (physical_revision != 0u) {
        device->physical_revision = physical_revision;
    }
}

static void apply_resume(locker_device_t *device,
                         const locker_wire_resume_ready_response_t *body)
{
    /* The device answers RESUME with a wire result code. Dropping it made a
       refused resume look identical to an accepted one: mode stayed 0 and the
       only visible symptom was "still RECONCILING" (S2-07 bench). */
    if (getenv("LOCKER_DEVICE_TRACE") != NULL) {
        fprintf(stderr, "resume reply result=%u mode=%u revision=%llu\n",
                (unsigned)body->result, (unsigned)body->device_mode,
                (unsigned long long)body->physical_revision);
        fflush(stderr);
    }
    if (body->physical_revision != 0u) {
        device->physical_revision = body->physical_revision;
    }
    device->device_mode = body->device_mode;
}

static void apply_disarm(locker_device_t *device, const locker_wire_disarm_response_t *body)
{
    if (body->physical_revision != 0u) {
        device->physical_revision = body->physical_revision;
    }
    device->device_mode = body->device_mode;
}

static void apply_release(locker_device_t *device,
                          const locker_wire_release_context_response_t *body)
{
    if (body->physical_revision != 0u) {
        device->physical_revision = body->physical_revision;
    }
}

static void apply_health(locker_device_t *device,
                         const locker_wire_host_health_response_t *body)
{
    device->device_mode = body->device_mode;
}

/* Fill the adapter's mirror from any successful response. Caller holds lock. */
static void apply_response(locker_device_t *device, uint8_t request_type,
                           const locker_wire_response_union_t *response)
{
    switch (request_type) {
    case 1u:
        apply_hello(device, &response->hello);
        break;
    case 2u:
        apply_snapshot(device, &response->get_snapshot);
        break;
    case 3u:
        device->journal_epoch = response->fetch_events.journal_epoch;
        break;
    case 4u:
        /* ACK only acknowledges a watermark; no mirror field changes. */
        break;
    case 5u:
        apply_resume(device, &response->resume_ready);
        break;
    case 6u:
        apply_disarm(device, &response->disarm);
        break;
    case 7u:
        apply_execute_or_query(device, response->execute_unlock.action_no,
                               response->execute_unlock.physical_revision);
        break;
    case 8u:
        /* Historical commands are queryable across sessions/boots, but their
         * action number and observation cannot replace the current mirror. */
        if (locker_uuid_equal(&response->query_command.command_session_id, &device->session_id) &&
            response->query_command.command_boot_id == device->boot_id) {
            apply_execute_or_query(device, response->query_command.action_no,
                                   response->query_command.physical_revision);
        }
        break;
    case 9u:
        apply_health(device, &response->host_health);
        break;
    case 10u:
        break;
    case 11u:
        apply_release(device, &response->release_context);
        break;
    default:
        break;
    }
}

static locker_status_t device_on_frame(void *user, const locker_wire_frame_t *frame)
{
    locker_device_t *device = (locker_device_t *)user;
    locker_device_reply_t reply;
    locker_device_event_t event;
    locker_wire_response_union_t response;
    locker_wire_event96_t record;
    locker_uuid_t event_session;
    locker_uuid_t event_epoch;
    pending_request_t pending;
    uint16_t decoded;
    bool matched;
    uint8_t request_type;

    if (frame->type == LOCKER_WIRE_DEVICE_EVENT) {
        decoded = locker_wire_decode_device_event(frame, &event_session, &event_epoch, &record);
        if (decoded != LOCKER_WIRE_RESULT_OK) {
            return LOCKER_STATUS_OK;
        }
        pthread_mutex_lock(&device->lock);
        /* Events for another session are stale and must not be applied. */
        if (!device->hello_complete || device->reconcile_required ||
            !locker_uuid_equal(&event_session, &device->session_id) ||
            !locker_uuid_equal(&event_epoch, &device->journal_epoch)) {
            pthread_mutex_unlock(&device->lock);
            return LOCKER_STATUS_OK;
        }
        memset(&event, 0, sizeof(event));
        snprintf(event.key.device_id, sizeof(event.key.device_id), "%s",
                 device->config.expected_device_id);
        copy_uuid(&event.key.journal_epoch, &event_epoch);
        event.key.boot_id = record.boot_id;
        event.key.event_seq = record.event_seq;
        event.mono_ms = record.mono_ms;
        event.physical_revision = record.physical_revision;
        event.detail = record.detail;
        event.event_type = record.event_type;
        event.reason = record.reason;
        event.flags = record.flags;
        copy_uuid(&event.operation_id, &record.operation_id);
        copy_uuid(&event.command_id, &record.command_id);
        event.door = (locker_door_state_t)record.door;
        event.item = (locker_item_state_t)record.item;
        event.supply_mv = record.supply_mv;
        event.card_uid_len = record.card_uid_len;
        memcpy(event.card_uid, record.card_uid, sizeof(event.card_uid));
        event.live_transport = true;
        pthread_mutex_unlock(&device->lock);
        if (device->sink_bound && device->sink.event != NULL) {
            return device_sink_result(device, device->sink.event(device->sink.user, &event));
        }
        return LOCKER_STATUS_OK;
    }

    if ((frame->type & LOCKER_WIRE_RESPONSE_FLAG) == 0u) {
        /* Device-to-host frames are responses or events only. */
        return LOCKER_STATUS_OK;
    }
    request_type = (uint8_t)(frame->type & ~LOCKER_WIRE_RESPONSE_FLAG);

    reply_reset(&reply);
    decoded = locker_wire_decode_response(frame, &response);
    if (decoded != LOCKER_WIRE_RESULT_OK) {
        return LOCKER_STATUS_OK;
    }

    pthread_mutex_lock(&device->lock);
    pending_expire(device);
    matched = !device->reconcile_required &&
              pending_take(device, frame->request_no, request_type, &response.hello.session_id, &pending);
    if (matched) {
        device->expired_streak = 0u;
    }
    if (matched && request_type == 1u && response.hello.result == 0u &&
        !device_identity_matches(device, &response.hello)) {
        locker_uuid_t response_session = response.hello.session_id;
        memset(&response, 0, sizeof(response));
        response.hello.session_id = response_session;
        response.hello.result = LOCKER_WIRE_RESULT_BAD_PRECONDITION;
    }
    if (matched && response.hello.result == 0u) {
        apply_response(device, request_type, &response);
    } else if (matched && getenv("LOCKER_DEVICE_TRACE") != NULL) {
        /* A refused reply used to vanish here: the slot was taken (streak stayed
           0) but apply_response was skipped, so a device-side check failure was
           indistinguishable from silence (S2-07 bench, mode stuck at 0). */
        fprintf(stderr, "reply refused type=%u result=%u request_no=%u\n",
                (unsigned)request_type, (unsigned)response.hello.result,
                (unsigned)frame->request_no);
        fflush(stderr);
    }
    pthread_mutex_unlock(&device->lock);
    if (!matched) {
        return LOCKER_STATUS_OK;
    }

    reply.request_no = frame->request_no;
    /* The reply reports the decoded wire response type, so it carries the
     * response flag the frame did (uart-v1.md:9-17); the request type alone
     * would collide with a request type number. */
    reply.message_type = (uint8_t)(request_type | LOCKER_WIRE_RESPONSE_FLAG);
    reply.request_started_host_mono_ms = pending.started_host_mono_ms;
    reply.received_host_mono_ms = device_now_ms(device);
    /* reply.body is the contract's own anonymous union of the same members, so
     * the decoded response is copied member-wise by size (never a struct on wire). */
    memcpy(&reply.body, &response, sizeof(reply.body));
    if (device->sink_bound && device->sink.reply != NULL) {
        locker_status_t sink_status = device->sink.reply(device->sink.user, &reply);
        if (sink_status != LOCKER_STATUS_OK) {
            /* Any non-OK sink result latches reconcile_required and tears the
             * session down, which the supervisor then reports as
             * "session not established; reopening". Without this line the whole
             * flap looks like a link problem; with it the offending reply type
             * and status are visible. */
            static int budget = 16;
            if (budget > 0 && getenv("LOCKER_DEVICE_TRACE") != NULL) {
                --budget;
                fprintf(stderr, "sink rejected type=%u status=%d request_no=%u\n",
                        (unsigned)request_type, (int)sink_status, (unsigned)frame->request_no);
                fflush(stderr);
            }
        }
        return device_sink_result(device, sink_status);
    }
    return LOCKER_STATUS_OK;
}

static void *device_reader_main(void *argument)
{
    locker_device_t *device = (locker_device_t *)argument;
    uint8_t buffer[256];
    int received;
    bool stop;

    pthread_mutex_lock(&device->lock);
    device->reader_alive = true;
    pthread_mutex_unlock(&device->lock);
    for (;;) {
        pthread_mutex_lock(&device->lock);
        stop = device->reader_stop;
        pthread_mutex_unlock(&device->lock);
        if (stop) {
            break;
        }
        received = locker_device_transport_read(&device->transport, buffer, sizeof(buffer),
                                               LOCKER_DEVICE_READ_TIMEOUT_MS);
        if (received < 0) {
            (void)device_sink_result(device, LOCKER_STATUS_IO_ERROR);
            break;
        }
        /* Expire on every pass: a floating RX line keeps returning noise bytes,
         * so "received == 0" is not a reliable clock for detecting silence. */
        pthread_mutex_lock(&device->lock);
        pending_expire(device);
        pthread_mutex_unlock(&device->lock);
        if (received == 0) {
            continue;
        }
        if (locker_wire_stream_feed(&device->stream, buffer, (size_t)received) !=
            LOCKER_STATUS_OK) {
            break;
        }
    }
    pthread_mutex_lock(&device->lock);
    device->reader_alive = false;
    pthread_mutex_unlock(&device->lock);
    return NULL;
}

/* Only this worker writes bytes. Public submissions copy a complete frame into
 * a bounded FIFO and never wait for socket readiness. Shutdown wakes a blocked
 * write; uncertain or partially written frames are never retried/replayed. */
static void *device_writer_main(void *argument)
{
    locker_device_t *device = (locker_device_t *)argument;
    pthread_mutex_lock(&device->lock);
    device->writer_alive = true;
    pthread_mutex_unlock(&device->lock);
    for (;;) {
        transmit_frame_t frame;
        bool pending = false;
        size_t index;
        pthread_mutex_lock(&device->lock);
        while (!device->writer_stop && device->transmit_count == 0u) {
            pthread_cond_wait(&device->writer_ready, &device->lock);
        }
        if (device->writer_stop) {
            pthread_mutex_unlock(&device->lock);
            break;
        }
        frame = device->transmit[device->transmit_head];
        device->transmit_head = (device->transmit_head + 1u) % LOCKER_DEVICE_PENDING_MAX;
        --device->transmit_count;
        pending_expire(device);
        for (index = 0u; index < LOCKER_DEVICE_PENDING_MAX; ++index) {
            if (device->pending[index].used &&
                device->pending[index].request_no == frame.request_no) {
                device->pending[index].started_host_mono_ms = device_now_ms(device);
                pending = true;
                break;
            }
        }
        pthread_mutex_unlock(&device->lock);
        if (!pending) {
            continue; /* expired before any byte was sent; never replay it */
        }
        if (locker_device_transport_write(&device->transport,
                                           frame.bytes, frame.length) != LOCKER_STATUS_OK) {
            (void)device_sink_result(device, LOCKER_STATUS_IO_ERROR);
            /* A dead writer must be visible as a lost link, otherwise every later
             * request is queued for ever and the cabinet looks silently healthy. */
            pthread_mutex_lock(&device->lock);
            device->link_lost = true;
            pthread_mutex_unlock(&device->lock);
            break;
        }
    }
    pthread_mutex_lock(&device->lock);
    device->writer_alive = false;
    pthread_mutex_unlock(&device->lock);
    return NULL;
}

static locker_status_t device_send_request(locker_device_t *device, uint8_t type,
                                           uint32_t request_no,
                                           const locker_wire_request_union_t *request)
{
    transmit_frame_t frame;
    locker_status_t status;
    const locker_uuid_t *session = type == 1u ? &request->hello.new_session_id :
                                                   &request->hello.session_id;
    memset(&frame, 0, sizeof(frame));
    pthread_mutex_lock(&device->lock);
    if (!device->opened || device->writer_stop || device->reconcile_required ||
        (type != 1u && (!device->hello_complete ||
         !locker_uuid_equal(session, &device->session_id)))) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_NOT_READY;
    }
    if (device->transmit_count == LOCKER_DEVICE_PENDING_MAX) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_BUSY;
    }
    if (request_no == 0u) {
        request_no = pending_next_request_no(device);
    }
    if (request_no == 0u || device->request_numbers_exhausted) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_NOT_READY;
    }
    status = locker_wire_encode_request(type, request_no, request, frame.bytes,
                                        sizeof(frame.bytes), &frame.length);
    if (status != LOCKER_STATUS_OK ||
        !pending_add(device, request_no, type, session, device_now_ms(device))) {
        pthread_mutex_unlock(&device->lock);
        return status == LOCKER_STATUS_OK ? LOCKER_STATUS_BUSY : LOCKER_STATUS_INVALID;
    }
    if (request_no > device->next_request_no) {
        device->next_request_no = request_no;
    }
    if (request_no == UINT32_MAX) {
        device->request_numbers_exhausted = true;
    }
    frame.request_no = request_no;
    device->transmit[(device->transmit_head + device->transmit_count) %
                     LOCKER_DEVICE_PENDING_MAX] = frame;
    ++device->transmit_count;
    pthread_cond_signal(&device->writer_ready);
    pthread_mutex_unlock(&device->lock);
    return LOCKER_STATUS_OK; /* admitted locally, not an MCU acknowledgement */
}

/* Caller must have validated that the adapter is open and HELLO completed. */
static locker_status_t device_send_with_new_request_no(locker_device_t *device, uint8_t type,
                                                       const locker_wire_request_union_t *request)
{
    return device_send_request(device, type, 0u, request);
}

locker_status_t locker_device_create(locker_device_t **out)
{
    locker_device_t *device;

    if (out == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    *out = NULL;
    device = (locker_device_t *)calloc(1u, sizeof(*device));
    if (device == NULL) {
        return LOCKER_STATUS_NO_SPACE;
    }
    if (pthread_mutex_init(&device->lock, NULL) != 0) {
        free(device);
        return LOCKER_STATUS_IO_ERROR;
    }
    if (pthread_mutex_init(&device->write_lock, NULL) != 0) {
        pthread_mutex_destroy(&device->lock);
        free(device);
        return LOCKER_STATUS_IO_ERROR;
    }
    if (pthread_cond_init(&device->writer_ready, NULL) != 0) {
        pthread_mutex_destroy(&device->write_lock);
        pthread_mutex_destroy(&device->lock);
        free(device);
        return LOCKER_STATUS_IO_ERROR;
    }
    locker_device_transport_init(&device->transport);
    locker_wire_stream_init(&device->stream, device, device_on_frame);
    device->created = true;
    *out = device;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_device_bind_sink(locker_device_t *device, const locker_device_sink_t *sink)
{
    if (device == NULL || !device->created || sink == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (device->opened) {
        return LOCKER_STATUS_CONFLICT;
    }
    device->sink = *sink;
    device->sink_bound = true;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_device_open(locker_device_t *device, const locker_device_config_t *config)
{
    locker_wire_request_union_t hello;
    locker_status_t status;
    uint32_t hello_request_no;

    if (device == NULL || !device->created || config == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (device->opened) {
        return LOCKER_STATUS_CONFLICT;
    }
    if (!device->sink_bound) {
        return LOCKER_STATUS_NOT_READY;
    }
    if (!locker_clock_port_is_valid(&config->clock)) {
        return LOCKER_STATUS_INVALID;
    }
    if (config->path[0] == '\0' ||
        memchr(config->path, '\0', sizeof(config->path)) == NULL ||
        config->expected_device_id[24] != '\0' ||
        strspn(config->expected_device_id, "0123456789abcdef") != 24u) {
        return LOCKER_STATUS_INVALID;
    }
    if (!config->simulated && strncmp(config->path, "/dev/", 5u) != 0) {
        return LOCKER_STATUS_INVALID;
    }

    device->config = *config;
    status = locker_device_transport_connect(&device->transport, config->path,
                                            LOCKER_DEVICE_CONNECT_TIMEOUT_MS);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    pthread_mutex_lock(&device->lock);
    pending_clear(device);
    device->next_request_no = 0u;
    device->expired_request_floor = 0u;
    device->request_numbers_exhausted = false;
    device->reconcile_required = false;
    device->link_lost = false;
    device->expired_streak = 0u;
    device->reader_alive = false;
    device->writer_alive = false;
    device->hello_complete = false;
    device->device_mode = (uint8_t)LOCKER_DEVICE_MODE_RECONCILING;
    device->reader_stop = false;
    device->writer_stop = false;
    device->transmit_head = 0u;
    device->transmit_count = 0u;
    pthread_mutex_unlock(&device->lock);

    if (locker_clock_port_new_uuid(&config->clock, &device->pending_session_id) !=
        LOCKER_STATUS_OK) {
        locker_device_transport_close(&device->transport);
        return LOCKER_STATUS_IO_ERROR;
    }
    if (locker_uuid_is_nil(&device->pending_session_id)) {
        locker_device_transport_close(&device->transport);
        return LOCKER_STATUS_IO_ERROR;
    }
    copy_uuid(&device->host_instance_id, &config->host_instance_id);

    if (pthread_create(&device->reader_thread, NULL, device_reader_main, device) != 0) {
        locker_device_transport_close(&device->transport);
        return LOCKER_STATUS_IO_ERROR;
    }
    device->reader_started = true;

    pthread_mutex_lock(&device->lock);
    device->opened = true;
    pthread_mutex_unlock(&device->lock);
    if (pthread_create(&device->writer_thread, NULL, device_writer_main, device) != 0) {
        (void)locker_device_close(device);
        return LOCKER_STATUS_IO_ERROR;
    }
    device->writer_started = true;

    /* HELLO starts a new session and revokes the old one (uart-v1.md:360). It is
     * the one request sent before a session exists, so it allocates its own
     * request number instead of going through device_send_with_new_request_no(). */
    memset(&hello, 0, sizeof(hello));
    copy_uuid(&hello.hello.new_session_id, &device->pending_session_id);
    copy_uuid(&hello.hello.host_instance_id, &config->host_instance_id);
    hello.hello.requested_version = LOCKER_WIRE_VERSION;
    pthread_mutex_lock(&device->lock);
    hello_request_no = pending_next_request_no(device);
    pthread_mutex_unlock(&device->lock);
    if (hello_request_no == 0u) {
        (void)locker_device_close(device);
        return LOCKER_STATUS_BUSY;
    }
    status = device_send_request(device, 1u, hello_request_no, &hello);
    if (status != LOCKER_STATUS_OK) {
        (void)locker_device_close(device);
        return status;
    }
    return LOCKER_STATUS_OK;
}

locker_status_t locker_device_get_snapshot(locker_device_t *device, uint32_t request_no)
{
    locker_wire_request_union_t request;

    if (device == NULL || !device->created || request_no == 0u) {
        return LOCKER_STATUS_INVALID;
    }
    memset(&request, 0, sizeof(request));
    pthread_mutex_lock(&device->lock);
    if (!device->opened || !device->hello_complete || device->reconcile_required) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_NOT_READY;
    }
    copy_uuid(&request.get_snapshot.session_id, &device->session_id);
    pthread_mutex_unlock(&device->lock);
    return device_send_request(device, 2u, request_no, &request);
}

locker_status_t locker_device_execute_once(locker_device_t *device,
                                           const locker_unlock_intent_t *intent)
{
    locker_wire_request_union_t request;

    if (device == NULL || !device->created || intent == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (intent->slot_id != 1u) {
        return LOCKER_STATUS_INVALID;
    }
    if (intent->kind != LOCKER_OPERATION_KIND_BORROW &&
        intent->kind != LOCKER_OPERATION_KIND_RETURN) {
        return LOCKER_STATUS_INVALID;
    }
    memset(&request, 0, sizeof(request));
    pthread_mutex_lock(&device->lock);
    if (!device->opened || !device->hello_complete || device->reconcile_required) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_NOT_READY;
    }
    if (!locker_uuid_equal(&intent->session_id, &device->session_id) ||
        !locker_uuid_equal(&intent->journal_epoch, &device->journal_epoch) ||
        intent->expected_boot_id != device->boot_id ||
        intent->expected_physical_revision != device->physical_revision) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_CONFLICT;
    }
    copy_uuid(&request.execute_unlock.session_id, &device->session_id);
    pthread_mutex_unlock(&device->lock);

    copy_uuid(&request.execute_unlock.journal_epoch, &intent->journal_epoch);
    request.execute_unlock.expected_boot_id = intent->expected_boot_id;
    request.execute_unlock.expected_physical_revision = intent->expected_physical_revision;
    request.execute_unlock.action_no = intent->action_no;
    request.execute_unlock.not_after_mcu_ms = intent->not_after_mcu_ms;
    copy_uuid(&request.execute_unlock.operation_id, &intent->operation_id);
    copy_uuid(&request.execute_unlock.command_id, &intent->command_id);
    request.execute_unlock.slot_id = intent->slot_id;
    request.execute_unlock.kind = (uint8_t)intent->kind;
    return device_send_with_new_request_no(device, 7u, &request);
}

locker_status_t locker_device_query_command(locker_device_t *device,
                                            const locker_uuid_t *command_id)
{
    locker_wire_request_union_t request;

    if (device == NULL || !device->created || command_id == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    memset(&request, 0, sizeof(request));
    pthread_mutex_lock(&device->lock);
    if (!device->opened || !device->hello_complete || device->reconcile_required) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_NOT_READY;
    }
    copy_uuid(&request.query_command.session_id, &device->session_id);
    copy_uuid(&request.query_command.journal_epoch, &device->journal_epoch);
    pthread_mutex_unlock(&device->lock);
    copy_uuid(&request.query_command.command_id, command_id);
    return device_send_with_new_request_no(device, 8u, &request);
}

/* FETCH alone accepts an entirely empty origin. ACK and ready guards always
 * name the configured device and epoch, even when their cursor is (0,0). */
static bool device_cursor_matches(const locker_device_t *device,
                                   const locker_event_key_t *key, bool allow_origin)
{
    if ((key->boot_id == 0u) != (key->event_seq == 0u)) {
        return false;
    }
    if (allow_origin && key->boot_id == 0u && key->device_id[0] == '\0' &&
        locker_uuid_is_nil(&key->journal_epoch)) {
        return true;
    }
    return memchr(key->device_id, '\0', sizeof(key->device_id)) != NULL &&
           strcmp(key->device_id, device->config.expected_device_id) == 0 &&
           locker_uuid_equal(&key->journal_epoch, &device->journal_epoch);
}

locker_status_t locker_device_fetch_events(locker_device_t *device,
                                           const locker_event_key_t *after, uint8_t limit)
{
    locker_wire_request_union_t request;

    if (device == NULL || !device->created || after == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (limit == 0u || limit > 4u) {
        return LOCKER_STATUS_INVALID;
    }
    memset(&request, 0, sizeof(request));
    pthread_mutex_lock(&device->lock);
    if (!device->opened || !device->hello_complete || device->reconcile_required) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_NOT_READY;
    }
    if (!device_cursor_matches(device, after, true)) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_CONFLICT;
    }
    copy_uuid(&request.fetch_events.session_id, &device->session_id);
    copy_uuid(&request.fetch_events.journal_epoch, &device->journal_epoch);
    pthread_mutex_unlock(&device->lock);
    request.fetch_events.after.boot_id = after->boot_id;
    request.fetch_events.after.event_seq = after->event_seq;
    request.fetch_events.max_count = limit;
    return device_send_with_new_request_no(device, 3u, &request);
}

locker_status_t locker_device_ack_persisted(locker_device_t *device,
                                            const locker_event_key_t *cursor)
{
    locker_wire_request_union_t request;

    if (device == NULL || !device->created || cursor == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    memset(&request, 0, sizeof(request));
    pthread_mutex_lock(&device->lock);
    if (!device->opened || !device->hello_complete || device->reconcile_required) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_NOT_READY;
    }
    if (!device_cursor_matches(device, cursor, false)) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_CONFLICT;
    }
    copy_uuid(&request.ack_persisted.session_id, &device->session_id);
    copy_uuid(&request.ack_persisted.journal_epoch, &device->journal_epoch);
    pthread_mutex_unlock(&device->lock);
    request.ack_persisted.through.boot_id = cursor->boot_id;
    request.ack_persisted.through.event_seq = cursor->event_seq;
    return device_send_with_new_request_no(device, 4u, &request);
}

locker_status_t locker_device_resume_ready(locker_device_t *device,
                                           const locker_ready_guard_t *guard)
{
    locker_wire_request_union_t request;

    if (device == NULL || !device->created || guard == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    memset(&request, 0, sizeof(request));
    pthread_mutex_lock(&device->lock);
    if (!device->opened || !device->hello_complete || device->reconcile_required) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_NOT_READY;
    }
    if (!locker_uuid_equal(&guard->session_id, &device->session_id) ||
        !locker_uuid_equal(&guard->journal_epoch, &device->journal_epoch) ||
        guard->expected_boot_id != device->boot_id ||
        guard->expected_physical_revision != device->physical_revision) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_CONFLICT;
    }
    if (!device_cursor_matches(device, &guard->persisted_through, false)) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_CONFLICT;
    }
    copy_uuid(&request.resume_ready.session_id, &device->session_id);
    pthread_mutex_unlock(&device->lock);
    copy_uuid(&request.resume_ready.journal_epoch, &guard->journal_epoch);
    request.resume_ready.expected_boot_id = guard->expected_boot_id;
    request.resume_ready.expected_physical_revision = guard->expected_physical_revision;
    request.resume_ready.persisted_through.boot_id = guard->persisted_through.boot_id;
    request.resume_ready.persisted_through.event_seq = guard->persisted_through.event_seq;
    return device_send_with_new_request_no(device, 5u, &request);
}

locker_status_t locker_device_release_context(locker_device_t *device,
                                              const locker_context_guard_t *guard)
{
    locker_wire_request_union_t request;

    if (device == NULL || !device->created || guard == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    memset(&request, 0, sizeof(request));
    pthread_mutex_lock(&device->lock);
    if (!device->opened || !device->hello_complete || device->reconcile_required) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_NOT_READY;
    }
    if (!locker_uuid_equal(&guard->session_id, &device->session_id) ||
        !locker_uuid_equal(&guard->journal_epoch, &device->journal_epoch) ||
        guard->expected_boot_id != device->boot_id ||
        guard->expected_physical_revision != device->physical_revision) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_CONFLICT;
    }
    copy_uuid(&request.release_context.session_id, &device->session_id);
    pthread_mutex_unlock(&device->lock);
    copy_uuid(&request.release_context.journal_epoch, &guard->journal_epoch);
    request.release_context.expected_boot_id = guard->expected_boot_id;
    request.release_context.expected_physical_revision = guard->expected_physical_revision;
    copy_uuid(&request.release_context.operation_id, &guard->operation_id);
    copy_uuid(&request.release_context.command_id, &guard->command_id);
    return device_send_with_new_request_no(device, 11u, &request);
}

locker_status_t locker_device_send_health(locker_device_t *device,
                                          const locker_health_report_t *report)
{
    locker_wire_request_union_t request;

    if (device == NULL || !device->created || report == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    memset(&request, 0, sizeof(request));
    pthread_mutex_lock(&device->lock);
    if (!device->opened || !device->hello_complete || device->reconcile_required) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_NOT_READY;
    }
    copy_uuid(&request.host_health.session_id, &device->session_id);
    pthread_mutex_unlock(&device->lock);
    copy_uuid(&request.host_health.host_instance_id, &report->host_instance_id);
    copy_uuid(&request.host_health.ui_instance_id, &report->ui_instance_id);
    request.host_health.core_progress = report->core_progress;
    request.host_health.ui_progress = report->ui_progress;
    request.host_health.core_observed_age_ms = report->core_age_ms;
    request.host_health.ui_observed_age_ms = report->ui_age_ms;
    request.host_health.supervisor_state = report->supervisor_state;
    return device_send_with_new_request_no(device, 9u, &request);
}

locker_status_t locker_device_time_anchor(locker_device_t *device, uint32_t request_no)
{
    locker_wire_request_union_t request;

    if (device == NULL || !device->created || request_no == 0u) {
        return LOCKER_STATUS_INVALID;
    }
    memset(&request, 0, sizeof(request));
    pthread_mutex_lock(&device->lock);
    if (!device->opened || !device->hello_complete || device->reconcile_required) {
        pthread_mutex_unlock(&device->lock);
        return LOCKER_STATUS_NOT_READY;
    }
    copy_uuid(&request.time_anchor.session_id, &device->session_id);
    pthread_mutex_unlock(&device->lock);
    request.time_anchor.anchor_id = request_no;
    request.time_anchor.host_monotonic_ms = device_now_ms(device);
    request.time_anchor.host_utc_ms = 0u;
    if (locker_clock_port_is_valid(&device->config.clock)) {
        (void)locker_clock_port_wall_utc_ms(&device->config.clock,
                                           &request.time_anchor.host_utc_ms);
    }
    return device_send_request(device, 10u, request_no, &request);
}

locker_status_t locker_device_close(locker_device_t *device)
{
    if (device == NULL || !device->created) {
        return LOCKER_STATUS_INVALID;
    }
    if (device->reader_started && pthread_equal(pthread_self(), device->reader_thread)) {
        (void)device_sink_result(device, LOCKER_STATUS_BUSY);
        return LOCKER_STATUS_BUSY;
    }
    pthread_mutex_lock(&device->lock);
    device->opened = false;
    device->reader_stop = true;
    device->writer_stop = true;
    device->hello_complete = false;
    device->transmit_count = 0u;
    pending_clear(device);
    pthread_cond_signal(&device->writer_ready);
    pthread_mutex_unlock(&device->lock);

    pthread_mutex_lock(&device->write_lock);
    /* fd remains stable until both IO owners have exited. shutdown may race
     * with read/send, but close never does. Do not hold write_lock while joining
     * a worker that may be propagating a failure through device_sink_result. */
    locker_device_transport_shutdown(&device->transport);
    pthread_mutex_unlock(&device->write_lock);
    if (device->writer_started) {
        (void)pthread_join(device->writer_thread, NULL);
        device->writer_started = false;
    }
    if (device->reader_started) {
        (void)pthread_join(device->reader_thread, NULL);
        device->reader_started = false;
    }
    pthread_mutex_lock(&device->write_lock);
    locker_device_transport_close(&device->transport);
    pthread_mutex_unlock(&device->write_lock);
    locker_wire_stream_reset(&device->stream);
    device->closed = true;
    return LOCKER_STATUS_OK;
}

void locker_device_destroy(locker_device_t *device)
{
    if (device == NULL) {
        return;
    }
    if (locker_device_close(device) != LOCKER_STATUS_OK) {
        return; /* A callback cannot free its own executing reader context. */
    }
    pthread_cond_destroy(&device->writer_ready);
    pthread_mutex_destroy(&device->write_lock);
    pthread_mutex_destroy(&device->lock);
    free(device);
}

bool locker_device_link_lost(const locker_device_t *device)
{
    return device != NULL && device->created && device->link_lost;
}

bool locker_device_session_ready(locker_device_t *device)
{
    bool ready;

    if (device == NULL || !device->created) {
        return false;
    }
    pthread_mutex_lock(&device->lock);
    ready = device->opened && device->hello_complete;
    pthread_mutex_unlock(&device->lock);
    return ready;
}

bool locker_device_diagnose(locker_device_t *device, locker_device_diagnosis_t *out)
{
    unsigned index;
    unsigned pending_used = 0u;

    if (device == NULL || !device->created || out == NULL) {
        return false;
    }
    pthread_mutex_lock(&device->lock);
    for (index = 0u; index < LOCKER_DEVICE_PENDING_MAX; ++index) {
        if (device->pending[index].used) {
            ++pending_used;
        }
    }
    out->opened = device->opened;
    out->hello_complete = device->hello_complete;
    out->link_lost = device->link_lost;
    out->writer_alive = device->writer_alive;
    out->reader_alive = device->reader_alive;
    out->expired_streak = device->expired_streak;
    out->pending_used = pending_used;
    out->transmit_used = device->transmit_count;
    pthread_mutex_unlock(&device->lock);
    return true;
}
