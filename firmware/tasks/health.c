/* Minimal S2-05 local progress gate. S4-03 owns IWDG and full diagnostics. */
#include "locker_contracts.h"
#include "timebase.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
struct mcu_health {
    mcu_health_config_t config;
    uint64_t progress[4], sampled[4];
    bool known[4];
};
static mcu_health_t singleton;
locker_status_t mcu_health_init(const mcu_health_config_t *config,mcu_health_t **out)
{
    unsigned n;
    if(out) *out=NULL;
    if(!config || !out || config->host_timeout_ms!=LOCKER_HOST_TIMEOUT_MS) return LOCKER_STATUS_INVALID;
    for(n=0;n<4;n++) if(!config->task_deadline_ms[n]) return LOCKER_STATUS_INVALID;
    memset(&singleton,0,sizeof(singleton)); singleton.config=*config; *out=&singleton; return LOCKER_STATUS_OK;
}
locker_status_t mcu_health_report_progress(mcu_health_t *h,uint8_t id,uint64_t progress)
{
    locker_status_t s=LOCKER_STATUS_OK;
    if(h!=&singleton || id>=4u || !progress) return LOCKER_STATUS_INVALID;
    taskENTER_CRITICAL();
    if(h->known[id] && progress<=h->progress[id]) s=LOCKER_STATUS_CONFLICT;
    else { h->known[id]=true; h->progress[id]=progress; h->sampled[id]=mcu_board_monotonic_ms(); }
    taskEXIT_CRITICAL(); return s;
}
locker_status_t mcu_health_evaluate(mcu_health_t *h,uint64_t now)
{
    unsigned n; locker_status_t s=LOCKER_STATUS_OK;
    if(h!=&singleton) return LOCKER_STATUS_INVALID;
    taskENTER_CRITICAL();
    for(n=0;n<4;n++) if(!h->known[n] || now<h->sampled[n] || now-h->sampled[n]>=h->config.task_deadline_ms[n]) s=LOCKER_STATUS_NOT_READY;
    taskEXIT_CRITICAL(); return s;
}
