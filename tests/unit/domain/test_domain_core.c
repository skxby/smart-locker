#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 单元测试：Core 所有者线程生命周期（domain_core.c）与开放前恢复。
 *
 * 钉死的东西：
 *   1. 准入守卫：submit/query/apply_device_event/apply_device_reply/tick 在线程未启动时
 *      一律 NOT_READY，参数缺失一律 INVALID；绝不把作业塞进没人消费的队列。
 *   2. 生命周期：start 只允许一个所有者线程（重复 start 是 CONFLICT）；stop 幂等
 *      （未运行时 OK）、超时是 BUSY 而不是取消；destroy 只在线程停下后释放。
 *   3. submit 只表示「排队成功」：返回 QUEUED，最终结果必须经 submission_token 取回；
 *      未知 token 是 NOT_READY，不是空结果。
 *   4. 凭据校验与管理员健康端口在本卡是显式 NOT_READY，绝不空实现冒充通过。
 *   5. 恢复只做读取与内存重建：终态历史不重建、只允许一个活动操作、REQUESTED /
 *      WAIT_CONFIRM 的截止时间按记录里的 updated_at 还原；已发送的意图
 *      （ACCESS_PENDING / ACCESS_ACTIVE）一律转 REVIEW_REQUIRED 且落库。
 *   6. 恢复失败必须原样透传且**不清库**：仓储还在，下一次启动才能重试。
 *   7. 动作号必须抬到 action_watermark + 1，溢出时回到 1（不能给设备送 0 号动作）。
 */

#include "domain_test_support.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---------- 小工具 ---------- */

/* 轮询等待 Core 线程发布第一份进度样本（它在线程启动后立刻发布，不需要 tick）。 */
static bool tc_wait_progress(locker_core_t *core, locker_progress_t *out)
{
    for (int i = 0; i < 400; ++i) {
        if (locker_core_get_progress(core, out) == LOCKER_STATUS_OK) {
            return true;
        }
        struct timespec pause;
        pause.tv_sec = 0;
        pause.tv_nsec = 2000000L;
        nanosleep(&pause, NULL);
    }
    return false;
}

/* 借还投影：closed=false 时 closed_at 为 null（恢复把它当有效借用）。 */
static bool tc_put_loan(dt_fixture_t *fixture, const locker_uuid_t *loan_id, const locker_uuid_t *tool_id,
                        const locker_uuid_t *borrower, bool closed)
{
    char key[LOCKER_DOMAIN_UUID_TEXT];
    char value[1024];
    locker_json_writer_t writer;

    dt_uuid_text(loan_id, key, sizeof(key));
    if (key[0] == '\0') {
        return false;
    }
    locker_domain_json_writer_init(&writer, value, sizeof(value));
    domain_write_loan(&writer, loan_id, tool_id, borrower, loan_id, closed ? loan_id : NULL, closed,
                      "2025-10-21T00:00:00Z", closed ? "2025-10-21T01:00:00Z" : NULL, closed);
    if (!locker_domain_json_writer_ok(&writer)) {
        return false;
    }
    return dt_repo_put(fixture->repo, "loans", key, value);
}

/* 已提交的导入水位：恢复时用它重建 cursor/ack_cursor。 */
static bool tc_put_import_cursor(dt_fixture_t *fixture, const locker_uuid_t *epoch, const char *device_id,
                                 uint64_t boot_id, uint64_t event_seq, bool continuity)
{
    locker_event_key_t through;
    char key[LOCKER_DOMAIN_UUID_TEXT];
    char value[512];
    locker_json_writer_t writer;

    memset(&through, 0, sizeof(through));
    memcpy(through.device_id, device_id, strlen(device_id) + 1u);
    through.journal_epoch = *epoch;
    through.boot_id = boot_id;
    through.event_seq = event_seq;
    dt_uuid_text(epoch, key, sizeof(key));
    if (key[0] == '\0') {
        return false;
    }
    locker_domain_json_writer_init(&writer, value, sizeof(value));
    domain_write_import_cursor(&writer, epoch, device_id, &through, continuity);
    if (!locker_domain_json_writer_ok(&writer)) {
        return false;
    }
    return dt_repo_put(fixture->repo, "import_cursors", key, value);
}

