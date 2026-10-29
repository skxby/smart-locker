#include "runtime.h"
#include "locker/common/uuid.h"
#include <string.h>
void runtime_health_report(runtime_health_t *cache, const locker_uuid_t *instance,
                           const locker_progress_t *sample, uint64_t now,
                           locker_health_report_t *out)
{
    memset(out,0,sizeof(*out));
    out->host_instance_id = *instance;
    out->core_age_ms = out->ui_age_ms = UINT32_MAX;
    out->supervisor_state = LOCKER_SUPERVISOR_STATE_UNKNOWN;
    if (sample && locker_uuid_equal(&sample->instance_id,instance) && sample->progress &&
        sample->sampled_mono_ms<=now && sample->observed_age_ms!=UINT32_MAX &&
        (!cache->valid || sample->progress>cache->sample.progress)) {
        cache->sample = *sample;
        cache->valid = true;
    }
    if (cache->valid) {
        out->core_progress = cache->sample.progress;
        if (now>=cache->sample.sampled_mono_ms) {
            uint64_t delta=now-cache->sample.sampled_mono_ms;
            uint32_t age=cache->sample.observed_age_ms;
            out->core_age_ms = delta>=UINT32_MAX-age ? UINT32_MAX : age+(uint32_t)delta;
        }
    }
}
