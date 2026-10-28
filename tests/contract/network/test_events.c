#include "events.h"
#include "test_support.h"

int main(void)
{
    locker_publisher_t *p = NULL;
    locker_events_cursor_t fast, slow;
    locker_resource_change_t change = {0}, result;
    TEST_CHECK(locker_events_create(&p) == LOCKER_STATUS_OK);
    TEST_CHECK(locker_events_subscribe(p, &fast) == LOCKER_STATUS_OK);
    slow = fast;
    strcpy(change.resource, "operations");
    for (unsigned i = 0; i <= LOCKER_WS_QUEUE_CAPACITY; ++i) {
        change.snapshot_revision = i;
        TEST_CHECK(locker_events_try_publish(p, &change) == LOCKER_STATUS_OK);
        TEST_CHECK(locker_events_next(p, &fast, &result) == LOCKER_STATUS_OK);
        TEST_CHECK(result.snapshot_revision == i);
    }
    TEST_CHECK(locker_events_next(p, &slow, &result) == LOCKER_STATUS_CONFLICT);
    TEST_CHECK(locker_events_next(p, &slow, &result) == LOCKER_STATUS_NOT_READY);
    TEST_CHECK(locker_events_try_publish(NULL, &change) == LOCKER_STATUS_INVALID);
    memset(change.resource, 'x', sizeof(change.resource));
    TEST_CHECK(locker_events_try_publish(p, &change) == LOCKER_STATUS_INVALID);
    locker_events_destroy(p);
    TEST_REPORT("network.events");
}
