#ifndef LOCKER_COMMON_BOUNDED_QUEUE_H
#define LOCKER_COMMON_BOUNDED_QUEUE_H

/*
 * S1-01 host infrastructure: fixed-capacity FIFO for cross-thread hand-off.
 *
 * Ownership rules (docs/architecture/module-ownership.md:50):
 *   - elements are copied into caller-provided storage at init time, so a queued
 *     message never depends on the producer's stack, CivetWeb buffer or callback
 *     argument lifetime;
 *   - no allocation happens after init and no blocking pop exists: the Core loop
 *     owns draining, callers only try_push/try_pop;
 *   - a full queue fails explicitly with LOCKER_STATUS_NO_SPACE (the contract's
 *     QUEUE_FULL condition), it never silently drops the element.
 *
 * Thread safety: try_push/try_pop/peek/get_stats/size are safe to call
 * concurrently from multiple threads. init and destroy are NOT: they are
 * owner-exclusive lifecycle calls. A destroy must not overlap any other call on
 * the same queue - join the producer/consumer threads first, exactly as with
 * pthread_mutex_destroy(). Concurrent destroy vs push/pop is a data race, not a
 * recoverable error. The queue struct itself is not movable once initialised. A
 * queue must be zero initialised before the first locker_bounded_queue_init()
 * call.
 */

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "locker_contracts.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct locker_bounded_queue_stats {
    size_t size;
    size_t capacity;
    uint64_t pushed_total;
    uint64_t popped_total;
    uint64_t rejected_total;
} locker_bounded_queue_stats_t;

typedef struct locker_bounded_queue {
    uint8_t *storage;
    size_t capacity;
    size_t element_size;
    size_t head;
    size_t count;
    uint64_t pushed_total;
    uint64_t popped_total;
    uint64_t rejected_total;
    pthread_mutex_t mutex;
    bool initialized;
} locker_bounded_queue_t;

/* storage must hold capacity * element_size bytes and outlive the queue. */
locker_status_t locker_bounded_queue_init(locker_bounded_queue_t *queue, void *storage,
                                          size_t capacity, size_t element_size);
void locker_bounded_queue_destroy(locker_bounded_queue_t *queue);

/* Copy one element in. Returns OK or NO_SPACE; INVALID for bad arguments. */
locker_status_t locker_bounded_queue_try_push(locker_bounded_queue_t *queue, const void *element);
/* Copy one element out. Returns OK or NOT_READY when empty. */
locker_status_t locker_bounded_queue_try_pop(locker_bounded_queue_t *queue, void *out_element);
/* Copy the oldest element without removing it. */
locker_status_t locker_bounded_queue_peek(locker_bounded_queue_t *queue, void *out_element);

locker_status_t locker_bounded_queue_get_stats(locker_bounded_queue_t *queue,
                                               locker_bounded_queue_stats_t *out_stats);
size_t locker_bounded_queue_size(locker_bounded_queue_t *queue);
size_t locker_bounded_queue_capacity(const locker_bounded_queue_t *queue);
size_t locker_bounded_queue_element_size(const locker_bounded_queue_t *queue);
bool locker_bounded_queue_is_initialized(const locker_bounded_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* LOCKER_COMMON_BOUNDED_QUEUE_H */
