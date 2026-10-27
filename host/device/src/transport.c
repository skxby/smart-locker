/*
 * S1-03 AF_UNIX SOCK_STREAM transport for the simulator-backed device adapter.
 */

/* clock_gettime/nanosleep are POSIX; -std=c11 without extensions hides them. */
#define _POSIX_C_SOURCE 200809L

#include "locker/device/transport.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define TRANSPORT_CONNECT_RETRY_MS 10u

static uint64_t transport_real_monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0u;
    }
    return ((uint64_t)now.tv_sec * 1000u) + ((uint64_t)now.tv_nsec / 1000000u);
}

void locker_device_transport_init(locker_device_transport_t *transport)
{
    if (transport == NULL) {
        return;
    }
    memset(transport, 0, sizeof(*transport));
    transport->fd = -1;
    atomic_init(&transport->connected, false);
}

bool locker_device_transport_connected(const locker_device_transport_t *transport)
{
    return transport != NULL && transport->connected && transport->fd >= 0;
}

void locker_device_transport_shutdown(locker_device_transport_t *transport)
{
    if (transport == NULL || transport->fd < 0) {
        return;
    }
    /* Wake a reader blocked in recv(); the reader then observes EOF. */
    (void)shutdown(transport->fd, SHUT_RDWR);
}

void locker_device_transport_close(locker_device_transport_t *transport)
{
    if (transport == NULL) {
        return;
    }
    if (transport->fd >= 0) {
        (void)close(transport->fd);
    }
    transport->fd = -1;
    transport->connected = false;
}

/* Reads the deferred connect() result. Only meaningful once poll() reported the
 * socket writable; a refused connection shows up as a non-zero SO_ERROR. */
static locker_status_t transport_connect_result(int fd)
{
    int error = 0;
    socklen_t length = (socklen_t)sizeof(error);

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0) {
        return LOCKER_STATUS_IO_ERROR;
    }
    return LOCKER_STATUS_OK;
}

/*
 * One connection attempt, bounded by deadline_ms.
 *
 * connect() must not run in blocking mode: a listener whose accept queue is
 * full parks a blocking connect() inside the kernel, where no deadline, no
 * signal and no clock can reach it, so the documented 5000 ms connect budget
 * would be unenforceable. The socket is therefore created non-blocking, the
 * deferred result is waited for with poll() until the same deadline, and the
 * socket is switched back to blocking mode before publication because read()
 * relies on SO_RCVTIMEO.
 */
static locker_status_t transport_try_connect(locker_device_transport_t *transport,
                                             uint64_t deadline_ms)
{
    struct sockaddr_un address;
    struct pollfd writable;
    uint64_t now_ms;
    int fd;
    int flags;
    size_t path_length;

    path_length = strlen(transport->path);
    if (path_length == 0u || path_length >= sizeof(address.sun_path)) {
        return LOCKER_STATUS_INVALID;
    }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return LOCKER_STATUS_IO_ERROR;
    }
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        (void)close(fd);
        return LOCKER_STATUS_IO_ERROR;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, transport->path, path_length + 1u);
    if (connect(fd, (const struct sockaddr *)&address, (socklen_t)sizeof(address)) != 0) {
        locker_status_t status;
        if (errno != EINPROGRESS && errno != EINTR) {
            /* EAGAIN means a full accept queue: the caller retries until the
             * deadline instead of hanging here. */
            (void)close(fd);
            return LOCKER_STATUS_IO_ERROR;
        }
        now_ms = transport_real_monotonic_ms();
        if (now_ms >= deadline_ms) {
            (void)close(fd);
            return LOCKER_STATUS_IO_ERROR;
        }
        writable.fd = fd;
        writable.events = POLLOUT;
        writable.revents = 0;
        if (poll(&writable, 1u, (int)(deadline_ms - now_ms)) <= 0) {
            (void)close(fd);
            return LOCKER_STATUS_IO_ERROR;
        }
        status = transport_connect_result(fd);
        if (status != LOCKER_STATUS_OK) {
            (void)close(fd);
            return status;
        }
    }
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) != 0) {
        (void)close(fd);
        return LOCKER_STATUS_IO_ERROR;
    }
    transport->fd = fd;
    transport->connected = true;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_device_transport_connect(locker_device_transport_t *transport,
                                                const char *path, uint32_t timeout_ms)
{
    uint64_t started_ms;
    uint64_t deadline_ms;

    if (transport == NULL || path == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (strlen(path) >= sizeof(transport->path)) {
        return LOCKER_STATUS_INVALID;
    }
    locker_device_transport_close(transport);
    memset(transport->path, 0, sizeof(transport->path));
    memcpy(transport->path, path, strlen(path));

    started_ms = transport_real_monotonic_ms();
    deadline_ms = started_ms + (uint64_t)timeout_ms;
    for (;;) {
        locker_status_t status = transport_try_connect(transport, deadline_ms);
        struct timespec pause;
        uint64_t now_ms;

        if (status == LOCKER_STATUS_OK) {
            return LOCKER_STATUS_OK;
        }
        if (status == LOCKER_STATUS_INVALID) {
            return LOCKER_STATUS_INVALID;
        }
        now_ms = transport_real_monotonic_ms();
        if (now_ms >= deadline_ms) {
            return LOCKER_STATUS_IO_ERROR;
        }
        pause.tv_sec = 0;
        pause.tv_nsec = (long)TRANSPORT_CONNECT_RETRY_MS * 1000000L;
        (void)nanosleep(&pause, NULL);
    }
}

locker_status_t locker_device_transport_write(locker_device_transport_t *transport,
                                              const uint8_t *data, size_t length)
{
    size_t written = 0u;
    uint64_t deadline_ms = transport_real_monotonic_ms() + 1000u;

    if (!locker_device_transport_connected(transport)) {
        return LOCKER_STATUS_NOT_READY;
    }
    if (data == NULL && length != 0u) {
        return LOCKER_STATUS_INVALID;
    }
    while (written < length) {
        struct pollfd writable;
        uint64_t now_ms = transport_real_monotonic_ms();
        int ready;
        ssize_t result;
        if (now_ms >= deadline_ms) {
            transport->connected = false;
            return LOCKER_STATUS_IO_ERROR;
        }
        writable.fd = transport->fd;
        writable.events = POLLOUT;
        writable.revents = 0;
        ready = poll(&writable, 1u, (int)(deadline_ms - now_ms));
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0 || (writable.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            transport->connected = false;
            return LOCKER_STATUS_IO_ERROR;
        }
        result = send(transport->fd, &data[written], length - written,
                      MSG_NOSIGNAL | MSG_DONTWAIT);
        if (result < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            transport->connected = false;
            return LOCKER_STATUS_IO_ERROR;
        }
        if (result == 0) {
            transport->connected = false;
            return LOCKER_STATUS_IO_ERROR;
        }
        written += (size_t)result;
    }
    return LOCKER_STATUS_OK;
}

int locker_device_transport_read(locker_device_transport_t *transport, uint8_t *buffer,
                                 size_t capacity, int timeout_ms)
{
    struct timeval timeout;
    ssize_t result;

    if (!locker_device_transport_connected(transport) || buffer == NULL || capacity == 0u) {
        return -1;
    }
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    (void)setsockopt(transport->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, (socklen_t)sizeof(timeout));
    result = recv(transport->fd, buffer, capacity, 0);
    if (result == 0) {
        transport->connected = false;
        return -1;
    }
    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;
        }
        transport->connected = false;
        return -1;
    }
    return (int)result;
}
