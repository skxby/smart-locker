/*
 * S1-04 集成测试：真实 Core 线程 + 真实 SQLite + 真实设备适配器 + 真实模拟器。
 *
 * 与 tests/unit/domain 的区别（这正是本文件存在的理由）：
 *   - 单元测试直接调 domain_handle_command / domain_completion_wait_final，
 *     并用 dt_core_device_ready() 手工把 device_session_up 置真，绕过了启动路径。
 *     本文件只走公开端口：locker_core_submit / locker_core_wait_result /
 *     locker_core_query / locker_local_presence_submit /
 *     locker_local_maintenance_reconcile，设备会话必须由 Core 自己的泵真实建立。
 *   - 单元测试用假仓储；本文件用真实 repository.c + 真实 SQLite 文件。
 *   - 单元测试用假设备端口；本文件用真实 port.c + 真实 AF_UNIX socket +
 *     真实 sim.c，通过进程内 server 驱动。
 *
 * 覆盖的核心证据：S1-04 任务卡要求的「准备 → 发送 → ACK → 脉冲 → 落库」
 * 中断位置矩阵，以及重启恢复屏障不重放旧动作。
 *
 * 断言只走公开观察面（Core 查询端口 + 模拟器观测 + server 请求计数），
 * 不读取 Core 私有结构，因此不会把实现细节当成验收。
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "locker/common/clock.h"
#include "locker/common/uuid.h"
#include "locker_contracts.h"
#include "locker/device/codec.h"
#include "locker/device/transport.h"
#include "locker/device/wire.h"
#include "locker/simulator/sim.h"
#include "test_support.h"
#ifdef LOCKER_HTTP_FLOW_FIXTURE
#include <sodium.h>
#endif

#define IT_WAIT_MS 8000u
#define IT_SILENCE_MS 300u
#define IT_SERVER_BUFFER (LOCKER_WIRE_MAX_FRAME * 8u)

/* 契约里的 UUID 是 RFC4122 v4 形态（第 13 位固定为 4、第 17 位为 8/9/a/b）。
 * 这里用 canonical 文本解析，避免手写 bytes 与仓储记录对不上。 */
#define IT_USER_TEXT "11111111-1111-4111-8111-111111111111"
#define IT_SESSION_TEXT "22222222-2222-4222-8222-222222222222"
#define IT_ADMIN_TEXT "55555555-5555-4555-8555-555555555555"
#define IT_ADMIN_SESSION_TEXT "66666666-6666-4666-8666-666666666666"
#define IT_TOOL_TEXT "33333333-3333-4333-8333-333333333333"
#define IT_HOST_TEXT "44444444-4444-4444-8444-444444444444"
#define IT_TOKEN_HASH "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
/* 会话过期时间必须晚于真实时钟，否则 principal 复检会拒。 */
#define IT_SESSION_EXPIRES "2037-01-01T00:00:00Z"

/* ---------- 进程内 AF_UNIX 服务器：驱动真实 sim.c ---------- */

typedef struct it_server {
    int listen_fd;
    int client_fd;
    pthread_t thread;
    bool thread_started;
    pthread_mutex_t lock;
    char socket_path[LOCKER_SIM_PATH_SIZE];
    char journal_path[LOCKER_SIM_PATH_SIZE];
    locker_sim_t *sim;
    /* 按报文类型计请求数：中断矩阵用它证明「只发一次 EXECUTE」「之后只发 QUERY」。 */
    unsigned hello_requests;
    unsigned snapshot_requests;
    unsigned fetch_requests;
    unsigned ack_requests;
    unsigned execute_requests;
    unsigned query_requests;
    unsigned health_requests;
    unsigned resume_requests;
    unsigned release_requests;
    unsigned total_frames;
    bool pause_reads, reads_paused, stopping;
} it_server_t;

static bool it_write_all(int fd, const uint8_t *data, size_t length)
{
    size_t written = 0;
    while (written < length) {
        ssize_t rc = send(fd, data + written, length - written, MSG_NOSIGNAL);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (rc == 0) {
            return false;
        }
        written += (size_t)rc;
    }
    return true;
}

/* 与 host/device/src/wire.c 相同的分帧规则：返回帧总长、0 表示不足一帧、
 * -1 表示头部已损坏需要丢 1 字节重新同步。 */
static long it_frame_total(const uint8_t *buffer, size_t available)
{
    uint16_t payload_length;
    size_t total;
    if (available < 2u) {
        return 0;
    }
    if (buffer[0] != LOCKER_WIRE_MAGIC_FIRST || buffer[1] != LOCKER_WIRE_MAGIC_SECOND) {
        return -1;
    }
    if (available < LOCKER_WIRE_HEADER_SIZE) {
        return 0;
    }
    payload_length = (uint16_t)((uint16_t)buffer[4] | (uint16_t)((uint16_t)buffer[5] << 8));
    if (payload_length > LOCKER_WIRE_MAX_PAYLOAD) {
        return -1;
    }
    total = locker_wire_frame_size(payload_length);
    if (available < total) {
        return 0;
    }
    return (long)total;
}

static void it_server_count(it_server_t *server, uint8_t type)
{
    switch (type) {
    case 1u: server->hello_requests += 1u; break;
    case 2u: server->snapshot_requests += 1u; break;
    case 3u: server->fetch_requests += 1u; break;
    case 4u: server->ack_requests += 1u; break;
    case 5u: server->resume_requests += 1u; break;
    case 7u: server->execute_requests += 1u; break;
    case 8u: server->query_requests += 1u; break;
    case 9u: server->health_requests += 1u; break;
    case 11u: server->release_requests += 1u; break;
    default: break;
    }
}

static void it_server_consume(it_server_t *server, int client, uint8_t *buffer, size_t *used)
{
    size_t offset = 0;
    uint8_t reply[LOCKER_WIRE_MAX_FRAME];
    uint8_t event[LOCKER_WIRE_MAX_FRAME];
    uint8_t out[IT_SERVER_BUFFER];
    size_t reply_length;
    size_t out_length;

    while (offset < *used) {
        long total = it_frame_total(buffer + offset, *used - offset);
        if (total < 0) {
            offset += 1u;
            continue;
        }
        if (total == 0) {
            break;
        }

        reply_length = 0;
        out_length = 0;
        pthread_mutex_lock(&server->lock);
        it_server_count(server, buffer[offset + 3u]);
        (void)locker_sim_handle_frame(server->sim, buffer + offset, (size_t)total, reply,
                                      sizeof(reply), &reply_length);
        server->total_frames += 1u;
        pthread_mutex_unlock(&server->lock);

        /* 回复（长度可能是 0：drop_ack / drop_execute_ack 故意抑制）。 */
        if (reply_length != 0u) {
            memcpy(out + out_length, reply, reply_length);
            out_length += reply_length;
        }

        /* 同一请求的应答之后，把所有排队的事件追加在同一批字节里。 */
        for (;;) {
            size_t event_length = 0;
            locker_status_t status;
            pthread_mutex_lock(&server->lock);
            status = locker_sim_pop_event(server->sim, event, sizeof(event), &event_length);
            pthread_mutex_unlock(&server->lock);
            if (status != LOCKER_STATUS_OK || event_length == 0u) {
                break;
            }
            if (out_length + event_length > sizeof(out)) {
                break;
            }
            memcpy(out + out_length, event, event_length);
            out_length += event_length;
        }

        if (out_length != 0u) {
            (void)it_write_all(client, out, out_length);
        }
        offset += (size_t)total;
    }

    if (offset != 0u) {
        memmove(buffer, buffer + offset, *used - offset);
        *used -= offset;
    }
}

/* 单连接服务循环：消费到对端关闭为止。计数器（it_server_count）挂在
 * server 结构体上，跨连接天然保留。 */
static void it_server_serve_connection(it_server_t *server, int client)
{
    uint8_t *buffer;
    size_t used = 0;

    buffer = (uint8_t *)calloc(1, IT_SERVER_BUFFER);
    if (buffer == NULL) {
        close(client);
        return;
    }
    pthread_mutex_lock(&server->lock);
    server->client_fd = client;
    pthread_mutex_unlock(&server->lock);

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
        if (used == IT_SERVER_BUFFER) {
            break;
        }
        received = recv(client, buffer + used, IT_SERVER_BUFFER - used, 0);
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
        it_server_consume(server, client, buffer, &used);
    }

    free(buffer);
    pthread_mutex_lock(&server->lock);
    server->client_fd = -1;
    pthread_mutex_unlock(&server->lock);
    close(client);
}

