#include "domain_test_support.h"
#include <sodium.h>

static locker_status_t run_job(dt_fixture_t *f, domain_job_t *job,
                               locker_query_result_t *result, locker_principal_t *principal)
{
    job->completion = domain_completion_acquire(f->core, &job->completion_token);
    domain_handle_job(f->core, job);
    TEST_CHECK(domain_completion_wait_final(f->core, job->completion,
        &job->completion_token, 0, result) == LOCKER_STATUS_OK);
    if (principal != NULL && result->status == LOCKER_STATUS_OK)
        TEST_CHECK(domain_completion_take_principal(f->core, job->completion,
            &job->completion_token, principal));
    domain_completion_release_owned(f->core, job->completion, &job->completion_token);
    return result->status;
}

int main(void)
{
    dt_fixture_t f;
    domain_job_t job = {0};
    locker_query_result_t result;
    locker_principal_t principal = {0};
    char hash[crypto_pwhash_STRBYTES], record[1024];
    TEST_CHECK(sodium_init() >= 0);
    TEST_CHECK(dt_fixture_init(&f, false));
    const char *id = "11111111-1111-4111-8111-111111111111";
    const char *password = "formal-auth-password";
    TEST_CHECK(crypto_pwhash_str_alg(hash, password, strlen(password),
        crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
        crypto_pwhash_ALG_ARGON2ID13) == 0);
    snprintf(record, sizeof(record), "{\"user\":{\"id\":\"%s\",\"username\":\"tester\",\"display_name\":\"Tester\",\"role\":\"ADMIN\",\"enabled\":true,\"user_revision\":\"1\"},\"password_hash\":\"%s\"}", id, hash);
    TEST_CHECK(dt_repo_put(f.repo, "users", id, record));
    job.kind = DOMAIN_JOB_COMMAND;
    strcpy(job.body.command.kind, LOCKER_CORE_SESSION_CREATE);
    strcpy(job.body.command.arguments_json, "{\"username\":\"tester\",\"password\":\"wrong-password\"}");
    TEST_CHECK(run_job(&f, &job, &result, NULL) == LOCKER_STATUS_INVALID);
    strcpy(job.body.command.arguments_json, "{\"username\":\"tester\",\"password\":\"formal-auth-password\"}");
    TEST_CHECK(run_job(&f, &job, &result, NULL) == LOCKER_STATUS_BUSY);
    f.clock.monotonic_ms += 1001;
    TEST_CHECK(run_job(&f, &job, &result, NULL) == LOCKER_STATUS_OK);
    locker_json_document_t doc = {0};
    locker_json_node_t nodes[128]; locker_json_member_t members[128];
    doc.nodes = nodes; doc.node_capacity = 128; doc.members = members; doc.member_capacity = 128;
    TEST_CHECK(locker_domain_json_parse(&doc, result.json, result.length));
    const locker_json_node_t *data = domain_json_member(&doc, locker_domain_json_root(&doc), "data");
    char token[129];
    TEST_CHECK(domain_json_read_text(&doc, domain_json_member(&doc, data, "token"), token, sizeof(token)));
    TEST_CHECK(strlen(token) == 64);
    memset(&job, 0, sizeof(job)); job.kind = DOMAIN_JOB_AUTH;
    strcpy(job.body.auth.token, token); job.body.auth.token_length = strlen(token);
    TEST_CHECK(run_job(&f, &job, &result, &principal) == LOCKER_STATUS_OK);
    TEST_CHECK(principal.authenticated && principal.role == LOCKER_ROLE_ADMIN);
    domain_job_t query = {0};
    query.kind = DOMAIN_JOB_QUERY;
    query.body.query.principal = principal;
    strcpy(query.body.query.kind, LOCKER_CORE_SESSION_CURRENT);
    TEST_CHECK(run_job(&f, &query, &result, NULL) == LOCKER_STATUS_OK);
    TEST_CHECK(strstr(result.json, "tester") != NULL);
    TEST_CHECK(strstr(result.json, "password_hash") == NULL);
    TEST_CHECK(strstr(result.json, token) == NULL);
    job.body.auth.token[0] = job.body.auth.token[0] == '0' ? '1' : '0';
    TEST_CHECK(run_job(&f, &job, &result, NULL) == LOCKER_STATUS_INVALID);
    strcpy(job.body.auth.token, token);
    f.clock.wall_ms += 28800001;
    TEST_CHECK(run_job(&f, &job, &result, NULL) == LOCKER_STATUS_INVALID);
    f.clock.wall_ms -= 28800001;
    domain_job_t logout = {0};
    logout.kind = DOMAIN_JOB_COMMAND;
    logout.body.command.principal = principal;
    strcpy(logout.body.command.kind, LOCKER_CORE_SESSION_REVOKE);
    TEST_CHECK(run_job(&f, &logout, &result, NULL) == LOCKER_STATUS_OK);
    TEST_CHECK(run_job(&f, &job, &result, NULL) == LOCKER_STATUS_INVALID);
    TEST_CHECK(run_job(&f, &query, &result, NULL) == LOCKER_STATUS_INVALID);
    dt_fixture_free(&f);
    TEST_REPORT("network.core_auth");
}
