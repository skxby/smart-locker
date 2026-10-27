#ifndef LOCKER_COMMON_CLOCK_H
#define LOCKER_COMMON_CLOCK_H

/*
 * S1-01 host infrastructure: monotonic time and clock injection.
 *
 * Production code must use locker_clock_port_real(). Test doubles are ordinary
 * locker_clock_port_t values built inside the test binary; the library exposes no
 * setter, so a test clock can never be switched on through a public API.
 * All timeouts are measured on the monotonic clock (docs/architecture/
 * internal-interfaces.md:15); wall time is only a correlation anchor.
 */

#include <stdbool.h>
#include <stdint.h>

#include "locker_contracts.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct locker_deadline {
    uint64_t expires_ms;
    bool valid;
} locker_deadline_t;

/*
 * Real monotonic milliseconds; returns 0 if the platform call fails.
 *
 * The monotonic port callback is uint64_t(void *): the contract gives it no error
 * channel, so a platform failure is reported as 0 and is indistinguishable from a
 * genuine 0 reading (CLOCK_MONOTONIC is uptime, which is legitimately near 0 just
 * after boot). locker_clock_port_monotonic_ms() therefore returns OK with 0 in
 * that case. Callers that must not fail open on an unavailable clock have to
 * treat "monotonic did not advance across a known interval" as an anomaly;
 * adding a status channel to the port is a contract change owned by the contract
 * owner (see docs/evidence/stage-01/S1-01/handoff.md).
 */
uint64_t locker_clock_real_monotonic_ms(void);
/* Real UTC milliseconds since the Unix epoch; false on failure (out untouched). */
bool locker_clock_real_wall_utc_ms(uint64_t *out_wall_ms);
/* Cryptographically random RFC 4122 version 4 UUID; IO_ERROR if no entropy source. */
locker_status_t locker_clock_real_new_uuid(locker_uuid_t *out_uuid);

/* Production clock port: real monotonic/wall clock and real UUID source. */
locker_clock_port_t locker_clock_port_real(void);
bool locker_clock_port_is_valid(const locker_clock_port_t *port);

locker_status_t locker_clock_port_monotonic_ms(const locker_clock_port_t *port, uint64_t *out_monotonic_ms);
locker_status_t locker_clock_port_wall_utc_ms(const locker_clock_port_t *port, uint64_t *out_wall_ms);
locker_status_t locker_clock_port_new_uuid(const locker_clock_port_t *port, locker_uuid_t *out_uuid);

/* Saturating deadline arithmetic: now_ms + timeout_ms never wraps. */
locker_deadline_t locker_deadline_after(uint64_t now_ms, uint64_t timeout_ms);
/* An invalid deadline counts as expired (fail closed). */
bool locker_deadline_expired(const locker_deadline_t *deadline, uint64_t now_ms);
uint64_t locker_deadline_remaining_ms(const locker_deadline_t *deadline, uint64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* LOCKER_COMMON_CLOCK_H */
