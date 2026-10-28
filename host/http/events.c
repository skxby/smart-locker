#include "events.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>

struct locker_publisher {
    pthread_mutex_t mutex;
    uint64_t sequence;
    atomic_uint_fast64_t losses;
    locker_resource_change_t changes[LOCKER_WS_QUEUE_CAPACITY];
};

locker_status_t locker_events_create(locker_publisher_t **out)
{
    if (out == NULL) return LOCKER_STATUS_INVALID;
    *out = NULL;
    locker_publisher_t *p = calloc(1, sizeof(*p));
    if (p == NULL) return LOCKER_STATUS_NO_SPACE;
    if (pthread_mutex_init(&p->mutex, NULL) != 0) {
        free(p);
        return LOCKER_STATUS_IO_ERROR;
    }
    atomic_init(&p->losses, 0);
    *out = p;
    return LOCKER_STATUS_OK;
}

void locker_events_destroy(locker_publisher_t *p)
{
    if (p == NULL) return;
    pthread_mutex_destroy(&p->mutex);
    free(p);
}

locker_status_t locker_events_try_publish(locker_publisher_t *p,
                                         const locker_resource_change_t *change)
{
    if (p == NULL || change == NULL ||
        memchr(change->id, 0, sizeof(change->id)) == NULL ||
        memchr(change->resource, 0, sizeof(change->resource)) == NULL)
        return LOCKER_STATUS_INVALID;
    if (pthread_mutex_trylock(&p->mutex) != 0) {
        /* Each subscriber tracks this generation independently. Dropping a hint
         * under contention must cause requery for every affected subscriber. */
        atomic_fetch_add(&p->losses, 1);
        return LOCKER_STATUS_BUSY;
    }
    if (p->sequence == UINT64_MAX) {
        pthread_mutex_unlock(&p->mutex);
        return LOCKER_STATUS_NO_SPACE;
    }
    p->changes[p->sequence % LOCKER_WS_QUEUE_CAPACITY] = *change;
    ++p->sequence;
    pthread_mutex_unlock(&p->mutex);
    return LOCKER_STATUS_OK;
}

locker_status_t locker_events_subscribe(locker_publisher_t *p, locker_events_cursor_t *cursor)
{
    if (p == NULL || cursor == NULL) return LOCKER_STATUS_INVALID;
    pthread_mutex_lock(&p->mutex);
    cursor->sequence = p->sequence;
    cursor->losses = atomic_load(&p->losses);
    pthread_mutex_unlock(&p->mutex);
    return LOCKER_STATUS_OK;
}

locker_status_t locker_events_next(locker_publisher_t *p, locker_events_cursor_t *cursor,
                                  locker_resource_change_t *change)
{
    if (p == NULL || cursor == NULL || change == NULL) return LOCKER_STATUS_INVALID;
    memset(change, 0, sizeof(*change));
    pthread_mutex_lock(&p->mutex);
    locker_status_t status;
    if (cursor->losses != atomic_load(&p->losses)) {
        cursor->losses = atomic_load(&p->losses);
        cursor->sequence = p->sequence;
        status = LOCKER_STATUS_CONFLICT;
    } else if (cursor->sequence > p->sequence) {
        status = LOCKER_STATUS_INVALID;
    } else if (p->sequence - cursor->sequence > LOCKER_WS_QUEUE_CAPACITY) {
        cursor->sequence = p->sequence;
        cursor->losses = atomic_load(&p->losses);
        status = LOCKER_STATUS_CONFLICT;
    } else if (cursor->sequence == p->sequence) {
        status = LOCKER_STATUS_NOT_READY;
    } else {
        *change = p->changes[cursor->sequence % LOCKER_WS_QUEUE_CAPACITY];
        ++cursor->sequence;
        status = LOCKER_STATUS_OK;
    }
    pthread_mutex_unlock(&p->mutex);
    return status;
}
