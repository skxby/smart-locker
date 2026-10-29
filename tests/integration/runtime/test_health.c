#include "runtime.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

int main(void)
{
    runtime_health_t cache = {0};
    locker_uuid_t instance = {{1}}, zero = {{0}};
    locker_health_report_t report;
    locker_progress_t sample = {0};
    runtime_health_report(&cache, &instance, NULL, 100, &report);
    CHECK(report.core_progress == 0 && report.core_age_ms == UINT32_MAX);
    CHECK(report.ui_progress == 0 && report.ui_age_ms == UINT32_MAX);
    CHECK(memcmp(&report.ui_instance_id, &zero, sizeof(zero)) == 0);
    CHECK(memcmp(&report.host_instance_id, &instance, sizeof(instance)) == 0);
    CHECK(report.supervisor_state == LOCKER_SUPERVISOR_STATE_UNKNOWN);

    sample.instance_id = instance;
    sample.progress = 4;
    sample.sampled_mono_ms = 100;
    sample.observed_age_ms = 7;
    runtime_health_report(&cache, &instance, &sample, 110, &report);
    CHECK(report.core_progress == 4 && report.core_age_ms == 17);
    /* A fresh copy timestamp cannot conceal a stopped Core counter. */
    sample.sampled_mono_ms = 200;
    sample.observed_age_ms = 0;
    runtime_health_report(&cache, &instance, &sample, 210, &report);
    CHECK(report.core_progress == 4 && report.core_age_ms == 117);
    runtime_health_report(&cache, &instance, NULL, 220, &report);
    CHECK(report.core_age_ms == 127);
    sample.progress = 3;
    runtime_health_report(&cache, &instance, &sample, 230, &report);
    CHECK(report.core_progress == 4 && report.core_age_ms == 137);
    sample.progress = 5;
    sample.instance_id.bytes[0] = 2;
    runtime_health_report(&cache, &instance, &sample, 240, &report);
    CHECK(report.core_progress == 4 && report.core_age_ms == 147);
    sample.instance_id = instance;
    sample.sampled_mono_ms = 300;
    runtime_health_report(&cache, &instance, &sample, 250, &report);
    CHECK(report.core_progress == 4 && report.core_age_ms == 157);
    sample.sampled_mono_ms = 250;
    sample.observed_age_ms = UINT32_MAX;
    runtime_health_report(&cache, &instance, &sample, 250, &report);
    CHECK(report.core_progress == 4 && report.core_age_ms == 157);
    sample.observed_age_ms = UINT32_MAX - 2;
    runtime_health_report(&cache, &instance, &sample, 251, &report);
    CHECK(report.core_progress == 5 && report.core_age_ms == UINT32_MAX - 1);
    runtime_health_report(&cache, &instance, NULL, 252, &report);
    CHECK(report.core_age_ms == UINT32_MAX);
    runtime_health_report(&cache, &instance, NULL, UINT64_MAX, &report);
    CHECK(report.core_age_ms == UINT32_MAX);
    CHECK(report.ui_age_ms == UINT32_MAX && report.ui_progress == 0);

    memset(&cache, 0, sizeof(cache));
    sample.instance_id.bytes[0] = 2;
    runtime_health_report(&cache, &instance, &sample, 300, &report);
    CHECK(!cache.valid && report.core_age_ms == UINT32_MAX);
    puts("runtime health: unknown UI, source counter freshness, identity, saturation passed");
    return 0;
}