static void *it_server_main(void *user)
{
    it_server_t *server = (it_server_t *)user;

    /* 循环 accept：restart_barrier 用例会销毁旧设备再开新设备，旧连接
     * 关闭后 server 必须能接受下一个连接，否则新设备的 HELLO 没人收，
     * 重连链路（hello_requests 不再增长）在这里断掉。 */
    for (;;) {
        int client = accept(server->listen_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        it_server_serve_connection(server, client);
    }
    return NULL;
}

static void it_make_paths(it_server_t *server, const char *tag)
{
    static unsigned counter;
    counter += 1u;
    snprintf(server->socket_path, sizeof(server->socket_path), "/tmp/locker_s104_it_%s_%u_%u.sock",
             tag, (unsigned)getpid(), counter);
    snprintf(server->journal_path, sizeof(server->journal_path), "/tmp/locker_s104_it_%s_%u_%u.db",
             tag, (unsigned)getpid(), counter);
    (void)unlink(server->socket_path);
    (void)unlink(server->journal_path);
}

static void it_unlink_journal(const char *path)
{
    char side[LOCKER_SIM_PATH_SIZE + 8];
    (void)unlink(path);
    snprintf(side, sizeof(side), "%s-wal", path);
    (void)unlink(side);
    snprintf(side, sizeof(side), "%s-shm", path);
    (void)unlink(side);
}

static locker_status_t it_server_start(it_server_t *server, const char *tag)
{
    struct sockaddr_un address;
    locker_sim_config_t config;
    mode_t previous;
    int fd;

    memset(server, 0, sizeof(*server));
    server->listen_fd = -1;
    server->client_fd = -1;
    pthread_mutex_init(&server->lock, NULL);
    it_make_paths(server, tag);

    memset(&config, 0, sizeof(config));
    snprintf(config.journal_path, sizeof(config.journal_path), "%s", server->journal_path);
    config.device_uid[0] = 0x01u;
    config.device_uid[LOCKER_SIM_DEVICE_UID_SIZE - 1u] = 0x0cu;
    config.clock = locker_clock_port_real();
    if (locker_sim_create(&server->sim, &config) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_IO_ERROR;
    }
    if (locker_sim_open(server->sim) != LOCKER_STATUS_OK) {
        locker_sim_destroy(server->sim);
        server->sim = NULL;
        return LOCKER_STATUS_IO_ERROR;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        locker_sim_close(server->sim);
        locker_sim_destroy(server->sim);
        server->sim = NULL;
        return LOCKER_STATUS_IO_ERROR;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", server->socket_path);
    previous = umask(0077);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        umask(previous);
        close(fd);
        locker_sim_close(server->sim);
        locker_sim_destroy(server->sim);
        server->sim = NULL;
        return LOCKER_STATUS_IO_ERROR;
    }
    umask(previous);
    (void)chmod(server->socket_path, 0600);
    if (listen(fd, 4) != 0) {
        close(fd);
        (void)unlink(server->socket_path);
        locker_sim_close(server->sim);
        locker_sim_destroy(server->sim);
        server->sim = NULL;
        return LOCKER_STATUS_IO_ERROR;
    }
    /* listen_fd 必须在起线程之前写入：server 线程的 accept 直接读这个字段，
     * 先起线程再赋值会让 accept 拿到 -1（EBADF），整个会话就此丢失。 */
    server->listen_fd = fd;
    if (pthread_create(&server->thread, NULL, it_server_main, server) != 0) {
        close(fd);
        server->listen_fd = -1;
        (void)unlink(server->socket_path);
        locker_sim_close(server->sim);
        locker_sim_destroy(server->sim);
        server->sim = NULL;
        return LOCKER_STATUS_IO_ERROR;
    }
    server->thread_started = true;
    return LOCKER_STATUS_OK;
}

static void it_server_stop(it_server_t *server)
{
    int client;
    if (server->listen_fd >= 0) {
        (void)shutdown(server->listen_fd, SHUT_RDWR);
    }
    pthread_mutex_lock(&server->lock);
    server->stopping = true;
    client = server->client_fd;
    pthread_mutex_unlock(&server->lock);
    if (client >= 0) {
        (void)shutdown(client, SHUT_RDWR);
    }
    if (server->thread_started) {
        pthread_join(server->thread, NULL);
        server->thread_started = false;
    }
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }
    if (server->sim != NULL) {
        locker_sim_close(server->sim);
        locker_sim_destroy(server->sim);
        server->sim = NULL;
    }
    (void)unlink(server->socket_path);
    it_unlink_journal(server->journal_path);
    pthread_mutex_destroy(&server->lock);
}

/* 加锁读取模拟器观测，避免与 server 线程竞争。 */
static uint64_t it_sim_pulses(it_server_t *server)
{
    uint64_t value;
    pthread_mutex_lock(&server->lock);
    value = locker_sim_pulse_count(server->sim);
    pthread_mutex_unlock(&server->lock);
    return value;
}

static uint8_t it_sim_command_state(it_server_t *server)
{
    uint8_t value;
    pthread_mutex_lock(&server->lock);
    value = locker_sim_command_state(server->sim);
    pthread_mutex_unlock(&server->lock);
    return value;
}

static uint8_t it_sim_device_mode(it_server_t *server)
{
    uint8_t value;
    pthread_mutex_lock(&server->lock);
    value = locker_sim_device_mode(server->sim);
    pthread_mutex_unlock(&server->lock);
    return value;
}

static locker_uuid_t it_sim_epoch(it_server_t *server)
{
    locker_uuid_t value;
    pthread_mutex_lock(&server->lock);
    value = locker_sim_journal_epoch(server->sim);
    pthread_mutex_unlock(&server->lock);
    return value;
}

static uint64_t it_sim_boot(it_server_t *server)
{
    uint64_t value;
    pthread_mutex_lock(&server->lock);
    value = locker_sim_boot_id(server->sim);
    pthread_mutex_unlock(&server->lock);
    return value;
}

static unsigned it_counter(it_server_t *server, unsigned *slot)
{
    unsigned value;
    pthread_mutex_lock(&server->lock);
    value = *slot;
    pthread_mutex_unlock(&server->lock);
    return value;
}

static void it_set_door(it_server_t *server, uint8_t door)
{
    pthread_mutex_lock(&server->lock);
    (void)locker_sim_control_set_door(server->sim, door);
    pthread_mutex_unlock(&server->lock);
}

static void it_set_item(it_server_t *server, uint8_t item)
{
    pthread_mutex_lock(&server->lock);
    (void)locker_sim_control_set_item(server->sim, item);
    pthread_mutex_unlock(&server->lock);
}

static void it_fail_next(it_server_t *server, locker_sim_failure_stage_t stage)
{
    pthread_mutex_lock(&server->lock);
    (void)locker_sim_control_fail_next(server->sim, stage);
    pthread_mutex_unlock(&server->lock);
}

static void it_drop_execute_ack(it_server_t *server, bool enabled)
{
    pthread_mutex_lock(&server->lock);
    (void)locker_sim_control_drop_execute_ack(server->sim, enabled);
    pthread_mutex_unlock(&server->lock);
}

static void it_suppress_pulse(it_server_t *server, bool enabled)
{
    pthread_mutex_lock(&server->lock);
    (void)locker_sim_control_suppress_pulse(server->sim, enabled);
    pthread_mutex_unlock(&server->lock);
}

/* ---------- 夹具：真实仓储 + 真实 Core + 真实适配器 ---------- */

typedef struct it_env {
    it_server_t server;
    locker_repo_t *repo;
    locker_device_t *device;
    locker_core_t *core;
    /* repo_path 要能装进 locker_repo_config_t.path(256)；repo_dir 比它短，
     * 这样拼 "/locker.db" 不会触发 -Werror=format-truncation。 */
    char repo_path[256];
    char repo_dir[128];
    char device_id[25];
    bool server_started;
    bool repo_opened;
    bool core_started;
    /* 虚拟时钟的累计偏移：业务超时是 30/120 秒级，真实等不起。
     * 必须累计——每次都用 it_now_ms()+delta 的话，Core 的 now_ms 已是
     * 上一轮的虚拟值，新一轮只比它大一点点（真实流逝的几十毫秒），等于
     * 虚拟时钟按真实速率走，30 秒的窗口永远等不到。 */
    uint64_t clock_offset_ms;
} it_env_t;

/* ---------- 发布器替身 ----------
 *
 * 契约声明了 locker_events_try_publish(publisher, change)（locker_contracts.h:235），
 * 但 host 侧还没有 events 模块落地（属于后续 Runtime 装配的卡），
 * Core 的 domain_publish 会对它无条件发符号引用。因此本文件必须提供一个
 * 最小替身，否则链接就过不去。这里的行为与 tests/unit/domain 的替身一致：
 * 只计数并记录最后一次变更，绝不假装投递成功。
 * 这条「events 模块未实现」已记入 S1-04 handoff 的未实现项。
 */
#ifndef LOCKER_HTTP_FLOW_FIXTURE
struct locker_publisher {
    unsigned publish_calls;
    locker_resource_change_t last;
    char last_id[65];
    char last_resource[16];
};

