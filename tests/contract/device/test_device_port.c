/*
 * S1-03 contract test: the production DevicePort adapter (host/device/src/port.c)
 * over a real AF_UNIX SOCK_STREAM link to a real locker_sim_t.
 *
 * The daemon (locker-device-sim) is deliberately NOT linked here: this test owns
 * an in-process AF_UNIX server that accepts the adapter connection and drives
 * locker_sim_handle_frame()/locker_sim_pop_event(), so the exercised path is
 * port.c -> transport.c -> socket -> sim.c without duplicating sim_main.c.
 *
 * Concurrency: the adapter runs one reader thread, the server runs one thread.
 * All simulator access on the test thread goes through the server mutex, so the
 * reply callback provides the happens-before edge a race detector needs.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "../../unit/device/device_test_support.h"
#include "test_support.h"

#include "locker/common/clock.h"
#include "locker/device/codec.h"
#include "locker/device/wire.h"
#include "locker/device/transport.h"
#include "locker/simulator/sim.h"

#define PORT_SERVER_BUFFER (LOCKER_WIRE_MAX_FRAME * 4u)
#define PORT_SERVER_OUT (LOCKER_WIRE_MAX_FRAME * 4u)
#define PORT_WAIT_MS 4000
#define PORT_SILENCE_MS 250
/* Mirrors LOCKER_DEVICE_PENDING_TIMEOUT_MS in host/device/src/port.c, which is
 * module-private; the timeout is part of the port's observable behaviour. */
#define PORT_PENDING_TIMEOUT_MS 5000u

/* ------------------------------------------------------------------ */
/* In-process AF_UNIX server that owns the simulator                   */
/* ------------------------------------------------------------------ */

typedef struct port_server {
    int listen_fd;
    int client_fd;
    pthread_t thread;
    bool thread_started;
    char socket_path[LOCKER_SIM_PATH_SIZE];
    char journal_path[LOCKER_SIM_PATH_SIZE];
    locker_sim_t *sim;
    pthread_mutex_t lock; /* guards sim, fd fields and counters */
    unsigned frames;
    unsigned writes;
    bool hold_responses;
    bool pause_reads;
    bool reads_paused;
    bool stopping;
    unsigned hello_fault;
    uint32_t last_request_no;
    uint8_t held_reply[LOCKER_WIRE_MAX_FRAME];
    size_t held_reply_length;
} port_server_t;

static bool port_write_all(int fd, const uint8_t *data, size_t length)
{
    size_t written = 0u;

    while (written < length) {
        ssize_t result = send(fd, data + written, length - written, MSG_NOSIGNAL);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (result == 0) {
            return false;
        }
        written += (size_t)result;
    }
    return true;
}

/* Same framing rules as the daemon: <0 means "drop one byte and resync". */
static long port_frame_total(const uint8_t *data, size_t available)
{
    uint16_t payload_len;
    size_t total;

    if (available < 2u) {
        return 0;
    }
    if (data[0] != LOCKER_WIRE_MAGIC_FIRST || data[1] != LOCKER_WIRE_MAGIC_SECOND) {
        return -1;
    }
    if (available < LOCKER_WIRE_HEADER_SIZE) {
        return 0;
    }
    payload_len = (uint16_t)((uint16_t)data[4] | ((uint16_t)data[5] << 8));
    if (payload_len > LOCKER_WIRE_MAX_PAYLOAD) {
        return -1;
    }
    total = locker_wire_frame_size((size_t)payload_len);
    if (available < total) {
        return 0;
    }
    return (long)total;
}

static void port_server_consume(port_server_t *server, int client, uint8_t *buffer,
                                size_t *used)
{
    size_t offset = 0u;

    while (offset < *used) {
        long total = port_frame_total(buffer + offset, *used - offset);

        if (total < 0) {
            offset += 1u;
            continue;
        }
        if (total == 0) {
            break;
        }
        {
            uint8_t reply[LOCKER_WIRE_MAX_FRAME];
            size_t reply_length = 0u;
            uint8_t out[PORT_SERVER_OUT];
            size_t out_length = 0u;
            locker_status_t status;

            pthread_mutex_lock(&server->lock);
            status = locker_sim_handle_frame(server->sim, buffer + offset, (size_t)total, reply,
                                             sizeof(reply), &reply_length);
            server->last_request_no = (uint32_t)buffer[offset + 6u] |
                ((uint32_t)buffer[offset + 7u] << 8) |
                ((uint32_t)buffer[offset + 8u] << 16) |
                ((uint32_t)buffer[offset + 9u] << 24);
            if (status == LOCKER_STATUS_OK && buffer[offset + 3u] == 1u) {
                memcpy(server->held_reply, reply, reply_length);
                server->held_reply_length = reply_length;
            }
            if (status == LOCKER_STATUS_OK && reply_length > 0u &&
                buffer[offset + 3u] == 1u && server->hello_fault != 0u) {
                uint16_t crc;
                if (server->hello_fault == 1u) {
                    reply[28] ^= 1u; /* first device UID byte */
                } else if (server->hello_fault == 2u) {
                    reply[67] &= (uint8_t)~LOCKER_WIRE_CAP_BOUNDED_PULSE;
                } else if (server->hello_fault == 3u) {
                    reply[10] ^= 1u; /* foreign new_session_id */
                } else {
                    locker_uuid_t session;
                    memcpy(session.bytes, reply + 10u, 16u);
                    (void)locker_wire_encode_error_response(1u, server->last_request_no,
                        &session, LOCKER_WIRE_RESULT_BUSY, reply, sizeof(reply), &reply_length);
                }
                crc = locker_wire_crc16(reply + 2u, reply_length - 4u);
                reply[reply_length - 2u] = (uint8_t)crc;
                reply[reply_length - 1u] = (uint8_t)(crc >> 8);
            }
            if (status == LOCKER_STATUS_OK && server->hold_responses) {
                memcpy(server->held_reply, reply, reply_length);
                server->held_reply_length = reply_length;
                server->frames += 1u;
                pthread_mutex_unlock(&server->lock);
                offset += (size_t)total;
                continue;
            }
            pthread_mutex_unlock(&server->lock);
            if (status == LOCKER_STATUS_OK) {
                if (reply_length > 0u) {
                    memcpy(out + out_length, reply, reply_length);
                    out_length += reply_length;
                }
                for (;;) {
                    uint8_t event[LOCKER_WIRE_MAX_FRAME];
                    size_t event_length = 0u;
                    locker_status_t event_status;

                    pthread_mutex_lock(&server->lock);
                    event_status = locker_sim_pop_event(server->sim, event, sizeof(event),
                                                        &event_length);
                    pthread_mutex_unlock(&server->lock);
                    if (event_status != LOCKER_STATUS_OK) {
                        break;
                    }
                    if (out_length + event_length > sizeof(out)) {
                        break;
                    }
                    memcpy(out + out_length, event, event_length);
                    out_length += event_length;
                }
                pthread_mutex_lock(&server->lock);
                server->frames += 1u;
                pthread_mutex_unlock(&server->lock);
                if (out_length > 0u && port_write_all(client, out, out_length)) {
                    pthread_mutex_lock(&server->lock);
                    server->writes += 1u;
                    pthread_mutex_unlock(&server->lock);
                }
            }
        }
        offset += (size_t)total;
    }
    if (offset > 0u) {
        memmove(buffer, buffer + offset, *used - offset);
        *used -= offset;
    }
}

