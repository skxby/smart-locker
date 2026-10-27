/*
 * locker-device-sim: UART v1 device simulator daemon (S1-03).
 *
 * Two AF_UNIX SOCK_STREAM sockets, both created with mode 0600 and never
 * reachable over HTTP:
 *   --uart PATH     raw UART v1 bytes: request frames in, replies and
 *                   asynchronous DEVICE_EVENT frames out.
 *   --control PATH  private control entry (door/item/card/fault injection and a
 *                   status readback). One text command per line.
 *
 * The device engine (host/simulator/src/sim.c) is socket-free so unit tests can
 * drive it in-process; this file owns only sockets, frame boundaries and the
 * control text protocol.
 *
 * This daemon is a simulator. It does not drive a GPIO, does not implement
 * littlefs and does not prove MCU power-fail behaviour.
 */

/* sigaction, umask and AF_UNIX are POSIX; realpath() is XSI. -std=c11 without
 * extensions (CMAKE_C_EXTENSIONS OFF) hides both, and glibc only declares
 * realpath() under _XOPEN_SOURCE, so ask for XSI 700 rather than POSIX alone. */
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "locker/common/clock.h"
#include "locker/common/uuid.h"
#include "locker/simulator/sim.h"

#define SIM_MAIN_BACKLOG 4
#define SIM_MAIN_LINE_SIZE 256u
#define SIM_MAIN_MAX_FRAME_OUT LOCKER_WIRE_MAX_FRAME
/* Room for a canonical form of any path this daemon accepts. */
#define SIM_MAIN_CANON_SIZE 4096u
/* Symbolic links followed when canonicalising one path. A link chain longer
 * than this is refused rather than resolved, so a loop cannot spin here. */
#define SIM_MAIN_LINK_HOPS 8u

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int signal_number)
{
    (void)signal_number;
    g_stop = 1;
}

static void usage(const char *program)
{
    (void)fprintf(stderr,
                  "usage: %s --uart PATH --control PATH [--journal PATH] [--device-uid HEX12]\n",
                  program);
}

/* Rejects an over-long socket path before any database or socket exists, so a
 * configuration error cannot leave a half-initialised journal behind. */
static bool socket_path_fits(const char *path)
{
    struct sockaddr_un address;

    return path != NULL && strlen(path) < sizeof(address.sun_path);
}

/* Canonicalises a path whose final entry does not exist yet: the parent
 * directory must resolve and the leaf is kept as spelled. */
static bool canonical_missing(const char *path, char *out, size_t out_size)
{
    char parent[LOCKER_SIM_PATH_SIZE];
    char parent_real[SIM_MAIN_CANON_SIZE];
    const char *slash;
    const char *name;
    int written;

    slash = strrchr(path, '/');
    if (slash == NULL) {
        if (getcwd(parent_real, sizeof(parent_real)) == NULL) {
            return false;
        }
        name = path;
    } else {
        size_t length = (size_t)(slash - path);

        if (length == 0u) {
            length = 1u;
        }
        if (length >= sizeof(parent)) {
            return false;
        }
        memcpy(parent, path, length);
        parent[length] = '\0';
        if (realpath(parent, parent_real) == NULL) {
            return false;
        }
        name = slash + 1;
    }
    if (name[0] == '\0') {
        return false;
    }
    written = snprintf(out, out_size, "%s/%s", parent_real, name);
    return written > 0 && (size_t)written < out_size;
}

/* Reads one symbolic link and returns the absolute path it names, resolving a
 * relative target against the link's own directory. */
static bool read_link_target(const char *link_path, char *out, size_t out_size)
{
    char target[SIM_MAIN_CANON_SIZE];
    char base[LOCKER_SIM_PATH_SIZE];
    char base_real[SIM_MAIN_CANON_SIZE];
    const char *slash;
    ssize_t length;
    int written;

    if (strlen(link_path) >= sizeof(target)) {
        return false;
    }
    length = readlink(link_path, target, sizeof(target) - 1u);
    if (length <= 0 || (size_t)length >= sizeof(target) - 1u) {
        return false;
    }
    target[length] = '\0';
    if (target[0] == '/') {
        written = snprintf(out, out_size, "%s", target);
        return written > 0 && (size_t)written < out_size;
    }
    slash = strrchr(link_path, '/');
    if (slash == NULL) {
        if (getcwd(base_real, sizeof(base_real)) == NULL) {
            return false;
        }
    } else {
        size_t base_length = (size_t)(slash - link_path);

        if (base_length == 0u) {
            base_length = 1u;
        }
        if (base_length >= sizeof(base)) {
            return false;
        }
        memcpy(base, link_path, base_length);
        base[base_length] = '\0';
        if (realpath(base, base_real) == NULL) {
            return false;
        }
    }
    written = snprintf(out, out_size, "%s/%s", base_real, target);
    return written > 0 && (size_t)written < out_size;
}