locker_status_t locker_events_try_publish(locker_publisher_t *publisher,
                                          const locker_resource_change_t *change)
{
    if (publisher == NULL || change == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    publisher->publish_calls += 1u;
    publisher->last = *change;
    snprintf(publisher->last_id, sizeof(publisher->last_id), "%s", change->id);
    snprintf(publisher->last_resource, sizeof(publisher->last_resource), "%s", change->resource);
    return LOCKER_STATUS_OK;
}

#endif

static void it_sleep_ms(unsigned milliseconds)
{
    struct timespec request;
    request.tv_sec = (time_t)(milliseconds / 1000u);
    request.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    while (nanosleep(&request, &request) != 0 && errno == EINTR) {
    }
}

static uint64_t it_now_ms(void)
{
    return locker_clock_real_monotonic_ms();
}

/* 轮询直到条件为真或超时；返回是否在超时前满足。 */
static bool it_wait_until(bool (*predicate)(void *), void *user, unsigned timeout_ms)
{
    uint64_t deadline = it_now_ms() + timeout_ms;
    for (;;) {
        if (predicate(user)) {
            return true;
        }
        if (it_now_ms() >= deadline) {
            return false;
        }
        it_sleep_ms(20);
    }
}

static locker_uuid_t it_uuid_text(const char *text)
{
    locker_uuid_t uuid;
    memset(&uuid, 0, sizeof(uuid));
    (void)locker_uuid_parse(text, strlen(text), &uuid);
    return uuid;
}

/* 测试内部用的确定 UUID：request_id / idempotency_key 不需要 RFC4122 v4 校验，
 * 但业务 resource_id 必须是真实的 tool/user/session，所以那些走 it_uuid_text。 */
static locker_uuid_t it_uuid(uint8_t seed)
{
    locker_uuid_t uuid;
    memset(&uuid, 0, sizeof(uuid));
    uuid.bytes[0] = seed;
    uuid.bytes[6] = (uint8_t)(0x40u | (seed & 0x0fu));
    uuid.bytes[8] = (uint8_t)(0x80u | (seed & 0x3fu));
    uuid.bytes[15] = seed;
    return uuid;
}

/* 播种一条仓储记录（真实 repository.c）。 */
static locker_status_t it_put(locker_repo_t *repo, uint64_t expected, const char *collection,
                              const char *key, const char *value, uint64_t *next_revision)
{
    char *mutation;
    char *change;
    locker_change_t document;
    locker_status_t status;
    int mutation_length;
    int change_length;

    mutation = (char *)malloc(32768);
    change = (char *)malloc(32768);
    if (mutation == NULL || change == NULL) {
        free(mutation);
        free(change);
        return LOCKER_STATUS_NO_SPACE;
    }
    mutation_length = snprintf(
        mutation, 32768,
        "{\"op\":\"PUT\",\"document\":{\"collection\":\"%s\",\"key\":\"%s\",\"value\":%s}}",
        collection, key, value);
    change_length = snprintf(
        change, 32768,
        "{\"schema_version\":1,\"expected_snapshot_revision\":\"%llu\","
        "\"new_snapshot_revision\":\"%llu\",\"checks\":[],\"mutations\":[%s]}",
        (unsigned long long)expected, (unsigned long long)(expected + 1u), mutation);
    if (mutation_length <= 0 || change_length <= 0 || (size_t)change_length >= sizeof(document.json)) {
        free(mutation);
        free(change);
        return LOCKER_STATUS_NO_SPACE;
    }
    memset(&document, 0, sizeof(document));
    memcpy(document.json, change, (size_t)change_length + 1u);
    document.length = (size_t)change_length;
    status = locker_repo_commit_change(repo, &document);
    free(mutation);
    free(change);
    if (status == LOCKER_STATUS_OK && next_revision != NULL) {
        *next_revision = expected + 1u;
    }
    return status;
}

static locker_status_t it_seed_user(locker_repo_t *repo, uint64_t *revision, const char *user_id,
                                    const char *session_id, const char *username,
                                    const char *role)
{
    char user[1024];
    char session[1024];
    locker_status_t status;

    const char *password_hash="$argon2id$v=19$m=65536,t=3,p=1$c2FsdA$aGFzaA";
#ifdef LOCKER_HTTP_FLOW_FIXTURE
    char secure_hash[crypto_pwhash_STRBYTES];
    if (sodium_init()<0 || crypto_pwhash_str_alg(secure_hash,"network-flow-password",21,
        crypto_pwhash_OPSLIMIT_INTERACTIVE,crypto_pwhash_MEMLIMIT_INTERACTIVE,crypto_pwhash_ALG_ARGON2ID13)) return LOCKER_STATUS_IO_ERROR;
    password_hash=secure_hash;
#endif
    snprintf(user, sizeof(user),
             "{\"user\":{\"id\":\"%s\",\"username\":\"%s\","
             "\"display_name\":\"Tester\",\"role\":\"%s\",\"enabled\":true,"
             "\"user_revision\":\"1\"},\"password_hash\":\"%s\"}",
             user_id, username, role,password_hash);
    snprintf(session, sizeof(session),
             "{\"id\":\"%s\",\"user_id\":\"%s\","
             "\"token_hash\":\"" IT_TOKEN_HASH "\",\"expires_at\":\"" IT_SESSION_EXPIRES "\","
             "\"revoked\":false}",
             session_id, user_id);

    status = it_put(repo, *revision, "users", user_id, user, revision);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    return it_put(repo, *revision, "sessions", session_id, session, revision);
}

static locker_status_t it_env_open_repo(it_env_t *env)
{
    locker_repo_config_t repo_config;
    locker_status_t status;

    memset(&repo_config, 0, sizeof(repo_config));
    snprintf(repo_config.path, sizeof(repo_config.path), "%s", env->repo_path);
    repo_config.schema_version = 1u;
    repo_config.initialize_empty = true;
    status = locker_repo_open(&repo_config, &env->repo);
    if (status == LOCKER_STATUS_OK) {
        env->repo_opened = true;
    }
    return status;
}

/* 只创建适配器对象，不 open：契约要求 bind_sink 早于 open，
 * 而 Core 的 sink 绑定发生在 locker_core_start 里。 */
static locker_status_t it_env_create_device(it_env_t *env)
{
    return locker_device_create(&env->device);
}

/* 打开设备（HELLO 握手 + 新会话）。必须在 Core 已 start 之后调用。 */
static locker_status_t it_env_attach_device(it_env_t *env)
{
    locker_device_config_t device_config;

    if (env->device == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    memset(&device_config, 0, sizeof(device_config));
    device_config.simulated = true;
    snprintf(device_config.path, sizeof(device_config.path), "%s", env->server.socket_path);
    snprintf(device_config.expected_device_id, sizeof(device_config.expected_device_id), "%s",
             env->device_id);
    device_config.baud_rate = 115200u;
    device_config.host_instance_id = it_uuid_text(IT_HOST_TEXT);
    device_config.clock = locker_clock_port_real();
    return locker_device_open(env->device, &device_config);
}

/* 先 create+start（start 会绑定 sink），再 open 设备：
 * 契约要求 bind_sink 在 open 之前，open 之后再绑会返回 CONFLICT。 */
static locker_status_t it_env_start_core(it_env_t *env)
{
    locker_core_config_t core_config;
    locker_status_t status;

    memset(&core_config, 0, sizeof(core_config));
    snprintf(core_config.cabinet_name, sizeof(core_config.cabinet_name), "integration-cabinet");
    core_config.instance_id = it_uuid_text(IT_HOST_TEXT);
    core_config.clock = locker_clock_port_real();
    core_config.queue_capacity = 0u;
    core_config.completion_retention_ms = 0u;
    status = locker_core_create(&core_config, env->repo, env->device, NULL, &env->core);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = locker_core_start(env->core);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    env->core_started = true;
    return LOCKER_STATUS_OK;
}

static locker_status_t it_env_start(it_env_t *env, const char *tag)
{
    uint64_t revision = 0u;
    locker_status_t status;

    memset(env, 0, sizeof(*env));
    snprintf(env->device_id, sizeof(env->device_id), "%s", "01000000000000000000000c");
    snprintf(env->repo_dir, sizeof(env->repo_dir), "/tmp/locker_s104_db_%s_%u", tag,
             (unsigned)getpid());
    (void)mkdir(env->repo_dir, 0700);
    snprintf(env->repo_path, sizeof(env->repo_path), "%s/locker.db", env->repo_dir);
    (void)unlink(env->repo_path);

    status = it_env_open_repo(env);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    /* 身份数据必须先于 Core 启动写入：Core 恢复时读 snapshot_revision 作为基线。 */
    status = it_seed_user(env->repo, &revision, IT_USER_TEXT, IT_SESSION_TEXT, "tester", "USER");
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = it_seed_user(env->repo, &revision, IT_ADMIN_TEXT, IT_ADMIN_SESSION_TEXT, "root",
                          "ADMIN");
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    /* 工具记录也必须在 Core 启动前落库：Core 恢复时用 tools 集合填工具投影，
       申请要靠它校验 tool_id / tool_revision。真实部署里这一行由本机初始化
       写入（tool 集合的写者不属于本卡），但库里有这件工具是 Core 能接业务的
       前提，不是可选项——没有它每个申请都以 NOT_READY 收场。 */
    {
        char tool[512];
        snprintf(tool, sizeof(tool),
                 "{\"id\":\"" IT_TOOL_TEXT "\",\"name\":\"柜内工具\",\"slot_id\":1,"
                 "\"tool_revision\":\"1\",\"enabled\":true}");
        status = it_put(env->repo, revision, "tools", IT_TOOL_TEXT, tool, &revision);
        if (status != LOCKER_STATUS_OK) {
            return status;
        }
    }

    status = it_server_start(&env->server, tag);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    env->server_started = true;

    /* 装配顺序与真实 Runtime 一致：create device → create+start core（此时绑定
     * sink）→ open device（HELLO 握手）。open 之后再 bind_sink 会被端口拒绝。 */
    status = it_env_create_device(env);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    return it_env_start_core(env);
}

static void it_env_stop(it_env_t *env)
{
    if (env->device != NULL) {
        locker_device_close(env->device);
    }
    if (env->core_started) {
        (void)locker_core_stop(env->core, LOCKER_HOST_TIMEOUT_MS);
        env->core_started = false;
    }
    if (env->core != NULL) {
        locker_core_destroy(env->core);
        env->core = NULL;
    }
    if (env->device != NULL) {
        locker_device_destroy(env->device);
        env->device = NULL;
    }
    if (env->server_started) {
        it_server_stop(&env->server);
        env->server_started = false;
    }
    if (env->repo_opened) {
        locker_repo_close(env->repo);
        env->repo = NULL;
        env->repo_opened = false;
    }
    (void)unlink(env->repo_path);
    (void)rmdir(env->repo_dir);
}

/* ---------- Core 观察辅助（只走公开端口） ---------- */

static locker_principal_t it_principal(locker_role_t role)
{
    locker_principal_t principal;
    memset(&principal, 0, sizeof(principal));
    principal.user_id = it_uuid_text(role == LOCKER_ROLE_ADMIN ? IT_ADMIN_TEXT : IT_USER_TEXT);
    principal.session_id =
        it_uuid_text(role == LOCKER_ROLE_ADMIN ? IT_ADMIN_SESSION_TEXT : IT_SESSION_TEXT);
    principal.role = role;
    principal.authenticated = true;
    return principal;
}

static locker_peer_t it_ui_peer(void)
{
    locker_peer_t peer;
    memset(&peer, 0, sizeof(peer));
    peer.verified_ui_peer = true;
    return peer;
}

static locker_peer_t it_admin_peer(void)
{
    locker_peer_t peer;
    memset(&peer, 0, sizeof(peer));
    peer.verified_admin_peer = true;
    return peer;
}

/* 提交一条命令并等待结果。 */
static locker_status_t it_command(it_env_t *env, const locker_command_t *command,
                                  locker_query_result_t *result)
{
    locker_receipt_t receipt;
    locker_status_t status;

    memset(&receipt, 0, sizeof(receipt));
    status = locker_core_submit(env->core, command, &receipt);
    if (status != LOCKER_STATUS_QUEUED) {
        return status;
    }
    status = locker_core_wait_result(env->core, &receipt.submission_token,
                                     LOCKER_CORE_RESULT_WAIT_MS, result);
    return status;
}

static locker_status_t it_query(it_env_t *env, const char *kind, const locker_uuid_t *resource,
                                locker_query_result_t *result)
{
    locker_query_t query;
    memset(&query, 0, sizeof(query));
    query.principal = it_principal(LOCKER_ROLE_USER);
    query.request_id = it_uuid(0x77u);
    if (resource != NULL) {
        query.resource_id = *resource;
    }
    snprintf(query.kind, sizeof(query.kind), "%s", kind);
    query.limit = 1u;
    return locker_core_query(env->core, &query, result);
}

static bool it_json_has(const locker_query_result_t *result, const char *needle)
{
    if (result->length == 0u || result->length >= sizeof(result->json)) {
        return false;
    }
    return strstr(result->json, needle) != NULL;
}

/* 操作是否处于某状态（可选校验 reason_code）。 */
static bool it_operation_in(it_env_t *env, const locker_uuid_t *operation_id, const char *state,
                            const char *reason)
{
    locker_query_result_t result;
    char expected[128];

    if (it_query(env, LOCKER_CORE_OPERATION_GET, operation_id, &result) != LOCKER_STATUS_OK) {
        return false;
    }
    snprintf(expected, sizeof(expected), "\"state\":\"%s\"", state);
    if (!it_json_has(&result, expected)) {
        return false;
    }
    if (reason != NULL) {
        snprintf(expected, sizeof(expected), "\"reason_code\":\"%s\"", reason);
        return it_json_has(&result, expected);
    }
    return true;
}

typedef struct it_expect {
    it_env_t *env;
    locker_uuid_t operation_id;
    const char *state;
    const char *reason;
} it_expect_t;

static bool it_expect_met(void *user)
{
    it_expect_t *expect = (it_expect_t *)user;
    return it_operation_in(expect->env, &expect->operation_id, expect->state, expect->reason);
}

/* 等待操作进入某状态；异步链路必须用它可以避免竞态断言。 */
static bool it_wait_operation(it_env_t *env, const locker_uuid_t *operation_id, const char *state,
                              const char *reason, unsigned timeout_ms)
{
    it_expect_t expect;
    expect.env = env;
    expect.operation_id = *operation_id;
    expect.state = state;
    expect.reason = reason;
    if (it_wait_until(it_expect_met, &expect, timeout_ms)) {
        return true;
    }
    /* 超时后把实际状态打出来：只说「没等到」无法区分是状态走错了、
       还是 reason_code 不对（两者都让断言失败，但修法完全不同）。 */
    {
        locker_query_result_t actual;
        memset(&actual, 0, sizeof(actual));
        if (it_query(env, LOCKER_CORE_OPERATION_GET, operation_id, &actual) == LOCKER_STATUS_OK) {
            /* reason_code 在对象中间，头尾窗口都可能切掉；直接定位再打。 */
            const char *state_at = strstr(actual.json, "\"state\":\"");
            const char *reason_at = strstr(actual.json, "\"reason_code\":");
            fprintf(stderr, "it_wait_operation timeout: want=%s/%s state=%.24s reason=%.40s\n", state,
                    reason == NULL ? "-" : reason, state_at == NULL ? "?" : state_at + 9,
                    reason_at == NULL ? "?" : reason_at + 14);
        } else {
            fprintf(stderr, "it_wait_operation timeout: want=%s/%s operation-missing status=%d\n",
                    state, reason == NULL ? "-" : reason,
                    (int)it_query(env, LOCKER_CORE_OPERATION_GET, operation_id, &actual));
        }
    }
    return false;
}

typedef struct it_progress_wait {
    locker_core_t *core;
} it_progress_wait_t;

static bool it_progress_ready(void *user)
{
    it_progress_wait_t *wait = (it_progress_wait_t *)user;
    locker_progress_t progress;
    memset(&progress, 0, sizeof(progress));
    return locker_core_get_progress(wait->core, &progress) == LOCKER_STATUS_OK;
}

typedef struct it_counter_wait {
    it_server_t *server;
    unsigned *slot;
    unsigned target;
} it_counter_wait_t;

static bool it_counter_reached(void *user)
{
    it_counter_wait_t *wait = (it_counter_wait_t *)user;
    return it_counter(wait->server, wait->slot) >= wait->target;
}

/* 维护核验：让模拟器从 RECONCILING 进入 READY。
 * guard 的四个 CAS 字段必须与设备当前状态一致，否则设备端口返回 CONFLICT；
 * 这个调用是异步的（completion=UINT32_MAX），因此结果要轮询模拟器观测。 */
static bool it_reconcile_once(it_env_t *env)
{
    locker_maintenance_t request;
    locker_query_result_t result;
    locker_peer_t peer = it_admin_peer();
    const char *marker;

    if (it_query(env, LOCKER_CORE_DEVICE_GET, NULL, &result) != LOCKER_STATUS_OK) {
        return false;
    }
    marker = strstr(result.json, "\"physical_revision\":\"");
    if (marker == NULL) {
        return false;
    }
    marker += strlen("\"physical_revision\":\"");

    memset(&request, 0, sizeof(request));
    request.principal = it_principal(LOCKER_ROLE_ADMIN);
    request.journal_epoch = it_sim_epoch(&env->server);
    request.expected_boot_id = it_sim_boot(&env->server);
    request.physical_revision = strtoull(marker, NULL, 10);
    request.snapshot_revision = result.snapshot_revision;
    snprintf(request.reason, sizeof(request.reason), "integration reconcile");
    return locker_local_maintenance_reconcile(env->core, &peer, &request, NULL) == LOCKER_STATUS_OK;
}

static bool it_wait_device_ready(it_env_t *env, unsigned timeout_ms)
{
    uint64_t deadline = it_now_ms() + timeout_ms;
    for (;;) {
        if (it_sim_device_mode(&env->server) == LOCKER_WIRE_WIRE_DEVICE_MODE_READY) {
            return true;
        }
        (void)it_reconcile_once(env);
        if (it_now_ms() >= deadline) {
            return it_sim_device_mode(&env->server) == LOCKER_WIRE_WIRE_DEVICE_MODE_READY;
        }
        it_sleep_ms(50);
    }
}

/* 提交 OPERATION_CREATE 并返回 operation_id。 */
static locker_status_t it_create_operation_kind(it_env_t *env, const char *kind, locker_uuid_t *operation_id)
{
    locker_command_t command;
    locker_query_result_t result;
    locker_status_t status;
    static unsigned sequence;
    const char *marker;

    sequence += 1u;
    memset(&command, 0, sizeof(command));
    command.principal = it_principal(LOCKER_ROLE_USER);
    command.request_id = it_uuid((uint8_t)(0x30u + sequence));
    command.idempotency_key = it_uuid((uint8_t)(0x60u + sequence));
    command.resource_id = it_uuid_text(IT_TOOL_TEXT);
    command.expected_revision = 0u;
    snprintf(command.kind, sizeof(command.kind), "%s", LOCKER_CORE_OPERATION_CREATE);
    snprintf(command.arguments_json, sizeof(command.arguments_json),
             "{\"tool_id\":\"" IT_TOOL_TEXT "\",\"kind\":\"%s\",\"tool_revision\":\"1\"}", kind);

    memset(&result, 0, sizeof(result));
    status = it_command(env, &command, &result);
    if (status != LOCKER_STATUS_OK) {
        /* 区分「提交/等待链路失败」与「处理器拒绝」：两者都可能返回 IO_ERROR，
           只报一个数字看不出是哪一层。 */
        fprintf(stderr, "it_create_operation: transport=%d business=%d length=%u\n", (int)status,
                (int)result.status, (unsigned)result.length);
        return status;
    }
    /* 传输层 OK 只说明完成槽拿到了结果；业务结论在 result.status 里。
       这里把业务状态原样上抛，避免「处理器拒绝」被误报成 IO_ERROR。 */
    if (result.status != LOCKER_STATUS_OK) {
        fprintf(stderr, "it_create_operation: business=%d length=%u json=%.200s\n",
                (int)result.status, (unsigned)result.length, result.json);
        return result.status;
    }
    marker = strstr(result.json, "\"data\":{\"id\":\"");
    if (marker == NULL) {
        fprintf(stderr, "it_create_operation: response has no data.id (length=%u) json=%.300s\n",
                (unsigned)result.length, result.json);
        return LOCKER_STATUS_IO_ERROR;
    }
    marker += strlen("\"data\":{\"id\":\"");
    /* locker_uuid_parse 返回的是状态枚举，不是布尔：LOCKER_STATUS_OK 就是 0，
       写成 `!locker_uuid_parse(...)` 会把「解析成功」当成失败，于是每个
       正常响应都被判成 IO_ERROR。必须显式和 LOCKER_STATUS_OK 比较。 */
    if (locker_uuid_parse(marker, 36u, operation_id) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_IO_ERROR;
    }
    return LOCKER_STATUS_OK;
}

static locker_status_t it_create_operation(it_env_t *env, locker_uuid_t *operation_id)
{
    return it_create_operation_kind(env, "BORROW", operation_id);
}

static uint64_t it_operation_revision(it_env_t *env, const locker_uuid_t *operation_id)
{
    locker_query_result_t result;
    const char *marker;

    if (it_query(env, LOCKER_CORE_OPERATION_GET, operation_id, &result) != LOCKER_STATUS_OK) {
        return 0u;
    }
    marker = strstr(result.json, "\"operation_revision\":\"");
    if (marker == NULL) {
        return 0u;
    }
    marker += strlen("\"operation_revision\":\"");
    return strtoull(marker, NULL, 10);
}

/* 到柜证明：只有本人 + 只有 UI peer 能提交。 */
static locker_status_t it_presence(it_env_t *env, const locker_uuid_t *operation_id)
{
    locker_presence_t presence;
    locker_peer_t peer = it_ui_peer();

    memset(&presence, 0, sizeof(presence));
    presence.principal = it_principal(LOCKER_ROLE_USER);
    presence.operation_id = *operation_id;
    presence.operation_revision = it_operation_revision(env, operation_id);
    return locker_local_presence_submit(env->core, &peer, &presence, NULL);
}

/* 把 Core 的时钟快进 delta 毫秒（累计），驱动超时分支而不用真的等几十秒。
 * locker_core_tick 只入队；domain_now_advance 只前进不回退。
 *
 * 偏移必须累计：Core 的 now_ms 会被上一轮的虚拟值留在高位，若这里仍传
 * it_now_ms()+delta（真实时间基准），每轮只比 Core 现值大出真实流逝的
 * 几十毫秒，等于虚拟时钟退化成真实时钟——30 秒的 LOCKER_WAIT_OPEN_MS
 * 永远到不了（实测 r191000 推进 80 秒仍不触发 OPEN_TIMEOUT）。 */
static void it_advance_clock(it_env_t *env, uint64_t delta_ms)
{
    env->clock_offset_ms += delta_ms;
    (void)locker_core_tick(env->core, it_now_ms() + env->clock_offset_ms);
    it_sleep_ms(80);
}

/* 等某个超时分支落地：每轮把 Core 时钟推进 10 秒，直到状态命中或超时。
 * 用推进而不是真的 sleep，是因为业务超时是 30/120 秒级；IT_WAIT_MS 的
 * 真实窗口只有 8 秒，而 it_operation_in 每轮的查询可能要等 completion
 * 超时（至多 1 秒），5 秒步长的净推进 ≈ 4 秒，8 轮只有 ~32 秒，刚好
 * 压着 30 秒的 LOCKER_WAIT_OPEN_MS——实测不够。10 秒步长留足余量。 */
static bool it_wait_operation_progress(it_env_t *env, const locker_uuid_t *operation_id,
                                       const char *state, const char *reason)
{
    uint64_t deadline = it_now_ms() + IT_WAIT_MS;
    for (;;) {
        if (it_operation_in(env, operation_id, state, reason)) {
            return true;
        }
        if (it_now_ms() >= deadline) {
            return false;
        }
        it_advance_clock(env, 10000u);
    }
}

/* 建立好环境并让设备进入 READY；失败时已自行清理，调用方直接 return。 */
static bool it_bring_up(it_env_t *env, const char *tag)
{
    locker_status_t status;

    status = it_env_start(env, tag);
    if (status != LOCKER_STATUS_OK) {
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, status);
        it_env_stop(env);
        return false;
    }
    status = it_env_attach_device(env);
    if (status != LOCKER_STATUS_OK) {
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, status);
        it_env_stop(env);
        return false;
    }
    if (!it_wait_device_ready(env, IT_WAIT_MS)) {
        TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_READY, it_sim_device_mode(&env->server));
        it_env_stop(env);
        return false;
    }
    return true;
}

