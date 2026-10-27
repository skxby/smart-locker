#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 单元测试：Core 的仓储访问层与完成槽（domain_repo.c）。
 *
 * 这里钉死两类东西：
 *   1. 查询/分页的边界（collection 32、key 193、limit 1..100、recovery 只走
 *      load_recovery），以及仓储返回畸形 JSON 时必须 IO_ERROR 而不是「当作空页」。
 *   2. RepositoryChange 的字节形状：expected/new 必须是 U64 十进制**字符串**，
 *      checks 先于 mutations，提交成功才推进 core->revision。
 *   3. 完成槽的保留语义：终态保留到保留期，超时不取消（domain_repo.c:287-289）。
 */

#include "domain_test_support.h"

#include <stdio.h>
#include <string.h>

#define RT_DEVICE_STATE_KEY DT_DEVICE_ID

static void test_repo_doc_lookup(void)
{
    dt_fixture_t fixture;
    domain_doc_t doc;
    char value[256];
    size_t i;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);

    /* 命中：value 指向 documents[0].value。 */
    TEST_CHECK(dt_repo_put(fixture.repo, "operations", "k1", "{\"a\":1}"));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_repo_doc(fixture.core, 0, "operations", "k1", &doc));
    TEST_CHECK(doc.found == true);
    TEST_CHECK(doc.value != NULL);
    TEST_CHECK(doc.value->type == LOCKER_JSON_OBJECT);
    TEST_CHECK(locker_domain_json_render(&doc.doc, doc.value, value, sizeof(value), NULL));
    TEST_CHECK_STRING("{\"a\":1}", value);

    /* 未命中：found=false、value=NULL，不是错误。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_repo_doc(fixture.core, 0, "operations", "missing", &doc));
    TEST_CHECK(doc.found == false);
    TEST_CHECK(doc.value == NULL);

    /* 两个槽各自独立（doc_nodes[2]），交错使用不互相覆盖。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_repo_doc(fixture.core, 1, "operations", "k1", &doc));
    TEST_CHECK(doc.found == true);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_repo_doc(fixture.core, 0, "operations", "k1", &doc));
    TEST_CHECK(doc.found == true);

    /* 参数与边界。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_repo_doc(fixture.core, 2, "operations", "k1", &doc));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_repo_doc(fixture.core, -1, "operations", "k1", &doc));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_repo_doc(fixture.core, 0, "operations", "k1", NULL));

    /* collection 32 字节（含 NUL 就放不下）→ INVALID。 */
    char long_collection[40];
    for (i = 0; i < 32u; i++) {
        long_collection[i] = 'c';
    }
    long_collection[32] = '\0';
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_repo_doc(fixture.core, 0, long_collection, "k1", &doc));
    long_collection[31] = '\0';
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_repo_doc(fixture.core, 0, long_collection, "k1", &doc));

    /* key 193 字节上限。 */
    char long_key[LOCKER_DOMAIN_KEY_TEXT + 8u];
    for (i = 0; i < sizeof(long_key) - 1u; i++) {
        long_key[i] = 'k';
    }
    long_key[192] = '\0';
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_repo_doc(fixture.core, 0, "operations", long_key, &doc));
    long_key[192] = 'k';
    long_key[193] = '\0';
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_repo_doc(fixture.core, 0, "operations", long_key, &doc));

    /* 仓储错误原样上抛（不吞成 NOT_READY / 空页）。 */
    fixture.repo->query_status = LOCKER_STATUS_IO_ERROR;
    TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR,
                      domain_repo_doc(fixture.core, 0, "operations", "k1", &doc));
    fixture.repo->query_status = LOCKER_STATUS_OK;

    /* 仓储返回的 JSON 畸形（value 里塞非法 JSON 会把整页弄坏）→ IO_ERROR。 */
    TEST_CHECK(dt_repo_put(fixture.repo, "operations", "broken", "{not json"));
    TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR,
                      domain_repo_doc(fixture.core, 0, "operations", "broken", &doc));

    dt_fixture_free(&fixture);
}

