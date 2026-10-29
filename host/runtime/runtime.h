#ifndef LOCKER_RUNTIME_H
#define LOCKER_RUNTIME_H
#include "locker_contracts.h"
typedef struct {
    char state_dir[80], database[256], lock_path[108];
    locker_http_config_t http;
    locker_ipc_config_t ipc;
    locker_device_config_t device;
    char cabinet_name[257];
} runtime_config_t;
typedef struct { bool valid; locker_progress_t sample; } runtime_health_t;
bool runtime_config_load(const char *path, runtime_config_t *out);
/* Owner-only local directory; canonical spelling prevents lock aliases. */
int runtime_lock(const runtime_config_t *config, bool create_directory);
bool runtime_database_check(const runtime_config_t *config);
void runtime_health_report(runtime_health_t *cache, const locker_uuid_t *instance,
                           const locker_progress_t *sample, uint64_t now,
                           locker_health_report_t *out);
#endif