/* ---------- 测试用例 ---------- */

/* 真实启动路径：Core 自己建会话、补日志、ACK。
 * 这是启动死锁的回归护栏：一旦快照请求又被放到会话门之后，
 * 这里会因为永远拿不到会话而失败。 */
static void test_boot_and_session(void)
{
    it_env_t env;
    it_progress_wait_t progress;
    it_counter_wait_t snapshot;
    it_counter_wait_t ack;
    locker_status_t status;

    status = it_env_start(&env, "boot");
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, status);
    if (status != LOCKER_STATUS_OK) {
        it_env_stop(&env);
        return;
    }
    /* 打开设备建立 HELLO 会话。此前的泵周期里 Core 已经在请求快照，
     * 但端口在 hello_complete 之前一律返回 NOT_READY，所以帧还没上总线。 */
    status = it_env_attach_device(&env);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, status);
    if (status != LOCKER_STATUS_OK) {
        it_env_stop(&env);
        return;
    }
    /* 会话未建立时设备侧不应收到任何快照请求。 */
    TEST_CHECK_U64(0u, it_counter(&env.server, &env.server.snapshot_requests));

    /* 关键回归护栏：快照请求必须在 device_session_up 之前就由 Core 发出。
     * 一旦泵把快照放到会话门之后，这里会永远停在 0（启动死锁）。 */
    snapshot.server = &env.server;
    snapshot.slot = &env.server.snapshot_requests;
    snapshot.target = 1u;
    TEST_CHECK(it_wait_until(it_counter_reached, &snapshot, IT_WAIT_MS));

    progress.core = env.core;
    TEST_CHECK(it_wait_until(it_progress_ready, &progress, IT_WAIT_MS));

    /* 会话建立后 Core 才会补日志；补日志成功才 ACK。 */
    ack.server = &env.server;
    ack.slot = &env.server.ack_requests;
    ack.target = 1u;
    TEST_CHECK(it_wait_until(it_counter_reached, &ack, IT_WAIT_MS));

    /* 模拟器在维护核验之前必须停在 RECONCILING，不能自己跳到 READY。 */
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING, it_sim_device_mode(&env.server));

    TEST_CHECK(it_wait_device_ready(&env, IT_WAIT_MS));
    TEST_CHECK_U64(LOCKER_WIRE_WIRE_DEVICE_MODE_READY, it_sim_device_mode(&env.server));

    /* Core 从不自己发 RESUME_READY：只有维护核验路径会发（可能重试多次）。 */
    TEST_CHECK(it_counter(&env.server, &env.server.resume_requests) >= 1u);

    it_env_stop(&env);
}