static void *port_server_main(void *argument)
{
    port_server_t *server = (port_server_t *)argument;
    uint8_t *buffer;
    size_t used = 0u;
    int client;

    client = accept(server->listen_fd, NULL, NULL);
    if (client < 0) {
        return NULL;
    }
    pthread_mutex_lock(&server->lock);
    server->client_fd = client;
    pthread_mutex_unlock(&server->lock);

    buffer = (uint8_t *)calloc(1u, PORT_SERVER_BUFFER);
    if (buffer == NULL) {
        (void)close(client);
        pthread_mutex_lock(&server->lock);
        server->client_fd = -1;
        pthread_mutex_unlock(&server->lock);
        return NULL;
    }
    {
        struct timeval timeout = {0, 50000};
        (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }
    for (;;) {
        ssize_t received;
        bool pause, stop;
        pthread_mutex_lock(&server->lock);
        pause = server->pause_reads;
        stop = server->stopping;
        server->reads_paused = pause;
        pthread_mutex_unlock(&server->lock);
        if (stop) break;
        if (pause) {
            struct timespec delay = {0, 1000000L};
            (void)nanosleep(&delay, NULL);
            continue;
        }
        if (used == PORT_SERVER_BUFFER) {
            break; /* unframed peer: drop the link instead of growing */
        }
        received = recv(client, buffer + used, PORT_SERVER_BUFFER - used, 0);
        if (received < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }
        if (received == 0) {
            break;
        }
        used += (size_t)received;
        port_server_consume(server, client, buffer, &used);
    }
    free(buffer);
    pthread_mutex_lock(&server->lock);
    server->client_fd = -1;
    pthread_mutex_unlock(&server->lock);
    (void)close(client);
    return NULL;
}

static void port_server_make_paths(port_server_t *server, const char *tag)
{
    static unsigned counter = 0u;

    counter += 1u;
    (void)snprintf(server->socket_path, sizeof(server->socket_path),
                   "/tmp/locker_s103_port_%s_%u_%u.sock", tag, (unsigned)getpid(), counter);
    (void)snprintf(server->journal_path, sizeof(server->journal_path),
                   "/tmp/locker_s103_port_%s_%u_%u.db", tag, (unsigned)getpid(), counter);
    (void)unlink(server->socket_path);
    (void)unlink(server->journal_path);
}

static void port_server_unlink_journal(const char *path)
{
    char side[LOCKER_SIM_PATH_SIZE + 8u];

    (void)unlink(path);
    (void)snprintf(side, sizeof(side), "%s-wal", path);
    (void)unlink(side);
    (void)snprintf(side, sizeof(side), "%s-shm", path);
    (void)unlink(side);
}

static locker_status_t port_server_start(port_server_t *server, const char *tag,
                                         test_clock_t *clock)
{
    struct sockaddr_un address;
    locker_sim_config_t config;
    mode_t previous;
    size_t path_length;

    memset(server, 0, sizeof(*server));
    server->listen_fd = -1;
    server->client_fd = -1;
    if (pthread_mutex_init(&server->lock, NULL) != 0) {
        return LOCKER_STATUS_IO_ERROR;
    }
    port_server_make_paths(server, tag);
    if (strcmp(tag, "badidentity") == 0) server->hello_fault = 1u;
    if (strcmp(tag, "badcaps") == 0) server->hello_fault = 2u;
    if (strcmp(tag, "badhellosession") == 0) server->hello_fault = 3u;
    if (strcmp(tag, "helloerror") == 0) server->hello_fault = 4u;

    memset(&config, 0, sizeof(config));
    memcpy(config.journal_path, server->journal_path, strlen(server->journal_path));
    config.device_uid[0] = 0x01u;
    config.device_uid[11] = 0x0cu;
    config.clock = test_clock_port(clock);
    if (locker_sim_create(&server->sim, &config) != LOCKER_STATUS_OK) {
        pthread_mutex_destroy(&server->lock);
        return LOCKER_STATUS_IO_ERROR;
    }
    if (locker_sim_open(server->sim) != LOCKER_STATUS_OK) {
        locker_sim_destroy(server->sim);
        server->sim = NULL;
        pthread_mutex_destroy(&server->lock);
        return LOCKER_STATUS_IO_ERROR;
    }

    server->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server->listen_fd < 0) {
        locker_sim_close(server->sim);
        locker_sim_destroy(server->sim);
        server->sim = NULL;
        pthread_mutex_destroy(&server->lock);
        return LOCKER_STATUS_IO_ERROR;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    path_length = strlen(server->socket_path);
    memcpy(address.sun_path, server->socket_path, path_length + 1u);
    previous = umask(0077);
    if (bind(server->listen_fd, (const struct sockaddr *)&address, (socklen_t)sizeof(address)) !=
        0) {
        (void)umask(previous);
        (void)close(server->listen_fd);
        server->listen_fd = -1;
        locker_sim_close(server->sim);
        locker_sim_destroy(server->sim);
        server->sim = NULL;
        pthread_mutex_destroy(&server->lock);
        return LOCKER_STATUS_IO_ERROR;
    }
    (void)umask(previous);
    (void)chmod(server->socket_path, 0600);
    if (listen(server->listen_fd, 4) != 0 ||
        pthread_create(&server->thread, NULL, port_server_main, server) != 0) {
        (void)close(server->listen_fd);
        server->listen_fd = -1;
        (void)unlink(server->socket_path);
        locker_sim_close(server->sim);
        locker_sim_destroy(server->sim);
        server->sim = NULL;
        pthread_mutex_destroy(&server->lock);
        return LOCKER_STATUS_IO_ERROR;
    }
    server->thread_started = true;
    return LOCKER_STATUS_OK;
}

static void port_server_stop(port_server_t *server)
{
    int client;

    pthread_mutex_lock(&server->lock);
    server->stopping = true;
    client = server->client_fd;
    pthread_mutex_unlock(&server->lock);
    if (server->listen_fd >= 0) {
        /* Wakes accept() when the adapter never connected. */
        (void)shutdown(server->listen_fd, SHUT_RDWR);
    }
    if (client >= 0) {
        (void)shutdown(client, SHUT_RDWR);
    }
    if (server->thread_started) {
        (void)pthread_join(server->thread, NULL);
        server->thread_started = false;
    }
    if (server->listen_fd >= 0) {
        (void)close(server->listen_fd);
        server->listen_fd = -1;
    }
    if (server->sim != NULL) {
        locker_sim_close(server->sim);
        locker_sim_destroy(server->sim);
        server->sim = NULL;
    }
    (void)unlink(server->socket_path);
    port_server_unlink_journal(server->journal_path);
    pthread_mutex_destroy(&server->lock);
}

static locker_status_t port_server_door(port_server_t *server, uint8_t door)
{
    locker_status_t status;

    pthread_mutex_lock(&server->lock);
    status = locker_sim_control_set_door(server->sim, door);
    pthread_mutex_unlock(&server->lock);
    return status;
}

static locker_status_t port_server_item(port_server_t *server, uint8_t item)
{
    locker_status_t status;

    pthread_mutex_lock(&server->lock);
    status = locker_sim_control_set_item(server->sim, item);
    pthread_mutex_unlock(&server->lock);
    return status;
}

static locker_status_t port_server_card(port_server_t *server, const uint8_t *uid, size_t uid_len)
{
    locker_status_t status;

    pthread_mutex_lock(&server->lock);
    status = locker_sim_control_card(server->sim, uid, uid_len);
    pthread_mutex_unlock(&server->lock);
    return status;
}

/* Writes a frame straight to the adapter socket, bypassing the simulator, so a
 * test can present bytes the simulator would never produce (a DEVICE_EVENT for
 * another session). */
static bool port_server_inject(port_server_t *server, const uint8_t *frame, size_t length)
{
    int client;

    pthread_mutex_lock(&server->lock);
    client = server->client_fd;
    pthread_mutex_unlock(&server->lock);
    if (client < 0) {
        return false;
    }
    return port_write_all(client, frame, length);
}

static locker_status_t port_server_drop_ack(port_server_t *server, bool enabled)
{
    locker_status_t status;

    pthread_mutex_lock(&server->lock);
    status = locker_sim_control_drop_ack(server->sim, enabled);
    pthread_mutex_unlock(&server->lock);
    return status;
}

static locker_uuid_t port_server_epoch(port_server_t *server)
{
    locker_uuid_t epoch;

    pthread_mutex_lock(&server->lock);
    epoch = locker_sim_journal_epoch(server->sim);
    pthread_mutex_unlock(&server->lock);
    return epoch;
}

static locker_wire_cursor_t port_server_last_committed(port_server_t *server)
{
    locker_wire_cursor_t cursor;

    pthread_mutex_lock(&server->lock);
    cursor = locker_sim_last_committed(server->sim);
    pthread_mutex_unlock(&server->lock);
    return cursor;
}

static uint64_t port_server_boot_id(port_server_t *server)
{
    uint64_t boot_id;

    pthread_mutex_lock(&server->lock);
    boot_id = locker_sim_boot_id(server->sim);
    pthread_mutex_unlock(&server->lock);
    return boot_id;
}

static unsigned port_server_frames(port_server_t *server)
{
    unsigned frames;

    pthread_mutex_lock(&server->lock);
    frames = server->frames;
    pthread_mutex_unlock(&server->lock);
    return frames;
}

/* ------------------------------------------------------------------ */
/* Device sink: records replies and events from the adapter thread     */
/* ------------------------------------------------------------------ */

typedef struct port_sink {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    unsigned replies;
    unsigned events;
    locker_device_reply_t last_reply;
    locker_device_event_t last_event;
    locker_device_event_t event_log[8];
    unsigned event_log_count;
    locker_status_t reject_status;
} port_sink_t;

static locker_status_t port_sink_on_reply(void *user, const locker_device_reply_t *reply)
{
    port_sink_t *sink = (port_sink_t *)user;
    locker_status_t status;

    if (sink == NULL || reply == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    pthread_mutex_lock(&sink->lock);
    sink->replies += 1u;
    sink->last_reply = *reply;
    status = sink->reject_status;
    pthread_cond_broadcast(&sink->cond);
    pthread_mutex_unlock(&sink->lock);
    return status;
}

static locker_status_t port_sink_on_event(void *user, const locker_device_event_t *event)
{
    port_sink_t *sink = (port_sink_t *)user;
    locker_status_t status;

    if (sink == NULL || event == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    pthread_mutex_lock(&sink->lock);
    sink->events += 1u;
    sink->last_event = *event;
    if (sink->event_log_count < (unsigned)(sizeof(sink->event_log) / sizeof(sink->event_log[0]))) {
        sink->event_log[sink->event_log_count] = *event;
        sink->event_log_count += 1u;
    }
    status = sink->reject_status;
    pthread_cond_broadcast(&sink->cond);
    pthread_mutex_unlock(&sink->lock);
    return status;
}

static bool port_sink_wait(port_sink_t *sink, unsigned replies, unsigned events, int timeout_ms)
{
    struct timespec deadline;
    bool satisfied;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&sink->lock);
    while (sink->replies < replies || sink->events < events) {
        if (pthread_cond_timedwait(&sink->cond, &sink->lock, &deadline) != 0) {
            break;
        }
    }
    satisfied = sink->replies >= replies && sink->events >= events;
    pthread_mutex_unlock(&sink->lock);
    return satisfied;
}

static unsigned port_sink_reply_count(port_sink_t *sink)
{
    unsigned count;

    pthread_mutex_lock(&sink->lock);
    count = sink->replies;
    pthread_mutex_unlock(&sink->lock);
    return count;
}

static unsigned port_sink_event_count(port_sink_t *sink)
{
    unsigned count;

    pthread_mutex_lock(&sink->lock);
    count = sink->events;
    pthread_mutex_unlock(&sink->lock);
    return count;
}

static locker_device_reply_t port_sink_reply(port_sink_t *sink)
{
    locker_device_reply_t reply;

    pthread_mutex_lock(&sink->lock);
    reply = sink->last_reply;
    pthread_mutex_unlock(&sink->lock);
    return reply;
}

static locker_device_event_t port_sink_event(port_sink_t *sink)
{
    locker_device_event_t event;

    pthread_mutex_lock(&sink->lock);
    event = sink->last_event;
    pthread_mutex_unlock(&sink->lock);
    return event;
}

static locker_device_event_t port_sink_event_at(port_sink_t *sink, unsigned index)
{
    locker_device_event_t event;

    memset(&event, 0, sizeof(event));
    pthread_mutex_lock(&sink->lock);
    if (index < sink->event_log_count) {
        event = sink->event_log[index];
    }
    pthread_mutex_unlock(&sink->lock);
    return event;
}

/* ------------------------------------------------------------------ */
/* Environment: server + sink + adapter                                */
/* ------------------------------------------------------------------ */

typedef struct port_env {
    port_server_t server;
    port_sink_t sink;
    test_clock_t clock;
    locker_device_t *device;
    locker_device_config_t config;
    bool server_started;
    bool sink_ready;
    bool real_clock;
    uint32_t snapshot_request_highwater;
} port_env_t;

static locker_uuid_t port_make_uuid(uint8_t seed)
{
    locker_uuid_t uuid;

    test_uuid_fill(&uuid, seed);
    return uuid;
}

static void port_env_init(port_env_t *env)
{
    memset(env, 0, sizeof(*env));
    env->clock.monotonic_ms = 5000u;
    env->clock.wall_ms = 1700000000000u;
    env->clock.uuid_counter = 0u;
    if (pthread_mutex_init(&env->sink.lock, NULL) == 0 &&
        pthread_cond_init(&env->sink.cond, NULL) == 0) {
        env->sink_ready = true;
    }
}

static void port_env_finish(port_env_t *env)
{
    if (env->device != NULL) {
        locker_device_destroy(env->device);
        env->device = NULL;
    }
    if (env->server_started) {
        port_server_stop(&env->server);
        env->server_started = false;
    }
    if (env->sink_ready) {
        pthread_cond_destroy(&env->sink.cond);
        pthread_mutex_destroy(&env->sink.lock);
        env->sink_ready = false;
    }
}

static void port_env_build_config(port_env_t *env)
{
    memset(&env->config, 0, sizeof(env->config));
    env->config.simulated = true;
    memcpy(env->config.path, env->server.socket_path, strlen(env->server.socket_path));
    (void)snprintf(env->config.expected_device_id, sizeof(env->config.expected_device_id),
                   "01000000000000000000000c");
    env->config.baud_rate = 115200u;
    env->config.host_instance_id = port_make_uuid(0x40u);
    /* Production wiring: the adapter runs on locker_clock_port_real(). The
     * simulator side always uses the deterministic test clock so its own
     * timestamps stay reproducible. */
    env->config.clock = env->real_clock ? locker_clock_port_real() : test_clock_port(&env->clock);
}

static locker_device_sink_t port_env_sink(port_env_t *env)
{
    locker_device_sink_t sink;

    sink.user = &env->sink;
    sink.reply = port_sink_on_reply;
    sink.event = port_sink_on_event;
    return sink;
}

static locker_status_t port_env_open(port_env_t *env, const char *tag)
{
    locker_device_sink_t sink;
    locker_status_t status;

    status = port_server_start(&env->server, tag, &env->clock);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    env->server_started = true;
    port_env_build_config(env);
    sink = port_env_sink(env);
    status = locker_device_create(&env->device);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = locker_device_bind_sink(env->device, &sink);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    return locker_device_open(env->device, &env->config);
}

static bool port_env_open_ready(port_env_t *env, const char *tag)
{
    if (port_env_open(env, tag) != LOCKER_STATUS_OK) {
        return false;
    }
    return port_sink_wait(&env->sink, 1u, 0u, PORT_WAIT_MS);
}

static bool port_env_snapshot(port_env_t *env, locker_wire_get_snapshot_response_t *out)
{
    unsigned expected = port_sink_reply_count(&env->sink) + 1u;
    locker_device_reply_t reply = port_sink_reply(&env->sink);
    uint32_t request_no = env->snapshot_request_highwater;

    if (reply.request_no > request_no) request_no = reply.request_no;
    pthread_mutex_lock(&env->server.lock);
    if (env->server.last_request_no > request_no) request_no = env->server.last_request_no;
    pthread_mutex_unlock(&env->server.lock);
    if (request_no == UINT32_MAX) return false;
    ++request_no;
    env->snapshot_request_highwater = request_no;
    if (locker_device_get_snapshot(env->device, request_no) != LOCKER_STATUS_OK) {
        return false;
    }
    if (!port_sink_wait(&env->sink, expected, 0u, PORT_WAIT_MS)) {
        return false;
    }
    reply = port_sink_reply(&env->sink);
    if (reply.message_type != (uint8_t)(2u | LOCKER_WIRE_RESPONSE_FLAG) ||
        reply.request_no != request_no) {
        return false;
    }
    *out = reply.body.get_snapshot;
    return out->result == LOCKER_WIRE_RESULT_OK;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void test_open_and_snapshot(void)
{
    port_env_t env;
    locker_device_reply_t reply;
    locker_wire_get_snapshot_response_t snapshot;
    locker_uuid_t epoch;
    locker_wire_cursor_t last;

    port_env_init(&env);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, port_env_open(&env, "open"));
    TEST_CHECK(port_sink_wait(&env.sink, 1u, 0u, PORT_WAIT_MS));

    reply = port_sink_reply(&env.sink);
    TEST_CHECK_U64(1u, reply.request_no);
    TEST_CHECK_U64((uint64_t)(1u | LOCKER_WIRE_RESPONSE_FLAG), reply.message_type);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, reply.body.hello.result);
    TEST_CHECK_U64(1u, reply.body.hello.boot_id);
    TEST_CHECK_U64(0x01u, reply.body.hello.device_uid[0]);
    TEST_CHECK_U64(0x0cu, reply.body.hello.device_uid[11]);
    TEST_CHECK_U64(0u, reply.body.hello.firmware_major);
    TEST_CHECK_U64(1u, reply.body.hello.firmware_minor);
    TEST_CHECK_U64(0u, reply.body.hello.firmware_patch);
    TEST_CHECK_U64(0x1fu, reply.body.hello.capabilities);
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY, reply.body.hello.journal_state);
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING, reply.body.hello.device_mode);
    TEST_CHECK_U64(0u, reply.body.hello.action_watermark);
    TEST_CHECK(reply.received_host_mono_ms >= reply.request_started_host_mono_ms);

    epoch = port_server_epoch(&env.server);
    TEST_CHECK(memcmp(epoch.bytes, reply.body.hello.journal_epoch.bytes, 16u) == 0);
    TEST_CHECK(!locker_uuid_is_nil(&epoch));
    TEST_CHECK_U64(1u, port_server_boot_id(&env.server));

    TEST_CHECK(port_env_snapshot(&env, &snapshot));
    TEST_CHECK_U64(1u, snapshot.boot_id);
    TEST_CHECK(memcmp(snapshot.journal_epoch.bytes, epoch.bytes, 16u) == 0);
    TEST_CHECK_U64(5000u, snapshot.sampled_mono_ms);
    TEST_CHECK_U64(1u, snapshot.physical_revision);
    TEST_CHECK_U64(LOCKER_WIRE_DOOR_STATE_CLOSED, snapshot.door);
    TEST_CHECK_U64(LOCKER_WIRE_ITEM_STATE_PRESENT, snapshot.item);
    TEST_CHECK_U64(5000u, snapshot.supply_mv);
    TEST_CHECK_U64(LOCKER_WIRE_FLAG_SUPPLY_VALID | LOCKER_WIRE_FLAG_PHYSICAL_STABLE,
                   snapshot.flags);
    TEST_CHECK_U64(0u, snapshot.dropped_events);
    TEST_CHECK_U64(0u, snapshot.action_watermark);
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING, snapshot.device_mode);
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY, snapshot.journal_state);

    last = port_server_last_committed(&env.server);
    TEST_CHECK_U64(last.boot_id, snapshot.last_committed.boot_id);
    TEST_CHECK_U64(last.event_seq, snapshot.last_committed.event_seq);
    TEST_CHECK_U64(1u, snapshot.first_available.boot_id);
    TEST_CHECK_U64(1u, snapshot.first_available.event_seq);
    TEST_CHECK_U64(0u, snapshot.ack_persisted.boot_id);
    TEST_CHECK_U64(0u, snapshot.ack_persisted.event_seq);
    TEST_CHECK(port_server_frames(&env.server) >= 2u);

    port_env_finish(&env);
}