/* 上次设备快照：恢复只读 device.device_id 与 device.mode。 */
static bool tc_put_device_state(dt_fixture_t *fixture, const char *device_id, const char *mode)
{
    char value[512];
    int written = snprintf(value, sizeof(value),
                           "{\"device\":{\"device_id\":\"%s\",\"cabinet_name\":\"test-cabinet\","
                           "\"contract_version\":\"0.2.1\",\"simulated\":false,\"mode\":\"%s\"},"
                           "\"last_snapshot_revision\":\"3\",\"active_command_id\":null}",
                           device_id, mode);
    if (written <= 0 || (size_t)written >= sizeof(value)) {
        return false;
    }
    return dt_repo_put(fixture->repo, "device_state", device_id, value);
}

/* ---------- create / start / stop ---------- */

static void test_create_guards(void)
{
    test_clock_t clock;
    locker_core_config_t config;
    locker_core_t *core = NULL;
    locker_repo_t *repo;

    memset(&clock, 0, sizeof(clock));
    clock.monotonic_ms = 1000u;
    clock.wall_ms = 1761004800000ull;
    memset(&config, 0, sizeof(config));
    memcpy(config.cabinet_name, "test-cabinet", sizeof("test-cabinet"));
    dt_uuid_fill(&config.instance_id, 0x40u);
    config.clock = dt_clock_port(&clock);

    repo = (locker_repo_t *)calloc(1u, sizeof(struct locker_repo));
    TEST_CHECK(repo != NULL);
    if (repo == NULL) {
        return;
    }

    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_create(&config, repo, NULL, NULL, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_create(NULL, repo, NULL, NULL, &core));
    TEST_CHECK(core == NULL);
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_create(&config, NULL, NULL, NULL, &core));
    TEST_CHECK(core == NULL);

    /* 时钟端口缺函数指针：配置期就要 fail closed，不能等到第一次取时间才炸。 */
    locker_core_config_t bad_clock = config;
    memset(&bad_clock.clock, 0, sizeof(bad_clock.clock));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_create(&bad_clock, repo, NULL, NULL, &core));
    TEST_CHECK(core == NULL);

    /* 柜名缓冲被填满而没有 NUL：必须在 257 字节边界内判 INVALID，
       不能靠 strlen 读穿调用方结构体。 */
    locker_core_config_t long_name = config;
    memset(long_name.cabinet_name, 'x', sizeof(long_name.cabinet_name));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_create(&long_name, repo, NULL, NULL, &core));
    TEST_CHECK(core == NULL);

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_create(&config, repo, NULL, NULL, &core));
    TEST_CHECK(core != NULL);
    if (core != NULL) {
        /* queue_capacity 为 0 时取契约默认 64。 */
        TEST_CHECK_U64(LOCKER_CORE_QUEUE_CAPACITY, (uint64_t)locker_bounded_queue_capacity(&core->queue));
        TEST_CHECK_U64(LOCKER_COMPLETION_RETENTION_MS, core->completion_retention_ms);
        TEST_CHECK(core->phase == DOMAIN_PHASE_INIT);
        TEST_CHECK_U64(1u, core->next_action_no);
        TEST_CHECK(!core->thread_running);
        /* 未启动时 stop 是幂等的 OK，不是 NOT_READY。 */
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_stop(core, 0u));
        locker_core_destroy(core);
    }

    /* 显式容量按调用方给的值，不再套默认。 */
    locker_core_config_t small = config;
    small.queue_capacity = 8u;
    core = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_create(&small, repo, NULL, NULL, &core));
    TEST_CHECK(core != NULL);
    if (core != NULL) {
        TEST_CHECK_U64(8u, (uint64_t)locker_bounded_queue_capacity(&core->queue));
        locker_core_destroy(core);
    }

    /* destroy(NULL) 必须安全。 */
    locker_core_destroy(NULL);
    free(repo);
}