/* Resolves one path to the filesystem entry it will actually address, so two
 * spellings of the same entry ("D/x" and "D/./x", or a symlinked parent) compare
 * equal. Missing entries are canonicalised through their parent directory, which
 * is what lets the check run before anything is created. A final component that
 * is a dangling symbolic link is followed to its target: SQLite creates the
 * target file, so the link name is not the entry the daemon really writes. */
static bool canonical_entry(const char *path, char *out, size_t out_size)
{
    char current[SIM_MAIN_CANON_SIZE];
    char resolved[SIM_MAIN_CANON_SIZE];
    char next[SIM_MAIN_CANON_SIZE];
    unsigned hops = 0u;

    if (path == NULL || out == NULL || out_size == 0u || path[0] == '\0') {
        return false;
    }
    if (strlen(path) >= sizeof(current)) {
        return false;
    }
    (void)snprintf(current, sizeof(current), "%s", path);
    for (;;) {
        struct stat info;
        int written;

        if (realpath(current, resolved) != NULL) {
            written = snprintf(out, out_size, "%s", resolved);
            return written > 0 && (size_t)written < out_size;
        }
        if (errno != ENOENT) {
            return false;
        }
        if (lstat(current, &info) != 0) {
            if (errno != ENOENT) {
                return false;
            }
            return canonical_missing(current, out, out_size);
        }
        if (!S_ISLNK(info.st_mode)) {
            return false;
        }
        hops += 1u;
        if (hops > SIM_MAIN_LINK_HOPS) {
            return false;
        }
        if (!read_link_target(current, next, sizeof(next))) {
            return false;
        }
        memcpy(current, next, strlen(next) + 1u);
    }
}

/* Reports whether a resolved entry collides with either socket path. */
static bool clashes_with_sockets(const char *entry, const char *uart, const char *control)
{
    return strcmp(entry, uart) == 0 || strcmp(entry, control) == 0;
}

/* Rejects any configuration where two roles would fight over one filesystem
 * entry. A literal strcmp is not enough: "D/./c" and "D/c" name the same entry,
 * and a journal whose -wal file is a socket path silently replaces the control
 * entry on the next open. The journal is checked under both spellings: the
 * resolved target is the entry the database becomes, while the literal spelling
 * is this module's own conservative rule, not a SQLite requirement (the VFS
 * resolves links and would put -wal/-shm on the target). */
static bool paths_are_distinct(const char *uart_path, const char *control_path,
                               const char *journal_path)
{
    char uart[SIM_MAIN_CANON_SIZE];
    char control[SIM_MAIN_CANON_SIZE];
    char journal[SIM_MAIN_CANON_SIZE];
    char journal_spelling[SIM_MAIN_CANON_SIZE];
    char sidecar[SIM_MAIN_CANON_SIZE + 8u];
    const char *bases[2];
    const char *suffixes[2];
    size_t base_index;
    size_t suffix_index;
    int written;

    if (!canonical_entry(uart_path, uart, sizeof(uart)) ||
        !canonical_entry(control_path, control, sizeof(control)) ||
        !canonical_entry(journal_path, journal, sizeof(journal)) ||
        !canonical_missing(journal_path, journal_spelling, sizeof(journal_spelling))) {
        return false;
    }
    if (strcmp(uart, control) == 0) {
        return false;
    }
    /* Only the resolved target can equal a socket path here: realpath() never
     * returns a path whose last component is a link, so a socket that resolved
     * to the link spelling would have been followed to the same target. The
     * spelling still matters below as an extra restriction of this module,
     * which does not rely on how a VFS resolves links when appending -wal. */
    if (clashes_with_sockets(journal, uart, control)) {
        return false;
    }
    bases[0] = journal;
    bases[1] = journal_spelling;
    suffixes[0] = "-wal";
    suffixes[1] = "-shm";
    for (base_index = 0u; base_index < 2u; base_index += 1u) {
        for (suffix_index = 0u; suffix_index < 2u; suffix_index += 1u) {
            written = snprintf(sidecar, sizeof(sidecar), "%s%s", bases[base_index],
                               suffixes[suffix_index]);
            if (written <= 0 || (size_t)written >= sizeof(sidecar)) {
                return false;
            }
            if (clashes_with_sockets(sidecar, uart, control)) {
                return false;
            }
        }
    }
    return true;
}

