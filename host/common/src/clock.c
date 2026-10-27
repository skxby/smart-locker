#if defined(__linux__)
/* getrandom(2) lives behind _GNU_SOURCE in glibc. */
#define _GNU_SOURCE 1
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include "locker/common/clock.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/random.h>
#endif

static bool timespec_to_ms(const struct timespec *ts, uint64_t *out_ms) {
    if (ts == NULL || out_ms == NULL) {
        return false;
    }
    if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L) {
        return false;
    }
    *out_ms = ((uint64_t)ts->tv_sec * 1000u) + ((uint64_t)ts->tv_nsec / 1000000u);
    return true;
}

uint64_t locker_clock_real_monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    uint64_t ms = 0;
    if (!timespec_to_ms(&ts, &ms)) {
        return 0;
    }
    return ms;
}

bool locker_clock_real_wall_utc_ms(uint64_t *out_wall_ms) {
    if (out_wall_ms == NULL) {
        return false;
    }
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return false;
    }
    uint64_t ms = 0;
    if (!timespec_to_ms(&ts, &ms)) {
        return false;
    }
    *out_wall_ms = ms;
    return true;
}

static bool fill_random(uint8_t *buffer, size_t length) {
    size_t filled = 0;
#if defined(__linux__)
    while (filled < length) {
        ssize_t got = getrandom(buffer + filled, length - filled, 0);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        filled += (size_t)got;
    }
    if (filled == length) {
        return true;
    }
    filled = 0;
#endif
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return false;
    }
    while (filled < length) {
        ssize_t got = read(fd, buffer + filled, length - filled);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return false;
        }
        if (got == 0) {
            close(fd);
            return false;
        }
        filled += (size_t)got;
    }
    close(fd);
    return true;
}

locker_status_t locker_clock_real_new_uuid(locker_uuid_t *out_uuid) {
    if (out_uuid == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    uint8_t bytes[16];
    if (!fill_random(bytes, sizeof(bytes))) {
        return LOCKER_STATUS_IO_ERROR;
    }
    bytes[6] = (uint8_t)((bytes[6] & 0x0Fu) | 0x40u);
    bytes[8] = (uint8_t)((bytes[8] & 0x3Fu) | 0x80u);
    memcpy(out_uuid->bytes, bytes, sizeof(bytes));
    return LOCKER_STATUS_OK;
}

static uint64_t port_real_monotonic(void *user) {
    (void)user;
    return locker_clock_real_monotonic_ms();
}

static bool port_real_wall(void *user, uint64_t *out_wall_ms) {
    (void)user;
    return locker_clock_real_wall_utc_ms(out_wall_ms);
}

static locker_status_t port_real_new_uuid(void *user, locker_uuid_t *out_uuid) {
    (void)user;
    return locker_clock_real_new_uuid(out_uuid);
}

locker_clock_port_t locker_clock_port_real(void) {
    locker_clock_port_t port;
    port.user = NULL;
    port.monotonic_ms = port_real_monotonic;
    port.wall_utc_ms = port_real_wall;
    port.new_uuid = port_real_new_uuid;
    return port;
}

bool locker_clock_port_is_valid(const locker_clock_port_t *port) {
    return port != NULL && port->monotonic_ms != NULL && port->wall_utc_ms != NULL &&
           port->new_uuid != NULL;
}

locker_status_t locker_clock_port_monotonic_ms(const locker_clock_port_t *port, uint64_t *out_monotonic_ms) {
    if (!locker_clock_port_is_valid(port) || out_monotonic_ms == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    *out_monotonic_ms = port->monotonic_ms(port->user);
    return LOCKER_STATUS_OK;
}

locker_status_t locker_clock_port_wall_utc_ms(const locker_clock_port_t *port, uint64_t *out_wall_ms) {
    if (!locker_clock_port_is_valid(port) || out_wall_ms == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!port->wall_utc_ms(port->user, out_wall_ms)) {
        return LOCKER_STATUS_IO_ERROR;
    }
    return LOCKER_STATUS_OK;
}

locker_status_t locker_clock_port_new_uuid(const locker_clock_port_t *port, locker_uuid_t *out_uuid) {
    if (!locker_clock_port_is_valid(port) || out_uuid == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    return port->new_uuid(port->user, out_uuid);
}

locker_deadline_t locker_deadline_after(uint64_t now_ms, uint64_t timeout_ms) {
    locker_deadline_t deadline;
    deadline.valid = true;
    if (now_ms > UINT64_MAX - timeout_ms) {
        deadline.expires_ms = UINT64_MAX;
    } else {
        deadline.expires_ms = now_ms + timeout_ms;
    }
    return deadline;
}

bool locker_deadline_expired(const locker_deadline_t *deadline, uint64_t now_ms) {
    if (deadline == NULL || !deadline->valid) {
        return true;
    }
    return now_ms >= deadline->expires_ms;
}

uint64_t locker_deadline_remaining_ms(const locker_deadline_t *deadline, uint64_t now_ms) {
    if (deadline == NULL || !deadline->valid || now_ms >= deadline->expires_ms) {
        return 0;
    }
    return deadline->expires_ms - now_ms;
}