static void test_start_guards(void)
{
    dt_fixture_t fixture;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_start(NULL));

    /* 设备 sink 绑定失败必须原样透传，且线程不得启动。 */
    fixture.device->bind_status = LOCKER_STATUS_IO_ERROR;
    TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR, locker_core_start(fixture.core));
    TEST_CHECK(!fixture.core->thread_running);
    TEST_CHECK_U64(1u, (uint64_t)fixture.device->bind_calls);

    fixture.device->bind_status = LOCKER_STATUS_OK;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_start(fixture.core));
    TEST_CHECK(fixture.core->thread_running);
    TEST_CHECK_U64(2u, (uint64_t)fixture.device->bind_calls);

    /* 只允许一个所有者线程。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, locker_core_start(fixture.core));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_stop(fixture.core, LOCKER_HOST_TIMEOUT_MS));
    TEST_CHECK(!fixture.core->thread_running);
    TEST_CHECK(atomic_load_explicit(&fixture.core->thread_exited, memory_order_acquire));
    TEST_CHECK(fixture.core->stop_requested_joined);
    TEST_CHECK(fixture.core->phase == DOMAIN_PHASE_STOPPED);

    /* stop 之后可以重新 start（同一实例复用）。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_start(fixture.core));
    TEST_CHECK(fixture.core->thread_running);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_stop(fixture.core, LOCKER_HOST_TIMEOUT_MS));

    dt_fixture_free(&fixture);
}

static void test_lifecycle_thread(void)
{
    dt_fixture_t fixture;
    locker_progress_t progress;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    TEST_CHECK(!fixture.core->thread_running);
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_core_get_progress(fixture.core, &progress));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_start(fixture.core));
    /* 线程启动后立刻发布一份真实进度样本（不是合成自增计数）。 */
    TEST_CHECK(tc_wait_progress(fixture.core, &progress));
    TEST_CHECK_U64(LOCKER_PROGRESS_TARGET_SERVICE, progress.target);
    TEST_CHECK(locker_uuid_equal(&progress.instance_id, &fixture.core->instance_id));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_stop(fixture.core, LOCKER_HOST_TIMEOUT_MS));
    TEST_CHECK(!fixture.core->thread_running);
    TEST_CHECK(atomic_load_explicit(&fixture.core->thread_exited, memory_order_acquire));
    TEST_CHECK(fixture.core->phase == DOMAIN_PHASE_STOPPED);

    dt_fixture_free(&fixture);
}

/* ---------- 排队入口 ---------- */

static void test_submit_queue(void)
{
    dt_fixture_t fixture;
    locker_command_t command;
    locker_receipt_t receipt;
    locker_query_result_t result;
    locker_uuid_t unknown;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    memset(&command, 0, sizeof(command));
    command.principal = dt_principal(LOCKER_ROLE_USER, 0x50u);
    dt_uuid_fill(&command.request_id, 0x51u);
    memcpy(command.kind, LOCKER_CORE_DEVICE_GET, sizeof(LOCKER_CORE_DEVICE_GET));

    /* 未启动：一律 NOT_READY，绝不进无人消费的队列。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_submit(NULL, &command, &receipt));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_submit(fixture.core, NULL, &receipt));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_submit(fixture.core, &command, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_core_submit(fixture.core, &command, &receipt));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_start(fixture.core));

    memset(&receipt, 0, sizeof(receipt));
    TEST_CHECK_STATUS(LOCKER_STATUS_QUEUED, locker_core_submit(fixture.core, &command, &receipt));
    /* 排队成功只说明受理：状态是 QUEUED，没有操作号。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_QUEUED, receipt.status);
    TEST_CHECK(!receipt.has_operation);
    TEST_CHECK(!locker_uuid_is_nil(&receipt.submission_token));
    TEST_CHECK(locker_uuid_equal(&receipt.request_id, &command.request_id));

    /* 主身份没落库：Core 复检后拒绝，但结果必须通过 submission_token 取回。 */
    memset(&result, 0, sizeof(result));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_core_wait_result(fixture.core, &receipt.submission_token, 2000u, &result));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, result.status);

    /* 未知 token 不是空结果，而是 NOT_READY。 */
    dt_uuid_fill(&unknown, 0x99u);
    memset(&result, 0, sizeof(result));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_core_wait_result(fixture.core, &unknown, 0u, &result));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_wait_result(NULL, &unknown, 0u, &result));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_core_wait_result(fixture.core, NULL, 0u, &result));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_stop(fixture.core, LOCKER_HOST_TIMEOUT_MS));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_core_submit(fixture.core, &command, &receipt));

    dt_fixture_free(&fixture);
}

