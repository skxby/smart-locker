#ifndef MCU_JOURNAL_PRIVATE_H
#define MCU_JOURNAL_PRIVATE_H
#include "locker_contracts.h"
#include "lfs.h"
#define MCU_JOURNAL_MAX_EVENTS 64u
#define MCU_JOURNAL_QUEUE_CAPACITY 8u
#define MCU_JOURNAL_RESERVE_EVENTS 8u
/* Platform callbacks are permanent. lock/unlock protect only the inbox and
 * completion flags, never Flash IO. wait yields until Storage services FIFO.
 * is_owner must identify the sole Storage task, including during init. */
typedef struct {
    const struct lfs_config *fs;
    void *user;
    void (*lock)(void *);
    void (*unlock)(void *);
    void (*wait)(void *);
    bool (*is_owner)(void *);
} mcu_journal_port_t;
locker_status_t mcu_journal_attach(const mcu_journal_port_t *);
/* Restricted onsite provisioning, never called on mount failure or via UART.
 * Caller must back up/verify the device is disposable before invoking format. */
locker_status_t mcu_journal_provision(const mcu_journal_config_t *, const locker_uuid_t *, const char *device_id);
locker_status_t mcu_journal_process_one(mcu_journal_t *);
locker_status_t mcu_journal_shutdown(mcu_journal_t *);
#endif
