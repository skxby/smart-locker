#define _POSIX_C_SOURCE 200809L
#include "runtime.h"
#include "locker/common/clock.h"
#include "locker/common/uuid.h"
#include "cJSON.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static volatile sig_atomic_t input_signal;
static void interrupt_input(int sig) { input_signal=sig; }

/* A dedicated controlling terminal or an explicitly selected private regular
 * stdin file/anonymous pipe. Passwords never travel in argv or environment. */
static bool password_read(bool use_stdin, char password[129])
{
    int fd=use_stdin ? STDIN_FILENO : open("/dev/tty",O_RDWR|O_CLOEXEC);
    if (fd<0) return false;
    bool terminal=isatty(fd), ok=false;
    struct termios saved, hidden;
    sigset_t block, old;
    const int signals[]={SIGINT,SIGTERM,SIGHUP,SIGTSTP};
    struct sigaction previous[4], handler={0};
    if (terminal) {
        sigemptyset(&block); sigaddset(&block,SIGINT); sigaddset(&block,SIGTERM); sigaddset(&block,SIGHUP); sigaddset(&block,SIGTSTP);
        if (sigprocmask(SIG_BLOCK,&block,&old)<0) goto done;
        if (tcgetattr(fd,&saved)<0) { sigprocmask(SIG_SETMASK,&old,NULL); goto done; }
        handler.sa_handler=interrupt_input; sigemptyset(&handler.sa_mask);
        for (unsigned i=0;i<4;++i) sigaction(signals[i],&handler,&previous[i]);
        hidden=saved; hidden.c_lflag &= (tcflag_t)~(ECHO|ECHONL);
        if (tcsetattr(fd,TCSAFLUSH,&hidden)<0) {
            for (unsigned i=0;i<4;++i) sigaction(signals[i],&previous[i],NULL);
            sigprocmask(SIG_SETMASK,&old,NULL); goto done;
        }
        (void)write(fd,"Administrator password: ",24);
        sigprocmask(SIG_SETMASK,&old,NULL);
    } else {
        struct stat st;
        if (!use_stdin || fstat(fd,&st)<0 ||
            !((S_ISFIFO(st.st_mode) && (st.st_nlink==0 ||
                (st.st_uid==geteuid() && !(st.st_mode&0077) && st.st_nlink==1))) ||
              (S_ISREG(st.st_mode) && st.st_uid==geteuid() && !(st.st_mode&0077) && st.st_nlink==1))) goto done;
    }
    size_t count=0;
    while (count<129) {
        if (terminal) {
            if (input_signal) break;
            struct pollfd input={fd,POLLIN,0};
            int ready=poll(&input,1,100);
            if (ready<0 && errno!=EINTR) break;
            if (ready<=0) continue;
        }
        unsigned char c; ssize_t n=read(fd,&c,1);
        if (n<0 && errno==EINTR) continue;
        if (n==0 || (n==1 && c=='\n')) { ok=count>=12 && count<=128; break; }
        if (n!=1 || c<32 || c>126) break;
        password[count++]=(char)c;
    }
    if (ok) password[count]=0;
    if (terminal) {
        sigprocmask(SIG_BLOCK,&block,NULL);
        (void)tcsetattr(fd,TCSAFLUSH,&saved); (void)write(fd,"\n",1);
        for (unsigned i=0;i<4;++i) sigaction(signals[i],&previous[i],NULL);
        sigprocmask(SIG_SETMASK,&old,NULL);
        if (input_signal) ok=false;
    }
done:
    if (!use_stdin) close(fd);
    return ok;
}
static char *json_string(const char *text)
{
    cJSON *v=cJSON_CreateString(text);
    char *out=v ? cJSON_PrintUnformatted(v) : NULL;
    cJSON_Delete(v); return out;
}
int main(int argc,char **argv)
{
    bool input=argc==9 && !strcmp(argv[8],"--password-stdin");
    runtime_config_t config;
    if ((argc!=8 && !input) || strcmp(argv[1],"init") || strcmp(argv[2],"--config") ||
        strcmp(argv[4],"--username") || strcmp(argv[6],"--tool-name") || !runtime_config_load(argv[3],&config)) {
        fputs("usage: locker-admin init --config FILE --username NAME --tool-name NAME [--password-stdin]\n",stderr); return 2;
    }
    const char *username=argv[5]; size_t n=strlen(username);
    if (n<3 || n>32 || strspn(username,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=n ||
        !argv[7][0] || strlen(argv[7])>64) { fputs("invalid username or tool name\n",stderr); return 2; }
    umask(0077);
    struct rlimit no_dump={0,0}; (void)setrlimit(RLIMIT_CORE,&no_dump);
    if (sodium_init()<0) return 1;
    int lock=runtime_lock(&config,true);
    if (lock<0) { fputs("state directory unsafe or instance/maintenance lock busy\n",stderr); return 1; }
    int result=1; locker_repo_t *repo=NULL;
    char password[129]={0}, hash[crypto_pwhash_STRBYTES]={0};
    locker_change_t *change=NULL;
    char *user_json=NULL,*tool_json=NULL;
    struct stat st;
    char dbpath[270];
    static const char *suffixes[]={"","-wal","-shm","-journal"};
    for (unsigned i=0;i<4;++i) {
        snprintf(dbpath,sizeof(dbpath),"%s%s",config.database,suffixes[i]);
        if (lstat(dbpath,&st)==0 || errno!=ENOENT) { fputs("database or sidecar already exists; initialization refused\n",stderr); goto done; }
    }
    if (!password_read(input,password)) { fputs("password input rejected (12..128 printable ASCII bytes; private stdin required)\n",stderr); goto done; }
    if (crypto_pwhash_str_alg(hash,password,strlen(password),crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE,crypto_pwhash_ALG_ARGON2ID13)!=0) goto done;
    sodium_memzero(password,sizeof(password));
    locker_uuid_t user_id,tool_id; char user[37],tool[37];
    if (locker_clock_real_new_uuid(&user_id)!=LOCKER_STATUS_OK || locker_clock_real_new_uuid(&tool_id)!=LOCKER_STATUS_OK) goto done;
    locker_uuid_format(&user_id,user,sizeof(user)); locker_uuid_format(&tool_id,tool,sizeof(tool));
    user_json=json_string(username); tool_json=json_string(argv[7]);
    change=calloc(1,sizeof(*change));
    if (!user_json || !tool_json || !change) goto done;
    int written=snprintf(change->json,sizeof(change->json),
        "{\"schema_version\":1,\"expected_snapshot_revision\":\"0\",\"new_snapshot_revision\":\"1\",\"checks\":[],\"mutations\":["
        "{\"op\":\"PUT\",\"document\":{\"collection\":\"users\",\"key\":\"%s\",\"value\":{\"user\":{\"id\":\"%s\",\"username\":%s,\"display_name\":%s,\"role\":\"ADMIN\",\"enabled\":true,\"user_revision\":\"1\"},\"password_hash\":\"%s\"}}},"
        "{\"op\":\"PUT\",\"document\":{\"collection\":\"username_index\",\"key\":%s,\"value\":{\"value\":%s,\"resource_id\":\"%s\"}}},"
        "{\"op\":\"PUT\",\"document\":{\"collection\":\"tools\",\"key\":\"%s\",\"value\":{\"id\":\"%s\",\"name\":%s,\"slot_id\":1,\"tool_revision\":\"1\",\"enabled\":true}}}]}",
        user,user,user_json,user_json,hash,user_json,user_json,user,tool,tool,tool_json);
    if (written<0 || (size_t)written>=sizeof(change->json)) goto done;
    change->length=(size_t)written;
    /* Reserve a new inode. Never reopen an existing or partially initialized DB
     * as a fresh installation, and never delete evidence after a failure. */
    int db=open(config.database,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);
    if (db<0) goto done;
    close(db);
    locker_repo_config_t rc={0}; strcpy(rc.path,config.database); rc.schema_version=1; rc.initialize_empty=true;
    if (locker_repo_open(&rc,&repo)!=LOCKER_STATUS_OK || locker_repo_commit_change(repo,change)!=LOCKER_STATUS_OK) {
        fputs("initialization failed; database preserved for diagnosis\n",stderr); goto done;
    }
    puts("SIMULATOR_ONLY initialized: one administrator, one tool; no physical state asserted"); result=0;
done:
    if (result) fputs("initialization did not commit; existing state is preserved\n",stderr);
    sodium_memzero(password,sizeof(password)); sodium_memzero(hash,sizeof(hash));
    if (change) { sodium_memzero(change,sizeof(*change)); free(change); }
    free(user_json); free(tool_json);
    locker_repo_close(repo); close(lock);
    return result;
}