static void test_repo_page(void)
{
    dt_fixture_t fixture;
    domain_doc_t doc;
    const locker_json_node_t *root;
    const locker_json_node_t *documents;
    char text[64];

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);

    TEST_CHECK(dt_repo_put(fixture.repo, "operations", "a", "{\"n\":1}"));
    TEST_CHECK(dt_repo_put(fixture.repo, "operations", "b", "{\"n\":2}"));
    TEST_CHECK(dt_repo_put(fixture.repo, "operations", "c", "{\"n\":3}"));
    TEST_CHECK(dt_repo_put(fixture.repo, "operations", "d", "{\"n\":4}"));
    TEST_CHECK(dt_repo_put(fixture.repo, "operations", "e", "{\"n\":5}"));

    /* 分页：limit 决定页大小，next_after_key 指向下一页起点。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_repo_page(fixture.core, "operations", NULL, 2u, &doc));
    TEST_CHECK(doc.found == true);
    root = doc.value;
    documents = locker_domain_json_get(&doc.doc, root, "documents");
    TEST_CHECK(documents != NULL);
    TEST_CHECK_U64(2u, (uint64_t)documents->count);
    TEST_CHECK(locker_domain_json_text(
        &doc.doc, locker_domain_json_get(&doc.doc, root, "next_after_key"), text, sizeof(text),
        NULL));
    TEST_CHECK_STRING("b", text);

    /* 从 after_key 续取。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_repo_page(fixture.core, "operations", "b", 2u, &doc));
    root = doc.value;
    documents = locker_domain_json_get(&doc.doc, root, "documents");
    TEST_CHECK_U64(2u, (uint64_t)documents->count);

    /* 空集合：documents 为空数组，next_after_key 为 null。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_repo_page(fixture.core, "loans", NULL, 10u, &doc));
    documents = locker_domain_json_get(&doc.doc, doc.value, "documents");
    TEST_CHECK(documents != NULL);
    TEST_CHECK_U64(0u, (uint64_t)documents->count);
    TEST_CHECK(locker_domain_json_get(&doc.doc, doc.value, "next_after_key")->type ==
               LOCKER_JSON_NULL);

    /* 边界：collection/after_key 超长、out 为 NULL。 */
    char long_collection[40];
    memset(long_collection, 'c', sizeof(long_collection));
    long_collection[32] = '\0';
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_repo_page(fixture.core, long_collection, NULL, 10u, &doc));
    char long_after[LOCKER_DOMAIN_KEY_TEXT + 8u];
    memset(long_after, 'k', sizeof(long_after));
    long_after[193] = '\0';
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_repo_page(fixture.core, "operations", long_after, 10u, &doc));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_repo_page(fixture.core, "operations", NULL, 10u, NULL));

    dt_fixture_free(&fixture);
}

static void test_repo_recovery_page(void)
{
    dt_fixture_t fixture;
    domain_doc_t doc;
    const locker_json_node_t *collection;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);
    TEST_CHECK(dt_repo_put(fixture.repo, "operations", "a", "{\"n\":1}"));

    /* recovery 走 load_recovery，并且结果里带 collection 字段（x-rules 10）。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_repo_recovery_page(fixture.core, "operations", NULL, 50u, &doc));
    TEST_CHECK_U64(1u, (uint64_t)fixture.repo->recovery_calls);
    TEST_CHECK_U64(0u, (uint64_t)fixture.repo->query_calls);
    collection = locker_domain_json_get(&doc.doc, doc.value, "collection");
    TEST_CHECK(collection != NULL);
    TEST_CHECK(collection->type == LOCKER_JSON_STRING);

    /* limit 必须 1..100。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_repo_recovery_page(fixture.core, "operations", NULL, 0u, &doc));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_repo_recovery_page(fixture.core, "operations", NULL, 101u, &doc));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_repo_recovery_page(fixture.core, "operations", NULL, 100u, &doc));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_repo_recovery_page(fixture.core, NULL, NULL, 10u, &doc));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_repo_recovery_page(fixture.core, "operations", NULL, 10u, NULL));

    /* 仓储错误上抛。 */
    fixture.repo->recovery_status = LOCKER_STATUS_NOT_READY;
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY,
                      domain_repo_recovery_page(fixture.core, "operations", NULL, 10u, &doc));

    dt_fixture_free(&fixture);
}

