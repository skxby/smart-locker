#ifndef LOCKER_HTTP_EVENTS_H
#define LOCKER_HTTP_EVENTS_H
#include "locker_contracts.h"
/* Private adapter API. Subscribe before reading a snapshot. A lagging reader
 * gets CONFLICT and must requery; publishing never waits for a consumer. */
typedef struct { uint64_t sequence, losses; } locker_events_cursor_t;
locker_status_t locker_events_subscribe(locker_publisher_t *publisher, locker_events_cursor_t *cursor);
locker_status_t locker_events_next(locker_publisher_t *publisher, locker_events_cursor_t *cursor,
                                  locker_resource_change_t *change);
#endif
