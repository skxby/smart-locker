#define _POSIX_C_SOURCE 200809L
#include "runtime.h"
#include "locker/common/clock.h"
#include "locker/device/recovery.h"
#include "cJSON.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static void stop_signal(int sig) { (void)sig; stopping=1; }
static bool initialized_database(locker_repo_t *repo)
{
    locker_repo_query_t q={0}; locker_repo_result_t *rows=calloc(1,sizeof(*rows));
    if (!rows) return false;
    q.schema_version=1; q.limit=2; strcpy(q.collection,"tools");
    bool ok=false;
    if (locker_repo_query(repo,&q,rows)==LOCKER_STATUS_OK) {
        cJSON *root=cJSON_Parse(rows->json);
        cJSON *docs=cJSON_GetObjectItemCaseSensitive(root,"documents");
        cJSON *value=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(docs,0),"value");
        cJSON *slot=cJSON_GetObjectItemCaseSensitive(value,"slot_id");
        ok=cJSON_IsArray(docs) && cJSON_GetArraySize(docs)==1 && cJSON_IsNumber(slot) && slot->valuedouble==1;
        cJSON_Delete(root);
    }
    if (ok) {
        strcpy(q.collection,"users"); q.limit=100;
        ok=false;
        if (locker_repo_query(repo,&q,rows)==LOCKER_STATUS_OK) {
            cJSON *root=cJSON_Parse(rows->json);
            cJSON *docs=cJSON_GetObjectItemCaseSensitive(root,"documents");
            for (cJSON *doc=docs ? docs->child : NULL; doc; doc=doc->next) {
                cJSON *user=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(doc,"value"),"user");
                cJSON *role=cJSON_GetObjectItemCaseSensitive(user,"role");
                if (cJSON_IsString(role) && !strcmp(role->valuestring,"ADMIN") &&
                    cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(user,"enabled"))) ok=true;
            }
            cJSON_Delete(root);
        }
    }
    free(rows); return ok;
}
/*
 * The tty cannot report a pulled wire, so the adapter marks the link lost after
 * three unanswered requests. This supervisor performs the recovery a service
 * restart would do, with backoff, and lets HELLO re-establish the session before
 * any action is admitted again.
 */
typedef struct {
    locker_device_t *device;
    const locker_device_config_t *config;
} supervisor_ctx_t;
static supervisor_ctx_t supervisor_ctx;
static volatile sig_atomic_t supervisor_stop;
static pthread_t supervisor_thread;
static bool supervisor_started;

#define SUPERVISOR_GRACE_MS 3000u

static uint64_t supervisor_now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)(now.tv_nsec / 1000000L);
}