/* Creates a listening AF_UNIX socket whose filesystem entry is mode 0600.
 * The umask is narrowed before bind() and the mode is re-applied afterwards so a
 * pre-existing permissive file cannot survive. */
static int listen_unix(const char *path)
{
    struct sockaddr_un address;
    mode_t previous;
    int fd;

    if (path == NULL || strlen(path) >= sizeof(address.sun_path)) {
        return -1;
    }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    (void)snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
    (void)unlink(path);
    previous = umask(0077);
    if (bind(fd, (struct sockaddr *)&address, (socklen_t)sizeof(address)) != 0) {
        (void)umask(previous);
        (void)close(fd);
        return -1;
    }
    (void)umask(previous);
    if (chmod(path, S_IRUSR | S_IWUSR) != 0) {
        (void)close(fd);
        (void)unlink(path);
        return -1;
    }
    if (listen(fd, SIM_MAIN_BACKLOG) != 0) {
        (void)close(fd);
        (void)unlink(path);
        return -1;
    }
    return fd;
}

/* A slow peer cannot pin the single daemon loop indefinitely. */
static bool write_all(int fd, const uint8_t *data, size_t length)
{
    size_t written = 0u;
    struct timespec start;

    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        return false;
    }
    while (written < length) {
        struct timespec now;
        int64_t elapsed;
        ssize_t rc;
        struct pollfd writable = {fd, POLLOUT, 0};

        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            return false;
        }
        elapsed = (int64_t)(now.tv_sec - start.tv_sec) * 1000 +
                  (int64_t)(now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed >= 1000 || g_stop) {
            return false;
        }
        rc = send(fd, data + written, length - written, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (rc > 0) {
            written += (size_t)rc;
        } else if (rc < 0 && errno == EINTR) {
            continue;
        } else if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            int ready = poll(&writable, 1u, (int)(1000 - elapsed));
            if (ready < 0 && errno == EINTR) {
                continue;
            }
            if (ready <= 0 || (writable.revents & POLLOUT) == 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

typedef struct sim_service {
    locker_sim_t *sim;
    int uart_fd;
    int uart_client;
    int control_fd;
    int control_client;
    locker_wire_stream_t stream;
    char line[SIM_MAIN_LINE_SIZE];
    size_t line_length;
    bool discarding_line;
} sim_service_t;

static void service_drop_uart(sim_service_t *service)
{
    if (service->uart_client >= 0) {
        (void)close(service->uart_client);
        service->uart_client = -1;
    }
    locker_wire_stream_reset(&service->stream);
}

static void service_drop_control(sim_service_t *service)
{
    if (service->control_client >= 0) {
        (void)close(service->control_client);
        service->control_client = -1;
    }
    service->line_length = 0u;
    service->discarding_line = false;
}

/* Events are durable before they enter this best-effort notification path.
 * A failed write drops the link; FETCH recovers every committed event. */
static bool service_flush_events(sim_service_t *service)
{
    uint8_t event[LOCKER_WIRE_MAX_FRAME];
    size_t length = 0u;
    locker_status_t status;

    if (service->uart_client < 0) {
        return true;
    }
    while ((status = locker_sim_pop_event(service->sim, event, sizeof(event), &length)) ==
           LOCKER_STATUS_OK) {
        if (!write_all(service->uart_client, event, length)) {
            return false;
        }
    }
    return status == LOCKER_STATUS_NOT_READY;
}

/* wire.c owns all framing and byte-by-byte CRC resynchronization. Reconstitute
 * the exact accepted header (including unsupported type/version) for dispatch. */
static locker_status_t service_on_frame(void *user, const locker_wire_frame_t *frame)
{
    sim_service_t *service = user;
    uint8_t raw[LOCKER_WIRE_MAX_FRAME];
    uint8_t reply[SIM_MAIN_MAX_FRAME_OUT];
    size_t length = locker_wire_frame_size(frame->payload_len);
    size_t reply_length = 0u;
    uint16_t crc;
    unsigned index;
    locker_status_t status;

    raw[0] = LOCKER_WIRE_MAGIC_FIRST;
    raw[1] = LOCKER_WIRE_MAGIC_SECOND;
    raw[2] = frame->version;
    raw[3] = frame->type;
    raw[4] = (uint8_t)frame->payload_len;
    raw[5] = (uint8_t)(frame->payload_len >> 8);
    for (index = 0u; index < 4u; ++index) {
        raw[6u + index] = (uint8_t)(frame->request_no >> (index * 8u));
    }
    memcpy(raw + LOCKER_WIRE_HEADER_SIZE, frame->payload, frame->payload_len);
    crc = locker_wire_crc16(raw + 2u, length - 4u);
    raw[length - 2u] = (uint8_t)crc;
    raw[length - 1u] = (uint8_t)(crc >> 8);
    status = locker_sim_handle_frame(service->sim, raw, length, reply, sizeof(reply),
                                    &reply_length);
    if (status != LOCKER_STATUS_OK) {
        /* Invalid requests have no side effects; keep parsing the next frame. */
        return LOCKER_STATUS_OK;
    }
    if ((reply_length > 0u && !write_all(service->uart_client, reply, reply_length)) ||
        !service_flush_events(service)) {
        return LOCKER_STATUS_IO_ERROR;
    }
    return LOCKER_STATUS_OK;
}

static bool service_read_uart(sim_service_t *service)
{
    uint8_t chunk[LOCKER_WIRE_MAX_FRAME];
    ssize_t rc = read(service->uart_client, chunk, sizeof(chunk));

    if (rc <= 0) {
        if (rc < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        service_drop_uart(service);
        return true;
    }
    if (locker_wire_stream_feed(&service->stream, chunk, (size_t)rc) != LOCKER_STATUS_OK) {
        service_drop_uart(service);
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* control protocol                                                    */
/* ------------------------------------------------------------------ */

/* Short, stable token for a rejected control request. A bare "error" leaves the
 * operator guessing which guard fired, so every refusal names its status. */
static const char *status_token(locker_status_t status)
{
    switch (status) {
    case LOCKER_STATUS_INVALID:
        return "invalid";
    case LOCKER_STATUS_NOT_READY:
        return "not_ready";
    case LOCKER_STATUS_CONFLICT:
        return "conflict";
    case LOCKER_STATUS_NO_SPACE:
        return "no_space";
    case LOCKER_STATUS_IO_ERROR:
        return "io_error";
    case LOCKER_STATUS_BUSY:
        return "busy";
    case LOCKER_STATUS_UNKNOWN:
        return "unknown";
    case LOCKER_STATUS_REPLAY_BLOCKED:
        return "replay_blocked";
    default:
        return "failed";
    }
}

static void control_reply(int fd, bool ok, const char *detail)
{
    char line[SIM_MAIN_LINE_SIZE];

    if (ok) {
        (void)snprintf(line, sizeof(line), "ok%s%s\n", detail != NULL ? " " : "",
                       detail != NULL ? detail : "");
    } else {
        (void)snprintf(line, sizeof(line), "error%s%s\n", detail != NULL ? " " : "",
                       detail != NULL ? detail : "");
    }
    (void)write_all(fd, (const uint8_t *)line, strlen(line));
}

/* Reports a simulator status: "ok" or "error <token>", never a bare "error". */
static void control_reply_status(int fd, locker_status_t status)
{
    control_reply(fd, status == LOCKER_STATUS_OK,
                  status == LOCKER_STATUS_OK ? NULL : status_token(status));
}

static bool parse_on_off(const char *token, bool *out)
{
    if (strcmp(token, "on") == 0 || strcmp(token, "1") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(token, "off") == 0 || strcmp(token, "0") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_u64(const char **text, uint64_t *out)
{
    const char *cursor = *text;
    uint64_t value = 0u;

    if (*cursor < '0' || *cursor > '9') {
        return false;
    }
    while (*cursor >= '0' && *cursor <= '9') {
        unsigned digit = (unsigned)(*cursor - '0');
        if (value > (UINT64_MAX - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
        ++cursor;
    }
    *text = cursor;
    *out = value;
    return true;
}

static bool parse_cursor(const char *text, locker_wire_cursor_t *out)
{
    if (!parse_u64(&text, &out->boot_id) || *text++ != ':' ||
        !parse_u64(&text, &out->event_seq) || *text != '\0') {
        return false;
    }
    return locker_wire_cursor_valid(out);
}

static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static bool parse_door(const char *token, uint8_t *out)
{
    if (strcmp(token, "unknown") == 0) {
        *out = LOCKER_WIRE_DOOR_STATE_UNKNOWN;
        return true;
    }
    if (strcmp(token, "closed") == 0) {
        *out = LOCKER_WIRE_DOOR_STATE_CLOSED;
        return true;
    }
    if (strcmp(token, "open") == 0) {
        *out = LOCKER_WIRE_DOOR_STATE_OPEN;
        return true;
    }
    return false;
}

static bool parse_item(const char *token, uint8_t *out)
{
    if (strcmp(token, "unknown") == 0) {
        *out = LOCKER_WIRE_ITEM_STATE_UNKNOWN;
        return true;
    }
    if (strcmp(token, "absent") == 0) {
        *out = LOCKER_WIRE_ITEM_STATE_ABSENT;
        return true;
    }
    if (strcmp(token, "present") == 0) {
        *out = LOCKER_WIRE_ITEM_STATE_PRESENT;
        return true;
    }
    return false;
}

static bool parse_hex_bytes(const char *text, uint8_t *out, size_t out_size, size_t *out_length)
{
    size_t length = strlen(text);
    size_t index;

    if (length == 0u || (length % 2u) != 0u || length / 2u > out_size) {
        return false;
    }
    for (index = 0u; index < length; index += 2u) {
        int high = hex_digit(text[index]);
        int low = hex_digit(text[index + 1u]);

        if (high < 0 || low < 0) {
            return false;
        }
        out[index / 2u] = (uint8_t)((unsigned)high * 16u + (unsigned)low);
    }
    *out_length = length / 2u;
    return true;
}

static void control_status(sim_service_t *service, int fd)
{
    char line[512];
    locker_uuid_t session = locker_sim_session_id(service->sim);
    locker_uuid_t epoch = locker_sim_journal_epoch(service->sim);
    char session_text[37];
    char epoch_text[37];
    locker_wire_cursor_t first = locker_sim_first_available(service->sim);
    locker_wire_cursor_t last = locker_sim_last_committed(service->sim);
    locker_wire_cursor_t ack = locker_sim_ack_persisted(service->sim);

    (void)locker_uuid_format(&session, session_text, sizeof(session_text));
    (void)locker_uuid_format(&epoch, epoch_text, sizeof(epoch_text));
    (void)snprintf(line, sizeof(line),
                   "session=%s\njournal_epoch=%s\nboot_id=%llu\naction_watermark=%llu\n"
                   "dropped_events=%llu\ndevice_mode=%u\njournal_state=%u\ndoor=%u\nitem=%u\n"
                   "command_state=%u\nhas_active=%u\ncontinuity_ok=%u\n"
                   "first_available=%llu:%llu\nlast_committed=%llu:%llu\nack_persisted=%llu:%llu\n"
                   "event_count=%llu\n",
                   session_text, epoch_text, (unsigned long long)locker_sim_boot_id(service->sim),
                   (unsigned long long)locker_sim_action_watermark(service->sim),
                   (unsigned long long)locker_sim_dropped_events(service->sim),
                   (unsigned)locker_sim_device_mode(service->sim),
                   (unsigned)locker_sim_journal_state(service->sim),
                   (unsigned)locker_sim_door(service->sim),
                   (unsigned)locker_sim_item(service->sim),
                   (unsigned)locker_sim_command_state(service->sim),
                   locker_sim_has_active_intent(service->sim) ? 1u : 0u,
                   locker_sim_continuity_ok(service->sim) ? 1u : 0u,
                   (unsigned long long)first.boot_id, (unsigned long long)first.event_seq,
                   (unsigned long long)last.boot_id, (unsigned long long)last.event_seq,
                   (unsigned long long)ack.boot_id, (unsigned long long)ack.event_seq,
                   (unsigned long long)locker_sim_event_count(service->sim));
    (void)write_all(fd, (const uint8_t *)line, strlen(line));
}

static void control_command(sim_service_t *service, int fd, char *line)
{
    char *command = line;
    char *argument = NULL;
    char *space = strchr(line, ' ');

    if (space != NULL) {
        *space = '\0';
        argument = space + 1;
        while (*argument == ' ') {
            argument += 1;
        }
    }
    if ((strcmp(command, "status") == 0 || strcmp(command, "quit") == 0 ||
         strcmp(command, "restart") == 0 || strcmp(command, "drop_link") == 0) &&
        argument != NULL) {
        control_reply(fd, false, "bad_argument");
        return;
    }
    if (strcmp(command, "status") == 0) {
        control_status(service, fd);
        return;
    }
    if (strcmp(command, "quit") == 0) {
        control_reply(fd, true, NULL);
        g_stop = 1;
        return;
    }
    if (strcmp(command, "restart") == 0) {
        /* Reopen the journal and advance boot_id; no arguments are accepted. */
        control_reply_status(fd, locker_sim_restart(service->sim));
        return;
    }
    if (strcmp(command, "drop_link") == 0) {
        /* The card requires the control entry to inject a link break. Closing the
         * accepted UART socket is exactly what a pulled cable looks like to the
         * host adapter: the reader gets EOF, marks the link down and stops. The
         * device state itself is untouched, so a reconnect resumes from the same
         * journal - that is the point of the fault injection. */
        service_drop_uart(service);
        control_reply(fd, true, NULL);
        return;
    }
    if (argument == NULL) {
        control_reply(fd, false, "missing_argument");
        return;
    }
    if (strcmp(command, "door") == 0) {
        uint8_t door = 0u;

        if (!parse_door(argument, &door)) {
            control_reply(fd, false, "bad_argument");
            return;
        }
        control_reply_status(fd, locker_sim_control_set_door(service->sim, door));
        return;
    }
    if (strcmp(command, "item") == 0) {
        uint8_t item = 0u;

        if (!parse_item(argument, &item)) {
            control_reply(fd, false, "bad_argument");
            return;
        }
        control_reply_status(fd, locker_sim_control_set_item(service->sim, item));
        return;
    }
    if (strcmp(command, "card") == 0) {
        uint8_t uid[10];
        size_t uid_length = 0u;

        if (!parse_hex_bytes(argument, uid, sizeof(uid), &uid_length)) {
            control_reply(fd, false, "bad_argument");
            return;
        }
        control_reply_status(fd, locker_sim_control_card(service->sim, uid, uid_length));
        return;
    }
    if (strcmp(command, "reject_next") == 0 || strcmp(command, "drop_ack") == 0 ||
        strcmp(command, "drop_execute_ack") == 0 ||
        strcmp(command, "suppress_pulse") == 0 || strcmp(command, "fail_journal") == 0) {
        bool enabled = false;
        locker_status_t status;

        if (!parse_on_off(argument, &enabled)) {
            control_reply(fd, false, "bad_argument");
            return;
        }
        if (strcmp(command, "reject_next") == 0) {
            status = locker_sim_control_reject_next(service->sim, enabled);
        } else if (strcmp(command, "drop_ack") == 0) {
            status = locker_sim_control_drop_ack(service->sim, enabled);
        } else if (strcmp(command, "drop_execute_ack") == 0) {
            status = locker_sim_control_drop_execute_ack(service->sim, enabled);
        } else if (strcmp(command, "suppress_pulse") == 0) {
            status = locker_sim_control_suppress_pulse(service->sim, enabled);
        } else {
            status = locker_sim_control_fail_journal(service->sim, enabled);
        }
        control_reply_status(fd, status);
        return;
    }
    if (strcmp(command, "inject_gap") == 0) {
        uint64_t dropped = 0u;
        const char *end = argument;

        if (!parse_u64(&end, &dropped) || *end != '\0') {
            control_reply(fd, false, "bad_argument");
            return;
        }
        control_reply_status(fd,
                             locker_sim_control_inject_gap(service->sim, (uint64_t)dropped));
        return;
    }
    if (strcmp(command, "cursor") == 0) {
        locker_wire_cursor_t cursor = {0u, 0u};
        char text[64];

        if (!parse_cursor(argument, &cursor)) {
            control_reply(fd, false, "bad_argument");
            return;
        }
        (void)snprintf(text, sizeof(text), "%llu:%llu", (unsigned long long)cursor.boot_id,
                       (unsigned long long)cursor.event_seq);
        control_reply(fd, true, text);
        return;
    }
    control_reply(fd, false, "unknown_command");
}

static bool service_read_control(sim_service_t *service)
{
    char chunk[SIM_MAIN_LINE_SIZE];
    ssize_t rc = read(service->control_client, chunk, sizeof(chunk));
    size_t index;

    if (rc <= 0) {
        if (rc < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        service_drop_control(service);
        return true;
    }
    for (index = 0u; index < (size_t)rc; ++index) {
        char ch = chunk[index];
        if (service->discarding_line) {
            if (ch == '\n') {
                service->discarding_line = false;
            }
            continue;
        }
        if (ch == '\n') {
            if (service->line_length > 0u && service->line[service->line_length - 1u] == '\r') {
                --service->line_length;
            }
            service->line[service->line_length] = '\0';
            control_command(service, service->control_client, service->line);
            service->line_length = 0u;
            if (g_stop) {
                break;
            }
        } else if (ch == '\0' || service->line_length == sizeof(service->line) - 1u) {
            control_reply(service->control_client, false,
                          ch == '\0' ? "bad_argument" : "line_too_long");
            service->line_length = 0u;
            service->discarding_line = true;
        } else {
            service->line[service->line_length++] = ch;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static bool parse_uuid_hex(const char *text, uint8_t *out, size_t out_size)
{
    size_t length = strlen(text);
    size_t index;

    if (length != out_size * 2u) {
        return false;
    }
    for (index = 0u; index < out_size; index += 1u) {
        int high = hex_digit(text[index * 2u]);
        int low = hex_digit(text[index * 2u + 1u]);

        if (high < 0 || low < 0) {
            return false;
        }
        out[index] = (uint8_t)((unsigned)high * 16u + (unsigned)low);
    }
    return true;
}

int main(int argc, char **argv)
{
    const char *uart_path = NULL;
    const char *control_path = NULL;
    const char *journal_path = NULL;
    const char *device_uid_text = NULL;
    locker_sim_config_t config;
    locker_sim_t *sim = NULL;
    sim_service_t service;
    struct sigaction action;
    int index;

    for (index = 1; index < argc; index += 1) {
        if (strcmp(argv[index], "--uart") == 0 && index + 1 < argc) {
            uart_path = argv[++index];
        } else if (strcmp(argv[index], "--control") == 0 && index + 1 < argc) {
            control_path = argv[++index];
        } else if (strcmp(argv[index], "--journal") == 0 && index + 1 < argc) {
            journal_path = argv[++index];
        } else if (strcmp(argv[index], "--device-uid") == 0 && index + 1 < argc) {
            device_uid_text = argv[++index];
        } else if (strcmp(argv[index], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (uart_path == NULL || control_path == NULL) {
        usage(argv[0]);
        return 2;
    }
    /* Every path is validated before a single resource exists: a rejected
     * configuration must not leave a half-built journal or a stale socket. */
    if (!socket_path_fits(uart_path) || !socket_path_fits(control_path)) {
        (void)fprintf(stderr, "socket path too long\n");
        return 2;
    }
    memset(&config, 0, sizeof(config));
    config.clock = locker_clock_port_real();
    if (journal_path == NULL) {
        int written = snprintf(config.journal_path, sizeof(config.journal_path), "%s.journal",
                               control_path);

        /* A silently truncated default would overwrite the control socket path
         * (control of 107 bytes truncates "%s.journal" back to itself), so the
         * return value is checked instead of discarded. */
        if (written < 0 || (size_t)written >= sizeof(config.journal_path)) {
            (void)fprintf(stderr, "journal path too long\n");
            return 2;
        }
    } else if (strlen(journal_path) >= sizeof(config.journal_path)) {
        (void)fprintf(stderr, "journal path too long\n");
        return 2;
    } else {
        (void)snprintf(config.journal_path, sizeof(config.journal_path), "%s", journal_path);
    }
    /* Two roles must not share one filesystem entry, and a socket must not sit
     * on the journal's -wal/-shm names: the daemon would unlink its own
     * database on bind, or hand a later restart a control path that is no
     * longer a socket. Compared on resolved entries, not on raw strings. */
    if (!paths_are_distinct(uart_path, control_path, config.journal_path)) {
        (void)fprintf(stderr, "paths must name distinct filesystem entries\n");
        return 2;
    }
    if (device_uid_text != NULL &&
        !parse_uuid_hex(device_uid_text, config.device_uid, LOCKER_SIM_DEVICE_UID_SIZE)) {
        (void)fprintf(stderr, "device uid must be 24 hex characters\n");
        return 2;
    } else if (device_uid_text == NULL) {
        for (index = 0; index < (int)LOCKER_SIM_DEVICE_UID_SIZE; index += 1) {
            config.device_uid[index] = (uint8_t)(0x40u + index);
        }
    }
    if (locker_sim_create(&sim, &config) != LOCKER_STATUS_OK) {
        (void)fprintf(stderr, "cannot create simulator\n");
        return 1;
    }
    if (locker_sim_open(sim) != LOCKER_STATUS_OK) {
        (void)fprintf(stderr, "cannot open journal %s\n", config.journal_path);
        locker_sim_destroy(sim);
        return 1;
    }
    memset(&service, 0, sizeof(service));
    service.sim = sim;
    locker_wire_stream_init(&service.stream, &service, service_on_frame);
    service.uart_fd = -1;
    service.uart_client = -1;
    service.control_fd = -1;
    service.control_client = -1;
    service.uart_fd = listen_unix(uart_path);
    if (service.uart_fd < 0) {
        (void)fprintf(stderr, "cannot listen on %s\n", uart_path);
        locker_sim_destroy(sim);
        return 1;
    }
    service.control_fd = listen_unix(control_path);
    if (service.control_fd < 0) {
        (void)fprintf(stderr, "cannot listen on %s\n", control_path);
        (void)close(service.uart_fd);
        (void)unlink(uart_path);
        locker_sim_destroy(sim);
        return 1;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);
    (void)signal(SIGPIPE, SIG_IGN);

    /* Ready marker for scripts: both sockets exist and are mode 0600. */
    (void)fprintf(stdout, "ready device backend: simulator uart=%s control=%s journal=%s\n", uart_path, control_path,
                  config.journal_path);
    (void)fflush(stdout);

    while (!g_stop) {
        struct pollfd fds[4];
        nfds_t count = 0u;
        int uart_index = -1;
        int control_index = -1;
        int uart_client_index = -1;
        int control_client_index = -1;
        int rc;

        fds[count].fd = service.uart_fd;
        fds[count].events = POLLIN;
        uart_index = (int)count;
        count += 1u;
        fds[count].fd = service.control_fd;
        fds[count].events = POLLIN;
        control_index = (int)count;
        count += 1u;
        if (service.uart_client >= 0) {
            fds[count].fd = service.uart_client;
            fds[count].events = POLLIN;
            uart_client_index = (int)count;
            count += 1u;
        }
        if (service.control_client >= 0) {
            fds[count].fd = service.control_client;
            fds[count].events = POLLIN;
            control_client_index = (int)count;
            count += 1u;
        }
        rc = poll(fds, count, 200);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (!service_flush_events(&service)) {
            service_drop_uart(&service);
        }
        if (rc == 0) {
            continue;
        }
        if ((fds[uart_index].revents & POLLIN) != 0) {
            int client = accept(service.uart_fd, NULL, NULL);

            if (client >= 0) {
                service_drop_uart(&service);
                service.uart_client = client;
            }
        }
        if ((fds[control_index].revents & POLLIN) != 0) {
            int client = accept(service.control_fd, NULL, NULL);

            if (client >= 0) {
                service_drop_control(&service);
                service.control_client = client;
            }
        }
        if (uart_client_index >= 0 && fds[uart_client_index].fd == service.uart_client &&
            (fds[uart_client_index].revents & POLLIN) != 0) {
            (void)service_read_uart(&service);
        }
        if (control_client_index >= 0 && fds[control_client_index].fd == service.control_client &&
            (fds[control_client_index].revents & POLLIN) != 0) {
            (void)service_read_control(&service);
            if (!service_flush_events(&service)) {
                service_drop_uart(&service);
            }
        }
        if (uart_client_index >= 0 && fds[uart_client_index].fd == service.uart_client &&
            (fds[uart_client_index].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
            service_drop_uart(&service);
        }
        if (control_client_index >= 0 && fds[control_client_index].fd == service.control_client &&
            (fds[control_client_index].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
            service_drop_control(&service);
        }
    }

    service_drop_uart(&service);
    service_drop_control(&service);
    (void)close(service.uart_fd);
    (void)close(service.control_fd);
    (void)unlink(uart_path);
    (void)unlink(control_path);
    locker_sim_destroy(sim);
    return 0;
}
