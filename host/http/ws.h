#ifndef LOCKER_HTTP_WS_H
#define LOCKER_HTTP_WS_H
#include "locker_contracts.h"
struct mg_context;
struct mg_connection;
typedef int (*locker_ws_reject_t)(struct mg_connection *,int,const char *);
typedef struct locker_ws locker_ws_t;
locker_ws_t *locker_ws_start(struct mg_context *context, locker_core_t *core, locker_publisher_t *publisher, locker_ws_reject_t reject);
void locker_ws_stop(locker_ws_t *ws);
void locker_ws_destroy(locker_ws_t *ws);
#endif
