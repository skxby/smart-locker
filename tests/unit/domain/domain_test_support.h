#define _POSIX_C_SOURCE 200809L

/* S1-04 业务核心测试支撑：假仓储（4 个端口）、假设备（10 个端口）、假发布器、
   可注入测试时钟，以及把 Core 直接推到「READY 且设备已就绪」的夹具。

   这些替身只存在于测试二进制里：host/domain 的公共 API 不暴露任何 setter，
   生产代码拿不到它们的入口（host/common/include/locker/common/clock.h:9-12）。 */

#ifndef DOMAIN_TEST_SUPPORT_H
#define DOMAIN_TEST_SUPPORT_H

#include "domain_internal.h"
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#define LOCKER_TEST_HELPER __attribute__((unused))
#else
#define LOCKER_TEST_HELPER
#endif

#define DT_MAX_DOCS 64u
#define DT_VALUE_BYTES 8192u
#define DT_POOL_NODES 2048u
#define DT_POOL_MEMBERS 4096u
#define DT_DEVICE_ID "0123456789abcdef01234567"
#define DT_TOKEN_HASH "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

/* ---------- 可注入测试时钟 ---------- */

typedef struct test_clock {
    uint64_t monotonic_ms;
    uint64_t wall_ms;
    uint32_t uuid_counter;
} test_clock_t;

LOCKER_TEST_HELPER static uint64_t dt_clock_monotonic(void *user)
{
    test_clock_t *clock = (test_clock_t *)user;
    return (clock == NULL) ? 0u : clock->monotonic_ms;
}

LOCKER_TEST_HELPER static bool dt_clock_wall(void *user, uint64_t *out)
{
    test_clock_t *clock = (test_clock_t *)user;
    if (clock == NULL || out == NULL) {
        return false;
    }
    *out = clock->wall_ms;
    return true;
}

