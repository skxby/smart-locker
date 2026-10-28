#define _POSIX_C_SOURCE 200809L
#include "locker_contracts.h"
#include "locker/common/clock.h"
#include "locker/common/uuid.h"
#include "domain_json.h"
#include "test_support.h"
#include <sodium.h>
#include <stdlib.h>
#include <unistd.h>

static locker_status_t command(locker_core_t *core, const locker_command_t *c,
                                 locker_query_result_t *result)
{
    locker_receipt_t receipt;
    locker_status_t s = locker_core_submit(core, c, &receipt);
    if (s != LOCKER_STATUS_QUEUED) return s;
    s = locker_core_wait_result(core, &receipt.submission_token, 1000, result);
    return s == LOCKER_STATUS_OK ? result->status : s;
}

int main(int argc, char **argv)
{
    char directory[] = "/tmp/locker-auth-XXXXXX";
    char hash[crypto_pwhash_STRBYTES];
    locker_repo_config_t rc = {0}; locker_repo_t *repo = NULL;
    locker_core_config_t cc = {0}; locker_core_t *core = NULL;
    locker_publisher_t *publisher = NULL;
    locker_change_t change = {0};
    const char *password = "repository-auth-password";
    TEST_CHECK(mkdtemp(directory) != NULL);
    snprintf(rc.path, sizeof(rc.path), "%s/state.db", directory);
    rc.schema_version = 1; rc.initialize_empty = true;
    TEST_CHECK(locker_repo_open(&rc, &repo) == LOCKER_STATUS_OK);
    TEST_CHECK(sodium_init() >= 0);
    TEST_CHECK(crypto_pwhash_str_alg(hash, password, strlen(password),
        crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
        crypto_pwhash_ALG_ARGON2ID13) == 0);
    int n = snprintf(change.json, sizeof(change.json),
        "{\"schema_version\":1,\"expected_snapshot_revision\":\"0\",\"new_snapshot_revision\":\"1\",\"checks\":[],\"mutations\":[{\"op\":\"PUT\",\"document\":{\"collection\":\"users\",\"key\":\"11111111-1111-4111-8111-111111111111\",\"value\":{\"user\":{\"id\":\"11111111-1111-4111-8111-111111111111\",\"username\":\"tester\",\"display_name\":\"Tester\",\"role\":\"ADMIN\",\"enabled\":true,\"user_revision\":\"1\"},\"password_hash\":\"%s\"}}}]}", hash);
    TEST_CHECK(n > 0 && (size_t)n < sizeof(change.json)); change.length = (size_t)n;
    TEST_CHECK(locker_repo_commit_change(repo, &change) == LOCKER_STATUS_OK);
    cc.clock = locker_clock_port_real(); strcpy(cc.cabinet_name, "auth-test");
    TEST_CHECK(locker_clock_real_new_uuid(&cc.instance_id) == LOCKER_STATUS_OK);
    TEST_CHECK(locker_events_create(&publisher) == LOCKER_STATUS_OK);
    TEST_CHECK(locker_core_create(&cc, repo, NULL, publisher, &core) == LOCKER_STATUS_OK);
    TEST_CHECK(locker_core_start(core) == LOCKER_STATUS_OK);
    if (argc == 4 || argc == 5 || argc == 6) {
        locker_http_config_t hc = {0}; locker_http_t *http = NULL;
        strcpy(hc.bind_address, "127.0.0.1"); hc.port = (uint16_t)atoi(argv[3]); hc.worker_count = 2;
        snprintf(hc.certificate_path, sizeof(hc.certificate_path), "%s", argv[1]);
        snprintf(hc.private_key_path, sizeof(hc.private_key_path), "%s", argv[2]);
        TEST_CHECK(locker_http_start(&hc, core, publisher, &http) == LOCKER_STATUS_OK);
        locker_ipc_t *ipc = NULL;
        if (argc >= 5) {
            locker_ipc_config_t ic = {0}; ic.allowed_ui_uid = (uint32_t)getuid(); ic.allowed_admin_uid = (uint32_t)getuid(); ic.file_mode = 0600;
            if (getenv("LOCKER_TEST_IPC_DENY_PEER")) ic.allowed_ui_uid=ic.allowed_admin_uid=UINT32_MAX;
            snprintf(ic.socket_path, sizeof(ic.socket_path), "%s", argv[4]);
            TEST_CHECK(locker_ipc_start(&ic, core, &ipc) == LOCKER_STATUS_OK);
        }
        puts("HTTPS_READY"); fflush(stdout);
        int input;
        while ((input=getchar())!=EOF && input!='\n') {
            if (input=='B') {
                locker_resource_change_t hint={0}; strcpy(hint.resource,"device"); strcpy(hint.id,"fixture-burst");
                for (unsigned i=0;i<10000;++i) (void)locker_events_try_publish(publisher,&hint);
                puts("BURST_DONE"); fflush(stdout);
            }
        }
        TEST_CHECK(locker_http_stop(http, 5000) == LOCKER_STATUS_OK);
        locker_http_destroy(http);
        if (ipc) { TEST_CHECK(locker_ipc_stop(ipc,5000) == LOCKER_STATUS_OK); locker_ipc_destroy(ipc); }
    }
    locker_command_t c = {0}; locker_query_result_t result;
    strcpy(c.kind, LOCKER_CORE_SESSION_CREATE);
    TEST_CHECK(locker_clock_real_new_uuid(&c.request_id) == LOCKER_STATUS_OK);
    strcpy(c.arguments_json, "{\"username\":\"tester\",\"password\":\"repository-auth-password\"}");
    TEST_CHECK(command(core, &c, &result) == LOCKER_STATUS_OK);
    locker_json_node_t nodes[128]; locker_json_member_t members[128];
    locker_json_document_t doc = {0};
    doc.nodes = nodes; doc.node_capacity = 128; doc.members = members; doc.member_capacity = 128;
    TEST_CHECK(locker_domain_json_parse(&doc, result.json, result.length));
    const locker_json_node_t *data = locker_domain_json_get(&doc, locker_domain_json_root(&doc), "data");
    locker_auth_request_t auth = {0}; locker_principal_t p;
    TEST_CHECK(locker_domain_json_text(&doc, locker_domain_json_get(&doc, data, "token"),
        auth.token, sizeof(auth.token), &auth.token_length));
    TEST_CHECK(locker_core_authenticate(core, &auth, &p) == LOCKER_STATUS_OK);
    TEST_CHECK(p.authenticated && p.role == LOCKER_ROLE_ADMIN);
    locker_query_t query = {0}; query.principal = p;
    strcpy(query.kind, LOCKER_CORE_SESSION_CURRENT);
    TEST_CHECK(locker_core_query(core, &query, &result) == LOCKER_STATUS_OK);
    TEST_CHECK(result.status == LOCKER_STATUS_OK && strstr(result.json, "tester") != NULL);
    strcpy(c.kind, LOCKER_CORE_SESSION_REVOKE); c.principal = p;
    TEST_CHECK(command(core, &c, &result) == LOCKER_STATUS_OK);
    TEST_CHECK(locker_core_authenticate(core, &auth, &p) == LOCKER_STATUS_INVALID);
    TEST_CHECK(!p.authenticated);
    TEST_CHECK(locker_core_stop(core, 5000) == LOCKER_STATUS_OK);
    locker_core_destroy(core); locker_events_destroy(publisher);
    locker_repo_query_t rq = {0}; locker_repo_result_t stored;
    rq.schema_version = 1; rq.limit = 100; strcpy(rq.collection, "sessions");
    TEST_CHECK(locker_repo_query(repo, &rq, &stored) == LOCKER_STATUS_OK);
    TEST_CHECK(strstr(stored.json, auth.token) == NULL);
    TEST_CHECK(strstr(stored.json, "token_hash") != NULL);
    TEST_CHECK(strstr(stored.json, "\"revoked\":true") != NULL);
    if (argc==6) {
        memset(&rq,0,sizeof(rq)); rq.schema_version=1; rq.limit=1;
        strcpy(rq.collection,"idempotency"); strcpy(rq.key,"33333333-3333-4333-8333-333333333333");
        TEST_CHECK(locker_repo_query(repo,&rq,&stored)==LOCKER_STATUS_OK);
        TEST_CHECK(strstr(stored.json,"USER_CREATE")!=NULL && strstr(stored.json,"request_hash")!=NULL);
        TEST_CHECK(strstr(stored.json,argv[5])==NULL);
    }
    locker_repo_close(repo);
    unlink(rc.path);
    char sidecar[300];
    snprintf(sidecar, sizeof(sidecar), "%s-wal", rc.path); unlink(sidecar);
    snprintf(sidecar, sizeof(sidecar), "%s-shm", rc.path); unlink(sidecar);
    rmdir(directory);
    sodium_memzero(&auth, sizeof(auth));
    TEST_REPORT("network.auth_repository");
}