static void test_config_validation(void)
{
    port_env_t env;
    locker_device_sink_t sink;
    locker_device_config_t config;
    locker_device_t *device = NULL;
    locker_device_t *bare = NULL;

    port_env_init(&env);
    sink = port_env_sink(&env);

    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_create(NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_create(&device));
    TEST_CHECK(device != NULL);
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_time_anchor(device, 0u));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_bind_sink(device, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_bind_sink(device, &sink));

    memset(&config, 0, sizeof(config));
    config.simulated = true;
    config.clock = test_clock_port(&env.clock);
    (void)snprintf(config.expected_device_id, sizeof(config.expected_device_id),
                   "01000000000000000000000c");
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_open(device, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_open(NULL, &config));
    /* Empty path. */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_open(device, &config));
    /* Empty expected device id. */
    (void)snprintf(config.path, sizeof(config.path), "/tmp/locker_s103_port_absent.sock");
    memset(config.expected_device_id, 0, sizeof(config.expected_device_id));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_open(device, &config));
    /* Invalid clock port. */
    (void)snprintf(config.expected_device_id, sizeof(config.expected_device_id),
                   "01000000000000000000000c");
    memset(&config.clock, 0, sizeof(config.clock));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_open(device, &config));
    /* Real hardware is out of scope for stage 1: simulated must be true. */
    config.clock = test_clock_port(&env.clock);
    config.simulated = false;
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_device_open(device, &config));

    /* A device without a sink cannot be opened. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_create(&bare));
    config.simulated = true;
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_device_open(bare, &config));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_device_get_snapshot(bare, 3u));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_time_anchor(bare, 0u));
    {
        locker_event_key_t after;

        memset(&after, 0, sizeof(after));
        (void)snprintf(after.device_id, sizeof(after.device_id), "01000000000000000000000c");
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_device_fetch_events(bare, &after, 1u));
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_close(NULL));
    locker_device_destroy(bare);
    locker_device_destroy(device);
    locker_device_destroy(NULL);
    port_env_finish(&env);
}

static void test_port_guards(void)
{
    port_env_t env;
    locker_device_sink_t sink;
    locker_unlock_intent_t intent;
    locker_event_key_t cursor;

    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "guards"));

    sink = port_env_sink(&env);
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, locker_device_bind_sink(env.device, &sink));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, locker_device_open(env.device, &env.config));

    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_get_snapshot(NULL, 4u));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_fetch_events(env.device, NULL, 1u));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_time_anchor(env.device, 0u));

    memset(&intent, 0, sizeof(intent));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_execute_once(NULL, &intent));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_execute_once(env.device, NULL));
    intent.slot_id = 2u;
    intent.kind = LOCKER_OPERATION_KIND_BORROW;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_execute_once(env.device, &intent));
    intent.slot_id = 1u;
    intent.kind = (locker_operation_kind_t)9;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_execute_once(env.device, &intent));

    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_query_command(env.device, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_ack_persisted(env.device, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_resume_ready(env.device, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_release_context(env.device, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_send_health(env.device, NULL));

    memset(&cursor, 0, sizeof(cursor));
    (void)snprintf(cursor.device_id, sizeof(cursor.device_id), "01000000000000000000000c");
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_fetch_events(env.device, &cursor, 0u));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_device_fetch_events(env.device, &cursor, 5u));

    port_env_finish(&env);
}

static bool port_env_reconcile(port_env_t *env,
                               const locker_wire_get_snapshot_response_t *snapshot)
{
    locker_event_key_t key;
    locker_ready_guard_t guard;
    locker_health_report_t health;
    unsigned expected;
    unsigned pages;
    locker_device_reply_t reply;
    memset(&key, 0, sizeof(key));
    for (pages = 0u; pages < LOCKER_TEST_PAGE_LIMIT; ++pages) {
        expected = port_sink_reply_count(&env->sink) + 1u;
        if (locker_device_fetch_events(env->device, &key, 4u) != LOCKER_STATUS_OK ||
            !port_sink_wait(&env->sink, expected, 0u, PORT_WAIT_MS)) return false;
        reply = port_sink_reply(&env->sink);
        if (reply.body.fetch_events.result != LOCKER_WIRE_RESULT_OK) return false;
        (void)snprintf(key.device_id, sizeof(key.device_id), "%s", env->config.expected_device_id);
        key.journal_epoch = snapshot->journal_epoch;
        key.boot_id = reply.body.fetch_events.next_after.boot_id;
        key.event_seq = reply.body.fetch_events.next_after.event_seq;
        if (reply.body.fetch_events.has_more == 0u) break;
    }
    /* A page sequence that never terminates is an engine defect, not a slow test. */
    if (pages >= LOCKER_TEST_PAGE_LIMIT) return false;
    expected = port_sink_reply_count(&env->sink) + 1u;
    if (locker_device_ack_persisted(env->device, &key) != LOCKER_STATUS_OK ||
        !port_sink_wait(&env->sink, expected, 0u, PORT_WAIT_MS)) return false;
    if (port_sink_reply(&env->sink).body.ack_persisted.result != LOCKER_WIRE_RESULT_OK) return false;
    memset(&health, 0, sizeof(health));
    health.host_instance_id = env->config.host_instance_id;
    health.core_progress = 1u;
    health.core_age_ms = 0u;
    health.ui_age_ms = LOCKER_WIRE_AGE_UNKNOWN;
    expected = port_sink_reply_count(&env->sink) + 1u;
    if (locker_device_send_health(env->device, &health) != LOCKER_STATUS_OK ||
        !port_sink_wait(&env->sink, expected, 0u, PORT_WAIT_MS)) return false;
    memset(&guard, 0, sizeof(guard));
    guard.session_id = snapshot->session_id;
    guard.journal_epoch = snapshot->journal_epoch;
    guard.expected_boot_id = snapshot->boot_id;
    guard.expected_physical_revision = snapshot->physical_revision;
    guard.persisted_through = key;
    expected = port_sink_reply_count(&env->sink) + 1u;
    if (locker_device_resume_ready(env->device, &guard) != LOCKER_STATUS_OK ||
        !port_sink_wait(&env->sink, expected, 0u, PORT_WAIT_MS)) return false;
    reply = port_sink_reply(&env->sink);
    return reply.body.resume_ready.result == LOCKER_WIRE_RESULT_OK &&
           reply.body.resume_ready.device_mode == LOCKER_WIRE_WIRE_DEVICE_MODE_READY;
}