static void *device_supervisor(void *argument)
{
    supervisor_ctx_t *ctx = argument;
    unsigned backoff_ms = 1000u;
    unsigned trace_tick = 0u;
    uint64_t opened_ms = supervisor_now_ms();
    const bool trace = getenv("LOCKER_DEVICE_TRACE") != NULL;
    for (;;) {
        struct timespec pause = {0, 500000000L};
        locker_status_t status;
        locker_device_diagnosis_t diag;
        bool workers_dead;

        (void)nanosleep(&pause, NULL);
        if (supervisor_stop) {
            return NULL;
        }
        (void)locker_device_diagnose(ctx->device, &diag);
        if (trace && (++trace_tick % 2u) == 0u) {
            printf("dev open=%d hello=%d lost=%d rd=%d wr=%d streak=%u pend=%u tx=%u\n",
                   diag.opened, diag.hello_complete, diag.link_lost, diag.reader_alive,
                   diag.writer_alive, diag.expired_streak, diag.pending_used,
                   diag.transmit_used);
            fflush(stdout);
        }
        workers_dead = diag.opened && (!diag.reader_alive || !diag.writer_alive);
        /* Order matters: a dead link keeps opened/hello_complete true because
         * nothing tears the session down, so "session ready" must be tested only
         * after "link lost". Testing it first made the supervisor skip recovery
         * for ever while streak climbed to 63. */
        if (diag.link_lost || workers_dead) {
            printf("device link lost; closing before reopen (lost=%d rd=%d wr=%d)\n",
                   diag.link_lost, diag.reader_alive, diag.writer_alive);
            fflush(stdout);
        } else if (locker_device_session_ready(ctx->device)) {
            backoff_ms = 1000u;
            continue;
        } else if (supervisor_now_ms() - opened_ms < SUPERVISOR_GRACE_MS) {
            continue; /* HELLO may still be in flight right after open */
        } else {
            /* Opened but silent: the reopen landed while the wire was still out.
             * Without this branch hello_complete stays false for ever and every
             * request is refused, which looks exactly like a dead cabinet. */
            fputs("device session not established; reopening\n", stdout); fflush(stdout);
        }
        (void)locker_device_close(ctx->device);
        pause.tv_sec = (time_t)(backoff_ms / 1000u);
        pause.tv_nsec = (long)(backoff_ms % 1000u) * 1000000L;
        (void)nanosleep(&pause, NULL);
        if (supervisor_stop) {
            return NULL;
        }
        status = locker_device_open(ctx->device, ctx->config);
        opened_ms = supervisor_now_ms();
        printf("device reopen %s\n", status == LOCKER_STATUS_OK ? "ok" : "failed"); fflush(stdout);
        backoff_ms = backoff_ms < 8000u ? backoff_ms * 2u : 8000u;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    runtime_config_t config;
    if (argc!=3 || strcmp(argv[1],"--config") || !runtime_config_load(argv[2],&config)) {
        fputs("usage: locker-service --config FILE (valid SIMULATOR_ONLY or DEVICE_SERIAL config)\n",stderr); return 2;
    }
    umask(0077);
    struct sigaction action={0}; action.sa_handler=stop_signal; sigemptyset(&action.sa_mask);
    sigaction(SIGTERM,&action,NULL); sigaction(SIGINT,&action,NULL); signal(SIGPIPE,SIG_IGN);
    int lock=runtime_lock(&config,false);
    if (lock<0) { fputs("state directory unsafe, unavailable or instance/maintenance lock busy\n",stderr); return 1; }
    int result=1;
    locker_repo_t *repo=NULL; locker_core_t *core=NULL; locker_device_t *device=NULL;
    locker_http_t *http=NULL; locker_ipc_t *ipc=NULL; locker_publisher_t *events=NULL;
    locker_repo_config_t rc={0}; locker_core_config_t cc={0};
    strcpy(rc.path,config.database); rc.schema_version=1;
    if (!runtime_database_check(&config)) { fputs("database missing or unsafe; initialize explicitly\n",stderr); goto done; }
    if (locker_repo_open(&rc,&repo)!=LOCKER_STATUS_OK) { fputs("database open/validation failed\n",stderr); goto done; }
    /* Existence and schema alone do not establish completed initialization. */
    if (!initialized_database(repo)) { fputs("initialization incomplete: administrator/single tool missing\n",stderr); goto done; }
    cc.clock=locker_clock_port_real(); cc.queue_capacity=64; strcpy(cc.cabinet_name,config.cabinet_name);
    cc.runtime_health_owned=true;
    /* The core records provenance: a real UART must never be reported as simulated. */
    cc.device_simulated=config.device.simulated;
    if (locker_clock_real_new_uuid(&cc.instance_id)!=LOCKER_STATUS_OK) goto done;
    strcpy(cc.vision_policy.json,"{\"mode\":\"DISABLED_FOR_FOUNDATION\"}"); cc.vision_policy.length=strlen(cc.vision_policy.json);
    config.device.host_instance_id=cc.instance_id; config.device.clock=cc.clock;
    if (locker_events_create(&events)!=LOCKER_STATUS_OK || locker_device_create(&device)!=LOCKER_STATUS_OK ||
        locker_core_create(&cc,repo,device,events,&core)!=LOCKER_STATUS_OK || locker_core_start(core)!=LOCKER_STATUS_OK) {
        fputs("core assembly failed\n",stderr); goto done;
    }
    printf("%s startup RECONCILING; queue=64 http_workers=4 ws_buffer=16\n",
           config.device.simulated ? "SIMULATOR_ONLY" : "DEVICE_SERIAL"); fflush(stdout);
    if (locker_device_open(device,&config.device)!=LOCKER_STATUS_OK) {
        fputs(config.device.simulated ? "simulator connection failed\n"
                                      : "device open failed (UART missing, busy or permissions)\n",stderr); goto done; }
    {
        supervisor_ctx.device = device; supervisor_ctx.config = &config.device;
        if (pthread_create(&supervisor_thread, NULL, device_supervisor, &supervisor_ctx) != 0)
            fputs("device supervisor unavailable; a link loss will need a restart\n", stderr);
        else
            supervisor_started = true;
    }
    if (locker_http_start(&config.http,core,events,&http)!=LOCKER_STATUS_OK) { fputs("HTTPS start failed (certificate/bind/port)\n",stderr); goto done; }
    if (locker_ipc_start(&config.ipc,core,&ipc)!=LOCKER_STATUS_OK) { fputs("IPC start failed (path/permissions)\n",stderr); goto done; }
    printf("%s listening; business readiness requires reconciliation\n",
           config.device.simulated ? "SIMULATOR_ONLY" : "DEVICE_SERIAL"); fflush(stdout);
    runtime_health_t health={0}; uint64_t last=0;
    while (!stopping) {
        uint64_t now=locker_clock_real_monotonic_ms();
        if (!last || now-last>=1000) {
            locker_progress_t progress; locker_health_report_t report;
            locker_status_t status=locker_core_get_progress(core,&progress);
            runtime_health_report(&health,&cc.instance_id,status==LOCKER_STATUS_OK ? &progress : NULL,now,&report);
            (void)locker_device_send_health(device,&report); last=now;
        }
        struct timespec pause={0,100000000}; nanosleep(&pause,NULL);
    }
    result=0;
done:
    if (result) fputs("service startup failed; persistent state is preserved\n",stderr);
    /* Timeout is never permission to free borrowed objects. Keep waiting safely. */
    if (http) {
        while (locker_http_stop(http,1000)!=LOCKER_STATUS_OK) {
            struct timespec pause={0,10000000}; nanosleep(&pause,NULL);
        }
        locker_http_destroy(http);
    }
    if (ipc) {
        while (locker_ipc_stop(ipc,1000)!=LOCKER_STATUS_OK) {
            struct timespec pause={0,10000000}; nanosleep(&pause,NULL);
        }
        locker_ipc_destroy(ipc);
    }
    /* Stop the supervisor before the adapter goes away; a detached thread that
     * keeps reopening a device during teardown would be a race, not a recovery.
     * It polls every 500 ms, so the join costs at most one poll interval. */
    supervisor_stop = 1;
    if (supervisor_started) (void)pthread_join(supervisor_thread, NULL);
    if (device) (void)locker_device_close(device);
    if (core) {
        while (locker_core_stop(core,1000)!=LOCKER_STATUS_OK) {
            struct timespec pause={0,10000000}; nanosleep(&pause,NULL);
        }
        locker_core_destroy(core);
    }
    locker_device_destroy(device); locker_events_destroy(events); locker_repo_close(repo);
    close(lock);
    return result;
}