static void test_query_queue(void)
{
    dt_fixture_t fixture;
    locker_query_t query;
    locker_query_result_t result;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    memset(&query, 0, sizeof(query));
    query.principal = dt_principal(LOCKER_ROLE_USER, 0x60u);
    dt_uuid_fill(&query.request_id, 0x61u);
    memcpy(query.kind, LOCKER_CORE_DEVICE_GET, sizeof(LOCKER_CORE_DEVICE_GET));

    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_query(NULL, &query, &result));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_query(fixture.core, NULL, &result));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_query(fixture.core, &query, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_core_query(fixture.core, &query, &result));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_start(fixture.core));
    /* 查询走同一条队列并最多等 1000 ms；主身份未落库时结果是 INVALID 而不是超时。 */
    memset(&result, 0, sizeof(result));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_query(fixture.core, &query, &result));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, result.status);
    TEST_CHECK_U64(0u, result.length);
    TEST_CHECK_STRING("", result.json);

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_stop(fixture.core, LOCKER_HOST_TIMEOUT_MS));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_core_query(fixture.core, &query, &result));

    dt_fixture_free(&fixture);
}

/* ---------- 本卡显式 NOT_READY 的端口 ---------- */

static void test_authenticate_guards(void)
{
    dt_fixture_t fixture;
    locker_auth_request_t request;
    locker_principal_t principal;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    memset(&request, 0, sizeof(request));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_authenticate(NULL, &request, &principal));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_authenticate(fixture.core, NULL, &principal));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_authenticate(fixture.core, &request, NULL));

    /* 空 token 与装不下的 token 都是 INVALID，不是「校验失败」。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_authenticate(fixture.core, &request, &principal));
    request.token_length = (uint16_t)sizeof(request.token);
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_authenticate(fixture.core, &request, &principal));

    /* 真实凭据校验归 S1-05：本卡对任何合法形状的 token 都 NOT_READY。 */
    request.token_length = 8u;
    memcpy(request.token, "abcdefgh", 8u);
    memset(&principal, 0xff, sizeof(principal));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_core_authenticate(fixture.core, &request, &principal));
    TEST_CHECK(!principal.authenticated);
    TEST_CHECK(locker_uuid_is_nil(&principal.user_id));

    dt_fixture_free(&fixture);
}

static void test_private_entries(void)
{
    dt_fixture_t fixture;
    locker_peer_t peer;
    locker_presence_t presence;
    locker_maintenance_t maintenance;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    memset(&peer, 0, sizeof(peer));
    memset(&presence, 0, sizeof(presence));
    memset(&maintenance, 0, sizeof(maintenance));

    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_local_presence_submit(NULL, &peer, &presence));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_local_presence_submit(fixture.core, NULL, &presence));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_local_presence_submit(fixture.core, &peer, NULL));
    /* 未验证的本机 peer 不能提交到柜证明。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_local_presence_submit(fixture.core, &peer, &presence));
    peer.verified_ui_peer = true;
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_local_presence_submit(fixture.core, &peer, &presence));

    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_local_maintenance_reconcile(NULL, &peer, &maintenance));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_local_maintenance_reconcile(fixture.core, NULL, &maintenance));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_local_maintenance_reconcile(fixture.core, &peer, &maintenance));
    peer.verified_admin_peer = true;
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY,
                      locker_local_maintenance_reconcile(fixture.core, &peer, &maintenance));

    /* S4-04 拥有的端口：本卡绝不假装接受。 */
    locker_supervisor_health_t health;
    memset(&health, 0, sizeof(health));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_core_apply_supervisor_health(NULL, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_core_apply_supervisor_health(fixture.core, &health));

    dt_fixture_free(&fixture);
}