static void test_repo_refresh_revision(void)
{
    dt_fixture_t fixture;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);
    fixture.repo->revision = 4242u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_repo_refresh_revision(fixture.core));
    TEST_CHECK_U64(4242u, fixture.core->revision);

    fixture.repo->query_status = LOCKER_STATUS_IO_ERROR;
    TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR, domain_repo_refresh_revision(fixture.core));
    /* 失败不改 revision（不把 0 当成真实版本）。 */
    TEST_CHECK_U64(4242u, fixture.core->revision);

    dt_fixture_free(&fixture);
}

static void test_change_bytes(void)
{
    dt_fixture_t fixture;
    domain_change_t change;
    char expected[2048];

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);

    /* checks 先、mutations 后；U64 是十进制字符串。 */
    domain_change_init(fixture.core, &change, 0u, 1u);
    TEST_CHECK(domain_change_check(&change, "slot_guards", "1", "ABSENT"));
    TEST_CHECK(domain_change_put_begin(&change, "operations", "op-1"));
    locker_domain_json_literal(&change.writer, "{\"x\":1}");
    domain_change_put_end(&change);
    TEST_CHECK(domain_change_delete(&change, "loans", "loan-1"));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_change_commit(fixture.core, &change));

    snprintf(expected, sizeof(expected),
             "{\"schema_version\":1,\"expected_snapshot_revision\":\"0\","
             "\"new_snapshot_revision\":\"1\","
             "\"checks\":[{\"collection\":\"slot_guards\",\"key\":\"1\",\"condition\":\"ABSENT\"}],"
             "\"mutations\":["
             "{\"op\":\"PUT\",\"document\":{\"collection\":\"operations\",\"key\":\"op-1\","
             "\"value\":{\"x\":1}}},"
             "{\"op\":\"DELETE\",\"collection\":\"loans\",\"key\":\"loan-1\"}]}");
    TEST_CHECK_STRING(expected, fixture.repo->last_change);
    TEST_CHECK_U64((uint64_t)strlen(expected), (uint64_t)fixture.repo->last_change_length);
    /* 提交成功才推进内存 revision。 */
    TEST_CHECK_U64(1u, fixture.core->revision);
    TEST_CHECK_U64(1u, fixture.repo->revision);

    /* 第二次提交的 expected 必须跟着走。 */
    domain_change_init(fixture.core, &change, fixture.core->revision, fixture.core->revision + 1u);
    TEST_CHECK(domain_change_put_begin(&change, "operations", "op-2"));
    locker_domain_json_literal(&change.writer, "{\"x\":2}");
    domain_change_put_end(&change);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_change_commit(fixture.core, &change));
    TEST_CHECK_U64(2u, fixture.core->revision);
    TEST_CHECK(strstr(fixture.repo->last_change, "\"expected_snapshot_revision\":\"1\"") != NULL);
    TEST_CHECK(strstr(fixture.repo->last_change, "\"new_snapshot_revision\":\"2\"") != NULL);

    /* 无 mutation 的 change 不合法（schema minItems:1）。 */
    domain_change_init(fixture.core, &change, fixture.core->revision, fixture.core->revision + 1u);
    TEST_CHECK(domain_change_check(&change, "slot_guards", "1", "ABSENT"));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_change_commit(fixture.core, &change));

    dt_fixture_free(&fixture);
}

