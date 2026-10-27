/* nanosleep() needs POSIX.1-1993; CMAKE_C_EXTENSIONS is OFF for portability. */
#define _POSIX_C_SOURCE 199309L

#include "test_support.h"

#include <time.h>

#include "locker/common/clock.h"
#include "locker/common/uuid.h"

/*
 * Covers docs/stages/stage-01-task-cards.md:123 - timeouts are tested with an
 * injected monotonic clock, and at least one real-clock smoke test stays in the
 * suite.
 *
 * The fake clock below is built entirely inside this test binary. The library
 * exports only the real clock factory, so no public API can switch production
 * code onto a test clock.
 */

typedef struct fake_clock {
    uint64_t monotonic_ms;
    uint64_t wall_ms;
    bool wall_available;
    uint32_t next_uuid_seed;
    uint32_t uuid_calls;
} fake_clock_t;

static uint64_t fake_monotonic(void *user) {
    fake_clock_t *clock = (fake_clock_t *)user;
    return clock->monotonic_ms;
}

static bool fake_wall(void *user, uint64_t *out_wall_ms) {
    fake_clock_t *clock = (fake_clock_t *)user;
    if (!clock->wall_available) {
        return false;
    }
    *out_wall_ms = clock->wall_ms;
    return true;
}

static locker_status_t fake_new_uuid(void *user, locker_uuid_t *out_uuid) {
    fake_clock_t *clock = (fake_clock_t *)user;
    clock->uuid_calls += 1u;
    for (size_t i = 0; i < sizeof(out_uuid->bytes); ++i) {
        out_uuid->bytes[i] = (uint8_t)((clock->next_uuid_seed + i) & 0xFFu);
    }
    out_uuid->bytes[6] = (uint8_t)((out_uuid->bytes[6] & 0x0Fu) | 0x40u);
    out_uuid->bytes[8] = (uint8_t)((out_uuid->bytes[8] & 0x3Fu) | 0x80u);
    clock->next_uuid_seed += 16u;
    return LOCKER_STATUS_OK;
}

static locker_clock_port_t make_port(fake_clock_t *clock) {
    locker_clock_port_t port;
    port.user = clock;
    port.monotonic_ms = fake_monotonic;
    port.wall_utc_ms = fake_wall;
    port.new_uuid = fake_new_uuid;
    return port;
}

static void test_injected_port_is_used(void) {
    fake_clock_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.monotonic_ms = 123456u;
    fake.wall_ms = 1700000000000u;
    fake.wall_available = true;
    fake.next_uuid_seed = 0x10u;

    locker_clock_port_t port = make_port(&fake);
    TEST_CHECK(locker_clock_port_is_valid(&port));

    uint64_t monotonic = 0;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_clock_port_monotonic_ms(&port, &monotonic));
    TEST_CHECK_U64(123456u, monotonic);

    uint64_t wall = 0;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_clock_port_wall_utc_ms(&port, &wall));
    TEST_CHECK_U64(1700000000000u, wall);

    locker_uuid_t uuid;
    memset(&uuid, 0, sizeof(uuid));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_clock_port_new_uuid(&port, &uuid));
    TEST_CHECK(locker_uuid_is_rfc4122_v4(&uuid));
    TEST_CHECK_U64(1u, fake.uuid_calls);

    /* Moving the fake clock is only possible through the struct this test owns. */
    fake.monotonic_ms = 200000u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_clock_port_monotonic_ms(&port, &monotonic));
    TEST_CHECK_U64(200000u, monotonic);
}

static void test_wall_clock_failure_is_reported(void) {
    fake_clock_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.wall_available = false;
    locker_clock_port_t port = make_port(&fake);

    uint64_t wall = 0;
    TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR, locker_clock_port_wall_utc_ms(&port, &wall));
}

static void test_invalid_port_rejected(void) {
    locker_clock_port_t port;
    uint64_t value = 0;
    locker_uuid_t uuid;

    memset(&port, 0, sizeof(port));
    TEST_CHECK(!locker_clock_port_is_valid(&port));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_clock_port_monotonic_ms(&port, &value));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_clock_port_wall_utc_ms(&port, &value));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_clock_port_new_uuid(&port, &uuid));

    port = locker_clock_port_real();
    TEST_CHECK(locker_clock_port_is_valid(&port));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_clock_port_monotonic_ms(NULL, &value));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_clock_port_monotonic_ms(&port, NULL));
}