static void test_progress(void)
{
    dt_fixture_t fixture;
    locker_progress_t progress;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_get_progress(NULL, &progress));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_get_progress(fixture.core, NULL));
    /* 没有真实样本时保持未知，不合成自增计数。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_core_get_progress(fixture.core, &progress));

    domain_progress_publish(fixture.core);
    memset(&progress, 0, sizeof(progress));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_get_progress(fixture.core, &progress));
    TEST_CHECK_U64(1u, progress.progress);
    TEST_CHECK_U64(LOCKER_PROGRESS_TARGET_SERVICE, progress.target);
    TEST_CHECK_U64(0u, progress.observed_age_ms);
    TEST_CHECK(locker_uuid_equal(&progress.instance_id, &fixture.core->instance_id));

    /* 读进度不能推进计数。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_get_progress(fixture.core, &progress));
    TEST_CHECK_U64(1u, progress.progress);
    domain_progress_publish(fixture.core);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_get_progress(fixture.core, &progress));
    TEST_CHECK_U64(2u, progress.progress);

    dt_fixture_free(&fixture);
}

static void test_device_entry_guards(void)
{
    dt_fixture_t fixture;
    locker_device_event_t event;
    locker_device_reply_t reply;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    memset(&event, 0, sizeof(event));
    memset(&reply, 0, sizeof(reply));

    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_apply_device_event(NULL, &event));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_apply_device_event(fixture.core, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_core_apply_device_event(fixture.core, &event));

    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_apply_device_reply(NULL, &reply));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_apply_device_reply(fixture.core, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_core_apply_device_reply(fixture.core, &reply));

    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_core_tick(NULL, 1000u));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, locker_core_tick(fixture.core, 1000u));

    dt_fixture_free(&fixture);
}

/* ---------- 开放前恢复 ---------- */

static void test_recover_empty(void)
{
    dt_fixture_t fixture;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_recover(NULL));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_recover(fixture.core));
    TEST_CHECK(fixture.core->recovery_done);
    TEST_CHECK(fixture.core->recovery_ready);
    /* 恢复本身只把相位推到 RECOVERING；READY 由所有者线程在恢复成功后设置。 */
    TEST_CHECK(fixture.core->phase == DOMAIN_PHASE_RECOVERING);
    TEST_CHECK(!fixture.core->active.used);
    TEST_CHECK(!fixture.core->loan_open);
    TEST_CHECK(!fixture.core->persisted_state_valid);
    TEST_CHECK_U64(1u, fixture.core->next_action_no);
    /* 三个集合走恢复端口（operations/loans/import_cursors），
       device_state 走普通查询端口：一次取版本、一次取快照记录。 */
    TEST_CHECK_U64(3u, (uint64_t)fixture.repo->recovery_calls);
    TEST_CHECK_U64(2u, (uint64_t)fixture.repo->query_calls);
    TEST_CHECK_U64(0u, fixture.core->revision);

    dt_fixture_free(&fixture);
}

