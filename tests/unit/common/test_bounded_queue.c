#include "test_support.h"

#include <string.h>

#include "locker/common/bounded_queue.h"

/*
 * Covers docs/stages/stage-01-task-cards.md:39-46 acceptance items:
 *   - a full queue fails explicitly (never silently drops);
 *   - a queued element outlives the producer's stack frame.
 */

typedef struct sample_message {
    uint64_t sequence;
    uint32_t kind;
    uint8_t payload[40];
    char text[24];
} sample_message_t;

#define SAMPLE_CAPACITY 4u

static void fill_message(sample_message_t *message, uint64_t sequence) {
    memset(message, 0, sizeof(*message));
    message->sequence = sequence;
    message->kind = (uint32_t)(sequence % 7u);
    for (size_t i = 0; i < sizeof(message->payload); ++i) {
        message->payload[i] = (uint8_t)((sequence + i) & 0xFFu);
    }
    snprintf(message->text, sizeof(message->text), "message-%llu", (unsigned long long)sequence);
}

static bool message_matches(const sample_message_t *message, uint64_t sequence) {
    sample_message_t expected;
    fill_message(&expected, sequence);
    return memcmp(message, &expected, sizeof(expected)) == 0;
}

static void test_init_rejects_invalid_arguments(void) {
    locker_bounded_queue_t queue;
    sample_message_t storage[SAMPLE_CAPACITY];
    memset(&queue, 0, sizeof(queue));

    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_bounded_queue_init(NULL, storage, SAMPLE_CAPACITY, sizeof(sample_message_t)));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_bounded_queue_init(&queue, NULL, SAMPLE_CAPACITY, sizeof(sample_message_t)));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_bounded_queue_init(&queue, storage, 0u, sizeof(sample_message_t)));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_bounded_queue_init(&queue, storage, SAMPLE_CAPACITY, 0u));
    TEST_CHECK(!locker_bounded_queue_is_initialized(&queue));
    TEST_CHECK_U64(0u, locker_bounded_queue_size(&queue));
}

static void test_init_and_accessors(void) {
    locker_bounded_queue_t queue;
    sample_message_t storage[SAMPLE_CAPACITY];
    memset(&queue, 0, sizeof(queue));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_bounded_queue_init(&queue, storage, SAMPLE_CAPACITY, sizeof(sample_message_t)));
    TEST_CHECK(locker_bounded_queue_is_initialized(&queue));
    TEST_CHECK_U64(SAMPLE_CAPACITY, locker_bounded_queue_capacity(&queue));
    TEST_CHECK_U64(sizeof(sample_message_t), locker_bounded_queue_element_size(&queue));
    TEST_CHECK_U64(0u, locker_bounded_queue_size(&queue));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      locker_bounded_queue_init(&queue, storage, SAMPLE_CAPACITY, sizeof(sample_message_t)));

    locker_bounded_queue_destroy(&queue);
    TEST_CHECK(!locker_bounded_queue_is_initialized(&queue));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_bounded_queue_try_push(&queue, storage));
}

