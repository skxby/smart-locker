#define _POSIX_C_SOURCE 200809L

#include "domain_internal.h"

#include <string.h>

/* ---------- RepositoryChange 构造 ----------
 *
 * 仓储只接受一个事务内的完整 change：expected/new revision 必须严格 +1
 * （host/storage/src/repository.c:704），checks 先判、mutations 后写。这里按顺序
 * 拼 JSON，任何一步越界就把 broken 置位，提交时整体失败——绝不写半个事务。 */

/* RepositoryChange 的 expected_snapshot_revision / new_snapshot_revision 在
   internal-data.schema.json 里是 U64 类型，也就是十进制**字符串**而不是 JSON 数字。
   写裸数字会被仓储的 schema 校验器拒绝，因此这里统一转文本。 */
static void change_u64_text(locker_json_writer_t *writer, uint64_t value)
{
    char text[LOCKER_DOMAIN_U64_TEXT];
    if (locker_u64_format_decimal(value, text, sizeof(text)) != LOCKER_STATUS_OK) {
        writer->failed = true;
        return;
    }
    locker_domain_json_string(writer, text);
}

void domain_change_init(locker_core_t *core, domain_change_t *change, uint64_t expected, uint64_t next)
{
    if (change == NULL) {
        return;
    }
    memset(change, 0, sizeof(*change));
    change->buffer = core->change_buffer;
    change->capacity = sizeof(core->change_buffer);
    locker_domain_json_writer_init(&change->writer, change->buffer, change->capacity);
    locker_domain_json_literal(&change->writer, "{\"schema_version\":1,");
    locker_domain_json_literal(&change->writer, "\"expected_snapshot_revision\":");
    change_u64_text(&change->writer, expected);
    locker_domain_json_literal(&change->writer, ",\"new_snapshot_revision\":");
    change_u64_text(&change->writer, next);
    locker_domain_json_literal(&change->writer, ",\"checks\":[");
}

bool domain_change_check(domain_change_t *change, const char *collection, const char *key,
                         const char *condition)
{
    if (change == NULL || change->broken || collection == NULL || key == NULL || condition == NULL) {
        return false;
    }
    if (change->checks >= LOCKER_DOMAIN_MAX_CHECKS) {
        change->broken = true;
        return false;
    }
    if (change->checks > 0u) {
        locker_domain_json_literal(&change->writer, ",");
    }
    locker_domain_json_literal(&change->writer, "{\"collection\":");
    locker_domain_json_string(&change->writer, collection);
    locker_domain_json_literal(&change->writer, ",\"key\":");
    locker_domain_json_string(&change->writer, key);
    locker_domain_json_literal(&change->writer, ",\"condition\":");
    locker_domain_json_string(&change->writer, condition);
    locker_domain_json_literal(&change->writer, "}");
    change->checks += 1u;
    if (!locker_domain_json_writer_ok(&change->writer)) {
        change->broken = true;
        return false;
    }
    return true;
}

static bool change_mutations_open(domain_change_t *change)
{
    if (change == NULL || change->broken) {
        return false;
    }
    if (change->mutations >= LOCKER_DOMAIN_MAX_MUTATIONS) {
        change->broken = true;
        return false;
    }
    if (!change->mutations_started) {
        locker_domain_json_literal(&change->writer, "],\"mutations\":[");
        change->mutations_started = true;
    }
    if (change->mutations > 0u) {
        locker_domain_json_literal(&change->writer, ",");
    }
    change->mutations += 1u;
    return true;
}

bool domain_change_put_begin(domain_change_t *change, const char *collection, const char *key)
{
    if (collection == NULL || key == NULL || !change_mutations_open(change)) {
        return false;
    }
    locker_domain_json_literal(&change->writer, "{\"op\":\"PUT\",\"document\":{\"collection\":");
    locker_domain_json_string(&change->writer, collection);
    locker_domain_json_literal(&change->writer, ",\"key\":");
    locker_domain_json_string(&change->writer, key);
    locker_domain_json_literal(&change->writer, ",\"value\":");
    return locker_domain_json_writer_ok(&change->writer);
}

void domain_change_put_end(domain_change_t *change)
{
    if (change == NULL || change->broken) {
        return;
    }
    locker_domain_json_literal(&change->writer, "}}");
    if (!locker_domain_json_writer_ok(&change->writer)) {
        change->broken = true;
    }
}

bool domain_change_delete(domain_change_t *change, const char *collection, const char *key)
{
    if (collection == NULL || key == NULL || !change_mutations_open(change)) {
        return false;
    }
    locker_domain_json_literal(&change->writer, "{\"op\":\"DELETE\",\"collection\":");
    locker_domain_json_string(&change->writer, collection);
    locker_domain_json_literal(&change->writer, ",\"key\":");
    locker_domain_json_string(&change->writer, key);
    locker_domain_json_literal(&change->writer, "}");
    if (!locker_domain_json_writer_ok(&change->writer)) {
        change->broken = true;
        return false;
    }
    return true;
}

locker_status_t domain_change_commit(locker_core_t *core, domain_change_t *change)
{
    if (core == NULL || change == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (change->broken || !locker_domain_json_writer_ok(&change->writer)) {
        return LOCKER_STATUS_NO_SPACE;
    }
    if (change->mutations == 0u) {
        /* 空 mutations 不满足 schema 的 minItems:1；调用方不该提交。 */
        return LOCKER_STATUS_INVALID;
    }
    if (!change->mutations_started) {
        return LOCKER_STATUS_INVALID;
    }
    locker_domain_json_literal(&change->writer, "]}");
    if (!locker_domain_json_writer_ok(&change->writer)) {
        return LOCKER_STATUS_NO_SPACE;
    }

    locker_change_t payload;
    memset(&payload, 0, sizeof(payload));
    payload.length = change->writer.length;
    if (payload.length + 1u > sizeof(payload.json)) {
        return LOCKER_STATUS_NO_SPACE;
    }
    memcpy(payload.json, change->buffer, payload.length);
    payload.json[payload.length] = '\0';

    locker_status_t status = locker_repo_commit_change(core->repo, &payload);
    if (status == LOCKER_STATUS_OK) {
        core->revision += 1u;
    }
    return status;
}