static void test_recover_operations(void)
{
    locker_uuid_t id;
    locker_uuid_t tool;
    locker_uuid_t actor;

    /* REQUESTED：截止时间按记录里的 updated_at 还原，不是 now + TTL。 */
    {
        dt_fixture_t fixture;
        TEST_CHECK(dt_fixture_init(&fixture, false));
        if (fixture.core == NULL) {
            dt_fixture_free(&fixture);
            return;
        }
        dt_uuid_fill(&id, 0x21u);
        dt_uuid_fill(&tool, 0x22u);
        dt_uuid_fill(&actor, 0x23u);
        TEST_CHECK(dt_seed_operation(&fixture, &id, &tool, &actor, LOCKER_OPERATION_KIND_BORROW,
                                     LOCKER_OPERATION_STATE_REQUESTED, 2u, "2025-10-21T00:00:00Z"));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_recover(fixture.core));
        TEST_CHECK(fixture.core->active.used);
        TEST_CHECK(locker_uuid_equal(&fixture.core->active.id, &id));
        TEST_CHECK_U64(LOCKER_OPERATION_STATE_REQUESTED, fixture.core->active.state);
        TEST_CHECK_U64(1761004800000ull + LOCKER_REQUEST_TTL_MS, fixture.core->active.requested_deadline_ms);
        dt_fixture_free(&fixture);
    }

    /* WAIT_CONFIRM：确认截止时间同样按记录时间还原。 */
    {
        dt_fixture_t fixture;
    dt_fixture_free(&fixture);
        TEST_CHECK(dt_fixture_init(&fixture, false));
        if (fixture.core == NULL) {
            dt_fixture_free(&fixture);
            return;
        }
        dt_uuid_fill(&id, 0x31u);
        dt_uuid_fill(&tool, 0x32u);
        dt_uuid_fill(&actor, 0x33u);
        TEST_CHECK(dt_seed_operation(&fixture, &id, &tool, &actor, LOCKER_OPERATION_KIND_BORROW,
                                     LOCKER_OPERATION_STATE_WAIT_CONFIRM, 4u, "2025-10-21T00:00:00Z"));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_recover(fixture.core));
        TEST_CHECK(fixture.core->active.used);
        TEST_CHECK_U64(LOCKER_OPERATION_STATE_WAIT_CONFIRM, fixture.core->active.state);
        TEST_CHECK_U64(1761004800000ull + LOCKER_WAIT_CONFIRM_MS,
                       fixture.core->active.wait_confirm_deadline_ms);
        dt_fixture_free(&fixture);
    }

    /* 终态历史不重建工作副本。 */
    {
        dt_fixture_t fixture;
    dt_fixture_free(&fixture);
        TEST_CHECK(dt_fixture_init(&fixture, false));
        if (fixture.core == NULL) {
            dt_fixture_free(&fixture);
            return;
        }
        dt_uuid_fill(&id, 0x41u);
        dt_uuid_fill(&tool, 0x42u);
        dt_uuid_fill(&actor, 0x43u);
        TEST_CHECK(dt_seed_operation(&fixture, &id, &tool, &actor, LOCKER_OPERATION_KIND_BORROW,
                                     LOCKER_OPERATION_STATE_COMPLETED, 6u, "2025-10-21T00:00:00Z"));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_recover(fixture.core));
        TEST_CHECK(!fixture.core->active.used);
        dt_fixture_free(&fixture);
    }
}

