#define _XOPEN_SOURCE 700
#include "runtime.h"
#include "cJSON.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static bool text(const cJSON *root, const char *key, char *out, size_t size)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(v) || !v->valuestring[0] || strlen(v->valuestring) >= size) return false;
    strcpy(out, v->valuestring);
    return true;
}
static bool number(const cJSON *root, const char *key, uint32_t max, uint32_t *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(v) || !(v->valuedouble >= 0 && v->valuedouble <= max) ||
        v->valuedouble != (double)(uint32_t)v->valuedouble) return false;
    *out = (uint32_t)v->valuedouble;
    return true;
}
bool runtime_config_load(const char *path, runtime_config_t *out)
{
    static const char *keys[] = {"mode", "state_dir", "device_path", "device_id", "certificate",
        "private_key", "bind_address", "port", "ui_uid", "admin_uid", "cabinet_name"};
    char buffer[4097], mode[32];
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buffer, 1, sizeof(buffer), f);
    bool io_ok = !ferror(f);
    fclose(f);
    if (!io_ok || !n || n >= sizeof(buffer) || memchr(buffer, 0, n)) return false;
    buffer[n] = 0;
    /* cJSON represents strings as NUL-terminated C text: do not accept an
     * escaped NUL which would hide a key/value suffix from validation. */
    if (strstr(buffer,"\\u0000")) return false;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(buffer, n + 1, &end, 1);
    if (!root || !cJSON_IsObject(root)) { cJSON_Delete(root); return false; }
    bool ok = cJSON_GetArraySize(root) == (int)(sizeof(keys) / sizeof(keys[0]));
    for (size_t i = 0; ok && i < sizeof(keys) / sizeof(keys[0]); ++i) {
        unsigned count = 0;
        for (const cJSON *v = root->child; v; v = v->next) if (!strcmp(v->string, keys[i])) ++count;
        ok = count == 1;
    }
    memset(out, 0, sizeof(*out));
    uint32_t port = 0;
    unsigned char address[16];
    /* Two deployment modes share one schema. SIMULATOR_ONLY talks to
     * locker-device-sim over a socket; DEVICE_SERIAL drives the board's UART and
     * therefore demands a character device. Anything else is rejected rather
     * than guessed, because mistaking one for the other would report a real
     * cabinet from a fake device. */
    bool simulator = false;
    ok = ok && text(root,"mode",mode,sizeof(mode)) &&
        ((simulator = !strcmp(mode,"SIMULATOR_ONLY")) || !strcmp(mode,"DEVICE_SERIAL")) &&
        text(root,"state_dir",out->state_dir,sizeof(out->state_dir)) && out->state_dir[0]=='/' &&
        text(root,"device_path",out->device.path,sizeof(out->device.path)) && out->device.path[0]=='/' &&
        (simulator || !strncmp(out->device.path,"/dev/",5)) &&
        text(root,"device_id",out->device.expected_device_id,sizeof(out->device.expected_device_id)) &&
        strlen(out->device.expected_device_id)==24 && strspn(out->device.expected_device_id,"0123456789abcdef")==24 &&
        text(root,"certificate",out->http.certificate_path,sizeof(out->http.certificate_path)) && out->http.certificate_path[0]=='/' &&
        text(root,"private_key",out->http.private_key_path,sizeof(out->http.private_key_path)) && out->http.private_key_path[0]=='/' &&
        text(root,"cabinet_name",out->cabinet_name,sizeof(out->cabinet_name)) &&
        text(root,"bind_address",out->http.bind_address,sizeof(out->http.bind_address)) &&
        inet_pton(AF_INET,out->http.bind_address,address)==1 &&
        number(root,"port",65535,&port) && port>0 &&
        number(root,"ui_uid",UINT32_MAX,&out->ipc.allowed_ui_uid) &&
        number(root,"admin_uid",UINT32_MAX,&out->ipc.allowed_admin_uid);
    if (ok) {
        out->http.port = (uint16_t)port;
        out->http.worker_count = 4;
        out->ipc.file_mode = 0600;
        out->device.simulated = simulator;
        out->device.baud_rate = 115200;
        snprintf(out->database,sizeof(out->database),"%s/business.db",out->state_dir);
        snprintf(out->lock_path,sizeof(out->lock_path),"%s/instance.lock",out->state_dir);
        snprintf(out->ipc.socket_path,sizeof(out->ipc.socket_path),"%s/service.sock",out->state_dir);
        ok = strcmp(out->device.path,out->ipc.socket_path) && strcmp(out->device.path,out->lock_path) &&
            strcmp(out->device.path,out->database) && strcmp(out->http.private_key_path,out->database) &&
            strcmp(out->http.certificate_path,out->database);
    }
    cJSON_Delete(root);
    return ok;
}
static bool private_file(const struct stat *st)
{
    return S_ISREG(st->st_mode) && st->st_uid==geteuid() && !(st->st_mode & 0077) && st->st_nlink==1;
}
int runtime_lock(const runtime_config_t *config, bool create_directory)
{
    struct stat st, named;
    char canonical[PATH_MAX];
    if (create_directory && mkdir(config->state_dir,0700)<0 && errno!=EEXIST) return -1;
    if (lstat(config->state_dir,&st)<0 || !S_ISDIR(st.st_mode) || st.st_uid!=geteuid() ||
        (st.st_mode & 0077) || !realpath(config->state_dir,canonical) || strcmp(canonical,config->state_dir)) return -1;
    int fd = open(config->lock_path,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if (fd<0) return -1;
    if (fstat(fd,&st)<0 || !private_file(&st) || flock(fd,LOCK_EX|LOCK_NB)<0 ||
        lstat(config->lock_path,&named)<0 || st.st_dev!=named.st_dev || st.st_ino!=named.st_ino) {
        close(fd); return -1;
    }
    /* Never unlink: every service/init/maintenance owner locks the same inode. */
    return fd;
}
bool runtime_database_check(const runtime_config_t *config)
{
    char path[270]; struct stat st;
    static const char *suffixes[]={"","-wal","-shm","-journal"};
    for (unsigned i=0;i<4;++i) {
        snprintf(path,sizeof(path),"%s%s",config->database,suffixes[i]);
        if (lstat(path,&st)<0) { if (i && errno==ENOENT) continue; return false; }
        if (!private_file(&st)) return false;
    }
    return true;
}
