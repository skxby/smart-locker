#ifndef LOCKER_SIMULATOR_SIM_H
#define LOCKER_SIMULATOR_SIM_H

/*
 * S1-03 simulated locker device (docs/stages/stage-01-task-cards.md:58-65).
 *
 * This is the *device side* of UART v1: it consumes request frames and produces
 * reply frames plus asynchronous DEVICE_EVENT frames, keeping its own journal in
 * an independent SQLite file. It is a simulator, not firmware:
 *   - the unlock pulse is a counter/state transition, never a GPIO;
 *   - "restart" re-opens the process-local state and increments boot_id; it does
 *     NOT prove MCU/littlefs power-fail behaviour;
 *   - no HTTP surface exists; the only control entry is a private AF_UNIX socket
 *     created with mode 0600 by locker-device-sim.
 *
 * The engine is deliberately socket-free so unit tests can drive it in-process;
 * host/simulator/src/sim_main.c owns the sockets and the control protocol.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "locker_contracts.h"
#include "locker/device/codec.h"
#include "locker/device/wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Journal file path budget matches locker_device_config_t.path. */
#define LOCKER_SIM_PATH_SIZE 108u
#define LOCKER_SIM_DEVICE_UID_SIZE 12u
/* Frames queued per request; EXECUTE queues at most a few state events. */
#define LOCKER_SIM_EVENT_QUEUE 32u

typedef struct locker_sim locker_sim_t;

/* One-shot deterministic transaction faults used by the formal engine tests. */
typedef enum locker_sim_failure_stage {
    LOCKER_SIM_FAILURE_NONE = 0,
    LOCKER_SIM_FAILURE_BEGIN,
    LOCKER_SIM_FAILURE_INTENT,
    LOCKER_SIM_FAILURE_EVENT,
    LOCKER_SIM_FAILURE_COMMIT,
    LOCKER_SIM_FAILURE_RESULT
} locker_sim_failure_stage_t;

typedef struct locker_sim_config {
    /* Independent device journal. Never the business database. */
    char journal_path[LOCKER_SIM_PATH_SIZE];
    uint8_t device_uid[LOCKER_SIM_DEVICE_UID_SIZE];
    /* Injected clock: the daemon passes the real clock, tests pass a fake one. */
    locker_clock_port_t clock;
} locker_sim_config_t;

locker_status_t locker_sim_create(locker_sim_t **out, const locker_sim_config_t *config);
void locker_sim_destroy(locker_sim_t *sim);

/* Opens (or initialises) the journal and performs a boot: boot_id advances by
 * one, a BOOT event is appended and the previous session becomes invalid. */
locker_status_t locker_sim_open(locker_sim_t *sim);
void locker_sim_close(locker_sim_t *sim);
/* Simulated reboot of the process: same journal file, new boot_id, new session
 * required. Returns NOT_READY when the journal cannot be re-opened. */
locker_status_t locker_sim_restart(locker_sim_t *sim);

/* Consumes exactly one complete request frame. On OK, out_length is the reply
 * length, or 0 when the reply is deliberately suppressed (lost-ACK control).
 * A non-OK status means the frame was dropped and nothing was written. */
locker_status_t locker_sim_handle_frame(locker_sim_t *sim, const uint8_t *frame, size_t length,
                                        uint8_t *out, size_t out_size, size_t *out_length);

/* Drains one queued DEVICE_EVENT frame. Returns NOT_READY when the queue is
 * empty; the caller writes queued events after the reply for the same request. */
locker_status_t locker_sim_pop_event(locker_sim_t *sim, uint8_t *out, size_t out_size,
                                     size_t *out_length);

/* ---- private control entry (daemon exposes these over a 0600 socket) ---- */

locker_status_t locker_sim_control_set_door(locker_sim_t *sim, uint8_t door);
locker_status_t locker_sim_control_set_item(locker_sim_t *sim, uint8_t item);
locker_status_t locker_sim_control_card(locker_sim_t *sim, const uint8_t *uid, size_t uid_len);
locker_status_t locker_sim_control_reject_next(locker_sim_t *sim, bool enabled);
locker_status_t locker_sim_control_drop_ack(locker_sim_t *sim, bool enabled);
locker_status_t locker_sim_control_drop_execute_ack(locker_sim_t *sim, bool enabled);
locker_status_t locker_sim_control_fail_next(locker_sim_t *sim, locker_sim_failure_stage_t stage);
locker_status_t locker_sim_control_suppress_pulse(locker_sim_t *sim, bool enabled);
locker_status_t locker_sim_control_inject_gap(locker_sim_t *sim, uint64_t dropped);
locker_status_t locker_sim_control_fail_journal(locker_sim_t *sim, bool enabled);

/* ---- read-only observations for tests and the status control command ---- */

locker_uuid_t locker_sim_session_id(const locker_sim_t *sim);
locker_uuid_t locker_sim_journal_epoch(const locker_sim_t *sim);
uint64_t locker_sim_boot_id(const locker_sim_t *sim);
uint64_t locker_sim_action_watermark(const locker_sim_t *sim);
uint64_t locker_sim_dropped_events(const locker_sim_t *sim);
uint8_t locker_sim_device_mode(const locker_sim_t *sim);
uint8_t locker_sim_journal_state(const locker_sim_t *sim);
uint8_t locker_sim_door(const locker_sim_t *sim);
uint8_t locker_sim_item(const locker_sim_t *sim);
uint8_t locker_sim_command_state(const locker_sim_t *sim);
bool locker_sim_has_active_intent(const locker_sim_t *sim);
bool locker_sim_continuity_ok(const locker_sim_t *sim);
locker_wire_cursor_t locker_sim_first_available(const locker_sim_t *sim);
locker_wire_cursor_t locker_sim_last_committed(const locker_sim_t *sim);
locker_wire_cursor_t locker_sim_ack_persisted(const locker_sim_t *sim);
/* Journal row count, used to prove a failed commit wrote nothing. */
uint64_t locker_sim_event_count(const locker_sim_t *sim);
/* Journaled simulated pulse starts; never a GPIO measurement. */
uint64_t locker_sim_pulse_count(const locker_sim_t *sim);

#ifdef __cplusplus
}
#endif

#endif /* LOCKER_SIMULATOR_SIM_H */