static void test_recover_access_pending_review(void)
{
    dt_fixture_t fixture;
    locker_uuid_t id;
    locker_uuid_t tool;
    locker_uuid_t actor;
    char key[LOCKER_DOMAIN_UUID_TEXT];
    dt_doc_t *doc;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    dt_uuid_fill(&id, 0x51u);
    dt_uuid_fill(&tool, 0x52u);
    dt_uuid_fill(&actor, 0x53u);
    TEST_CHECK(dt_seed_operation(&fixture, &id, &tool, &actor, LOCKER_OPERATION_KIND_BORROW,
                                 LOCKER_OPERATION_STATE_ACCESS_PENDING, 3u, "2025-10-21T00:00:00Z"));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_recover(fixture.core));
    /* 可能已发送的意图：进核验，绝不重放发送。 */
    TEST_CHECK(fixture.core->active.used);
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, fixture.core->active.state);
    TEST_CHECK_STRING("RECOVERED_AFTER_RESTART", fixture.core->active.reason_code);
    TEST_CHECK(!fixture.core->active.send_started);
    TEST_CHECK_U64(4u, fixture.core->active.revision);
    TEST_CHECK_U64(1u, (uint64_t)fixture.repo->commit_calls);
    TEST_CHECK_U64(1u, fixture.core->revision);

    dt_uuid_text(&id, key, sizeof(key));
    doc = dt_repo_find(fixture.repo, "operations", key);
    TEST_CHECK(doc != NULL);
    if (doc != NULL) {
        TEST_CHECK(test_text_contains(doc->value, "\"state\":\"REVIEW_REQUIRED\""));
        TEST_CHECK(test_text_contains(doc->value, "RECOVERED_AFTER_RESTART"));
    }

    dt_fixture_free(&fixture);
}

static void test_recover_loans(void)
{
    locker_uuid_t loan_id;
    locker_uuid_t tool;
    locker_uuid_t borrower;

    {
        dt_fixture_t fixture;
        TEST_CHECK(dt_fixture_init(&fixture, false));
        if (fixture.core == NULL) {
            dt_fixture_free(&fixture);
            return;
        }
        dt_uuid_fill(&loan_id, 0x61u);
        dt_uuid_fill(&tool, 0x62u);
        dt_uuid_fill(&borrower, 0x63u);
        TEST_CHECK(tc_put_loan(&fixture, &loan_id, &tool, &borrower, false));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_recover(fixture.core));
        TEST_CHECK(fixture.core->loan_open);
        TEST_CHECK(locker_uuid_equal(&fixture.core->loan_id, &loan_id));
        TEST_CHECK(locker_uuid_equal(&fixture.core->loan_tool_id, &tool));
        TEST_CHECK(locker_uuid_equal(&fixture.core->loan_borrower_id, &borrower));
        TEST_CHECK_STRING("2025-10-21T00:00:00Z", fixture.core->loan_opened_at);
        dt_fixture_free(&fixture);
    }

    /* 已关闭的借用不是有效借用。 */
    {
        dt_fixture_t fixture;
    dt_fixture_free(&fixture);
        TEST_CHECK(dt_fixture_init(&fixture, false));
        if (fixture.core == NULL) {
            dt_fixture_free(&fixture);
            return;
        }
        dt_uuid_fill(&loan_id, 0x71u);
        dt_uuid_fill(&tool, 0x72u);
        dt_uuid_fill(&borrower, 0x73u);
        TEST_CHECK(tc_put_loan(&fixture, &loan_id, &tool, &borrower, true));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_recover(fixture.core));
        TEST_CHECK(!fixture.core->loan_open);
        TEST_CHECK(locker_uuid_is_nil(&fixture.core->loan_id));
        dt_fixture_free(&fixture);
    }
}

static void test_recover_import_cursor(void)
{
    dt_fixture_t fixture;
    locker_uuid_t epoch;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    dt_uuid_fill(&epoch, 0x80u);
    TEST_CHECK(tc_put_import_cursor(&fixture, &epoch, DT_DEVICE_ID, 3u, 42u, false));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_recover(fixture.core));
    TEST_CHECK(fixture.core->import_cursor_valid);
    TEST_CHECK(!fixture.core->import_continuity);
    TEST_CHECK(fixture.core->cursor.valid);
    TEST_CHECK(!fixture.core->cursor.continuity_ok);
    TEST_CHECK_U64(3u, fixture.core->cursor.through.boot_id);
    TEST_CHECK_U64(42u, fixture.core->cursor.through.event_seq);
    TEST_CHECK(locker_uuid_equal(&fixture.core->cursor.journal_epoch, &epoch));
    /* 已提交水位同时就是可确认水位：恢复后立刻允许 ACK，不必等新事件。 */
    TEST_CHECK(fixture.core->ack_cursor_valid);
    TEST_CHECK_U64(42u, fixture.core->ack_cursor.event_seq);

    dt_fixture_free(&fixture);
}