static void test_change_limits(void)
{
    dt_fixture_t fixture;
    domain_change_t change;
    uint32_t i;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);

    /* 32 条 checks 是上限，第 33 条把 change 置 broken。 */
    domain_change_init(fixture.core, &change, 0u, 1u);
    for (i = 0; i < LOCKER_DOMAIN_MAX_CHECKS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "k%u", i);
        TEST_CHECK(domain_change_check(&change, "c", key, "ABSENT"));
    }
    TEST_CHECK(change.broken == false);
    TEST_CHECK(!domain_change_check(&change, "c", "overflow", "ABSENT"));
    TEST_CHECK(change.broken == true);
    TEST_CHECK_STATUS(LOCKER_STATUS_NO_SPACE, domain_change_commit(fixture.core, &change));
    TEST_CHECK_U64(0u, fixture.core->revision);

    /* mutations 同样 32 条上限。 */
    domain_change_init(fixture.core, &change, 0u, 1u);
    for (i = 0; i < LOCKER_DOMAIN_MAX_MUTATIONS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "k%u", i);
        TEST_CHECK(domain_change_delete(&change, "c", key));
    }
    TEST_CHECK(change.broken == false);
    TEST_CHECK(!domain_change_delete(&change, "c", "overflow"));
    TEST_CHECK(change.broken == true);
    TEST_CHECK_STATUS(LOCKER_STATUS_NO_SPACE, domain_change_commit(fixture.core, &change));

    /* NULL 参数一律 false，不产生半截 JSON。 */
    domain_change_init(fixture.core, &change, 0u, 1u);
    TEST_CHECK(!domain_change_check(&change, NULL, "k", "ABSENT"));
    TEST_CHECK(!domain_change_check(&change, "c", NULL, "ABSENT"));
    TEST_CHECK(!domain_change_check(&change, "c", "k", NULL));
    TEST_CHECK(!domain_change_put_begin(&change, NULL, "k"));
    TEST_CHECK(!domain_change_delete(&change, "c", NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_change_commit(fixture.core, NULL));
    domain_change_put_end(NULL);
    domain_change_init(fixture.core, NULL, 0u, 1u);

    dt_fixture_free(&fixture);
}

static void test_change_conflict(void)
{
    dt_fixture_t fixture;
    domain_change_t change;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);

    /* 仓储 revision 领先 → CONFLICT，且内存 revision 不动。 */
    fixture.repo->revision = 5u;
    domain_change_init(fixture.core, &change, 0u, 1u);
    TEST_CHECK(domain_change_delete(&change, "c", "k"));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, domain_change_commit(fixture.core, &change));
    TEST_CHECK_U64(0u, fixture.core->revision);
    /* 冲突时仓储不应改 revision。 */
    TEST_CHECK_U64(5u, fixture.repo->revision);

    /* checks 不满足 → CONFLICT。 */
    fixture.repo->revision = 0u;
    TEST_CHECK(dt_repo_put(fixture.repo, "slot_guards", "1", "{}"));
    domain_change_init(fixture.core, &change, 0u, 1u);
    TEST_CHECK(domain_change_check(&change, "slot_guards", "1", "ABSENT"));
    TEST_CHECK(domain_change_delete(&change, "c", "k"));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, domain_change_commit(fixture.core, &change));
    TEST_CHECK_U64(0u, fixture.core->revision);

    /* 未知 condition 不是 CONFLICT 而是 INVALID（不认识的守卫不能当通过）。 */
    domain_change_init(fixture.core, &change, 0u, 1u);
    TEST_CHECK(domain_change_check(&change, "slot_guards", "1", "MAYBE"));
    TEST_CHECK(domain_change_delete(&change, "c", "k"));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_change_commit(fixture.core, &change));

    dt_fixture_free(&fixture);
}

