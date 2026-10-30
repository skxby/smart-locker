#ifndef LOCKER_DEVICE_TRANSPORT_H
#define LOCKER_DEVICE_TRANSPORT_H

/*
 * S1-03 byte transport for the device adapter; S2-06 adds the real UART.
 *
 * Two backends share one byte interface:
 *   - path under /dev/  -> a real character device (the board's UART), opened
 *     115200 8N1 raw with hardware flow control disabled;
 *   - any other path    -> AF_UNIX SOCK_STREAM to locker-device-sim, which
 *     carries the same UART v1 byte stream for host-only tests.
 * locker_device_open() no longer refuses config->simulated == false; the caller
 * chooses the backend by the path it supplies.
 *
 * Threading: one reader owns read(), one writer owns write(). Public port
 * calls only admit copied frames to a bounded FIFO. shutdown() may run during
 * either IO call to wake it; close() follows both joins. The fd is immutable
 * between connect and those joins; connected is atomic.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include "locker_contracts.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct locker_device_transport {
    int fd;
    char path[108];
    bool is_tty;
    atomic_bool connected;
} locker_device_transport_t;

void locker_device_transport_init(locker_device_transport_t *transport);
/* Connects to path (AF_UNIX SOCK_STREAM). Retries for at most timeout_ms of real
 * time, because socket setup is an OS operation and not part of the injected
 * protocol clock. */
locker_status_t locker_device_transport_connect(locker_device_transport_t *transport,
                                                const char *path, uint32_t timeout_ms);
/* Half-closes the socket so a blocked read() returns instead of waiting for the
 * receive timeout; safe to call from another thread than the reader. */
void locker_device_transport_shutdown(locker_device_transport_t *transport);
void locker_device_transport_close(locker_device_transport_t *transport);
bool locker_device_transport_connected(const locker_device_transport_t *transport);

/* Writes all bytes within 1000 ms of real monotonic time, or fails. The
 * sole writer owns this call; concurrent shutdown wakes it, and close follows
 * both IO-thread joins. Never call this function from the Core loop. */
locker_status_t locker_device_transport_write(locker_device_transport_t *transport,
                                              const uint8_t *data, size_t length);
/* Blocking read with a receive timeout. Returns the byte count, 0 on timeout and
 * -1 on a closed or broken connection. */
int locker_device_transport_read(locker_device_transport_t *transport, uint8_t *buffer,
                                 size_t capacity, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* LOCKER_DEVICE_TRANSPORT_H */
