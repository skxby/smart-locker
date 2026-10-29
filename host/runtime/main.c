#define _POSIX_C_SOURCE 200809L
#include "runtime.h"
#include "locker/common/clock.h"
#include "cJSON.h"
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
int main(int argc, char **argv)
{
    runtime_config_t config;
    if (argc!=3 || strcmp(argv[1],"--config") || !runtime_config_load(argv[2],&config)) {
        fputs("usage: locker-service --config FILE (valid SIMULATOR_ONLY config required)\n",stderr); return 2;
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
    cc.device_simulated=true;
    if (locker_clock_real_new_uuid(&cc.instance_id)!=LOCKER_STATUS_OK) goto done;
    strcpy(cc.vision_policy.json,"{\"mode\":\"DISABLED_FOR_FOUNDATION\"}"); cc.vision_policy.length=strlen(cc.vision_policy.json);
    config.device.host_instance_id=cc.instance_id; config.device.clock=cc.clock;
    if (locker_events_create(&events)!=LOCKER_STATUS_OK || locker_device_create(&device)!=LOCKER_STATUS_OK ||
        locker_core_create(&cc,repo,device,events,&core)!=LOCKER_STATUS_OK || locker_core_start(core)!=LOCKER_STATUS_OK) {
        fputs("core assembly failed\n",stderr); goto done;
    }
    fputs("SIMULATOR_ONLY startup RECONCILING; queue=64 http_workers=4 ws_buffer=16\n",stdout); fflush(stdout);
    if (locker_device_open(device,&config.device)!=LOCKER_STATUS_OK) { fputs("simulator connection failed\n",stderr); goto done; }
    if (locker_http_start(&config.http,core,events,&http)!=LOCKER_STATUS_OK) { fputs("HTTPS start failed (certificate/bind/port)\n",stderr); goto done; }
    if (locker_ipc_start(&config.ipc,core,&ipc)!=LOCKER_STATUS_OK) { fputs("IPC start failed (path/permissions)\n",stderr); goto done; }
    fputs("SIMULATOR_ONLY listening; business readiness requires reconciliation\n",stdout); fflush(stdout);
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