/* 正常借出全流程：创建 → 到柜 → 开门取物 → 关门 → 本人确认 → 完成 + 借用记录。 */
static void test_borrow_flow(void)
{
    it_env_t env;
    locker_uuid_t operation_id;
    locker_query_result_t result;
    locker_command_t command;
    locker_status_t status;
    unsigned executes;

    if (!it_bring_up(&env, "borrow")) {
        return;
    }

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_create_operation(&env, &operation_id));
    TEST_CHECK(it_wait_operation(&env, &operation_id, "REQUESTED", NULL, IT_WAIT_MS));

    /* 到柜证明：Core 在 presence 里直接推进到 ACCESS_PENDING，随后由 outbox 泵
       在**下一个 tick** 发 EXECUTE。arm 只把「准备发送」这件事提交落库
       （先提交后发送，internal-interfaces.md:114），所以看到 ACCESS_PENDING
       不等于 EXECUTE 已经发出去了——这里必须等计数器，不能直接断言 1，
       否则就是在和泵赛跑（r202100 的偶发失败正是这个竞态）。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_presence(&env, &operation_id));
    TEST_CHECK(it_wait_operation(&env, &operation_id, "ACCESS_PENDING", "ARMED", IT_WAIT_MS));
    {
        it_counter_wait_t wait = {&env.server, &env.server.execute_requests, 1u};
        TEST_CHECK(it_wait_until(it_counter_reached, &wait, IT_WAIT_MS));
    }
    executes = it_counter(&env.server, &env.server.execute_requests);
    TEST_CHECK_U64(1u, executes);
    TEST_CHECK_U64(1u, it_sim_pulses(&env.server));

    /* 物理事实：开门 → 取物 → 关门。物品变化 + 门已关才进入本人确认。 */
    it_set_door(&env.server, LOCKER_WIRE_DOOR_STATE_OPEN);
    it_sleep_ms(120);
    it_set_item(&env.server, LOCKER_WIRE_ITEM_STATE_ABSENT);
    it_sleep_ms(120);
    it_set_door(&env.server, LOCKER_WIRE_DOOR_STATE_CLOSED);
    TEST_CHECK(it_wait_operation(&env, &operation_id, "WAIT_CONFIRM", NULL, IT_WAIT_MS));

    /* 本人确认：revision 必须匹配当前操作版本。 */
    memset(&command, 0, sizeof(command));
    command.principal = it_principal(LOCKER_ROLE_USER);
    command.request_id = it_uuid(0x51u);
    command.resource_id = operation_id;
    command.expected_revision = it_operation_revision(&env, &operation_id);
    snprintf(command.kind, sizeof(command.kind), "%s", LOCKER_CORE_OPERATION_CONFIRM);
    snprintf(command.arguments_json, sizeof(command.arguments_json),
             "{\"operation_revision\":\"%llu\"}", (unsigned long long)command.expected_revision);
    memset(&result, 0, sizeof(result));
    status = it_command(&env, &command, &result);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, status);
    TEST_CHECK(it_wait_operation(&env, &operation_id, "COMPLETED", "CONFIRMED", IT_WAIT_MS));

    /* 借出完成后必须留下未归还的借用记录，且只有一次 EXECUTE（确认不会开第二次门）。 */
    TEST_CHECK_U64(1u, it_counter(&env.server, &env.server.execute_requests));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_query(&env, LOCKER_CORE_LOANS_LIST, NULL, &result));
    TEST_CHECK(it_json_has(&result, "\"closed_at\":null"));

    /* Same process, same user, same loan: verify the actual return and release
     * path rather than treating a successful borrow as a complete borrow/return. */
    {
        locker_uuid_t borrowed = operation_id;
        uint64_t deadline;
        bool armed = false;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                          it_create_operation_kind(&env, "RETURN", &operation_id));
        TEST_CHECK(it_wait_operation(&env, &operation_id, "REQUESTED", NULL, IT_WAIT_MS));
        deadline = it_now_ms() + IT_WAIT_MS;
        do {
            /* Presence admission is asynchronous. While still REQUESTED the
             * local UI can submit a fresh observation after the release barrier;
             * no EXECUTE is retried by either the Core or the adapter. */
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_presence(&env, &operation_id));
            it_sleep_ms(30u);
            armed = !it_operation_in(&env, &operation_id, "REQUESTED", NULL);
        } while (!armed && it_now_ms() < deadline);
        TEST_CHECK(armed);
        {
            it_counter_wait_t wait = {&env.server, &env.server.execute_requests, 2u};
            TEST_CHECK(it_wait_until(it_counter_reached, &wait, IT_WAIT_MS));
        }
        TEST_CHECK_U64(2u, it_sim_pulses(&env.server));
        it_set_door(&env.server, LOCKER_WIRE_DOOR_STATE_OPEN);
        it_sleep_ms(120u);
        it_set_item(&env.server, LOCKER_WIRE_ITEM_STATE_PRESENT);
        it_sleep_ms(120u);
        it_set_door(&env.server, LOCKER_WIRE_DOOR_STATE_CLOSED);
        TEST_CHECK(it_wait_operation(&env, &operation_id, "WAIT_CONFIRM", NULL, IT_WAIT_MS));
        memset(&command, 0, sizeof(command));
        command.principal = it_principal(LOCKER_ROLE_USER);
        command.request_id = it_uuid(0x52u);
        command.resource_id = operation_id;
        command.expected_revision = it_operation_revision(&env, &operation_id);
        snprintf(command.kind, sizeof(command.kind), "%s", LOCKER_CORE_OPERATION_CONFIRM);
        snprintf(command.arguments_json, sizeof(command.arguments_json),
                 "{\"operation_revision\":\"%llu\"}", (unsigned long long)command.expected_revision);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_command(&env, &command, &result));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, result.status);
        TEST_CHECK(it_wait_operation(&env, &operation_id, "COMPLETED", "CONFIRMED", IT_WAIT_MS));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_query(&env, LOCKER_CORE_LOANS_LIST, NULL, &result));
        TEST_CHECK(!it_json_has(&result, "\"items\":[]"));
        TEST_CHECK(!it_json_has(&result, "\"closed_at\":null"));
        TEST_CHECK_U64(2u, it_counter(&env.server, &env.server.execute_requests));
        TEST_CHECK(it_counter(&env.server, &env.server.release_requests) >= 1u);
        /* Stop the sole writer before inspecting the persistent ledger. */
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_stop(env.core, LOCKER_HOST_TIMEOUT_MS));
        env.core_started = false;
        {
            locker_repo_query_t query;
            locker_repo_result_t ledger;
            char id[37], needle[96];
            memset(&query, 0, sizeof(query));
            query.schema_version = 1u;
            query.limit = 1u;
            snprintf(query.collection, sizeof(query.collection), "loans");
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_repo_query(env.repo, &query, &ledger));
            TEST_CHECK(strstr(ledger.json, "\"closed_at\":null") == NULL);
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_uuid_format(&borrowed, id, sizeof(id)));
            snprintf(needle, sizeof(needle), "\"borrow_operation_id\":\"%s\"", id);
            TEST_CHECK(strstr(ledger.json, needle) != NULL);
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_uuid_format(&operation_id, id, sizeof(id)));
            snprintf(needle, sizeof(needle), "\"return_operation_id\":\"%s\"", id);
            TEST_CHECK(strstr(ledger.json, needle) != NULL);
            TEST_CHECK(strstr(ledger.json, "\"borrower_id\":\"" IT_USER_TEXT "\"") != NULL);
        }
    }

    it_env_stop(&env);
}