static void test_fifo_order_and_empty_pop(void) {
    locker_bounded_queue_t queue;
    sample_message_t storage[SAMPLE_CAPACITY];
    sample_message_t message;
    memset(&queue, 0, sizeof(queue));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_bounded_queue_init(&queue, storage, SAMPLE_CAPACITY, sizeof(sample_message_t)));

    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_bounded_queue_try_pop(&queue, &message));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_bounded_queue_peek(&queue, &message));

    for (uint64_t i = 0; i < 3u; ++i) {
        fill_message(&message, i + 1u);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_bounded_queue_try_push(&queue, &message));
    }
    TEST_CHECK_U64(3u, locker_bounded_queue_size(&queue));

    /* Peek must not remove the oldest element. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_bounded_queue_peek(&queue, &message));
    TEST_CHECK(message_matches(&message, 1u));
    TEST_CHECK_U64(3u, locker_bounded_queue_size(&queue));

    for (uint64_t i = 0; i < 3u; ++i) {
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_bounded_queue_try_pop(&queue, &message));
        TEST_CHECK(message_matches(&message, i + 1u));
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_bounded_queue_try_pop(&queue, &message));

    locker_bounded_queue_destroy(&queue);
}

static void test_full_queue_fails_explicitly(void) {
    locker_bounded_queue_t queue;
    sample_message_t storage[SAMPLE_CAPACITY];
    sample_message_t message;
    locker_bounded_queue_stats_t stats;
    memset(&queue, 0, sizeof(queue));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_bounded_queue_init(&queue, storage, SAMPLE_CAPACITY, sizeof(sample_message_t)));

    for (uint64_t i = 0; i < SAMPLE_CAPACITY; ++i) {
        fill_message(&message, i + 1u);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_bounded_queue_try_push(&queue, &message));
    }
    TEST_CHECK_U64(SAMPLE_CAPACITY, locker_bounded_queue_size(&queue));

    fill_message(&message, 99u);
    TEST_CHECK_STATUS(LOCKER_STATUS_NO_SPACE, locker_bounded_queue_try_push(&queue, &message));
    TEST_CHECK_U64(SAMPLE_CAPACITY, locker_bounded_queue_size(&queue));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_bounded_queue_get_stats(&queue, &stats));
    TEST_CHECK_U64(SAMPLE_CAPACITY, stats.size);
    TEST_CHECK_U64(SAMPLE_CAPACITY, stats.capacity);
    TEST_CHECK_U64(SAMPLE_CAPACITY, stats.pushed_total);
    TEST_CHECK_U64(0u, stats.popped_total);
    TEST_CHECK_U64(1u, stats.rejected_total);

    /* The rejected message must not have replaced the oldest queued element. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_bounded_queue_try_pop(&queue, &message));
    TEST_CHECK(message_matches(&message, 1u));

    locker_bounded_queue_destroy(&queue);
}

/* Pushes a message that only ever exists inside this call's stack frame. */
static void push_from_temporary_frame(locker_bounded_queue_t *queue, uint64_t sequence,
                                      locker_status_t *out_status) {
    sample_message_t message;
    fill_message(&message, sequence);
    *out_status = locker_bounded_queue_try_push(queue, &message);
}

/* Reuses the stack region the producer used, to expose any retained pointer. */
static void clobber_stack(void) {
    volatile uint8_t junk[1024];
    for (size_t i = 0; i < sizeof(junk); ++i) {
        junk[i] = 0xA5u;
    }
}

static void test_element_lifetime_independent_of_producer_stack(void) {
    locker_bounded_queue_t queue;
    sample_message_t storage[SAMPLE_CAPACITY];
    sample_message_t message;
    locker_status_t status = LOCKER_STATUS_INVALID;
    memset(&queue, 0, sizeof(queue));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_bounded_queue_init(&queue, storage, SAMPLE_CAPACITY, sizeof(sample_message_t)));

    for (uint64_t i = 0; i < SAMPLE_CAPACITY; ++i) {
        push_from_temporary_frame(&queue, i + 1u, &status);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, status);
    }
    /* The producer frames are gone and their stack region is overwritten; the
     queue must still hand back complete, unmodified copies. */
    clobber_stack();

    for (uint64_t i = 0; i < SAMPLE_CAPACITY; ++i) {
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_bounded_queue_try_pop(&queue, &message));
        TEST_CHECK(message_matches(&message, i + 1u));
    }

    locker_bounded_queue_destroy(&queue);
}

static void test_wrap_around_keeps_order(void) {
    locker_bounded_queue_t queue;
    sample_message_t storage[SAMPLE_CAPACITY];
    sample_message_t message;
    memset(&queue, 0, sizeof(queue));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_bounded_queue_init(&queue, storage, SAMPLE_CAPACITY, sizeof(sample_message_t)));

    uint64_t next_sequence = 1u;
    for (int round = 0; round < 6; ++round) {
        for (uint64_t i = 0; i < SAMPLE_CAPACITY / 2u; ++i) {
            fill_message(&message, next_sequence);
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_bounded_queue_try_push(&queue, &message));
            next_sequence += 1u;
        }
        for (uint64_t i = 0; i < SAMPLE_CAPACITY / 2u; ++i) {
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_bounded_queue_try_pop(&queue, &message));
            TEST_CHECK_U64(next_sequence - (SAMPLE_CAPACITY / 2u) + i, message.sequence);
        }
    }

    locker_bounded_queue_destroy(&queue);
}

int main(void) {
    test_init_rejects_invalid_arguments();
    test_init_and_accessors();
    test_fifo_order_and_empty_pop();
    test_full_queue_fails_explicitly();
    test_element_lifetime_independent_of_producer_stack();
    test_wrap_around_keeps_order();
    TEST_REPORT("common.bounded_queue");
}