static bool port_env_execute(port_env_t *env, uint64_t action_no, const locker_uuid_t *command_id,
                             const locker_uuid_t *operation_id,
                             const locker_wire_get_snapshot_response_t *snapshot, uint8_t kind,
                             uint16_t *out_result, uint8_t *out_state, uint64_t *out_action_no)
{
    locker_unlock_intent_t intent;
    unsigned expected = port_sink_reply_count(&env->sink) + 1u;
    locker_device_reply_t reply;

    memset(&intent, 0, sizeof(intent));
    intent.operation_id = *operation_id;
    intent.command_id = *command_id;
    intent.session_id = snapshot->session_id;
    intent.journal_epoch = snapshot->journal_epoch;
    intent.expected_boot_id = snapshot->boot_id;
    intent.expected_physical_revision = snapshot->physical_revision;
    intent.action_no = action_no;
    intent.not_after_mcu_ms = env->clock.monotonic_ms + 5000u;
    intent.slot_id = 1u;
    intent.kind = (locker_operation_kind_t)kind;
    if (locker_device_execute_once(env->device, &intent) != LOCKER_STATUS_OK) {
        return false;
    }
    if (!port_sink_wait(&env->sink, expected, 0u, PORT_WAIT_MS)) {
        return false;
    }
    reply = port_sink_reply(&env->sink);
    if (reply.message_type != (uint8_t)(7u | LOCKER_WIRE_RESPONSE_FLAG)) {
        return false;
    }
    *out_result = reply.body.execute_unlock.result;
    *out_state = reply.body.execute_unlock.command_state;
    *out_action_no = reply.body.execute_unlock.action_no;
    return true;
}

static void test_execute_and_query(void)
{
    port_env_t env;
    locker_wire_get_snapshot_response_t snapshot;
    locker_uuid_t operation_id = port_make_uuid(0x60u);
    locker_uuid_t command_id = port_make_uuid(0x70u);
    locker_uuid_t unknown_id = port_make_uuid(0x80u);
    uint16_t result = 0xffffu;
    uint8_t state = 0xffu;
    uint64_t action_no = 0u;
    unsigned expected;
    locker_device_reply_t reply;

    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "execute"));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));

    /* BORROW requires the item present (uart-v1.md:384): drive the control path. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      port_server_item(&env.server, LOCKER_WIRE_ITEM_STATE_ABSENT));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));
    TEST_CHECK_U64(LOCKER_WIRE_ITEM_STATE_ABSENT, snapshot.item);
    TEST_CHECK(port_env_execute(&env, 1u, &command_id, &operation_id, &snapshot,
                                (uint8_t)LOCKER_OPERATION_KIND_BORROW, &result, &state,
                                &action_no));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BAD_PRECONDITION, result);
    TEST_CHECK_U64(0u, action_no);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      port_server_item(&env.server, LOCKER_WIRE_ITEM_STATE_PRESENT));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));

    TEST_CHECK(port_env_reconcile(&env, &snapshot));

    /* Acceptance: the device persists the intent before answering ACCEPTED. */
    TEST_CHECK(port_env_execute(&env, 1u, &command_id, &operation_id, &snapshot,
                                (uint8_t)LOCKER_OPERATION_KIND_BORROW, &result, &state,
                                &action_no));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, result);
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED, state);
    TEST_CHECK_U64(1u, action_no);

    /* Two device events accompany the acceptance (COMMAND_ACCEPTED, PULSE_STARTED). */
    TEST_CHECK(port_sink_wait(&env.sink, port_sink_reply_count(&env.sink), 2u, PORT_WAIT_MS));

    /* Query returns the recorded state, never "not executed". */
    expected = port_sink_reply_count(&env.sink) + 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_query_command(env.device, &command_id));
    TEST_CHECK(port_sink_wait(&env.sink, expected, 0u, PORT_WAIT_MS));
    reply = port_sink_reply(&env.sink);
    TEST_CHECK_U64((uint64_t)(8u | LOCKER_WIRE_RESPONSE_FLAG), reply.message_type);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, reply.body.query_command.result);
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED,
                   reply.body.query_command.command_state);
    TEST_CHECK_U64(1u, reply.body.query_command.action_no);
    TEST_CHECK(memcmp(reply.body.query_command.command_id.bytes, command_id.bytes, 16u) == 0);

    /* Unknown command id -> UNKNOWN_RESULT, not a silent success. */
    expected = port_sink_reply_count(&env.sink) + 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_query_command(env.device, &unknown_id));
    TEST_CHECK(port_sink_wait(&env.sink, expected, 0u, PORT_WAIT_MS));
    reply = port_sink_reply(&env.sink);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_UNKNOWN_RESULT, reply.body.query_command.result);

    /* A skipped action number is an ACTION_GAP, not a new acceptance. */
    TEST_CHECK(port_env_execute(&env, 5u, &unknown_id, &operation_id, &snapshot,
                                (uint8_t)LOCKER_OPERATION_KIND_BORROW, &result, &state,
                                &action_no));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_ACTION_GAP, result);

    /* Replaying the accepted action with the same command is idempotent. */
    TEST_CHECK(port_env_execute(&env, 1u, &command_id, &operation_id, &snapshot,
                                (uint8_t)LOCKER_OPERATION_KIND_BORROW, &result, &state,
                                &action_no));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, result);
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED, state);

    port_env_finish(&env);
}

static void test_fetch_ack_resume_release_health_anchor(void)
{
    port_env_t env;
    locker_wire_get_snapshot_response_t snapshot;
    locker_event_key_t after;
    locker_event_key_t ack;
    locker_ready_guard_t guard;
    locker_health_report_t health;
    locker_context_guard_t context;
    unsigned expected;
    locker_device_reply_t reply;

    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "session"));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));

    memset(&after, 0, sizeof(after));
    expected = port_sink_reply_count(&env.sink) + 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_fetch_events(env.device, &after, 4u));
    TEST_CHECK(port_sink_wait(&env.sink, expected, 0u, PORT_WAIT_MS));
    reply = port_sink_reply(&env.sink);
    TEST_CHECK_U64((uint64_t)(3u | LOCKER_WIRE_RESPONSE_FLAG), reply.message_type);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, reply.body.fetch_events.result);
    TEST_CHECK(reply.body.fetch_events.event_count >= 1u);
    TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT_TYPE_BOOT, reply.body.fetch_events.events[0].event_type);
    TEST_CHECK_U64(1u, reply.body.fetch_events.first_available.boot_id);
    TEST_CHECK(memcmp(reply.body.fetch_events.journal_epoch.bytes, snapshot.journal_epoch.bytes,
                      16u) == 0);

    memset(&ack, 0, sizeof(ack));
    (void)snprintf(ack.device_id, sizeof(ack.device_id), "01000000000000000000000c");
    ack.journal_epoch = snapshot.journal_epoch;
    ack.boot_id = reply.body.fetch_events.events[0].boot_id;
    ack.event_seq = reply.body.fetch_events.events[0].event_seq;
    expected = port_sink_reply_count(&env.sink) + 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_ack_persisted(env.device, &ack));
    TEST_CHECK(port_sink_wait(&env.sink, expected, 0u, PORT_WAIT_MS));
    reply = port_sink_reply(&env.sink);
    TEST_CHECK_U64((uint64_t)(4u | LOCKER_WIRE_RESPONSE_FLAG), reply.message_type);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, reply.body.ack_persisted.result);
    TEST_CHECK_U64(ack.boot_id, reply.body.ack_persisted.ack_persisted.boot_id);
    TEST_CHECK_U64(ack.event_seq, reply.body.ack_persisted.ack_persisted.event_seq);

    memset(&health, 0, sizeof(health));
    health.host_instance_id = env.config.host_instance_id;
    health.core_progress = 1u;
    health.core_age_ms = 0u;
    health.ui_age_ms = LOCKER_WIRE_AGE_UNKNOWN;
    expected = port_sink_reply_count(&env.sink) + 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_send_health(env.device, &health));
    TEST_CHECK(port_sink_wait(&env.sink, expected, 0u, PORT_WAIT_MS));

    memset(&guard, 0, sizeof(guard));
    guard.session_id = snapshot.session_id;
    guard.journal_epoch = snapshot.journal_epoch;
    guard.expected_boot_id = snapshot.boot_id;
    guard.expected_physical_revision = snapshot.physical_revision;
    guard.persisted_through = ack;
    expected = port_sink_reply_count(&env.sink) + 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_resume_ready(env.device, &guard));
    TEST_CHECK(port_sink_wait(&env.sink, expected, 0u, PORT_WAIT_MS));
    reply = port_sink_reply(&env.sink);
    TEST_CHECK_U64((uint64_t)(5u | LOCKER_WIRE_RESPONSE_FLAG), reply.message_type);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, reply.body.resume_ready.result);
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_READY, reply.body.resume_ready.device_mode);
    TEST_CHECK_U64(snapshot.physical_revision, reply.body.resume_ready.physical_revision);

    memset(&health, 0, sizeof(health));
    health.host_instance_id = env.config.host_instance_id;
    health.ui_instance_id = port_make_uuid(0x50u);
    health.core_progress = 10u;
    health.ui_progress = 20u;
    health.core_age_ms = 100u;
    health.ui_age_ms = 200u;
    health.supervisor_state = LOCKER_SUPERVISOR_STATE_RUNNING;
    expected = port_sink_reply_count(&env.sink) + 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_send_health(env.device, &health));
    TEST_CHECK(port_sink_wait(&env.sink, expected, 0u, PORT_WAIT_MS));
    reply = port_sink_reply(&env.sink);
    TEST_CHECK_U64((uint64_t)(9u | LOCKER_WIRE_RESPONSE_FLAG), reply.message_type);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, reply.body.host_health.result);
    TEST_CHECK_U64(5000u, reply.body.host_health.received_mono_ms);
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_READY, reply.body.host_health.device_mode);

    expected = port_sink_reply_count(&env.sink) + 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_time_anchor(env.device, 7u));
    TEST_CHECK(port_sink_wait(&env.sink, expected, 0u, PORT_WAIT_MS));
    reply = port_sink_reply(&env.sink);
    TEST_CHECK_U64((uint64_t)(10u | LOCKER_WIRE_RESPONSE_FLAG), reply.message_type);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, reply.body.time_anchor.result);
    TEST_CHECK_U64(7u, reply.body.time_anchor.anchor_id);
    TEST_CHECK_U64(1u, reply.body.time_anchor.boot_id);
    TEST_CHECK_U64(5000u, reply.body.time_anchor.mcu_receive_mono_ms);
    TEST_CHECK_U64(5000u, reply.body.time_anchor.mcu_send_mono_ms);

    /* No active context: releasing is a safe idempotent no-op. */
    memset(&context, 0, sizeof(context));
    context.session_id = snapshot.session_id;
    context.journal_epoch = snapshot.journal_epoch;
    context.expected_boot_id = snapshot.boot_id;
    context.expected_physical_revision = snapshot.physical_revision;
    context.operation_id = port_make_uuid(0x60u);
    context.command_id = port_make_uuid(0x70u);
    expected = port_sink_reply_count(&env.sink) + 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_release_context(env.device, &context));
    TEST_CHECK(port_sink_wait(&env.sink, expected, 0u, PORT_WAIT_MS));
    reply = port_sink_reply(&env.sink);
    TEST_CHECK_U64((uint64_t)(11u | LOCKER_WIRE_RESPONSE_FLAG), reply.message_type);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, reply.body.release_context.result);
    TEST_CHECK_U64(1u, reply.body.release_context.already_released);

    port_env_finish(&env);
}