static void test_completion_slots(void)
{
    dt_fixture_t fixture;
    locker_uuid_t tokens[LOCKER_DOMAIN_COMPLETION_SLOTS];
    uint32_t slots[LOCKER_DOMAIN_COMPLETION_SLOTS];
    uint32_t i;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);

    /* 32 个槽，逐个分配；token 互不相同。 */
    for (i = 0; i < LOCKER_DOMAIN_COMPLETION_SLOTS; i++) {
        slots[i] = domain_completion_acquire(fixture.core, &tokens[i]);
        TEST_CHECK(slots[i] != UINT32_MAX);
        TEST_CHECK(!locker_uuid_is_nil(&tokens[i]));
        TEST_CHECK_U64(i, (uint64_t)slots[i]);
    }
    /* 第 33 个必须失败（BUSY），不能覆盖已有槽。 */
    locker_uuid_t extra;
    TEST_CHECK_U64(UINT32_MAX, (uint64_t)domain_completion_acquire(fixture.core, &extra));

    /* 释放后可重新分配。 */
    domain_completion_release(fixture.core, slots[3]);
    locker_uuid_t reused;
    uint32_t reused_slot = domain_completion_acquire(fixture.core, &reused);
    TEST_CHECK_U64(3u, (uint64_t)reused_slot);
    TEST_CHECK(!locker_uuid_equal(&reused, &tokens[3]));

    /* 参数边界。 */
    TEST_CHECK_U64(UINT32_MAX, (uint64_t)domain_completion_acquire(fixture.core, NULL));
    TEST_CHECK_U64(UINT32_MAX, (uint64_t)domain_completion_acquire(NULL, &extra));
    domain_completion_release(fixture.core, LOCKER_DOMAIN_COMPLETION_SLOTS);
    domain_completion_release(NULL, 0u);

    for (i = 0; i < LOCKER_DOMAIN_COMPLETION_SLOTS; i++) {
        domain_completion_release(fixture.core, slots[i]);
    }
    dt_fixture_free(&fixture);
}

static void test_completion_wait(void)
{
    dt_fixture_t fixture;
    locker_uuid_t token;
    uint32_t slot;
    locker_status_t accept_status = LOCKER_STATUS_OK;
    locker_uuid_t operation;
    bool has_operation = true;
    locker_query_result_t result;
    locker_principal_t principal;
    locker_principal_t taken;
    char json[64];
    size_t length = 0u;
    uint64_t revision = 0u;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);

    slot = domain_completion_acquire(fixture.core, &token);
    TEST_CHECK(slot != UINT32_MAX);
    TEST_CHECK_U64(slot, (uint64_t)domain_completion_find(fixture.core, &token));

    /* 还没终态时 wait_final 立刻返回 NOT_READY（超时不取消，domain_repo.c:299）。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY,
                      domain_completion_wait_final(fixture.core, slot, 0u, &result));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY,
                      domain_completion_wait_accept(fixture.core, slot, 0u, &accept_status, &operation,
                                                    &has_operation));

    /* accept 只带 status/operation，不带 JSON。 */
    dt_uuid_fill(&operation, 0x33u);
    domain_completion_accept(fixture.core, slot, LOCKER_STATUS_ACCEPTED, &operation, true);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_completion_wait_accept(fixture.core, slot, 0u, &accept_status, &operation,
                                                    &has_operation));
    TEST_CHECK_STATUS(LOCKER_STATUS_ACCEPTED, accept_status);
    TEST_CHECK(has_operation == true);
    TEST_CHECK_U64(0x33u, (uint64_t)operation.bytes[0]);

    /* finish 带终态与 JSON；take_json 取到同一份。 */
    domain_completion_finish(fixture.core, slot, LOCKER_STATUS_OK, "{\"ok\":true}", 11u, 7u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_completion_wait_final(fixture.core, slot, 0u, &result));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, result.status);
    TEST_CHECK_U64(7u, result.snapshot_revision);
    TEST_CHECK_U64(11u, (uint64_t)result.length);
    TEST_CHECK_STRING("{\"ok\":true}", result.json);

    TEST_CHECK(domain_completion_take_json(fixture.core, slot, json, sizeof(json), &length, &revision));
    TEST_CHECK_STRING("{\"ok\":true}", json);
    TEST_CHECK_U64(11u, (uint64_t)length);
    TEST_CHECK_U64(7u, revision);
    /* 缓冲不足整体失败。 */
    TEST_CHECK(!domain_completion_take_json(fixture.core, slot, json, 4u, &length, &revision));

    /* principal 存取。 */
    principal = dt_principal(LOCKER_ROLE_ADMIN, 0x50u);
    domain_completion_set_principal(fixture.core, slot, &principal);
    TEST_CHECK(domain_completion_take_principal(fixture.core, slot, &taken));
    TEST_CHECK(locker_uuid_equal(&taken.user_id, &principal.user_id));
    TEST_CHECK(taken.role == LOCKER_ROLE_ADMIN);

    /* 未设置的槽取不到 principal。 */
    locker_uuid_t other_token;
    uint32_t other = domain_completion_acquire(fixture.core, &other_token);
    TEST_CHECK(!domain_completion_take_principal(fixture.core, other, &taken));
    domain_completion_release(fixture.core, other);

    /* find 对未知 token 返回 UINT32_MAX。 */
    locker_uuid_t unknown;
    dt_uuid_fill(&unknown, 0x77u);
    TEST_CHECK_U64(UINT32_MAX, (uint64_t)domain_completion_find(fixture.core, &unknown));

    /* 槽越界/空指针的等待一律 INVALID。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_completion_wait_final(fixture.core, LOCKER_DOMAIN_COMPLETION_SLOTS, 0u,
                                                   &result));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_completion_wait_accept(fixture.core, slot, 0u, NULL, NULL, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_completion_wait_final(NULL, 0u, 0u, &result));

    /* 空 JSON 的终态：take_json 失败但 wait_final 成功。 */
    uint32_t empty = domain_completion_acquire(fixture.core, &other_token);
    domain_completion_finish(fixture.core, empty, LOCKER_STATUS_INVALID, NULL, 0u, 9u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_completion_wait_final(fixture.core, empty, 0u, &result));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, result.status);
    TEST_CHECK_U64(9u, result.snapshot_revision);
    TEST_CHECK(!domain_completion_take_json(fixture.core, empty, json, sizeof(json), &length,
                                            &revision));

    domain_completion_release(fixture.core, slot);
    domain_completion_release(fixture.core, empty);
    dt_fixture_free(&fixture);
}