static void test_recover_device_state(void)
{
    dt_fixture_t fixture;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    TEST_CHECK(tc_put_device_state(&fixture, DT_DEVICE_ID, "READY"));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_recover(fixture.core));
    TEST_CHECK(fixture.core->persisted_state_valid);
    TEST_CHECK_STRING(DT_DEVICE_ID, fixture.core->persisted_device_id);
    TEST_CHECK_U64(LOCKER_DEVICE_MODE_READY, fixture.core->persisted_mode);
    /* 恢复不伪造设备会话：没有快照就还没有 READY 的设备。 */
    TEST_CHECK(!fixture.core->device_session_up);
    TEST_CHECK(!fixture.core->device_snapshot_valid);

    dt_fixture_free(&fixture);
}

static void test_recover_failure_keeps_repo(void)
{
    dt_fixture_t fixture;
    locker_uuid_t id;
    locker_uuid_t tool;
    locker_uuid_t actor;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    dt_uuid_fill(&id, 0x91u);
    dt_uuid_fill(&tool, 0x92u);
    dt_uuid_fill(&actor, 0x93u);
    TEST_CHECK(dt_seed_operation(&fixture, &id, &tool, &actor, LOCKER_OPERATION_KIND_BORROW,
                                 LOCKER_OPERATION_STATE_REQUESTED, 2u, "2025-10-21T00:00:00Z"));

    fixture.repo->query_status = LOCKER_STATUS_IO_ERROR;
    TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR, domain_recover(fixture.core));
    TEST_CHECK(!fixture.core->recovery_done);
    TEST_CHECK(!fixture.core->recovery_ready);
    TEST_CHECK(fixture.core->phase == DOMAIN_PHASE_RECOVERING);
    /* 恢复失败不清库：仓储里的记录原样还在，下一次启动可以重试。 */
    TEST_CHECK(dt_repo_find(fixture.repo, "operations", "dummy") == NULL);
    TEST_CHECK_U64(1u, (uint64_t)fixture.repo->query_calls);
    fixture.repo->query_status = LOCKER_STATUS_OK;
    TEST_CHECK(dt_repo_find(fixture.repo, "operations", "dummy") == NULL);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_recover(fixture.core));
    TEST_CHECK(fixture.core->recovery_done);
    TEST_CHECK(fixture.core->active.used);

    dt_fixture_free(&fixture);
}

static void test_recover_action_watermark(void)
{
    dt_fixture_t fixture;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    /* 设备已见 7 号动作：下一个必须是 8，复用旧号会被设备判 REPLAY_BLOCKED。 */
    fixture.core->action_watermark = 7u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_recover(fixture.core));
    TEST_CHECK_U64(8u, fixture.core->next_action_no);
    dt_fixture_free(&fixture);

    /* 水位到顶时回落到 1，绝不产生 0 号动作。 */
    TEST_CHECK(dt_fixture_init(&fixture, false));
    if (fixture.core == NULL) {
        dt_fixture_free(&fixture);
        return;
    }
    fixture.core->action_watermark = UINT64_MAX;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_recover(fixture.core));
    TEST_CHECK_U64(1u, fixture.core->next_action_no);
    dt_fixture_free(&fixture);
}

int main(void)
{
    test_create_guards();
    test_start_guards();
    test_lifecycle_thread();
    test_submit_queue();
    test_query_queue();
    test_authenticate_guards();
    test_private_entries();
    test_progress();
    test_device_entry_guards();
    test_recover_empty();
    test_recover_operations();
    test_recover_access_pending_review();
    test_recover_loans();
    test_recover_import_cursor();
    test_recover_device_state();
    test_recover_failure_keeps_repo();
    test_recover_action_watermark();
    TEST_REPORT("domain.test_domain_core");
}