static void test_events_delivered(void)
{
    port_env_t env;
    locker_wire_get_snapshot_response_t snapshot;
    locker_device_event_t event;
    uint8_t card[4] = {0xdeu, 0xadu, 0xbeu, 0xefu};
    unsigned expected;

    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "events"));

    /* The simulator only flushes queued events when a frame is handled, which is
     * also how the daemon behaves; the snapshot is that frame. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, port_server_door(&env.server, LOCKER_WIRE_DOOR_STATE_OPEN));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));
    TEST_CHECK(port_sink_wait(&env.sink, port_sink_reply_count(&env.sink), 1u, PORT_WAIT_MS));
    event = port_sink_event(&env.sink);
    TEST_CHECK(event.live_transport);
    TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_OPENED, event.event_type);
    TEST_CHECK_U64(LOCKER_WIRE_DOOR_STATE_OPEN, event.door);
    TEST_CHECK_U64(1u, event.key.boot_id);
    TEST_CHECK(event.key.event_seq >= 2u);
    TEST_CHECK_STRING("01000000000000000000000c", event.key.device_id);
    TEST_CHECK(memcmp(event.key.journal_epoch.bytes, snapshot.journal_epoch.bytes, 16u) == 0);
    TEST_CHECK_U64(LOCKER_WIRE_FLAG_SUPPLY_VALID | LOCKER_WIRE_FLAG_PHYSICAL_STABLE,
                   event.flags);
    TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_OPENED,
                   port_sink_event_at(&env.sink, 0u).event_type);

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, port_server_card(&env.server, card, sizeof(card)));
    expected = port_sink_event_count(&env.sink) + 1u;
    TEST_CHECK(port_env_snapshot(&env, &snapshot));
    TEST_CHECK(port_sink_wait(&env.sink, port_sink_reply_count(&env.sink), expected,
                              PORT_WAIT_MS));
    event = port_sink_event(&env.sink);
    TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT_TYPE_CARD_OBSERVED, event.event_type);
    TEST_CHECK_U64(4u, event.card_uid_len);
    TEST_CHECK(memcmp(event.card_uid, card, sizeof(card)) == 0);
    TEST_CHECK_U64(LOCKER_WIRE_DEVICE_EVENT_TYPE_CARD_OBSERVED,
                   port_sink_event_at(&env.sink, 1u).event_type);
    TEST_CHECK_U64(4u, port_sink_event_at(&env.sink, 1u).card_uid_len);

    /* A closed door change also bumps physical_revision in the snapshot. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      port_server_door(&env.server, LOCKER_WIRE_DOOR_STATE_CLOSED));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));
    TEST_CHECK_U64(LOCKER_WIRE_DOOR_STATE_CLOSED, snapshot.door);
    TEST_CHECK_U64(3u, snapshot.physical_revision);

    port_env_finish(&env);
}

static void test_foreign_event_dropped(void)
{
    port_env_t env;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_event96_t record;
    locker_uuid_t foreign_session;
    locker_uuid_t epoch;
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    size_t frame_length = 0u;
    unsigned events;

    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "foreign"));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));
    events = port_sink_event_count(&env.sink);
    epoch = port_server_epoch(&env.server);

    /* A well-formed DEVICE_EVENT carrying another session belongs to a link the
     * adapter does not own; it must be dropped, never surfaced to the sink
     * (uart-v1.md:356 scopes async events to the live session). */
    test_uuid_fill(&foreign_session, 0x90u);
    memset(&record, 0, sizeof(record));
    record.boot_id = 1u;
    record.event_seq = 7u;
    record.mono_ms = 9000u;
    record.event_type = LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_OPENED;
    record.door = (uint8_t)LOCKER_DOOR_STATE_OPEN;
    record.item = (uint8_t)LOCKER_ITEM_STATE_PRESENT;
    record.flags = LOCKER_WIRE_FLAG_SUPPLY_VALID | LOCKER_WIRE_FLAG_PHYSICAL_STABLE;
    record.supply_mv = 5000u;
    record.physical_revision = 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_wire_encode_device_event(&foreign_session, &epoch, &record, frame,
                                                      sizeof(frame), &frame_length));
    TEST_CHECK(port_server_inject(&env.server, frame, frame_length));
    (void)port_sink_wait(&env.sink, port_sink_reply_count(&env.sink), events + 1u,
                         PORT_SILENCE_MS);
    TEST_CHECK_U64(events, port_sink_event_count(&env.sink));

    /* The adapter is still healthy afterwards: a real request still answers. */
    TEST_CHECK(port_env_snapshot(&env, &snapshot));

    port_env_finish(&env);
}

static void test_drop_ack_no_reply(void)
{
    port_env_t env;
    locker_wire_get_snapshot_response_t snapshot;
    locker_event_key_t ack;
    unsigned replies;

    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "dropack"));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));

    memset(&ack, 0, sizeof(ack));
    (void)snprintf(ack.device_id, sizeof(ack.device_id), "01000000000000000000000c");
    ack.journal_epoch = snapshot.journal_epoch;
    ack.boot_id = 1u;
    ack.event_seq = 1u;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, port_server_drop_ack(&env.server, true));
    replies = port_sink_reply_count(&env.sink);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_ack_persisted(env.device, &ack));
    /* The request was applied but the reply was suppressed: no callback, no crash. */
    (void)port_sink_wait(&env.sink, replies + 1u, 0u, PORT_SILENCE_MS);
    TEST_CHECK_U64(replies, port_sink_reply_count(&env.sink));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, port_server_drop_ack(&env.server, false));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));

    port_env_finish(&env);
}

static void test_close_paths(void)
{
    port_env_t env;
    locker_event_key_t after;

    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "close"));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_close(env.device));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_device_get_snapshot(env.device, 5u));
    memset(&after, 0, sizeof(after));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_device_fetch_events(env.device, &after, 1u));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_close(env.device));
    port_env_finish(&env);

    /* Closing straight after open must not hang on a pending HELLO. */
    port_env_init(&env);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, port_env_open(&env, "closefast"));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_close(env.device));
    port_env_finish(&env);
}

static void port_server_hold(port_server_t *server, bool hold)
{
    pthread_mutex_lock(&server->lock);
    server->hold_responses = hold;
    pthread_mutex_unlock(&server->lock);
}

static size_t port_server_held(port_server_t *server, uint8_t *out)
{
    size_t length;
    pthread_mutex_lock(&server->lock);
    length = server->held_reply_length;
    memcpy(out, server->held_reply, length);
    pthread_mutex_unlock(&server->lock);
    return length;
}

/* Waits until the server thread has produced a reply for a held request, so a
 * test never has to guess a fixed sleep. */
static bool port_server_wait_held(port_server_t *server, int timeout_ms)
{
    struct timespec pause;
    int waited = 0;

    pause.tv_sec = 0;
    pause.tv_nsec = 5000000L;
    for (;;) {
        bool ready;
        pthread_mutex_lock(&server->lock);
        ready = server->held_reply_length > 0u;
        pthread_mutex_unlock(&server->lock);
        if (ready) {
            return true;
        }
        if (waited >= timeout_ms) {
            return false;
        }
        (void)nanosleep(&pause, NULL);
        waited += 5;
    }
}