/* 中断位置 1：脉冲开始之前就失败（模拟器事务 BEGIN/INTENT/EVENT/COMMIT/RESULT）。
 * 预期：Core 收到非 OK，进核验；不重放 EXECUTE。 */
static void test_interrupt_before_pulse(void)
{
    it_env_t env;
    locker_uuid_t operation_id;
    locker_sim_failure_stage_t stages[] = {
        LOCKER_SIM_FAILURE_BEGIN,
        LOCKER_SIM_FAILURE_INTENT,
        LOCKER_SIM_FAILURE_EVENT,
        LOCKER_SIM_FAILURE_COMMIT,
        LOCKER_SIM_FAILURE_RESULT,
    };
    size_t index;
    char tag[16];

    for (index = 0u; index < sizeof(stages) / sizeof(stages[0]); ++index) {
        snprintf(tag, sizeof(tag), "int%u", (unsigned)index);
        if (!it_bring_up(&env, tag)) {
            continue;
        }
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_create_operation(&env, &operation_id));
        TEST_CHECK(it_wait_operation(&env, &operation_id, "REQUESTED", NULL, IT_WAIT_MS));

        /* 只让下一次 EXECUTE 的该阶段失败一次，之后恢复正常。 */
        it_fail_next(&env.server, stages[index]);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_presence(&env, &operation_id));
        it_sleep_ms(IT_SILENCE_MS);

        TEST_CHECK_U64(1u, it_counter(&env.server, &env.server.execute_requests));
        /* 失败发生在脉冲之前：模拟器没有开始脉冲。 */
        TEST_CHECK_U64(0u, it_sim_pulses(&env.server));

        /* 结果未知：必须进核验，不能自动重发第二个动作。 */
        TEST_CHECK(it_wait_operation(&env, &operation_id, "REVIEW_REQUIRED", "UNKNOWN_RESULT",
                                     IT_WAIT_MS));

        /* 再等一段时间，确认 Core 不会自己重放。 */
        it_sleep_ms(IT_SILENCE_MS);
        TEST_CHECK_U64(1u, it_counter(&env.server, &env.server.execute_requests));

        it_env_stop(&env);
    }
}

