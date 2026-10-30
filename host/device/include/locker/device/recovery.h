#ifndef LOCKER_DEVICE_RECOVERY_H
#define LOCKER_DEVICE_RECOVERY_H

/*
 * Host-side link supervision, not part of the device wire contract.
 *
 * A pulled UART wire does not surface as EIO/POLLHUP on a tty, so the adapter
 * cannot be told by the kernel that the board is gone. The only usable signal is
 * a run of requests that never get an answer: three consecutive expiries mark the
 * link lost, and one matched reply clears the mark.
 *
 * The runtime polls this flag and performs close + backoff + open, which is the
 * same path a service restart would take. Reopening re-sends HELLO, so a recovered
 * cabinet re-establishes its session before any action can be admitted.
 */

#include <stdbool.h>

#include "locker_contracts.h"

#ifdef __cplusplus
extern "C" {
#endif

bool locker_device_link_lost(const locker_device_t *device);

/*
 * open() only queues HELLO; it returns before the answer arrives. A reopen at the
 * moment the wire is still out therefore leaves the adapter open with no session,
 * and every request is refused with NOT_READY forever because hello_complete stays
 * false (port.c: "!device->opened || !device->hello_complete"). The supervisor must
 * treat "opened but no session" as a failed recovery and retry, otherwise a single
 * unlucky reopen permanently silences a cabinet that is physically fine.
 */
bool locker_device_session_ready(locker_device_t *device);

/* Bench diagnosis only (LOCKER_DEVICE_TRACE=1). The supervisor prints this to
 * show why recovery did or did not fire: a silent link is only detectable while
 * something still expires pending slots, and that is invisible from outside. */
typedef struct locker_device_diagnosis {
    bool opened;
    bool hello_complete;
    bool link_lost;
    bool writer_alive;
    bool reader_alive;
    unsigned expired_streak;
    unsigned pending_used;
    unsigned transmit_used;
} locker_device_diagnosis_t;

bool locker_device_diagnose(locker_device_t *device, locker_device_diagnosis_t *out);

#ifdef __cplusplus
}
#endif

#endif /* LOCKER_DEVICE_RECOVERY_H */