static void test_response_session_and_error_mirror(void)
{
    port_env_t env;
    locker_wire_get_snapshot_response_t snapshot;
    locker_uuid_t command = port_make_uuid(0x91u);
    uint8_t valid[LOCKER_WIRE_MAX_FRAME], foreign[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    uint16_t crc;
    unsigned replies, frames;
    locker_device_reply_t reply;
    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "responses"));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));
    port_server_hold(&env.server, true);
    replies = port_sink_reply_count(&env.sink);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_get_snapshot(env.device, 700u));
    (void)port_sink_wait(&env.sink, replies + 1u, 0u, PORT_SILENCE_MS);
    frames = port_server_frames(&env.server);
    TEST_CHECK_STATUS(LOCKER_STATUS_BUSY, locker_device_get_snapshot(env.device, 700u));
    TEST_CHECK_STATUS(LOCKER_STATUS_BUSY, locker_device_time_anchor(env.device, 700u));
    TEST_CHECK_U64(frames, port_server_frames(&env.server));
    length = port_server_held(&env.server, valid);
    TEST_CHECK(length > LOCKER_WIRE_HEADER_SIZE + 16u);
    memcpy(foreign, valid, length);
    foreign[LOCKER_WIRE_HEADER_SIZE] ^= 1u;
    crc = locker_wire_crc16(foreign + 2u, length - 4u);
    foreign[length - 2u] = (uint8_t)crc;
    foreign[length - 1u] = (uint8_t)(crc >> 8);
    TEST_CHECK(port_server_inject(&env.server, foreign, length));
    (void)port_sink_wait(&env.sink, replies + 1u, 0u, PORT_SILENCE_MS);
    TEST_CHECK_U64(replies, port_sink_reply_count(&env.sink));
    TEST_CHECK(port_server_inject(&env.server, valid, length));
    TEST_CHECK(port_sink_wait(&env.sink, replies + 1u, 0u, PORT_WAIT_MS));
    reply = port_sink_reply(&env.sink);
    TEST_CHECK(locker_uuid_equal(&snapshot.session_id, &reply.body.get_snapshot.session_id));

    replies = port_sink_reply_count(&env.sink);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_get_snapshot(env.device, 701u));
    (void)port_sink_wait(&env.sink, replies + 1u, 0u, PORT_SILENCE_MS);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
        locker_wire_encode_error_response(2u, 701u, &snapshot.session_id, LOCKER_WIRE_RESULT_BUSY,
                                          valid, sizeof(valid), &length));
    TEST_CHECK(port_server_inject(&env.server, valid, length));
    TEST_CHECK(port_sink_wait(&env.sink, replies + 1u, 0u, PORT_WAIT_MS));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_BUSY, port_sink_reply(&env.sink).body.get_snapshot.result);
    port_server_hold(&env.server, false);
    replies = port_sink_reply_count(&env.sink);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_query_command(env.device, &command));
    TEST_CHECK(port_sink_wait(&env.sink, replies + 1u, 0u, PORT_WAIT_MS));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_UNKNOWN_RESULT,
                   port_sink_reply(&env.sink).body.query_command.result);
    TEST_CHECK(port_env_snapshot(&env, &snapshot));
    TEST_CHECK_U64(1u, snapshot.boot_id);
    TEST_CHECK_U64(1u, snapshot.physical_revision);
    port_env_finish(&env);
}

static void test_hello_identity_capabilities_and_failure(void)
{
    static const char *const tags[] = {"badidentity", "badcaps", "helloerror", "badhellosession"};
    size_t index;
    for (index = 0u; index < sizeof(tags) / sizeof(tags[0]); ++index) {
        port_env_t env;
        unsigned frames;
        port_env_init(&env);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, port_env_open(&env, tags[index]));
        if (index < 3u) {
            TEST_CHECK(port_sink_wait(&env.sink, 1u, 0u, PORT_WAIT_MS));
            TEST_CHECK_U64(index == 2u ? LOCKER_WIRE_RESULT_BUSY : LOCKER_WIRE_RESULT_BAD_PRECONDITION,
                           port_sink_reply(&env.sink).body.hello.result);
        } else {
            (void)port_sink_wait(&env.sink, 1u, 0u, PORT_SILENCE_MS);
            TEST_CHECK_U64(0u, port_sink_reply_count(&env.sink));
        }
        frames = port_server_frames(&env.server);
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_device_get_snapshot(env.device, 8u));
        TEST_CHECK_U64(frames, port_server_frames(&env.server));
        TEST_CHECK_U64(0u, port_sink_event_count(&env.sink));
        if (index == 3u) {
            uint8_t valid[LOCKER_WIRE_MAX_FRAME];
            size_t length = port_server_held(&env.server, valid);
            locker_wire_get_snapshot_response_t snapshot;
            TEST_CHECK(port_server_inject(&env.server, valid, length));
            TEST_CHECK(port_sink_wait(&env.sink, 1u, 0u, PORT_WAIT_MS));
            TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, port_sink_reply(&env.sink).body.hello.result);
            TEST_CHECK(port_env_snapshot(&env, &snapshot));
        }
        port_env_finish(&env);
    }
}

static void test_bound_dtos_rejected_without_writes(void)
{
    port_env_t env;
    locker_wire_get_snapshot_response_t snapshot;
    locker_unlock_intent_t intent;
    locker_ready_guard_t ready;
    locker_context_guard_t context;
    locker_event_key_t key;
    unsigned frames;
    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "bindings"));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));
    memset(&intent, 0, sizeof(intent));
    intent.session_id = snapshot.session_id;
    intent.session_id.bytes[0] ^= 1u;
    intent.journal_epoch = snapshot.journal_epoch;
    intent.expected_boot_id = snapshot.boot_id;
    intent.expected_physical_revision = snapshot.physical_revision;
    intent.slot_id = 1u;
    intent.kind = LOCKER_OPERATION_KIND_BORROW;
    intent.action_no = 1u;
    intent.not_after_mcu_ms = 6000u;
    intent.operation_id = port_make_uuid(0x50u);
    intent.command_id = port_make_uuid(0x60u);
    frames = port_server_frames(&env.server);
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, locker_device_execute_once(env.device, &intent));
    memset(&ready, 0, sizeof(ready));
    ready.session_id = intent.session_id;
    ready.journal_epoch = snapshot.journal_epoch;
    ready.expected_boot_id = snapshot.boot_id;
    ready.expected_physical_revision = snapshot.physical_revision;
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, locker_device_resume_ready(env.device, &ready));
    memset(&context, 0, sizeof(context));
    context.session_id = snapshot.session_id;
    context.journal_epoch = snapshot.journal_epoch;
    context.expected_boot_id = snapshot.boot_id + 1u;
    context.expected_physical_revision = snapshot.physical_revision;
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, locker_device_release_context(env.device, &context));
    memset(&key, 0, sizeof(key));
    (void)snprintf(key.device_id, sizeof(key.device_id), "%s", env.config.expected_device_id);
    key.journal_epoch = snapshot.journal_epoch;
    key.journal_epoch.bytes[0] ^= 1u;
    key.boot_id = 1u;
    key.event_seq = 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, locker_device_fetch_events(env.device, &key, 4u));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, locker_device_ack_persisted(env.device, &key));
    key.journal_epoch = snapshot.journal_epoch;
    key.device_id[0] = 'f';
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, locker_device_fetch_events(env.device, &key, 4u));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, locker_device_ack_persisted(env.device, &key));
    (void)port_sink_wait(&env.sink, port_sink_reply_count(&env.sink) + 1u, 0u, PORT_SILENCE_MS);
    TEST_CHECK_U64(frames, port_server_frames(&env.server));
    port_env_finish(&env);
}

static void test_sink_backpressure_stops_writes(void)
{
    locker_status_t rejected[] = {LOCKER_STATUS_BUSY, LOCKER_STATUS_NO_SPACE};
    size_t index;
    for (index = 0u; index < sizeof(rejected) / sizeof(rejected[0]); ++index) {
        port_env_t env;
        locker_health_report_t health;
        locker_unlock_intent_t intent;
        locker_wire_get_snapshot_response_t snapshot;
        unsigned replies, frames;
        port_env_init(&env);
        TEST_CHECK(port_env_open_ready(&env, "backpressure"));
        TEST_CHECK(port_env_snapshot(&env, &snapshot));
        pthread_mutex_lock(&env.sink.lock);
        env.sink.reject_status = rejected[index];
        pthread_mutex_unlock(&env.sink.lock);
        replies = port_sink_reply_count(&env.sink);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_get_snapshot(env.device, 77u));
        TEST_CHECK(port_sink_wait(&env.sink, replies + 1u, 0u, PORT_WAIT_MS));
        (void)port_sink_wait(&env.sink, replies + 2u, 0u, PORT_SILENCE_MS);
        memset(&health, 0, sizeof(health));
        memset(&intent, 0, sizeof(intent));
        intent.session_id = snapshot.session_id;
        intent.journal_epoch = snapshot.journal_epoch;
        intent.expected_boot_id = snapshot.boot_id;
        intent.expected_physical_revision = snapshot.physical_revision;
        intent.slot_id = 1u;
        intent.kind = LOCKER_OPERATION_KIND_BORROW;
        frames = port_server_frames(&env.server);
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_device_send_health(env.device, &health));
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_device_execute_once(env.device, &intent));
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_device_query_command(env.device, &intent.command_id));
        TEST_CHECK_U64(frames, port_server_frames(&env.server));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_close(env.device));
        port_env_finish(&env);
    }
}

/* stage-01-task-cards.md:123: timeout tests use the injected clock, but the
 * integration suite must keep at least one real-clock smoke test so nothing
 * works only under the test double. This drives the whole adapter (open, HELLO,
 * GET_SNAPSHOT, close) on locker_clock_port_real() and asserts the replies carry
 * plausible real monotonic timestamps. */
static uint64_t port_real_now_ms(void);

static void test_real_clock_smoke(void)
{
    port_env_t env;
    locker_wire_get_snapshot_response_t snapshot;
    uint64_t before, after;
    unsigned replies;
    locker_device_reply_t reply;
    port_env_init(&env);
    env.real_clock = true;
    before = port_real_now_ms();
    TEST_CHECK(port_env_open_ready(&env, "realclock"));
    replies = port_sink_reply_count(&env.sink);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_get_snapshot(env.device, 910u));
    TEST_CHECK(port_sink_wait(&env.sink, replies + 1u, 0u, PORT_WAIT_MS));
    reply = port_sink_reply(&env.sink);
    TEST_CHECK_U64((uint64_t)(2u | LOCKER_WIRE_RESPONSE_FLAG), reply.message_type);
    TEST_CHECK_U64(910u, reply.request_no);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, reply.body.get_snapshot.result);
    snapshot = reply.body.get_snapshot;
    after = port_real_now_ms();
    /* The port stamps received_host_mono_ms from the injected clock, so on the
     * real clock port it must land inside the wall-clock window of the call. */
    TEST_CHECK(reply.received_host_mono_ms >= before);
    TEST_CHECK(reply.received_host_mono_ms <= after);
    TEST_CHECK_U64(1u, snapshot.boot_id);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_close(env.device));
    port_env_finish(&env);
}

static void test_pending_timeout_and_request_wrap(void)
{
    port_env_t env;
    locker_wire_get_snapshot_response_t snapshot;
    locker_uuid_t command = port_make_uuid(0x84u);
    uint8_t late[LOCKER_WIRE_MAX_FRAME];
    size_t length;
    unsigned replies;
    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "timeout"));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));
    port_server_hold(&env.server, true);
    replies = port_sink_reply_count(&env.sink);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_get_snapshot(env.device, 90u));
    TEST_CHECK(port_server_wait_held(&env.server, PORT_WAIT_MS));
    /* The pending deadline runs on the injected clock (config.clock), exactly
     * like every other timestamp this port produces, so the 5000 ms timeout is
     * exercised by moving the test clock instead of sleeping for five seconds.
     * Production injects locker_clock_port_real(), so no public API can move
     * time. stage-01-task-cards.md:123 requires exactly this split. */
    env.clock.monotonic_ms += PORT_PENDING_TIMEOUT_MS + 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_BUSY, locker_device_get_snapshot(env.device, 90u));
    length = port_server_held(&env.server, late);
    TEST_CHECK(length > 0u);
    TEST_CHECK(port_server_inject(&env.server, late, length));
    (void)port_sink_wait(&env.sink, replies + 1u, 0u, PORT_SILENCE_MS);
    TEST_CHECK_U64(replies, port_sink_reply_count(&env.sink));
    port_server_hold(&env.server, false);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_query_command(env.device, &command));
    TEST_CHECK(port_sink_wait(&env.sink, replies + 1u, 0u, PORT_WAIT_MS));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_get_snapshot(env.device, UINT32_MAX));
    TEST_CHECK(port_sink_wait(&env.sink, replies + 2u, 0u, PORT_WAIT_MS));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_device_query_command(env.device, &command));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_device_get_snapshot(env.device, 1u));
    port_env_finish(&env);
}