/* 中断位置 2：EXECUTE 已发出、应答丢失（脉冲已经打过了）。
 * 预期：只发 QUERY_COMMAND 核对，绝不重发 EXECUTE。 */
static void test_interrupt_ack_lost(void)
{
    it_env_t env;
    locker_uuid_t operation_id;
    unsigned executes_before;
    unsigned queries_before;

    if (!it_bring_up(&env, "acklost")) {
        return;
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_create_operation(&env, &operation_id));
    TEST_CHECK(it_wait_operation(&env, &operation_id, "REQUESTED", NULL, IT_WAIT_MS));

    /* 模拟器已经把意图落盘并打了脉冲，但应答帧被丢弃。 */
    it_drop_execute_ack(&env.server, true);
    executes_before = it_counter(&env.server, &env.server.execute_requests);
    queries_before = it_counter(&env.server, &env.server.query_requests);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_presence(&env, &operation_id));
    it_sleep_ms(IT_SILENCE_MS);

    /* 脉冲确实打过了：这正是「ACK 丢失不等于没执行」。SIM_PULSE_MS=150，
       sleep 300ms 之后脉冲可能已经收尾（PULSE_FINISHED），两个状态都证明
       「脉冲打过」这一事实。 */
    TEST_CHECK_U64(1u, it_sim_pulses(&env.server));
    {
        uint8_t command_state = it_sim_command_state(&env.server);
        TEST_CHECK(command_state == LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED ||
                   command_state == LOCKER_WIRE_COMMAND_STATE_PULSE_FINISHED);
    }

    /* 快进过 LOCKER_HOST_TIMEOUT_MS，让 Core 认为这条命令结果未知。
       tick 只入队一次；core 线程可能在入队前后已经跑过超时检查，
       所以这里轮询等 query 落到服务器，而不是只 sleep 一轮。 */
    it_advance_clock(&env, LOCKER_HOST_TIMEOUT_MS + 2000u);
    {
        uint64_t deadline = it_now_ms() + IT_WAIT_MS;
        while (it_counter(&env.server, &env.server.query_requests) <= queries_before &&
               it_now_ms() < deadline) {
            it_advance_clock(&env, 1000u);
        }
    }

    /* 关键断言：EXECUTE 只出现过一次，之后只出现 QUERY_COMMAND。 */
    TEST_CHECK_U64(executes_before + 1u, it_counter(&env.server, &env.server.execute_requests));
    TEST_CHECK(it_counter(&env.server, &env.server.query_requests) > queries_before);

    /* 恢复应答并再推进一轮：核对不会产生第二个脉冲。 */
    it_drop_execute_ack(&env.server, false);
    it_advance_clock(&env, LOCKER_HOST_TIMEOUT_MS + 2000u);
    it_sleep_ms(IT_SILENCE_MS);
    TEST_CHECK_U64(1u, it_sim_pulses(&env.server));
    TEST_CHECK_U64(executes_before + 1u, it_counter(&env.server, &env.server.execute_requests));

    it_env_stop(&env);
}

/* 中断位置 3：意图已持久化、脉冲被抑制（设备没有真的动作）。
 * 预期：Core 不把 ACCEPTED 当成已开门，仍停在 ACCESS_PENDING；
 * 超过开门等待窗口后进核验，而不是当成成功。 */
static void test_interrupt_pulse_suppressed(void)
{
    it_env_t env;
    locker_uuid_t operation_id;

    if (!it_bring_up(&env, "nopulse")) {
        return;
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_create_operation(&env, &operation_id));
    TEST_CHECK(it_wait_operation(&env, &operation_id, "REQUESTED", NULL, IT_WAIT_MS));

    it_suppress_pulse(&env.server, true);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_presence(&env, &operation_id));
    it_sleep_ms(IT_SILENCE_MS);

    /* 意图被接纳但没有脉冲：模拟器侧停在 ACCEPTED。 */
    TEST_CHECK_U64(0u, it_sim_pulses(&env.server));
    TEST_CHECK_U64(LOCKER_WIRE_COMMAND_STATE_ACCEPTED, it_sim_command_state(&env.server));
    /* Core 没有收到 PULSE_STARTED，因此仍是 ACCESS_PENDING，不是 ACCESS_ACTIVE。 */
    TEST_CHECK(it_operation_in(&env, &operation_id, "ACCESS_PENDING", "ARMED"));

    /* 超过开门等待窗口后必须给出确定出口：进核验。 */
    TEST_CHECK(it_wait_operation_progress(&env, &operation_id, "REVIEW_REQUIRED", "OPEN_TIMEOUT"));
    /* 进核验后不再有新的 EXECUTE。 */
    TEST_CHECK_U64(1u, it_counter(&env.server, &env.server.execute_requests));

    it_env_stop(&env);
}

/* 中断位置 4：脉冲之后、物理证据之前主机重启。
 * 预期：恢复出的未完成操作一律进核验，不恢复发送队列、不重放动作。 */
static void test_interrupt_restart_barrier(void)
{
    it_env_t env;
    locker_uuid_t operation_id;
    locker_status_t status;
    unsigned executes_before;
    unsigned hello_before;
    uint64_t deadline;

    if (!it_bring_up(&env, "restart")) {
        return;
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_create_operation(&env, &operation_id));
    TEST_CHECK(it_wait_operation(&env, &operation_id, "REQUESTED", NULL, IT_WAIT_MS));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_presence(&env, &operation_id));
    TEST_CHECK(it_wait_operation(&env, &operation_id, "ACCESS_PENDING", "ARMED", IT_WAIT_MS));
    /* arm 只提交「准备发送」，EXECUTE 由泵在下一个 tick 发出，服务端计数器要等
       帧真正到达才加一。直接采样会在和泵赛跑：先在 socket 里、后计入计数器，
       于是 executes_before 采到 0，重启后再读成 1，被误判成「重启重放动作」。 */
    {
        it_counter_wait_t wait = {&env.server, &env.server.execute_requests, 1u};
        TEST_CHECK(it_wait_until(it_counter_reached, &wait, IT_WAIT_MS));
    }
    executes_before = it_counter(&env.server, &env.server.execute_requests);
    TEST_CHECK_U64(1u, executes_before);

    /* 模拟主机进程重启：停 Core、关设备、销毁；仓储与设备日志全部保留。
     *
     * 顺序必须按生命周期契约（internal-interfaces.md）来：
     *   「sink 的 Core 上下文存活到 device_close 等待所有回调结束」，
     *   「locker_device_close 停止 IO 和回调，线程退出后才允许销毁」。
     * 先 core_destroy 再 device_close 会把 reader 线程正在写的
     * reply_ring/event_ring 释放掉——TSan 实测 7 处 data race 全指向
     * bounded_queue_destroy（主线程）vs try_push（reader 线程）。 */
    (void)locker_core_stop(env.core, LOCKER_HOST_TIMEOUT_MS);
    env.core_started = false;
    locker_device_close(env.device);
    locker_core_destroy(env.core);
    env.core = NULL;
    locker_device_destroy(env.device);
    env.device = NULL;

    hello_before = it_counter(&env.server, &env.server.hello_requests);

    status = locker_device_create(&env.device);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, status);
    if (status != LOCKER_STATUS_OK) {
        it_env_stop(&env);
        return;
    }
    /* 新 Core 先 start（重新绑定 sink），再 open 设备（建立新会话）。 */
    status = it_env_start_core(&env);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, status);
    if (status != LOCKER_STATUS_OK) {
        it_env_stop(&env);
        return;
    }
    status = it_env_attach_device(&env);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, status);

    /* 新会话必须重新握手：HELLO 计数增加。 */
    deadline = it_now_ms() + IT_WAIT_MS;
    while (it_now_ms() < deadline &&
           it_counter(&env.server, &env.server.hello_requests) <= hello_before) {
        it_sleep_ms(50);
    }
    TEST_CHECK(it_counter(&env.server, &env.server.hello_requests) > hello_before);

    /* 恢复完成后，可能执行过的意图必须进核验，且带明确原因。 */
    TEST_CHECK(it_wait_operation(&env, &operation_id, "REVIEW_REQUIRED",
                                 "RECOVERED_AFTER_RESTART", IT_WAIT_MS));

    /* 关键：重启不重放动作。EXECUTE 计数必须停在重启之前。 */
    it_sleep_ms(IT_SILENCE_MS * 2u);
    TEST_CHECK_U64(executes_before, it_counter(&env.server, &env.server.execute_requests));

    it_env_stop(&env);
}

