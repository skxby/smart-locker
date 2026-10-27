#include "locker/common/bounded_queue.h"

#include <string.h>

locker_status_t locker_bounded_queue_init(locker_bounded_queue_t *queue, void *storage,
                                          size_t capacity, size_t element_size) {
    if (queue == NULL || storage == NULL || capacity == 0 || element_size == 0) {
        return LOCKER_STATUS_INVALID;
    }
    if (queue->initialized) {
        return LOCKER_STATUS_CONFLICT;
    }
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        return LOCKER_STATUS_IO_ERROR;
    }
    queue->storage = (uint8_t *)storage;
    queue->capacity = capacity;
    queue->element_size = element_size;
    queue->head = 0;
    queue->count = 0;
    queue->pushed_total = 0;
    queue->popped_total = 0;
    queue->rejected_total = 0;
    queue->initialized = true;
    return LOCKER_STATUS_OK;
}

void locker_bounded_queue_destroy(locker_bounded_queue_t *queue) {
    if (queue == NULL || !queue->initialized) {
        return;
    }
    pthread_mutex_destroy(&queue->mutex);
    queue->storage = NULL;
    queue->capacity = 0;
    queue->element_size = 0;
    queue->head = 0;
    queue->count = 0;
    queue->initialized = false;
}

static uint8_t *slot_at(const locker_bounded_queue_t *queue, size_t index) {
    return queue->storage + (index * queue->element_size);
}

locker_status_t locker_bounded_queue_try_push(locker_bounded_queue_t *queue, const void *element) {
    if (queue == NULL || element == NULL || !queue->initialized) {
        return LOCKER_STATUS_INVALID;
    }
    locker_status_t status = LOCKER_STATUS_OK;
    pthread_mutex_lock(&queue->mutex);
    if (queue->count >= queue->capacity) {
        queue->rejected_total += 1u;
        status = LOCKER_STATUS_NO_SPACE;
    } else {
        size_t tail = (queue->head + queue->count) % queue->capacity;
        memcpy(slot_at(queue, tail), element, queue->element_size);
        queue->count += 1u;
        queue->pushed_total += 1u;
    }
    pthread_mutex_unlock(&queue->mutex);
    return status;
}

locker_status_t locker_bounded_queue_try_pop(locker_bounded_queue_t *queue, void *out_element) {
    if (queue == NULL || out_element == NULL || !queue->initialized) {
        return LOCKER_STATUS_INVALID;
    }
    locker_status_t status = LOCKER_STATUS_OK;
    pthread_mutex_lock(&queue->mutex);
    if (queue->count == 0) {
        status = LOCKER_STATUS_NOT_READY;
    } else {
        memcpy(out_element, slot_at(queue, queue->head), queue->element_size);
        queue->head = (queue->head + 1u) % queue->capacity;
        queue->count -= 1u;
        queue->popped_total += 1u;
    }
    pthread_mutex_unlock(&queue->mutex);
    return status;
}

locker_status_t locker_bounded_queue_peek(locker_bounded_queue_t *queue, void *out_element) {
    if (queue == NULL || out_element == NULL || !queue->initialized) {
        return LOCKER_STATUS_INVALID;
    }
    locker_status_t status = LOCKER_STATUS_OK;
    pthread_mutex_lock(&queue->mutex);
    if (queue->count == 0) {
        status = LOCKER_STATUS_NOT_READY;
    } else {
        memcpy(out_element, slot_at(queue, queue->head), queue->element_size);
    }
    pthread_mutex_unlock(&queue->mutex);
    return status;
}

locker_status_t locker_bounded_queue_get_stats(locker_bounded_queue_t *queue,
                                               locker_bounded_queue_stats_t *out_stats) {
    if (queue == NULL || out_stats == NULL || !queue->initialized) {
        return LOCKER_STATUS_INVALID;
    }
    pthread_mutex_lock(&queue->mutex);
    out_stats->size = queue->count;
    out_stats->capacity = queue->capacity;
    out_stats->pushed_total = queue->pushed_total;
    out_stats->popped_total = queue->popped_total;
    out_stats->rejected_total = queue->rejected_total;
    pthread_mutex_unlock(&queue->mutex);
    return LOCKER_STATUS_OK;
}

size_t locker_bounded_queue_size(locker_bounded_queue_t *queue) {
    if (queue == NULL || !queue->initialized) {
        return 0;
    }
    pthread_mutex_lock(&queue->mutex);
    size_t size = queue->count;
    pthread_mutex_unlock(&queue->mutex);
    return size;
}

size_t locker_bounded_queue_capacity(const locker_bounded_queue_t *queue) {
    return (queue == NULL) ? 0 : queue->capacity;
}

size_t locker_bounded_queue_element_size(const locker_bounded_queue_t *queue) {
    return (queue == NULL) ? 0 : queue->element_size;
}

bool locker_bounded_queue_is_initialized(const locker_bounded_queue_t *queue) {
    return queue != NULL && queue->initialized;
}