static void test_execute_ack_loss_queries_without_replay(void)
{
    port_env_t env;
    locker_wire_get_snapshot_response_t snapshot;
    locker_unlock_intent_t intent;
    locker_device_reply_t reply;
    unsigned replies, frames;
    uint64_t events_before;
    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "executeackloss"));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));
    TEST_CHECK(port_env_reconcile(&env, &snapshot));
    memset(&intent, 0, sizeof(intent));
    intent.session_id = snapshot.session_id;
    intent.journal_epoch = snapshot.journal_epoch;
    intent.expected_boot_id = snapshot.boot_id;
    intent.expected_physical_revision = snapshot.physical_revision;
    intent.action_no = 1u;
    intent.not_after_mcu_ms = 6000u;
    intent.operation_id = port_make_uuid(0x71u);
    intent.command_id = port_make_uuid(0x81u);
    intent.kind = LOCKER_OPERATION_KIND_BORROW;
    intent.slot_id = 1u;
    pthread_mutex_lock(&env.server.lock);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_sim_control_drop_execute_ack(env.server.sim, true));
    pthread_mutex_unlock(&env.server.lock);
    pthread_mutex_lock(&env.server.lock);
    events_before = locker_sim_event_count(env.server.sim);
    pthread_mutex_unlock(&env.server.lock);
    replies = port_sink_reply_count(&env.sink);
    frames = port_server_frames(&env.server);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_execute_once(env.device, &intent));
    (void)port_sink_wait(&env.sink, replies + 1u, 0u, PORT_SILENCE_MS);
    TEST_CHECK_U64(replies, port_sink_reply_count(&env.sink));
    TEST_CHECK_U64(frames + 1u, port_server_frames(&env.server));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_query_command(env.device, &intent.command_id));
    TEST_CHECK(port_sink_wait(&env.sink, replies + 1u, 0u, PORT_WAIT_MS));
    reply = port_sink_reply(&env.sink);
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, reply.body.query_command.result);
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED, reply.body.query_command.command_state);
    TEST_CHECK_U64(1u, reply.body.query_command.action_no);
    (void)port_sink_wait(&env.sink, replies + 2u, 0u, PORT_SILENCE_MS);
    TEST_CHECK_U64(frames + 2u, port_server_frames(&env.server));
    pthread_mutex_lock(&env.server.lock);
    TEST_CHECK_U64(events_before + 2u, locker_sim_event_count(env.server.sim));
    pthread_mutex_unlock(&env.server.lock);
    port_env_finish(&env);
}

static void test_historical_query_does_not_replace_current_mirror(void)
{
    port_env_t env;
    locker_wire_get_snapshot_response_t snapshot;
    locker_wire_response_union_t response;
    locker_context_guard_t guard;
    locker_uuid_t command = port_make_uuid(0x73u);
    uint8_t frame[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    unsigned replies;
    uint32_t request_no;
    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "historymirror"));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));
    port_server_hold(&env.server, true);
    replies = port_sink_reply_count(&env.sink);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_query_command(env.device, &command));
    (void)port_sink_wait(&env.sink, replies + 1u, 0u, PORT_SILENCE_MS);
    pthread_mutex_lock(&env.server.lock);
    request_no = env.server.last_request_no;
    pthread_mutex_unlock(&env.server.lock);
    memset(&response, 0, sizeof(response));
    response.query_command.session_id = snapshot.session_id;
    response.query_command.operation_id = port_make_uuid(0x62u);
    response.query_command.command_id = command;
    response.query_command.command_session_id = port_make_uuid(0x81u);
    response.query_command.command_boot_id = snapshot.boot_id;
    response.query_command.command_state = LOCKER_WIRE_COMMAND_STATE_PULSE_FINISHED;
    response.query_command.action_no = 9u;
    response.query_command.physical_revision = 999u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
        locker_wire_encode_response(8u, request_no, &response, frame, sizeof(frame), &length));
    TEST_CHECK(port_server_inject(&env.server, frame, length));
    TEST_CHECK(port_sink_wait(&env.sink, replies + 1u, 0u, PORT_WAIT_MS));
    TEST_CHECK_U64(9u, port_sink_reply(&env.sink).body.query_command.action_no);
    port_server_hold(&env.server, false);
    memset(&guard, 0, sizeof(guard));
    guard.session_id = snapshot.session_id;
    guard.journal_epoch = snapshot.journal_epoch;
    guard.expected_boot_id = snapshot.boot_id;
    guard.expected_physical_revision = snapshot.physical_revision;
    guard.operation_id = port_make_uuid(0x64u);
    guard.command_id = port_make_uuid(0x74u);
    replies = port_sink_reply_count(&env.sink);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_release_context(env.device, &guard));
    TEST_CHECK(port_sink_wait(&env.sink, replies + 1u, 0u, PORT_WAIT_MS));
    TEST_CHECK_U64(LOCKER_WIRE_RESULT_OK, port_sink_reply(&env.sink).body.release_context.result);
    port_env_finish(&env);
}

static uint64_t port_real_now_ms(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void test_transport_bounded_write_and_disconnect(void)
{
    locker_device_transport_t transport;
    int pair[2];
    int capacity = 4096;
    uint8_t *bytes;
    uint64_t started;
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    locker_device_transport_init(&transport);
    transport.fd = pair[0];
    transport.connected = true;
    (void)setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &capacity, sizeof(capacity));
    bytes = (uint8_t *)calloc(1024u * 1024u, 1u);
    TEST_CHECK(bytes != NULL);
    if (bytes != NULL) {
        started = port_real_now_ms();
        TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR,
            locker_device_transport_write(&transport, bytes, 1024u * 1024u));
        TEST_CHECK(port_real_now_ms() - started < 2500u);
        TEST_CHECK(!locker_device_transport_connected(&transport));
        free(bytes);
    }
    locker_device_transport_close(&transport);
    (void)close(pair[1]);
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    locker_device_transport_init(&transport);
    transport.fd = pair[0];
    transport.connected = true;
    (void)close(pair[1]);
    TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR,
        locker_device_transport_write(&transport, (const uint8_t *)"x", 1u));
    locker_device_transport_close(&transport);
}

/*
 * A listener whose accept queue is full parks a blocking connect() inside the
 * kernel, where no clock, signal or deadline can reach it: the documented
 * "connects for at most timeout_ms of real time" contract would then be a lie.
 * The attempt therefore has to be non-blocking and deadline-bounded.
 */
/* Find the real adapter socket by its peer address, without exposing a test
 * fd/API in production. A small kernel send buffer makes a non-reading peer
 * apply real backpressure within the port's 16 pending-request bound. */
static bool port_shrink_adapter_buffer(const char *path)
{
    int fd;
    for (fd = 3; fd < 1024; ++fd) {
        struct sockaddr_un peer;
        socklen_t length = sizeof(peer);
        int bytes = 1024;
        memset(&peer, 0, sizeof(peer));
        if (getpeername(fd, (struct sockaddr *)&peer, &length) == 0 &&
            peer.sun_family == AF_UNIX && strcmp(peer.sun_path, path) == 0) {
            return setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes)) == 0;
        }
    }
    return false;
}

static void test_async_send_under_socket_backpressure(void)
{
    unsigned pass;
    for (pass = 0u; pass < 2u; ++pass) {
        port_env_t env;
        uint64_t deadline, longest = 0u, started;
        unsigned accepted = 0u, busy = 0u, number;
        bool paused = false;
        port_env_init(&env);
        env.real_clock = true;
        TEST_CHECK(port_env_open_ready(&env, "asyncpressure"));
        TEST_CHECK(port_shrink_adapter_buffer(env.server.socket_path));
        pthread_mutex_lock(&env.server.lock);
        env.server.pause_reads = true;
        pthread_mutex_unlock(&env.server.lock);
        deadline = port_real_now_ms() + 1000u;
        do {
            struct timespec delay = {0, 1000000L};
            pthread_mutex_lock(&env.server.lock);
            paused = env.server.reads_paused;
            pthread_mutex_unlock(&env.server.lock);
            if (!paused) (void)nanosleep(&delay, NULL);
        } while (!paused && port_real_now_ms() < deadline);
        TEST_CHECK(paused);
        for (number = 1000u; number < 1040u; ++number) {
            locker_status_t status;
            uint64_t elapsed;
            started = port_real_now_ms();
            status = locker_device_get_snapshot(env.device, number);
            elapsed = port_real_now_ms() - started;
            if (elapsed > longest) longest = elapsed;
            if (status == LOCKER_STATUS_OK) ++accepted;
            else if (status == LOCKER_STATUS_BUSY) ++busy;
            else TEST_CHECK(false);
        }
        TEST_CHECK(accepted > 0u && accepted <= 16u && busy > 0u);
        TEST_CHECK(longest < 200u);
        if (pass == 0u) {
            /* Let the writer reach poll, then close must wake it rather than
             * waiting out the one-second IO budget or draining queued frames. */
            struct timespec delay = {0, 100000000L};
            (void)nanosleep(&delay, NULL);
        } else {
            locker_status_t status;
            deadline = port_real_now_ms() + 2500u;
            do {
                struct timespec delay = {0, 10000000L};
                (void)nanosleep(&delay, NULL);
                status = locker_device_get_snapshot(env.device, 2000u);
            } while (status == LOCKER_STATUS_BUSY && port_real_now_ms() < deadline);
            TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, status);
        }
        started = port_real_now_ms();
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_close(env.device));
        TEST_CHECK(port_real_now_ms() - started < 500u);
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY,
                          locker_device_get_snapshot(env.device, 3000u));
        port_env_finish(&env);
    }
}

