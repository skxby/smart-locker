/* Compile check of handoff.md section 3.2 fake clock port example. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "locker/common/clock.h"
#include "locker/common/log.h"

typedef struct fake_clock {
    uint64_t monotonic_ms;
    uint64_t wall_ms;
    bool wall_available;
    uint32_t next_uuid_seed;
} fake_clock_t;

static uint64_t fake_monotonic(void *user) {
    return ((fake_clock_t *)user)->monotonic_ms;
}

static bool fake_wall(void *user, uint64_t *out_wall_ms) {
    const fake_clock_t *clock = (const fake_clock_t *)user;
    if (!clock->wall_available) { return false; }
    *out_wall_ms = clock->wall_ms;
    return true;
}

static locker_status_t fake_new_uuid(void *user, locker_uuid_t *out_uuid) {
    fake_clock_t *clock = (fake_clock_t *)user;
    memset(out_uuid, 0, sizeof(*out_uuid));
    out_uuid->bytes[15] = (uint8_t)++clock->next_uuid_seed;
    return LOCKER_STATUS_OK;
}

static void sink(void *user, locker_log_level_t level, const char *line) {
    (void)user; (void)level; (void)line;
}

int main(void) {
    fake_clock_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.monotonic_ms = 123456u;
    fake.wall_ms = 1700000000000u;
    fake.wall_available = true;

    locker_clock_port_t port = {0};
    port.user         = &fake;
    port.monotonic_ms = fake_monotonic;
    port.wall_utc_ms  = fake_wall;
    port.new_uuid     = fake_new_uuid;

    locker_logger_t logger;
    locker_status_t st = locker_logger_init(&logger, sink, NULL, "core.queue",
                                            LOCKER_BACKEND_SIMULATOR,
                                            LOCKER_LOG_LEVEL_INFO, &port);
    if (st != LOCKER_STATUS_OK) { return 1; }

    const locker_log_field_t fields[] = {
        { "queue", "core_in" },
        { "size", "64" },
    };
    st = locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "queue_full", fields, 2);
    locker_logger_deinit(&logger);

    uint64_t mono = 0;
    locker_status_t ps = locker_clock_port_monotonic_ms(&port, &mono);
    if (st != LOCKER_STATUS_OK || ps != LOCKER_STATUS_OK || mono != 123456u) { return 2; }
    return 0;
}