static void test_completion_reap(void)
{
    dt_fixture_t fixture;
    locker_uuid_t token;
    uint32_t slot;
    locker_query_result_t result;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);

    slot = domain_completion_acquire(fixture.core, &token);
    domain_completion_finish(fixture.core, slot, LOCKER_STATUS_OK, "{}", 2u, 1u);
    /* expires_ms = domain_now(1000) + completion_retention_ms(60000) = 61000。 */
    domain_completion_reap(fixture.core, 60999u);
    TEST_CHECK_U64(slot, (uint64_t)domain_completion_find(fixture.core, &token));
    domain_completion_reap(fixture.core, 61000u);
    TEST_CHECK_U64(UINT32_MAX, (uint64_t)domain_completion_find(fixture.core, &token));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY,
                      domain_completion_wait_final(fixture.core, slot, 0u, &result));

    /* 未到终态的槽不被回收（不能把在途请求的槽收走）。 */
    locker_uuid_t pending_token;
    uint32_t pending = domain_completion_acquire(fixture.core, &pending_token);
    domain_completion_reap(fixture.core, 200000u);
    TEST_CHECK_U64(pending, (uint64_t)domain_completion_find(fixture.core, &pending_token));
    domain_completion_release(fixture.core, pending);
    domain_completion_reap(NULL, 0u);

    dt_fixture_free(&fixture);
}

int main(void)
{
    test_repo_doc_lookup();
    test_repo_page();
    test_repo_recovery_page();
    test_repo_refresh_revision();
    test_change_bytes();
    test_change_limits();
    test_change_conflict();
    test_completion_slots();
    test_completion_wait();
    test_completion_reap();
    TEST_REPORT("domain.test_domain_repo");
}
