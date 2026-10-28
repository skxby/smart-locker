#define _POSIX_C_SOURCE 200809L
#include "ws.h"
#include "events.h"
#include "civetweb.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sodium.h>

#define WS_CLIENTS 4
struct locker_ws {
    locker_ws_reject_t reject;
    locker_core_t *core;
    locker_publisher_t *publisher;
    pthread_mutex_t mutex;
    pthread_t thread;
    atomic_bool stopping;
    struct {
        struct mg_connection *connection;
        locker_auth_request_t auth;
        locker_events_cursor_t cursor;
        bool ready, initial;
    } clients[WS_CLIENTS];
};

static int connect_client(const struct mg_connection *connection, void *user)
{
    locker_ws_t *ws = user;
    const struct mg_request_info *r = mg_get_request_info(connection);
    const char *header = mg_get_header(connection, "Authorization");
    locker_auth_request_t auth = {0}; locker_principal_t principal;
    if (atomic_load(&ws->stopping)) { ws->reject((struct mg_connection *)connection,503,"DEVICE_UNAVAILABLE"); return 1; }
    if ((r->query_string && *r->query_string) || r->content_length > 0) { ws->reject((struct mg_connection *)connection,400,"VALIDATION_ERROR"); return 1; }
    if (!header || strncmp(header,"Bearer ",7) || strlen(header+7)!=64) { ws->reject((struct mg_connection *)connection,401,"UNAUTHENTICATED"); return 1; }
    memcpy(auth.token, header + 7, 64); auth.token_length = 64;
    if (locker_core_authenticate(ws->core, &auth, &principal) != LOCKER_STATUS_OK) {
        sodium_memzero(&auth, sizeof(auth)); ws->reject((struct mg_connection *)connection,401,"UNAUTHENTICATED"); return 1;
    }
    int rejected = 1;
    pthread_mutex_lock(&ws->mutex);
    for (size_t i = 0; i < WS_CLIENTS; ++i) if (!ws->clients[i].connection) {
        ws->clients[i].connection = (struct mg_connection *)connection;
        ws->clients[i].auth = auth;
        (void)locker_events_subscribe(ws->publisher, &ws->clients[i].cursor);
        rejected = 0; break;
    }
    pthread_mutex_unlock(&ws->mutex);
    sodium_memzero(&auth, sizeof(auth));
    if (rejected) ws->reject((struct mg_connection *)connection,429,"RATE_LIMITED");
    return rejected;
}

static void ready_client(struct mg_connection *connection, void *user)
{
    locker_ws_t *ws = user;
    pthread_mutex_lock(&ws->mutex);
    for (size_t i = 0; i < WS_CLIENTS; ++i) if (ws->clients[i].connection == connection) ws->clients[i].ready = true, ws->clients[i].initial=true;
    pthread_mutex_unlock(&ws->mutex);
}

static int data_client(struct mg_connection *connection, int bits, char *data, size_t length, void *user)
{
    (void)user;
    if ((bits&15)==MG_WEBSOCKET_OPCODE_PING) return mg_websocket_write(connection,MG_WEBSOCKET_OPCODE_PONG,data,length)>0;
    /* Clients cannot submit business commands over this hint channel. */
    return (bits & 15) == MG_WEBSOCKET_OPCODE_PONG;
}

static void close_client(const struct mg_connection *connection, void *user)
{
    locker_ws_t *ws = user;
    pthread_mutex_lock(&ws->mutex);
    for (size_t i = 0; i < WS_CLIENTS; ++i) if (ws->clients[i].connection == connection)
        sodium_memzero(&ws->clients[i], sizeof(ws->clients[i]));
    pthread_mutex_unlock(&ws->mutex);
}

static void *publish(void *user)
{
    locker_ws_t *ws = user;
    while (!atomic_load(&ws->stopping)) {
        pthread_mutex_lock(&ws->mutex);
        for (size_t i = 0; i < WS_CLIENTS; ++i) if (ws->clients[i].connection && ws->clients[i].ready) {
            locker_principal_t p;
            if (locker_core_authenticate(ws->core, &ws->clients[i].auth, &p) != LOCKER_STATUS_OK) {
                (void)mg_websocket_write(ws->clients[i].connection, MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE, NULL, 0);
                ws->clients[i].ready = false;
                continue;
            }
            locker_resource_change_t change;
            locker_status_t status = locker_events_next(ws->publisher, &ws->clients[i].cursor, &change);
            if (ws->clients[i].initial || status == LOCKER_STATUS_OK || status == LOCKER_STATUS_CONFLICT) {
                ws->clients[i].initial=false;
                /* Only invalidation is sent: never expose another user's resource IDs.
                   Queue loss also requests a new authenticated snapshot. */
                locker_query_t q = {0}; q.principal = p;
                strcpy(q.kind, LOCKER_CORE_SNAPSHOT_GET);
                locker_query_result_t *result = calloc(1, sizeof(*result));
                if (!result) continue;
                if (locker_core_query(ws->core, &q, result) == LOCKER_STATUS_OK && result->status == LOCKER_STATUS_OK) {
                    char body[128];
                    int count = snprintf(body, sizeof(body), "{\"type\":\"resync_required\",\"snapshot_revision\":\"%llu\"}",
                        (unsigned long long)result->snapshot_revision);
                    if (mg_websocket_write(ws->clients[i].connection, MG_WEBSOCKET_OPCODE_TEXT, body, (size_t)count) <= 0)
                        ws->clients[i].ready = false;
                }
                free(result);
            }
        }
        pthread_mutex_unlock(&ws->mutex);
        struct timespec delay = {0, 100000000}; nanosleep(&delay, NULL);
    }
    return NULL;
}

locker_ws_t *locker_ws_start(struct mg_context *context, locker_core_t *core, locker_publisher_t *publisher, locker_ws_reject_t reject)
{
    if (!context || !core || !publisher || !reject) return NULL;
    locker_ws_t *ws = calloc(1, sizeof(*ws));
    if (!ws) return NULL;
    ws->reject=reject; ws->core = core; ws->publisher = publisher; atomic_init(&ws->stopping, false);
    if (pthread_mutex_init(&ws->mutex, NULL)) { free(ws); return NULL; }
    if (pthread_create(&ws->thread, NULL, publish, ws)) { pthread_mutex_destroy(&ws->mutex); free(ws); return NULL; }
    mg_set_websocket_handler(context, "/api/v1/ws", connect_client, ready_client, data_client, close_client, ws);
    return ws;
}
void locker_ws_stop(locker_ws_t *ws)
{
    if (ws && !atomic_exchange(&ws->stopping, true)) (void)pthread_join(ws->thread, NULL);
}
void locker_ws_destroy(locker_ws_t *ws)
{
    if (!ws) return;
    locker_ws_stop(ws); pthread_mutex_destroy(&ws->mutex); sodium_memzero(ws, sizeof(*ws)); free(ws);
}