static void test_connect_deadline_when_accept_queue_full(void)
{
    locker_device_transport_t transport;
    struct sockaddr_un address;
    char path[LOCKER_SIM_PATH_SIZE];
    int listener = -1;
    int queued[8];
    size_t queued_count = 0u;
    size_t index;
    bool full = false;
    uint64_t started;
    size_t path_length;

    (void)snprintf(path, sizeof(path), "/tmp/locker_s103_backlog_%u.sock", (unsigned)getpid());
    (void)unlink(path);
    listener = socket(AF_UNIX, SOCK_STREAM, 0);
    TEST_CHECK(listener >= 0);
    if (listener < 0) {
        return;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    path_length = strlen(path);
    memcpy(address.sun_path, path, path_length + 1u);
    TEST_CHECK(bind(listener, (const struct sockaddr *)&address, (socklen_t)sizeof(address)) == 0);
    /* A small backlog fills predictably; no server thread ever calls accept(). */
    TEST_CHECK(listen(listener, 1) == 0);

    for (index = 0u; index < (sizeof(queued) / sizeof(queued[0])); index += 1u) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        int flags;
        TEST_CHECK(fd >= 0);
        if (fd < 0) {
            break;
        }
        flags = fcntl(fd, F_GETFL, 0);
        TEST_CHECK(flags >= 0);
        TEST_CHECK(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
        if (connect(fd, (const struct sockaddr *)&address, (socklen_t)sizeof(address)) != 0) {
            bool exhausted = (errno == EAGAIN || errno == EWOULDBLOCK);
            (void)close(fd);
            if (exhausted) {
                full = true;
                break;
            }
            TEST_CHECK(errno == EINPROGRESS);
            queued[queued_count] = fd;
            queued_count += 1u;
            continue;
        }
        queued[queued_count] = fd;
        queued_count += 1u;
    }
    TEST_CHECK(full);
    TEST_CHECK(queued_count > 0u);

    locker_device_transport_init(&transport);
    started = port_real_now_ms();
    TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR,
        locker_device_transport_connect(&transport, path, 700u));
    /* 700 ms budget plus scheduling slack; the pre-fix blocking connect never
     * returned at all until an accept() happened. */
    TEST_CHECK(port_real_now_ms() - started < 3000u);
    TEST_CHECK(!locker_device_transport_connected(&transport));
    locker_device_transport_close(&transport);

    /* Draining one slot must make the same call succeed: the deadline path must
     * not leave the transport wedged. */
    TEST_CHECK(accept(listener, NULL, NULL) >= 0);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_transport_connect(&transport, path, 2000u));
    TEST_CHECK(locker_device_transport_connected(&transport));
    locker_device_transport_close(&transport);

    for (index = 0u; index < queued_count; index += 1u) {
        (void)close(queued[index]);
    }
    (void)close(listener);
    (void)unlink(path);
}

typedef struct port_close_race {
    locker_device_t *device;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool started;
    bool unexpected;
} port_close_race_t;

static void *port_racing_writer(void *user)
{
    port_close_race_t *race = (port_close_race_t *)user;
    uint32_t number;
    pthread_mutex_lock(&race->lock);
    race->started = true;
    pthread_cond_broadcast(&race->cond);
    pthread_mutex_unlock(&race->lock);
    for (number = 1000u; number < 1100u; ++number) {
        locker_status_t status = locker_device_get_snapshot(race->device, number);
        if (status == LOCKER_STATUS_NOT_READY) break;
        if (status != LOCKER_STATUS_OK && status != LOCKER_STATUS_BUSY) {
            race->unexpected = true;
            break;
        }
    }
    return NULL;
}

static void test_close_serializes_with_writer(void)
{
    port_env_t env;
    port_close_race_t race;
    pthread_t writer;
    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "closerace"));
    memset(&race, 0, sizeof(race));
    race.device = env.device;
    TEST_CHECK(pthread_mutex_init(&race.lock, NULL) == 0);
    TEST_CHECK(pthread_cond_init(&race.cond, NULL) == 0);
    TEST_CHECK(pthread_create(&writer, NULL, port_racing_writer, &race) == 0);
    pthread_mutex_lock(&race.lock);
    while (!race.started) (void)pthread_cond_wait(&race.cond, &race.lock);
    pthread_mutex_unlock(&race.lock);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_close(env.device));
    TEST_CHECK(pthread_join(writer, NULL) == 0);
    TEST_CHECK(!race.unexpected);
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_device_get_snapshot(env.device, 1100u));
    pthread_cond_destroy(&race.cond);
    pthread_mutex_destroy(&race.lock);
    port_env_finish(&env);
}

/* Keep the same simulator, journal, boot, physical inputs and listener. Only
 * the adapter connection and protocol session change, so rejection cannot be
 * explained by a changed epoch/boot/revision or an expired intent. */
static void test_old_intent_after_close_open_rejected_without_write(void)
{
    port_env_t env;
    locker_wire_get_snapshot_response_t before, after;
    locker_unlock_intent_t old_intent;
    unsigned replies, frames;
    int result;

    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "oldintent"));
    TEST_CHECK(port_env_snapshot(&env, &before));
    memset(&old_intent, 0, sizeof(old_intent));
    old_intent.operation_id = port_make_uuid(0x51u);
    old_intent.command_id = port_make_uuid(0x61u);
    old_intent.session_id = before.session_id;
    old_intent.journal_epoch = before.journal_epoch;
    old_intent.expected_boot_id = before.boot_id;
    old_intent.expected_physical_revision = before.physical_revision;
    old_intent.action_no = 1u;
    old_intent.not_after_mcu_ms = before.sampled_mono_ms + 1000u;
    old_intent.slot_id = 1u;
    old_intent.kind = LOCKER_OPERATION_KIND_BORROW;

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_close(env.device));
    result = pthread_join(env.server.thread, NULL);
    TEST_CHECK(result == 0);
    if (result != 0) {
        port_env_finish(&env);
        return;
    }
    env.server.thread_started = false;
    result = pthread_create(&env.server.thread, NULL, port_server_main, &env.server);
    TEST_CHECK(result == 0);
    if (result != 0) {
        port_env_finish(&env);
        return;
    }
    env.server.thread_started = true;
    replies = port_sink_reply_count(&env.sink);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_open(env.device, &env.config));
    TEST_CHECK(port_sink_wait(&env.sink, replies + 1u, 0u, PORT_WAIT_MS));
    TEST_CHECK(port_env_snapshot(&env, &after));
    TEST_CHECK(!locker_uuid_equal(&before.session_id, &after.session_id));
    TEST_CHECK(locker_uuid_equal(&before.journal_epoch, &after.journal_epoch));
    TEST_CHECK_U64(before.boot_id, after.boot_id);
    TEST_CHECK_U64(before.physical_revision, after.physical_revision);
    TEST_CHECK(old_intent.not_after_mcu_ms > after.sampled_mono_ms);
    TEST_CHECK_U64(LOCKER_WIRE_DOOR_STATE_CLOSED, after.door);
    TEST_CHECK_U64(LOCKER_WIRE_ITEM_STATE_PRESENT, after.item);
    TEST_CHECK(port_env_reconcile(&env, &after));

    frames = port_server_frames(&env.server);
    replies = port_sink_reply_count(&env.sink);
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, locker_device_execute_once(env.device, &old_intent));
    (void)port_sink_wait(&env.sink, replies + 1u, 0u, PORT_SILENCE_MS);
    TEST_CHECK_U64(frames, port_server_frames(&env.server));
    TEST_CHECK_U64(replies, port_sink_reply_count(&env.sink));
    pthread_mutex_lock(&env.server.lock);
    TEST_CHECK_U64(0u, locker_sim_action_watermark(env.server.sim));
    TEST_CHECK_U64(0u, locker_sim_pulse_count(env.server.sim));
    pthread_mutex_unlock(&env.server.lock);
    port_env_finish(&env);
}

/* A reply is matched by request number AND response type. A frame that reuses a
 * live request number under another response type must not consume the pending
 * entry; otherwise one stray frame would silently swallow the real answer. */
static void test_pending_entry_matches_response_type(void)
{
    port_env_t env;
    locker_wire_get_snapshot_response_t snapshot;
    uint8_t valid[LOCKER_WIRE_MAX_FRAME], other[LOCKER_WIRE_MAX_FRAME];
    size_t valid_length = 0u, other_length = 0u;
    unsigned replies, frames;
    locker_device_reply_t reply;

    port_env_init(&env);
    TEST_CHECK(port_env_open_ready(&env, "pendingtype"));
    TEST_CHECK(port_env_snapshot(&env, &snapshot));
    port_server_hold(&env.server, true);
    replies = port_sink_reply_count(&env.sink);
    frames = port_server_frames(&env.server);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_device_get_snapshot(env.device, 704u));
    (void)port_sink_wait(&env.sink, replies + 1u, 0u, PORT_SILENCE_MS);
    TEST_CHECK_U64(frames + 1u, port_server_frames(&env.server));
    valid_length = port_server_held(&env.server, valid);
    TEST_CHECK(valid_length > LOCKER_WIRE_HEADER_SIZE + 16u);
    TEST_CHECK_U64((uint64_t)(2u | LOCKER_WIRE_RESPONSE_FLAG), valid[3]);

    /* Same request number and session, but a FETCH_EVENTS response instead of
     * the SNAPSHOT the adapter is waiting for. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
        locker_wire_encode_error_response(3u, 704u, &snapshot.session_id, LOCKER_WIRE_RESULT_BUSY,
                                          other, sizeof(other), &other_length));
    TEST_CHECK(port_server_inject(&env.server, other, other_length));
    (void)port_sink_wait(&env.sink, replies + 1u, 0u, PORT_SILENCE_MS);
    TEST_CHECK_U64(replies, port_sink_reply_count(&env.sink));

    /* The genuine answer is still pending and must be delivered unchanged. */
    TEST_CHECK(port_server_inject(&env.server, valid, valid_length));
    TEST_CHECK(port_sink_wait(&env.sink, replies + 1u, 0u, PORT_WAIT_MS));
    reply = port_sink_reply(&env.sink);
    TEST_CHECK_U64((uint64_t)(2u | LOCKER_WIRE_RESPONSE_FLAG), reply.message_type);
    TEST_CHECK_U64(704u, reply.request_no);
    TEST_CHECK(locker_uuid_equal(&snapshot.session_id, &reply.body.get_snapshot.session_id));
    port_server_hold(&env.server, false);
    port_env_finish(&env);
}

int main(void)
{
    test_async_send_under_socket_backpressure();
    test_old_intent_after_close_open_rejected_without_write();
    test_historical_query_does_not_replace_current_mirror();
    test_transport_bounded_write_and_disconnect();
    test_connect_deadline_when_accept_queue_full();
    test_close_serializes_with_writer();
    test_response_session_and_error_mirror();
    test_pending_entry_matches_response_type();
    test_hello_identity_capabilities_and_failure();
    test_bound_dtos_rejected_without_writes();
    test_sink_backpressure_stops_writes();
    test_pending_timeout_and_request_wrap();
    test_real_clock_smoke();
    test_execute_ack_loss_queries_without_replay();
    test_open_and_snapshot();
    test_config_validation();
    test_port_guards();
    test_execute_and_query();
    test_fetch_ack_resume_release_health_anchor();
    test_events_delivered();
    test_foreign_event_dropped();
    test_drop_ack_no_reply();
    test_close_paths();
    TEST_REPORT("contract.device.test_device_port");
}