/* 两申请竞争：第二条必须被 BUSY 拒绝，不排队、不覆盖、不产生额外动作。 */
static void test_competing_requests(void)
{
    it_env_t env;
    locker_uuid_t first;
    locker_uuid_t second;
    locker_status_t status;

    if (!it_bring_up(&env, "compete")) {
        return;
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_create_operation(&env, &first));
    TEST_CHECK(it_wait_operation(&env, &first, "REQUESTED", NULL, IT_WAIT_MS));

    status = it_create_operation(&env, &second);
    TEST_CHECK_STATUS(LOCKER_STATUS_BUSY, status);

    /* 被拒的申请不能留下动作痕迹。 */
    TEST_CHECK_U64(0u, it_counter(&env.server, &env.server.execute_requests));
    TEST_CHECK(it_operation_in(&env, &first, "REQUESTED", NULL));

    it_env_stop(&env);
}

/* 错误用户不能确认：别人的操作对本人不可见也不可推进。 */
static void test_wrong_principal(void)
{
    it_env_t env;
    locker_uuid_t operation_id;
    locker_command_t command;
    locker_query_result_t result;
    locker_status_t status;

    if (!it_bring_up(&env, "principal")) {
        return;
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_create_operation(&env, &operation_id));
    TEST_CHECK(it_wait_operation(&env, &operation_id, "REQUESTED", NULL, IT_WAIT_MS));

    /* 提前确认：状态还停在 REQUESTED，必须 CONFLICT。 */
    memset(&command, 0, sizeof(command));
    command.principal = it_principal(LOCKER_ROLE_USER);
    command.request_id = it_uuid(0x71u);
    command.resource_id = operation_id;
    command.expected_revision = it_operation_revision(&env, &operation_id);
    snprintf(command.kind, sizeof(command.kind), "%s", LOCKER_CORE_OPERATION_CONFIRM);
    snprintf(command.arguments_json, sizeof(command.arguments_json),
             "{\"operation_revision\":\"%llu\"}", (unsigned long long)command.expected_revision);
    memset(&result, 0, sizeof(result));
    status = it_command(&env, &command, &result);
    /* 提交被受理（QUEUED）时 transport status 是 OK；业务上的「提前确认必须
       CONFLICT」落在 result.status 里——只断言 transport 会永远绿（实测
       business=6 而断言放行）。两个都必须是 CONFLICT：submit 拒绝也是拒。 */
    TEST_CHECK(status == LOCKER_STATUS_CONFLICT || result.status == LOCKER_STATUS_CONFLICT);
    /* 提前确认不许改变状态。 */
    TEST_CHECK(it_operation_in(&env, &operation_id, "REQUESTED", NULL));

    it_env_stop(&env);
}

/* Test-only load generator: fills the actual adapter's small socket buffer
 * without touching the ledger, while Core queries and progress keep running. */
static void *it_pressure_submitter(void *user)
{
    locker_device_t *device = (locker_device_t *)user;
    uint32_t number;
    for (number = 100000u; number < 100040u; ++number) {
        locker_status_t status = locker_device_get_snapshot(device, number);
        if (status != LOCKER_STATUS_OK && status != LOCKER_STATUS_BUSY) break;
    }
    return NULL;
}

static void test_core_progress_under_device_backpressure(void)
{
    it_env_t env;
    locker_progress_t before, after;
    locker_query_result_t result;
    pthread_t submitter;
    uint64_t deadline, started;
    bool paused = false, shrunk = false;
    int fd;
    locker_status_t status = it_env_start(&env, "corepressure");
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, status);
    if (status != LOCKER_STATUS_OK) { it_env_stop(&env); return; }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_env_attach_device(&env));
    TEST_CHECK(it_wait_device_ready(&env, IT_WAIT_MS));
    for (fd = 3; fd < 1024; ++fd) {
        struct sockaddr_un peer;
        socklen_t length = sizeof(peer);
        int bytes = 1024;
        memset(&peer, 0, sizeof(peer));
        if (getpeername(fd, (struct sockaddr *)&peer, &length) == 0 &&
            peer.sun_family == AF_UNIX && strcmp(peer.sun_path, env.server.socket_path) == 0) {
            shrunk = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes)) == 0;
            break;
        }
    }
    TEST_CHECK(shrunk);
    pthread_mutex_lock(&env.server.lock);
    env.server.pause_reads = true;
    pthread_mutex_unlock(&env.server.lock);
    deadline = it_now_ms() + 1000u;
    do {
        pthread_mutex_lock(&env.server.lock);
        paused = env.server.reads_paused;
        pthread_mutex_unlock(&env.server.lock);
        if (!paused) it_sleep_ms(1u);
    } while (!paused && it_now_ms() < deadline);
    TEST_CHECK(paused);
    status = pthread_create(&submitter, NULL, it_pressure_submitter, env.device) == 0
                 ? LOCKER_STATUS_OK : LOCKER_STATUS_IO_ERROR;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, status);
    if (status != LOCKER_STATUS_OK) { it_env_stop(&env); return; }
    it_sleep_ms(100u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_get_progress(env.core, &before));
    started = it_now_ms();
    memset(&result, 0, sizeof(result));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, it_query(&env, LOCKER_CORE_SNAPSHOT_GET, NULL, &result));
    TEST_CHECK(it_now_ms() - started < 250u);
    it_sleep_ms(300u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_get_progress(env.core, &after));
    TEST_CHECK(after.progress >= before.progress + 3u);
    TEST_CHECK(locker_uuid_equal(&before.instance_id, &after.instance_id));
    TEST_CHECK(pthread_join(submitter, NULL) == 0);
    TEST_CHECK_U64(0u, it_sim_pulses(&env.server));
    it_env_stop(&env);
}

#ifdef LOCKER_HTTP_FLOW_FIXTURE
static int serve_flow(int argc,char **argv)
{
    if (argc!=5) return 2;
    it_env_t env;
    if (!it_bring_up(&env,"https-flow")) return 1;
    locker_publisher_t *publisher=NULL; locker_http_t *http=NULL; locker_ipc_t *ipc=NULL;
    locker_http_config_t hc={0}; strcpy(hc.bind_address,"127.0.0.1"); hc.port=(uint16_t)atoi(argv[3]); hc.worker_count=4;
    snprintf(hc.certificate_path,sizeof(hc.certificate_path),"%s",argv[1]); snprintf(hc.private_key_path,sizeof(hc.private_key_path),"%s",argv[2]);
    locker_ipc_config_t ic={0}; ic.allowed_ui_uid=(uint32_t)getuid(); ic.allowed_admin_uid=(uint32_t)getuid(); ic.file_mode=0600;
    snprintf(ic.socket_path,sizeof(ic.socket_path),"%s",argv[4]);
    if (locker_events_create(&publisher)!=LOCKER_STATUS_OK || locker_http_start(&hc,env.core,publisher,&http)!=LOCKER_STATUS_OK ||
        locker_ipc_start(&ic,env.core,&ipc)!=LOCKER_STATUS_OK) return 1;
    puts("FLOW_READY"); fflush(stdout);
    int input;
    while ((input=getchar())!=EOF && input!='\n') {
        if (input=='T' || input=='R') {
            it_set_door(&env.server,LOCKER_WIRE_DOOR_STATE_OPEN); it_sleep_ms(120);
            it_set_item(&env.server,input=='T' ? LOCKER_WIRE_ITEM_STATE_ABSENT : LOCKER_WIRE_ITEM_STATE_PRESENT); it_sleep_ms(120);
            it_set_door(&env.server,LOCKER_WIRE_DOOR_STATE_CLOSED); puts("SCENE_DONE"); fflush(stdout);
        }
        if (input=='C') {
            const uint8_t uid[4]={0x11,0x22,0x33,0x44};
            pthread_mutex_lock(&env.server.lock); locker_sim_control_card(env.server.sim,uid,sizeof(uid)); pthread_mutex_unlock(&env.server.lock);
            puts("CARD_DONE"); fflush(stdout);
        }
        if (input=='P') { printf("PULSES:%llu\n",(unsigned long long)it_sim_pulses(&env.server)); fflush(stdout); }
    }
    locker_http_stop(http,5000); locker_http_destroy(http); locker_ipc_stop(ipc,5000); locker_ipc_destroy(ipc); locker_events_destroy(publisher);
    it_env_stop(&env); return 0;
}
#endif
int main(int argc,char **argv)
{
#ifdef LOCKER_HTTP_FLOW_FIXTURE
    if (argc==5) return serve_flow(argc,argv);
#else
    (void)argc; (void)argv;
#endif
    test_core_progress_under_device_backpressure();
    test_boot_and_session();
    test_borrow_flow();
    test_interrupt_before_pulse();
    test_interrupt_ack_lost();
    test_interrupt_pulse_suppressed();
    test_interrupt_restart_barrier();
    test_competing_requests();
    test_wrong_principal();
    /* TEST_REPORT 展开后自带 return 0/1，不能再套一层 return。 */
    TEST_REPORT("integration.test_core_interrupt_matrix");
    return 1;
}