LOCKER_TEST_HELPER static locker_status_t dt_clock_uuid(void *user, locker_uuid_t *out)
{
    test_clock_t *clock = (test_clock_t *)user;
    if (clock == NULL || out == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    clock->uuid_counter += 1u;
    uint32_t value = clock->uuid_counter;
    memset(out->bytes, 0, sizeof(out->bytes));
    out->bytes[0] = (uint8_t)(value & 0xffu);
    out->bytes[1] = (uint8_t)((value >> 8u) & 0xffu);
    out->bytes[2] = (uint8_t)((value >> 16u) & 0xffu);
    out->bytes[3] = (uint8_t)((value >> 24u) & 0xffu);
    out->bytes[4] = 0x9au;
    out->bytes[5] = 0x5bu;
    out->bytes[6] = (uint8_t)(0x40u | (out->bytes[6] & 0x0fu));
    out->bytes[7] = 0x8cu;
    out->bytes[8] = 0x80u;
    out->bytes[9] = 0x1du;
    out->bytes[15] = (uint8_t)(value & 0xffu);
    return LOCKER_STATUS_OK;
}

LOCKER_TEST_HELPER static locker_clock_port_t dt_clock_port(test_clock_t *clock)
{
    locker_clock_port_t port;
    memset(&port, 0, sizeof(port));
    port.user = clock;
    port.monotonic_ms = dt_clock_monotonic;
    port.wall_utc_ms = dt_clock_wall;
    port.new_uuid = dt_clock_uuid;
    return port;
}

LOCKER_TEST_HELPER static void dt_uuid_fill(locker_uuid_t *uuid, uint8_t seed)
{
    uint32_t i;
    for (i = 0; i < sizeof(uuid->bytes); i++) {
        uuid->bytes[i] = (uint8_t)(seed + i);
    }
    uuid->bytes[6] = (uint8_t)(0x40u | (uuid->bytes[6] & 0x0fu));
    uuid->bytes[8] = (uint8_t)(0x80u | (uuid->bytes[8] & 0x3fu));
}

LOCKER_TEST_HELPER static void dt_uuid_text(const locker_uuid_t *uuid, char *out, size_t out_size)
{
    if (locker_uuid_format(uuid, out, out_size) != LOCKER_STATUS_OK) {
        if (out_size > 0u) {
            out[0] = '\0';
        }
    }
}

/* ---------- 假仓储 ---------- */

typedef struct dt_doc {
    bool used;
    char collection[LOCKER_DOMAIN_COLLECTION_TEXT];
    char key[LOCKER_DOMAIN_KEY_TEXT];
    char value[DT_VALUE_BYTES];
} dt_doc_t;

struct locker_repo {
    dt_doc_t docs[DT_MAX_DOCS];
    uint64_t revision;
    locker_status_t query_status;
    locker_status_t commit_status;
    locker_status_t import_status;
    locker_status_t recovery_status;
    uint32_t query_calls;
    uint32_t commit_calls;
    uint32_t import_calls;
    uint32_t recovery_calls;
    uint32_t last_import_count;
    bool last_import_gap;
    bool last_import_has_projection;
    char last_change[65536];
    size_t last_change_length;
    locker_event_key_t last_committed;
    locker_json_node_t pool_nodes[DT_POOL_NODES];
    locker_json_member_t pool_members[DT_POOL_MEMBERS];
};

LOCKER_TEST_HELPER static dt_doc_t *dt_repo_find(struct locker_repo *repo, const char *collection,
                                                 const char *key)
{
    uint32_t i;
    if (repo == NULL || collection == NULL || key == NULL) {
        return NULL;
    }
    for (i = 0; i < DT_MAX_DOCS; i++) {
        if (!repo->docs[i].used) {
            continue;
        }
        if (strcmp(repo->docs[i].collection, collection) != 0) {
            continue;
        }
        if (strcmp(repo->docs[i].key, key) == 0) {
            return &repo->docs[i];
        }
    }
    return NULL;
}

LOCKER_TEST_HELPER static bool dt_repo_put(struct locker_repo *repo, const char *collection,
                                           const char *key, const char *value)
{
    dt_doc_t *doc = dt_repo_find(repo, collection, key);
    uint32_t i;
    if (doc == NULL) {
        for (i = 0; i < DT_MAX_DOCS; i++) {
            if (!repo->docs[i].used) {
                doc = &repo->docs[i];
                break;
            }
        }
    }
    if (doc == NULL) {
        return false;
    }
    if (strlen(collection) >= sizeof(doc->collection) || strlen(key) >= sizeof(doc->key) ||
        strlen(value) >= sizeof(doc->value)) {
        return false;
    }
    memset(doc, 0, sizeof(*doc));
    doc->used = true;
    memcpy(doc->collection, collection, strlen(collection) + 1u);
    memcpy(doc->key, key, strlen(key) + 1u);
    memcpy(doc->value, value, strlen(value) + 1u);
    return true;
}

LOCKER_TEST_HELPER static bool dt_repo_delete(struct locker_repo *repo, const char *collection,
                                              const char *key)
{
    dt_doc_t *doc = dt_repo_find(repo, collection, key);
    if (doc == NULL) {
        return true;
    }
    memset(doc, 0, sizeof(*doc));
    return true;
}

LOCKER_TEST_HELPER static bool dt_node_text(const locker_json_document_t *doc,
                                            const locker_json_node_t *node, char *out,
                                            size_t out_size)
{
    size_t length = 0;
    if (node == NULL || out == NULL || out_size == 0u) {
        return false;
    }
    if (!locker_domain_json_text(doc, node, out, out_size - 1u, &length)) {
        return false;
    }
    out[length] = '\0';
    return true;
}

LOCKER_TEST_HELPER static bool dt_read_u64_text(const locker_json_document_t *doc,
                                                const locker_json_node_t *node, uint64_t *out)
{
    char text[LOCKER_DOMAIN_U64_TEXT];
    if (!dt_node_text(doc, node, text, sizeof(text))) {
        return false;
    }
    return locker_u64_parse_decimal(text, strlen(text), out) == LOCKER_STATUS_OK;
}

/* 按 (collection, key) 取下一个大于 after_key 的键；after_key 为 NULL 表示从头。 */
LOCKER_TEST_HELPER static bool dt_repo_next_key(struct locker_repo *repo, const char *collection,
                                                const char *after_key, char *out, size_t out_size)
{
    bool found = false;
    uint32_t i;
    for (i = 0; i < DT_MAX_DOCS; i++) {
        if (!repo->docs[i].used) {
            continue;
        }
        if (strcmp(repo->docs[i].collection, collection) != 0) {
            continue;
        }
        if (after_key != NULL && after_key[0] != '\0' &&
            strcmp(repo->docs[i].key, after_key) <= 0) {
            continue;
        }
        if (!found || strcmp(repo->docs[i].key, out) < 0) {
            memcpy(out, repo->docs[i].key, strlen(repo->docs[i].key) + 1u);
            found = true;
        }
    }
    (void)out_size;
    return found;
}

LOCKER_TEST_HELPER static void dt_append_doc(locker_json_writer_t *writer, const dt_doc_t *doc,
                                             uint32_t *included)
{
    if (*included > 0u) {
        locker_domain_json_literal(writer, ",");
    }
    locker_domain_json_literal(writer, "{\"collection\":");
    locker_domain_json_string(writer, doc->collection);
    locker_domain_json_literal(writer, ",\"key\":");
    locker_domain_json_string(writer, doc->key);
    locker_domain_json_literal(writer, ",\"value\":");
    locker_domain_json_literal(writer, doc->value);
    locker_domain_json_literal(writer, "}");
    *included += 1u;
}

LOCKER_TEST_HELPER static locker_status_t dt_repo_render_page(struct locker_repo *repo,
                                                             const locker_repo_query_t *query,
                                                             bool recovery, char *out,
                                                             size_t out_size, size_t *out_length)
{
    locker_json_writer_t writer;
    char revision[LOCKER_DOMAIN_U64_TEXT];
    char cursor[LOCKER_DOMAIN_KEY_TEXT];
    uint32_t included = 0;
    bool has_next = false;
    char next_key[LOCKER_DOMAIN_KEY_TEXT];

    if (locker_u64_format_decimal(repo->revision, revision, sizeof(revision)) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_IO_ERROR;
    }
    locker_domain_json_writer_init(&writer, out, out_size);
    locker_domain_json_literal(&writer, "{\"schema_version\":1,\"snapshot_revision\":");
    locker_domain_json_string(&writer, revision);
    if (recovery) {
        locker_domain_json_literal(&writer, ",\"collection\":");
        locker_domain_json_string(&writer, query->collection);
    }
    locker_domain_json_literal(&writer, ",\"documents\":[");

    memcpy(cursor, query->after_key, strlen(query->after_key) + 1u);
    next_key[0] = '\0';
    if (query->key[0] != '\0') {
        dt_doc_t *doc = dt_repo_find(repo, query->collection, query->key);
        if (doc != NULL) {
            dt_append_doc(&writer, doc, &included);
        }
    } else {
        for (;;) {
            char key[LOCKER_DOMAIN_KEY_TEXT];
            dt_doc_t *doc;
            if (included >= (uint32_t)query->limit) {
                break;
            }
            if (!dt_repo_next_key(repo, query->collection, (cursor[0] == '\0') ? NULL : cursor, key,
                                  sizeof(key))) {
                break;
            }
            doc = dt_repo_find(repo, query->collection, key);
            if (doc == NULL) {
                break;
            }
            dt_append_doc(&writer, doc, &included);
            memcpy(cursor, key, strlen(key) + 1u);
        }
        /* 与真仓储一致（host/storage/src/repository.c:1111/:1121-1127）：next_after_key 是
           最后一条已含文档的 key，续页用 key > after_key 继续，不是「下一条未含的 key」。 */
        if (cursor[0] != '\0' &&
            dt_repo_next_key(repo, query->collection, cursor, next_key, sizeof(next_key))) {
            memcpy(next_key, cursor, strlen(cursor) + 1u);
            has_next = true;
        }
    }

    locker_domain_json_literal(&writer, "],\"next_after_key\":");
    if (has_next) {
        locker_domain_json_string(&writer, next_key);
    } else {
        locker_domain_json_null(&writer);
    }
    locker_domain_json_literal(&writer, "}");
    if (!locker_domain_json_writer_ok(&writer)) {
        return LOCKER_STATUS_NO_SPACE;
    }
    *out_length = writer.length;
    return LOCKER_STATUS_OK;
}

LOCKER_TEST_HELPER static locker_status_t dt_repo_apply_change(struct locker_repo *repo,
                                                               const char *json, size_t length)
{
    locker_json_document_t doc;
    const locker_json_node_t *root;
    const locker_json_node_t *checks;
    const locker_json_node_t *mutations;
    uint64_t expected = 0;
    uint64_t next = 0;
    uint32_t i;

    memset(&doc, 0, sizeof(doc));
    doc.text = json;
    doc.length = length;
    doc.nodes = repo->pool_nodes;
    doc.node_capacity = DT_POOL_NODES;
    doc.members = repo->pool_members;
    doc.member_capacity = DT_POOL_MEMBERS;
    if (!locker_domain_json_parse(&doc, json, length)) {
        return LOCKER_STATUS_INVALID;
    }
    root = locker_domain_json_root(&doc);
    if (root == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!dt_read_u64_text(&doc, locker_domain_json_get(&doc, root, "expected_snapshot_revision"),
                          &expected)) {
        return LOCKER_STATUS_INVALID;
    }
    if (!dt_read_u64_text(&doc, locker_domain_json_get(&doc, root, "new_snapshot_revision"), &next)) {
        return LOCKER_STATUS_INVALID;
    }
    if (next != expected + 1u) {
        return LOCKER_STATUS_INVALID;
    }
    if (expected != repo->revision) {
        return LOCKER_STATUS_CONFLICT;
    }

    checks = locker_domain_json_get(&doc, root, "checks");
    if (checks == NULL || checks->type != LOCKER_JSON_ARRAY) {
        return LOCKER_STATUS_INVALID;
    }
    for (i = 0; i < checks->count; i++) {
        const locker_json_node_t *item = locker_domain_json_at(&doc, checks, i);
        char collection[LOCKER_DOMAIN_COLLECTION_TEXT];
        char key[LOCKER_DOMAIN_KEY_TEXT];
        char condition[16];
        bool exists;
        if (item == NULL) {
            return LOCKER_STATUS_INVALID;
        }
        if (!dt_node_text(&doc, locker_domain_json_get(&doc, item, "collection"), collection,
                          sizeof(collection))) {
            return LOCKER_STATUS_INVALID;
        }
        if (!dt_node_text(&doc, locker_domain_json_get(&doc, item, "key"), key, sizeof(key))) {
            return LOCKER_STATUS_INVALID;
        }
        if (!dt_node_text(&doc, locker_domain_json_get(&doc, item, "condition"), condition,
                          sizeof(condition))) {
            return LOCKER_STATUS_INVALID;
        }
        exists = dt_repo_find(repo, collection, key) != NULL;
        if (strcmp(condition, "EXISTS") == 0) {
            if (!exists) {
                return LOCKER_STATUS_CONFLICT;
            }
        } else if (strcmp(condition, "ABSENT") == 0) {
            if (exists) {
                return LOCKER_STATUS_CONFLICT;
            }
        } else {
            return LOCKER_STATUS_INVALID;
        }
    }

    mutations = locker_domain_json_get(&doc, root, "mutations");
    if (mutations == NULL || mutations->type != LOCKER_JSON_ARRAY || mutations->count == 0u) {
        return LOCKER_STATUS_INVALID;
    }
    for (i = 0; i < mutations->count; i++) {
        const locker_json_node_t *item = locker_domain_json_at(&doc, mutations, i);
        char op[16];
        if (item == NULL) {
            return LOCKER_STATUS_INVALID;
        }
        if (!dt_node_text(&doc, locker_domain_json_get(&doc, item, "op"), op, sizeof(op))) {
            return LOCKER_STATUS_INVALID;
        }
        if (strcmp(op, "DELETE") == 0) {
            char collection[LOCKER_DOMAIN_COLLECTION_TEXT];
            char key[LOCKER_DOMAIN_KEY_TEXT];
            if (!dt_node_text(&doc, locker_domain_json_get(&doc, item, "collection"), collection,
                              sizeof(collection))) {
                return LOCKER_STATUS_INVALID;
            }
            if (!dt_node_text(&doc, locker_domain_json_get(&doc, item, "key"), key, sizeof(key))) {
                return LOCKER_STATUS_INVALID;
            }
            dt_repo_delete(repo, collection, key);
        } else if (strcmp(op, "PUT") == 0) {
            const locker_json_node_t *document =
                locker_domain_json_get(&doc, item, "document");
            char collection[LOCKER_DOMAIN_COLLECTION_TEXT];
            char key[LOCKER_DOMAIN_KEY_TEXT];
            char value[DT_VALUE_BYTES];
            size_t value_length = 0;
            if (document == NULL) {
                return LOCKER_STATUS_INVALID;
            }
            if (!dt_node_text(&doc, locker_domain_json_get(&doc, document, "collection"),
                              collection, sizeof(collection))) {
                return LOCKER_STATUS_INVALID;
            }
            if (!dt_node_text(&doc, locker_domain_json_get(&doc, document, "key"), key,
                              sizeof(key))) {
                return LOCKER_STATUS_INVALID;
            }
            if (!locker_domain_json_render(&doc, locker_domain_json_get(&doc, document, "value"),
                                           value, sizeof(value), &value_length)) {
                return LOCKER_STATUS_INVALID;
            }
            value[value_length] = '\0';
            if (!dt_repo_put(repo, collection, key, value)) {
                return LOCKER_STATUS_NO_SPACE;
            }
        } else {
            return LOCKER_STATUS_INVALID;
        }
    }

    repo->revision = next;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_repo_query(locker_repo_t *repo,
                                                           const locker_repo_query_t *query,
                                                           locker_repo_result_t *result)
{
    size_t length = 0;
    locker_status_t status;
    if (repo == NULL || query == NULL || result == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    repo->query_calls += 1u;
    memset(result, 0, sizeof(*result));
    if (repo->query_status != LOCKER_STATUS_OK) {
        return repo->query_status;
    }
    status = dt_repo_render_page(repo, query, false, result->json, sizeof(result->json), &length);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    result->length = length;
    result->snapshot_revision = repo->revision;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_repo_load_recovery(locker_repo_t *repo,
                                                                   const locker_repo_query_t *query,
                                                                   locker_recovery_t *recovery)
{
    size_t length = 0;
    locker_status_t status;
    if (repo == NULL || query == NULL || recovery == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    repo->recovery_calls += 1u;
    memset(recovery, 0, sizeof(*recovery));
    if (repo->recovery_status != LOCKER_STATUS_OK) {
        return repo->recovery_status;
    }
    status = dt_repo_render_page(repo, query, true, recovery->json, sizeof(recovery->json), &length);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    recovery->length = length;
    recovery->snapshot_revision = repo->revision;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_repo_commit_change(locker_repo_t *repo,
                                                                   const locker_change_t *change)
{
    if (repo == NULL || change == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    repo->commit_calls += 1u;
    if (change->length + 1u < sizeof(repo->last_change)) {
        memcpy(repo->last_change, change->json, change->length);
        repo->last_change[change->length] = '\0';
        repo->last_change_length = change->length;
    }
    if (repo->commit_status != LOCKER_STATUS_OK) {
        return repo->commit_status;
    }
    return dt_repo_apply_change(repo, change->json, change->length);
}

locker_status_t locker_repo_import_events(
    locker_repo_t *repo, const locker_event_batch_t *batch, const locker_change_t *projection_change,
    locker_event_key_t *committed_cursor)
{
    if (repo == NULL || batch == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    repo->import_calls += 1u;
    repo->last_import_count = batch->count;
    repo->last_import_gap = batch->gap_before_page;
    repo->last_import_has_projection = projection_change != NULL;
    if (repo->import_status != LOCKER_STATUS_OK) {
        return repo->import_status;
    }
    if (batch->count == 0u || batch->count > LOCKER_DOMAIN_EVENT_PAGE_MAX) {
        return LOCKER_STATUS_INVALID;
    }
    repo->last_committed = batch->events[batch->count - 1u].key;
    if (committed_cursor != NULL) {
        *committed_cursor = repo->last_committed;
    }
    return LOCKER_STATUS_OK;
}

/* ---------- 假设备 ---------- */

/*
 * 端口 fake 必须是非 static 外部定义：locker_domain 是 STATIC 库，链接后这些符号
 * 已经是外部定义，头里再写 static 会报 "static declaration follows non-static
 * declaration"。一个测试就是一个可执行文件，不存在跨二进制重复定义。
 */
struct locker_device {
    locker_device_sink_t sink;
    bool bound;
    uint32_t bind_calls;
    uint32_t snapshot_calls;
    uint32_t fetch_calls;
    uint32_t ack_calls;
    uint32_t resume_calls;
    uint32_t release_calls;
    uint32_t health_calls;
    uint32_t anchor_calls;
    uint32_t execute_calls;
    uint32_t query_calls;
    uint32_t last_request_no;
    uint8_t last_fetch_limit;
    locker_event_key_t last_after;
    bool has_after;
    locker_event_key_t last_ack;
    bool has_ack;
    locker_ready_guard_t last_guard;
    locker_context_guard_t last_release;
    locker_health_report_t last_health;
    locker_unlock_intent_t last_intent;
    bool has_intent;
    locker_uuid_t last_query_command;
    bool has_query_command;
    locker_status_t snapshot_status;
    locker_status_t fetch_status;
    locker_status_t ack_status;
    locker_status_t execute_status;
    locker_status_t query_command_status;
    locker_status_t time_anchor_status;
    locker_status_t bind_status;
    /* 释放端口要能返回 CONFLICT：CAS 失败是「设备侧到底还持不持有上下文」
       的唯一来源，Core 必须转成「只查询」而不是假设已释放（审计 R3-B-2）。 */
    locker_status_t release_status;
};

locker_status_t locker_device_bind_sink(locker_device_t *device,
                                                                 const locker_device_sink_t *sink)
{
    if (device == NULL || sink == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    device->bind_calls += 1u;
    if (device->bind_status != LOCKER_STATUS_OK) {
        return device->bind_status;
    }
    device->sink = *sink;
    device->bound = true;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_device_get_snapshot(locker_device_t *device,
                                                                    uint32_t request_no)
{
    if (device == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    device->snapshot_calls += 1u;
    device->last_request_no = request_no;
    return device->snapshot_status;
}

locker_status_t locker_device_fetch_events(locker_device_t *device,
                                                                    const locker_event_key_t *after,
                                                                    uint8_t limit)
{
    if (device == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    device->fetch_calls += 1u;
    device->last_fetch_limit = limit;
    if (after != NULL) {
        device->last_after = *after;
        device->has_after = true;
    }
    return device->fetch_status;
}

locker_status_t locker_device_ack_persisted(locker_device_t *device,
                                                                     const locker_event_key_t *key)
{
    if (device == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    device->ack_calls += 1u;
    if (key != NULL) {
        device->last_ack = *key;
        device->has_ack = true;
    }
    return device->ack_status;
}

locker_status_t locker_device_resume_ready(locker_device_t *device,
                                                                    const locker_ready_guard_t *guard)
{
    if (device == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    device->resume_calls += 1u;
    if (guard != NULL) {
        device->last_guard = *guard;
    }
    return LOCKER_STATUS_OK;
}

locker_status_t locker_device_release_context(
    locker_device_t *device, const locker_context_guard_t *guard)
{
    if (device == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    device->release_calls += 1u;
    if (guard != NULL) {
        device->last_release = *guard;
    }
    return device->release_status;
}

locker_status_t locker_device_send_health(locker_device_t *device,
                                                                   const locker_health_report_t *report)
{
    if (device == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    device->health_calls += 1u;
    if (report != NULL) {
        device->last_health = *report;
    }
    return LOCKER_STATUS_OK;
}

locker_status_t locker_device_time_anchor(locker_device_t *device,
                                                                   uint32_t request_no)
{
    if (device == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    device->anchor_calls += 1u;
    device->last_request_no = request_no;
    return device->time_anchor_status;
}

locker_status_t locker_device_execute_once(
    locker_device_t *device, const locker_unlock_intent_t *intent)
{
    if (device == NULL || intent == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    device->execute_calls += 1u;
    device->last_intent = *intent;
    device->has_intent = true;
    return device->execute_status;
}

locker_status_t locker_device_query_command(locker_device_t *device,
                                                                     const locker_uuid_t *command_id)
{
    if (device == NULL || command_id == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    device->query_calls += 1u;
    device->last_query_command = *command_id;
    device->has_query_command = true;
    return device->query_command_status;
}

/* ---------- 端口 fake 原型（非 static，见上） ---------- */

locker_status_t locker_repo_query(locker_repo_t *repo, const locker_repo_query_t *query,
                                 locker_repo_result_t *result);
locker_status_t locker_repo_load_recovery(locker_repo_t *repo, const locker_repo_query_t *query,
                                          locker_recovery_t *recovery);
locker_status_t locker_repo_commit_change(locker_repo_t *repo, const locker_change_t *change);
locker_status_t locker_repo_import_events(locker_repo_t *repo, const locker_event_batch_t *batch,
                                          const locker_change_t *projection_change,
                                          locker_event_key_t *committed_cursor);

locker_status_t locker_device_bind_sink(locker_device_t *device, const locker_device_sink_t *sink);
locker_status_t locker_device_get_snapshot(locker_device_t *device, uint32_t request_no);
locker_status_t locker_device_fetch_events(locker_device_t *device, const locker_event_key_t *after,
                                           uint8_t limit);
locker_status_t locker_device_ack_persisted(locker_device_t *device, const locker_event_key_t *key);
locker_status_t locker_device_resume_ready(locker_device_t *device, const locker_ready_guard_t *guard);
locker_status_t locker_device_release_context(locker_device_t *device,
                                              const locker_context_guard_t *guard);
locker_status_t locker_device_send_health(locker_device_t *device,
                                          const locker_health_report_t *report);
locker_status_t locker_device_time_anchor(locker_device_t *device, uint32_t request_no);
locker_status_t locker_device_execute_once(locker_device_t *device,
                                           const locker_unlock_intent_t *intent);
locker_status_t locker_device_query_command(locker_device_t *device, const locker_uuid_t *command_id);

locker_status_t locker_events_try_publish(locker_publisher_t *publisher,
                                          const locker_resource_change_t *change);

/* ---------- 假发布器 ---------- */

struct locker_publisher {
    uint32_t publish_calls;
    locker_resource_change_t last;
    locker_status_t status;
};

locker_status_t locker_events_try_publish(
    locker_publisher_t *publisher, const locker_resource_change_t *change)
{
    if (publisher == NULL || change == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    publisher->publish_calls += 1u;
    publisher->last = *change;
    return publisher->status;
}

/* ---------- 夹具 ---------- */

typedef struct dt_fixture {
    test_clock_t clock;
    locker_repo_t *repo;
    locker_device_t *device;
    locker_publisher_t *publisher;
    locker_core_t *core;
} dt_fixture_t;

LOCKER_TEST_HELPER static bool dt_fixture_init(dt_fixture_t *fixture, bool with_device)
{
    locker_core_config_t config;
    locker_status_t status;

    memset(fixture, 0, sizeof(*fixture));
    fixture->clock.monotonic_ms = 1000u;
    /* 墙钟固定为 2025-10-21T00:00:00Z：所有时间戳断言都按这个基准写。 */
    fixture->clock.wall_ms = 1761004800000ull;
    fixture->clock.uuid_counter = 0u;
    fixture->repo = (locker_repo_t *)calloc(1u, sizeof(struct locker_repo));
    fixture->publisher = (locker_publisher_t *)calloc(1u, sizeof(struct locker_publisher));
    if (fixture->repo == NULL || fixture->publisher == NULL) {
        return false;
    }
    if (with_device) {
        fixture->device = (locker_device_t *)calloc(1u, sizeof(struct locker_device));
        if (fixture->device == NULL) {
            return false;
        }
    }
    memset(&config, 0, sizeof(config));
    memcpy(config.cabinet_name, "test-cabinet", sizeof("test-cabinet"));
    dt_uuid_fill(&config.instance_id, 0x40u);
    config.clock = dt_clock_port(&fixture->clock);
    config.queue_capacity = 0u;
    config.completion_retention_ms = LOCKER_COMPLETION_RETENTION_MS;
    status = locker_core_create(&config, fixture->repo, fixture->device, fixture->publisher,
                                &fixture->core);
    return status == LOCKER_STATUS_OK;
}

LOCKER_TEST_HELPER static void dt_fixture_free(dt_fixture_t *fixture)
{
    if (fixture->core != NULL) {
        locker_core_destroy(fixture->core);
        fixture->core = NULL;
    }
    free(fixture->repo);
    free(fixture->device);
    free(fixture->publisher);
    fixture->repo = NULL;
    fixture->device = NULL;
    fixture->publisher = NULL;
}

/* 把 Core 直接推到「已恢复 + 设备就绪」：单测只验证状态机，不重复跑恢复路径。 */
LOCKER_TEST_HELPER static void dt_core_ready(dt_fixture_t *fixture)
{
    locker_core_t *core = fixture->core;
    core->phase = DOMAIN_PHASE_READY;
    core->recovery_done = true;
    core->recovery_status = LOCKER_STATUS_OK;
    core->recovery_ready = true;
    core->now_ms = fixture->clock.monotonic_ms;
    core->next_action_no = 1u;
    core->action_watermark = 0u;
    /* 单柜格模型里工具是设备配置的一部分，真实恢复路径由 recover_tools 从
       tools 集合填好。create 现在要求工具可知且版本匹配（审计 R1-B-9），
       所以夹具也要给出这一份投影，否则每个申请都会卡在 NOT_READY。
       需要验证「工具不可知」的用例自行把它改回 false。 */
    dt_uuid_fill(&core->tool_id, 0x22u);
    core->tool_revision = 1u;
    domain_text_copy(core->tool_name, sizeof(core->tool_name), "柜内工具");
    core->tool_availability = LOCKER_TOOL_AVAILABILITY_AVAILABLE;
    core->tool_valid = true;
    core->loan_open = false;
    core->active.used = false;
    /* 时钟换算样本：真实路径由 domain_anchor_request 周期性采集，单测不跑
       设备环，所以要给一份等价样本。偏移取 0，于是 MCU 时钟与主机单调时钟
       同值，测试里比较 not_after_mcu_ms 时不用再算一遍换算。
       验证「锚点缺失一律拒发」的用例自行清 anchor_valid。 */
    core->anchor_valid = true;
    core->anchor_offset_low_ms = 0;
    core->anchor_offset_high_ms = 0;
    core->anchor_sampled_ms = fixture->clock.monotonic_ms;
}

LOCKER_TEST_HELPER static void dt_core_device_ready(dt_fixture_t *fixture, uint64_t boot_id,
                                                    uint8_t door, uint8_t item)
{
    locker_core_t *core = fixture->core;
    dt_core_ready(fixture);
    core->device_session_up = true;
    core->device_snapshot_valid = true;
    core->session.valid = true;
    dt_uuid_fill(&core->session.session_id, 0x10u);
    dt_uuid_fill(&core->session.journal_epoch, 0x20u);
    core->session.boot_id = boot_id;
    memcpy(core->session.device_id, DT_DEVICE_ID, sizeof(DT_DEVICE_ID));
    core->session.device_mode = LOCKER_DEVICE_MODE_READY;
    core->session.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
    core->session.capabilities = 0u;
    core->session.action_watermark = 0u;
    core->cursor.valid = true;
    core->cursor.journal_epoch = core->session.journal_epoch;
    memcpy(core->cursor.device_id, DT_DEVICE_ID, sizeof(DT_DEVICE_ID));
    /* through 是完整 EventKey：补取请求把它原样当 after 传给设备端口，
       缺 device_id/journal_epoch 会让设备侧拿到残缺游标。 */
    memcpy(core->cursor.through.device_id, DT_DEVICE_ID, sizeof(DT_DEVICE_ID));
    core->cursor.through.journal_epoch = core->session.journal_epoch;
    core->cursor.through.boot_id = boot_id;
    core->cursor.through.event_seq = 0u;
    core->cursor.continuity_ok = true;
    core->fetch_gap = false;
    /* 新的设备上下文没有已提交水位，也没有待确认水位。 */
    memset(&core->import_cursor, 0, sizeof(core->import_cursor));
    core->import_cursor_valid = false;
    core->import_continuity = false;
    memset(&core->ack_cursor, 0, sizeof(core->ack_cursor));
    core->ack_cursor_valid = false;
    core->physical.valid = true;
    memcpy(core->physical.device_id, DT_DEVICE_ID, sizeof(DT_DEVICE_ID));
    core->physical.journal_epoch = core->session.journal_epoch;
    core->physical.boot_id = boot_id;
    core->physical.physical_revision = 7u;
    core->physical.door = door;
    core->physical.item = item;
    core->physical.supply_valid = true;
    core->physical.supply_mv = 12000u;
    core->physical.observed_mono_ms = core->now_ms;
    core->physical.time_quality = LOCKER_TIME_QUALITY_HOST_SYNCED;
    core->physical.continuity_ok = true;
    core->physical.dropped_events = 0u;
    core->physical.durable_cursor = core->cursor.through;
}

LOCKER_TEST_HELPER static bool dt_seed_principal(dt_fixture_t *fixture,
                                                 const locker_principal_t *principal)
{
    char session_id[LOCKER_DOMAIN_UUID_TEXT];
    char user_id[LOCKER_DOMAIN_UUID_TEXT];
    char expires[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    char value[1024];
    locker_json_writer_t writer;
    const char *role;

    dt_uuid_text(&principal->session_id, session_id, sizeof(session_id));
    dt_uuid_text(&principal->user_id, user_id, sizeof(user_id));
    if (session_id[0] == '\0' || user_id[0] == '\0') {
        return false;
    }
    if (!locker_domain_time_format_utc(fixture->clock.wall_ms + 3600000ull, expires,
                                      sizeof(expires))) {
        return false;
    }
    role = domain_role_name(principal->role);
    if (role == NULL) {
        return false;
    }

    locker_domain_json_writer_init(&writer, value, sizeof(value));
    locker_domain_json_literal(&writer, "{\"id\":");
    locker_domain_json_string(&writer, session_id);
    locker_domain_json_literal(&writer, ",\"user_id\":");
    locker_domain_json_string(&writer, user_id);
    locker_domain_json_literal(&writer, ",\"token_hash\":\"" DT_TOKEN_HASH "\"");
    locker_domain_json_literal(&writer, ",\"expires_at\":");
    locker_domain_json_string(&writer, expires);
    locker_domain_json_literal(&writer, ",\"revoked\":false}");
    if (!locker_domain_json_writer_ok(&writer)) {
        return false;
    }
    if (!dt_repo_put(fixture->repo, "sessions", session_id, value)) {
        return false;
    }

    locker_domain_json_writer_init(&writer, value, sizeof(value));
    locker_domain_json_literal(&writer, "{\"user\":{\"id\":");
    locker_domain_json_string(&writer, user_id);
    locker_domain_json_literal(&writer, ",\"username\":\"tester\",\"display_name\":\"Tester\",\"role\":");
    locker_domain_json_string(&writer, role);
    locker_domain_json_literal(&writer, ",\"enabled\":true,\"user_revision\":\"1\"},");
    locker_domain_json_literal(&writer, "\"password_hash\":\"x\"}");
    if (!locker_domain_json_writer_ok(&writer)) {
        return false;
    }
    return dt_repo_put(fixture->repo, "users", user_id, value);
}

LOCKER_TEST_HELPER static locker_principal_t dt_principal(uint8_t role, uint8_t seed)
{
    locker_principal_t principal;
    memset(&principal, 0, sizeof(principal));
    dt_uuid_fill(&principal.user_id, seed);
    dt_uuid_fill(&principal.session_id, (uint8_t)(seed + 0x10u));
    principal.role = role;
    principal.authenticated = true;
    return principal;
}

/* 一条已提交的操作记录（用于加载路径），state 用枚举序号。 */
LOCKER_TEST_HELPER static bool dt_seed_operation(dt_fixture_t *fixture, const locker_uuid_t *id,
                                                 const locker_uuid_t *tool_id,
                                                 const locker_uuid_t *actor_id, uint8_t kind,
                                                 uint8_t state, uint64_t revision,
                                                 const char *updated_at)
{
    char key[LOCKER_DOMAIN_UUID_TEXT];
    char tool[LOCKER_DOMAIN_UUID_TEXT];
    char actor[LOCKER_DOMAIN_UUID_TEXT];
    char value[4096];
    locker_json_writer_t writer;
    char revision_text[LOCKER_DOMAIN_U64_TEXT];

    dt_uuid_text(id, key, sizeof(key));
    dt_uuid_text(tool_id, tool, sizeof(tool));
    dt_uuid_text(actor_id, actor, sizeof(actor));
    if (key[0] == '\0' || tool[0] == '\0' || actor[0] == '\0') {
        return false;
    }
    if (locker_u64_format_decimal(revision, revision_text, sizeof(revision_text)) !=
        LOCKER_STATUS_OK) {
        return false;
    }

    locker_domain_json_writer_init(&writer, value, sizeof(value));
    locker_domain_json_literal(&writer, "{\"operation\":{\"id\":");
    locker_domain_json_string(&writer, key);
    locker_domain_json_literal(&writer, ",\"tool_id\":");
    locker_domain_json_string(&writer, tool);
    locker_domain_json_literal(&writer, ",\"kind\":");
    locker_domain_json_string(&writer, domain_kind_name(kind));
    locker_domain_json_literal(&writer, ",\"actor_id\":");
    locker_domain_json_string(&writer, actor);
    locker_domain_json_literal(&writer, ",\"state\":");
    locker_domain_json_string(&writer, domain_state_name(state));
    locker_domain_json_literal(&writer, ",\"operation_revision\":");
    locker_domain_json_string(&writer, revision_text);
    locker_domain_json_literal(&writer, ",\"command_id\":null,\"reason_code\":null,\"created_at\":");
    locker_domain_json_string(&writer, updated_at);
    locker_domain_json_literal(&writer, ",\"updated_at\":");
    locker_domain_json_string(&writer, updated_at);
    locker_domain_json_literal(&writer, ",\"allowed_actions\":[],\"evidence\":{");
    locker_domain_json_literal(
        &writer,
        "\"history_complete\":true,\"door_open_seen\":false,\"item_transition_seen\":false,"
        "\"physical\":null,\"source_events\":[],\"manual_resolution\":false}},");
    locker_domain_json_literal(&writer, "\"baseline\":");
    locker_domain_json_literal(&writer, "{\"device_id\":\"" DT_DEVICE_ID "\",\"journal_epoch\":");
    locker_domain_json_string(&writer, "00000000-0000-4000-8000-000000000000");
    locker_domain_json_literal(
        &writer,
        ",\"boot_id\":\"1\",\"physical_revision\":\"7\",\"door\":\"CLOSED\",\"item\":\"PRESENT\","
        "\"supply_mv\":12000,\"observed_age_ms\":0,\"time_quality\":\"HOST_SYNCED\","
        "\"continuity_ok\":true,\"dropped_events\":\"0\",\"durable_cursor\":{"
        "\"device_id\":\"" DT_DEVICE_ID "\",\"journal_epoch\":");
    locker_domain_json_string(&writer, "00000000-0000-4000-8000-000000000000");
    locker_domain_json_literal(&writer, ",\"boot_id\":\"1\",\"event_seq\":\"0\"}},");
    locker_domain_json_literal(
        &writer, "\"related_loan_id\":null,\"proof_source\":null,\"proof_consumed\":false}");
    if (!locker_domain_json_writer_ok(&writer)) {
        return false;
    }
    return dt_repo_put(fixture->repo, "operations", key, value);
}

#endif /* DOMAIN_TEST_SUPPORT_H */