static void test_deadline_arithmetic(void) {
    locker_deadline_t deadline = locker_deadline_after(1000u, 2000u);
    TEST_CHECK(deadline.valid);
    TEST_CHECK_U64(3000u, deadline.expires_ms);
    TEST_CHECK(!locker_deadline_expired(&deadline, 1000u));
    TEST_CHECK(!locker_deadline_expired(&deadline, 2999u));
    TEST_CHECK(locker_deadline_expired(&deadline, 3000u));
    TEST_CHECK(locker_deadline_expired(&deadline, 4000u));
    TEST_CHECK_U64(2000u, locker_deadline_remaining_ms(&deadline, 1000u));
    TEST_CHECK_U64(0u, locker_deadline_remaining_ms(&deadline, 3000u));
    TEST_CHECK_U64(0u, locker_deadline_remaining_ms(&deadline, 9000u));

    /* Zero timeout expires immediately at the same instant. */
    locker_deadline_t immediate = locker_deadline_after(500u, 0u);
    TEST_CHECK(locker_deadline_expired(&immediate, 500u));

    /* Saturating add must not wrap at the u64 boundary. */
    locker_deadline_t saturated = locker_deadline_after(UINT64_MAX - 10u, 100u);
    TEST_CHECK(saturated.valid);
    TEST_CHECK_U64(UINT64_MAX, saturated.expires_ms);
    TEST_CHECK(!locker_deadline_expired(&saturated, UINT64_MAX - 1u));

    /* A default (invalid) deadline fails closed. */
    locker_deadline_t invalid;
    memset(&invalid, 0, sizeof(invalid));
    TEST_CHECK(!invalid.valid);
    TEST_CHECK(locker_deadline_expired(&invalid, 0u));
    TEST_CHECK(locker_deadline_expired(NULL, 0u));
    TEST_CHECK_U64(0u, locker_deadline_remaining_ms(&invalid, 0u));
}

/* Real-clock smoke test: docs/stages/stage-01-task-cards.md:123 requires at
   least one genuine clock reading in addition to the injected ones. */
static void test_real_clock_smoke(void) {
    locker_clock_port_t port = locker_clock_port_real();
    TEST_CHECK(locker_clock_port_is_valid(&port));

    uint64_t first = locker_clock_real_monotonic_ms();
    TEST_CHECK(first > 0u);
    uint64_t second = locker_clock_real_monotonic_ms();
    TEST_CHECK(second >= first);

    /* Sleeping 20 ms must be observable on the monotonic clock. */
    struct timespec delay;
    delay.tv_sec = 0;
    delay.tv_nsec = 20 * 1000 * 1000L;
    nanosleep(&delay, NULL);
    uint64_t third = locker_clock_real_monotonic_ms();
    TEST_CHECK(third > first);
    TEST_CHECK(third - first >= 15u);

    uint64_t wall = 0;
    TEST_CHECK(locker_clock_real_wall_utc_ms(&wall));
    /* Sanity: after 2020-01-01 and before 2100-01-01. */
    TEST_CHECK(wall > 1577836800000ull);
    TEST_CHECK(wall < 4102444800000ull);

    locker_uuid_t uuid;
    memset(&uuid, 0, sizeof(uuid));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_clock_real_new_uuid(&uuid));
    TEST_CHECK(!locker_uuid_is_nil(&uuid));
    TEST_CHECK(locker_uuid_is_rfc4122_v4(&uuid));

    locker_uuid_t other;
    memset(&other, 0, sizeof(other));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_clock_real_new_uuid(&other));
    TEST_CHECK(!locker_uuid_equal(&uuid, &other));

    char text[37];
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_uuid_format(&uuid, text, sizeof(text)));
    locker_uuid_t reparsed;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_uuid_parse(text, strlen(text), &reparsed));
    TEST_CHECK(locker_uuid_equal(&uuid, &reparsed));

    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_clock_real_new_uuid(NULL));
    TEST_CHECK(!locker_clock_real_wall_utc_ms(NULL));
}

int main(void) {
    test_injected_port_is_used();
    test_wall_clock_failure_is_reported();
    test_invalid_port_rejected();
    test_deadline_arithmetic();
    test_real_clock_smoke();
    TEST_REPORT("common.clock");
}
