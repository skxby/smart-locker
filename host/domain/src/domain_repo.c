#define _POSIX_C_SOURCE 200809L

#include "domain_internal.h"

#include <stdlib.h>
#include <string.h>

/* 仓储访问：全部在 Core 线程上同步调用（module-ownership.md:49）。 */

static locker_status_t doc_query(locker_core_t *core, int slot, const char *collection, const char *key,
                                 uint16_t limit, const char *after_key, locker_repo_result_t *result)
{
    if (core == NULL || core->repo == NULL || collection == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (slot < 0 || slot > 1) {
        return LOCKER_STATUS_INVALID;
    }
    memset(result, 0, sizeof(*result));
    locker_repo_query_t query;
    memset(&query, 0, sizeof(query));
    query.schema_version = 1u;
    if (key != NULL) {
        if (strlen(key) >= sizeof(query.key)) {
            return LOCKER_STATUS_INVALID;
        }
        memcpy(query.key, key, strlen(key) + 1u);
        query.limit = 1u;
        query.after_key[0] = '\0';
    } else {
        query.key[0] = '\0';
        if (after_key != NULL) {
            if (strlen(after_key) >= sizeof(query.after_key)) {
                return LOCKER_STATUS_INVALID;
            }
            memcpy(query.after_key, after_key, strlen(after_key) + 1u);
        }
        query.limit = limit;
    }
    if (strlen(collection) >= sizeof(query.collection)) {
        return LOCKER_STATUS_INVALID;
    }
    memcpy(query.collection, collection, strlen(collection) + 1u);
    (void)slot;
    return locker_repo_query(core->repo, &query, result);
}

/* 解析查询结果，把 slot 槽的缓冲交给 doc->doc；doc->value 指向 documents[0].value。 */
static bool doc_parse(locker_core_t *core, int slot, locker_repo_result_t *result, domain_doc_t *out)
{
    memset(out, 0, sizeof(*out));
    out->doc.nodes = core->doc_nodes[slot];
    out->doc.node_capacity = LOCKER_DOMAIN_DOC_NODES;
    out->doc.members = core->doc_members[slot];
    out->doc.member_capacity = LOCKER_DOMAIN_DOC_MEMBERS;
    out->revision = result->snapshot_revision;
    if (!locker_domain_json_parse(&out->doc, result->json, result->length)) {
        return false;
    }
    const locker_json_node_t *documents = locker_domain_json_get(&out->doc, locker_domain_json_root(&out->doc), "documents");
    if (documents == NULL || documents->type != LOCKER_JSON_ARRAY) {
        return false;
    }
    if (documents->count == 0u) {
        out->found = false;
        out->value = NULL;
        return true;
    }
    const locker_json_node_t *document = locker_domain_json_at(&out->doc, documents, 0u);
    if (document == NULL) {
        return false;
    }
    const locker_json_node_t *value = locker_domain_json_get(&out->doc, document, "value");
    if (value == NULL) {
        return false;
    }
    out->found = true;
    out->value = value;
    return true;
}

locker_status_t domain_repo_doc(locker_core_t *core, int slot, const char *collection, const char *key,
                                domain_doc_t *out)
{
    if (out == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    locker_repo_result_t *result = &core->repo_result;
    locker_status_t status = doc_query(core, slot, collection, key, 1u, NULL, result);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    if (!doc_parse(core, slot, result, out)) {
        return LOCKER_STATUS_IO_ERROR;
    }
    return LOCKER_STATUS_OK;
}

locker_status_t domain_repo_page(locker_core_t *core, const char *collection, const char *after_key,
                                 uint16_t limit, domain_doc_t *out)
{
    if (out == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    memset(out, 0, sizeof(*out));
    locker_repo_query_t query;
    memset(&query, 0, sizeof(query));
    query.schema_version = 1u;
    if (strlen(collection) >= sizeof(query.collection)) {
        return LOCKER_STATUS_INVALID;
    }
    memcpy(query.collection, collection, strlen(collection) + 1u);
    query.key[0] = '\0';
    if (after_key != NULL) {
        if (strlen(after_key) >= sizeof(query.after_key)) {
            return LOCKER_STATUS_INVALID;
        }
        memcpy(query.after_key, after_key, strlen(after_key) + 1u);
    }
    query.limit = limit;
    locker_repo_result_t *result = &core->repo_result;
    memset(result, 0, sizeof(*result));
    locker_status_t status = locker_repo_query(core->repo, &query, result);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    out->doc.nodes = core->page_nodes;
    out->doc.node_capacity = LOCKER_DOMAIN_PAGE_NODES;
    out->doc.members = core->page_members;
    out->doc.member_capacity = LOCKER_DOMAIN_PAGE_MEMBERS;
    out->revision = result->snapshot_revision;
    if (!locker_domain_json_parse(&out->doc, result->json, result->length)) {
        return LOCKER_STATUS_IO_ERROR;
    }
    out->found = true;
    out->value = locker_domain_json_root(&out->doc);
    return LOCKER_STATUS_OK;
}

/* 恢复期分页读取：走 locker_repo_load_recovery（RepositoryRecoveryPage），
   与普通查询共用 page 池。Core 在开放业务前按同一 snapshot_revision 完成必要
   集合恢复（internal-data.schema.json 的 x-rules 第 10 条）。 */
locker_status_t domain_repo_recovery_page(locker_core_t *core, const char *collection, const char *after_key,
                                         uint16_t limit, domain_doc_t *out)
{
    if (core == NULL || core->repo == NULL || collection == NULL || out == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (limit == 0u || limit > 100u) {
        return LOCKER_STATUS_INVALID;
    }
    memset(out, 0, sizeof(*out));
    locker_repo_query_t query;
    memset(&query, 0, sizeof(query));
    query.schema_version = 1u;
    if (strlen(collection) >= sizeof(query.collection)) {
        return LOCKER_STATUS_INVALID;
    }
    memcpy(query.collection, collection, strlen(collection) + 1u);
    query.key[0] = '\0';
    if (after_key != NULL && after_key[0] != '\0') {
        if (strlen(after_key) >= sizeof(query.after_key)) {
            return LOCKER_STATUS_INVALID;
        }
        memcpy(query.after_key, after_key, strlen(after_key) + 1u);
    }
    query.limit = limit;
    memset(&core->recovery, 0, sizeof(core->recovery));
    locker_status_t status = locker_repo_load_recovery(core->repo, &query, &core->recovery);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    out->doc.nodes = core->page_nodes;
    out->doc.node_capacity = LOCKER_DOMAIN_PAGE_NODES;
    out->doc.members = core->page_members;
    out->doc.member_capacity = LOCKER_DOMAIN_PAGE_MEMBERS;
    out->revision = core->recovery.snapshot_revision;
    if (!locker_domain_json_parse(&out->doc, core->recovery.json, core->recovery.length)) {
        return LOCKER_STATUS_IO_ERROR;
    }
    out->found = true;
    out->value = locker_domain_json_root(&out->doc);
    return LOCKER_STATUS_OK;
}

locker_status_t domain_repo_refresh_revision(locker_core_t *core)
{
    if (core == NULL || core->repo == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    domain_doc_t doc;
    locker_status_t status = domain_repo_page(core, "device_state", NULL, 1u, &doc);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    core->revision = doc.revision;
    return LOCKER_STATUS_OK;
}

/* ---------- 完成槽 ---------- */

uint32_t domain_completion_acquire(locker_core_t *core, locker_uuid_t *out_token)
{
    if (core == NULL || out_token == NULL) {
        return UINT32_MAX;
    }
    pthread_mutex_lock(&core->completion_mutex);
    uint32_t slot = UINT32_MAX;
    for (uint32_t i = 0u; i < LOCKER_DOMAIN_COMPLETION_SLOTS; ++i) {
        if (!core->completions[i].used) {
            slot = i;
            break;
        }
    }
    if (slot != UINT32_MAX) {
        domain_completion_t *completion = &core->completions[slot];
        memset(completion, 0, sizeof(*completion));
        completion->used = true;
        if (locker_clock_port_new_uuid(&core->clock, &completion->token) != LOCKER_STATUS_OK) {
            memset(completion, 0, sizeof(*completion));
            slot = UINT32_MAX;
        } else {
            *out_token = completion->token;
        }
    }
    pthread_mutex_unlock(&core->completion_mutex);
    return slot;
}

void domain_completion_release(locker_core_t *core, uint32_t slot)
{
    if (core == NULL || slot >= LOCKER_DOMAIN_COMPLETION_SLOTS) {
        return;
    }
    pthread_mutex_lock(&core->completion_mutex);
    domain_completion_t *completion = &core->completions[slot];
    free(completion->json);
    memset(completion, 0, sizeof(*completion));
    pthread_mutex_unlock(&core->completion_mutex);
}

void domain_completion_release_owned(locker_core_t *core, uint32_t slot, const locker_uuid_t *token)
{
    if (core == NULL || token == NULL || slot >= LOCKER_DOMAIN_COMPLETION_SLOTS) {
        return;
    }
    pthread_mutex_lock(&core->completion_mutex);
    domain_completion_t *completion = &core->completions[slot];
    /* 槽在等待期间可能已经被回收、又被下一个请求占用。此时按下标释放会
       抹掉别人的在途结果——只有令牌仍然对得上才清槽（审计 R3-B-14）。 */
    if (completion->used && locker_uuid_equal(&completion->token, token)) {
        free(completion->json);
        memset(completion, 0, sizeof(*completion));
    }
    pthread_mutex_unlock(&core->completion_mutex);
}

bool domain_completion_token_matches(locker_core_t *core, uint32_t slot, const locker_uuid_t *token)
{
    if (core == NULL || token == NULL || slot >= LOCKER_DOMAIN_COMPLETION_SLOTS) {
        return false;
    }
    bool matches = false;
    pthread_mutex_lock(&core->completion_mutex);
    const domain_completion_t *completion = &core->completions[slot];
    /* 作业在 Core 线程上异步执行：等待方可能已经超时释放，槽被别的请求复用。
       写结果前核对令牌，才能保证「谁的结果进谁的槽」（审计 R3-B-14）。 */
    matches = completion->used && locker_uuid_equal(&completion->token, token);
    pthread_mutex_unlock(&core->completion_mutex);
    return matches;
}

void domain_completion_accept(locker_core_t *core, uint32_t slot, const locker_uuid_t *token,
                              locker_status_t status, const locker_uuid_t *operation_id, bool has_operation)
{
    if (core == NULL || slot >= LOCKER_DOMAIN_COMPLETION_SLOTS) {
        return;
    }
    pthread_mutex_lock(&core->completion_mutex);
    domain_completion_t *completion = &core->completions[slot];
    /* 身份核对与写入必须在同一把锁里：分两次取锁时，中间槽可能被回收并被
       下一个请求复用，于是这次写入落到了别人的槽上（审计 round-5）。 */
    if (completion->used && (token == NULL || locker_uuid_equal(&completion->token, token))) {
        completion->accept_status = status;
        completion->has_operation = has_operation;
        if (has_operation && operation_id != NULL) {
            completion->operation_id = *operation_id;
        }
        completion->accept_ready = true;
        pthread_cond_broadcast(&core->completion_cond);
    }
    pthread_mutex_unlock(&core->completion_mutex);
}

void domain_completion_finish(locker_core_t *core, uint32_t slot, const locker_uuid_t *token,
                              locker_status_t status, const char *json, size_t length, uint64_t revision)
{
    if (core == NULL || slot >= LOCKER_DOMAIN_COMPLETION_SLOTS) {
        return;
    }
    char *copy = NULL;
    if (json != NULL && length > 0u) {
        copy = (char *)malloc(length + 1u);
        if (copy == NULL) {
            status = LOCKER_STATUS_NO_SPACE;
        } else {
            memcpy(copy, json, length);
            copy[length] = '\0';
        }
    }
    pthread_mutex_lock(&core->completion_mutex);
    domain_completion_t *completion = &core->completions[slot];
    /* 同上：核对与写入同一把锁，避免把结果写进已被别人复用的槽。 */
    if (completion->used && (token == NULL || locker_uuid_equal(&completion->token, token))) {
        free(completion->json);
        completion->json = copy;
        completion->length = (copy != NULL) ? length : 0u;
        completion->final_status = status;
        completion->revision = revision;
        completion->final_ready = true;
        /* 终态保留到保留期结束：调用方可能因为排队延迟而晚到，不能提前清槽。 */
        completion->expires_ms = domain_now(core) + core->completion_retention_ms;
        pthread_cond_broadcast(&core->completion_cond);
    } else {
        free(copy);
    }
    pthread_mutex_unlock(&core->completion_mutex);
}

void domain_completion_fail(locker_core_t *core, uint32_t slot, locker_status_t status)
{
    /* 失败收尾不产生结果体：json 置空、length 置 0，调用方拿到的是纯状态。
       槽已释放（例如结果已被 take 走或已过保留期）时什么也不做，绝不复活旧槽。 */
    if (core == NULL || slot >= LOCKER_DOMAIN_COMPLETION_SLOTS) {
        return;
    }
    pthread_mutex_lock(&core->completion_mutex);
    domain_completion_t *completion = &core->completions[slot];
    if (completion->used) {
        free(completion->json);
        completion->json = NULL;
        completion->length = 0u;
        completion->final_status = status;
        completion->final_ready = true;
        completion->expires_ms = domain_now(core) + core->completion_retention_ms;
        pthread_cond_broadcast(&core->completion_cond);
    }
    pthread_mutex_unlock(&core->completion_mutex);
}

static void completion_deadline(uint32_t timeout_ms, struct timespec *out)
{
    /* 等待上限由契约固定：wait_result 最多 1000 ms，超时不取消。 */
    uint32_t capped = timeout_ms > LOCKER_CORE_RESULT_WAIT_MS ? LOCKER_CORE_RESULT_WAIT_MS : timeout_ms;
    clock_gettime(CLOCK_REALTIME, out);
    out->tv_sec += (time_t)(capped / 1000u);
    out->tv_nsec += (long)(capped % 1000u) * 1000000L;
    if (out->tv_nsec >= 1000000000L) {
        out->tv_sec += 1;
        out->tv_nsec -= 1000000000L;
    }
}

locker_status_t domain_completion_wait_accept(locker_core_t *core, uint32_t slot, uint32_t timeout_ms,
                                              locker_status_t *out_status, locker_uuid_t *out_operation,
                                              bool *out_has_operation)
{
    if (core == NULL || slot >= LOCKER_DOMAIN_COMPLETION_SLOTS || out_status == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    struct timespec deadline;
    completion_deadline(timeout_ms, &deadline);
    pthread_mutex_lock(&core->completion_mutex);
    domain_completion_t *completion = &core->completions[slot];
    while (completion->used && !completion->accept_ready) {
        int rc = pthread_cond_timedwait(&core->completion_cond, &core->completion_mutex, &deadline);
        if (rc != 0) {
            break;
        }
    }
    bool ready = completion->used && completion->accept_ready;
    locker_status_t status = LOCKER_STATUS_OK;
    if (ready) {
        *out_status = completion->accept_status;
        if (out_has_operation != NULL) {
            *out_has_operation = completion->has_operation;
        }
        if (out_operation != NULL) {
            *out_operation = completion->operation_id;
        }
    } else {
        status = LOCKER_STATUS_NOT_READY;
    }
    pthread_mutex_unlock(&core->completion_mutex);
    return status;
}

locker_status_t domain_completion_wait_final(locker_core_t *core, uint32_t slot, const locker_uuid_t *token,
                                             uint32_t timeout_ms, locker_query_result_t *out_result)
{
    if (core == NULL || slot >= LOCKER_DOMAIN_COMPLETION_SLOTS || token == NULL || out_result == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    struct timespec deadline;
    completion_deadline(timeout_ms, &deadline);
    pthread_mutex_lock(&core->completion_mutex);
    domain_completion_t *completion = &core->completions[slot];
    /* 槽号不是身份：保留期结束后 reap 会把槽清空，下一个请求立刻复用它。
       只用槽号等待就会把**别人的终态**当成自己的结果交付（审计 R3-B-14）。
       这里在锁内逐轮核对 token：槽一换人立刻返回 NOT_READY，调用方按契约
       用幂等键重查，绝不会拿到不属于自己的响应体。 */
    while (completion->used && locker_uuid_equal(&completion->token, token) && !completion->final_ready) {
        int rc = pthread_cond_timedwait(&core->completion_cond, &core->completion_mutex, &deadline);
        if (rc != 0) {
            break;
        }
    }
    locker_status_t status = LOCKER_STATUS_NOT_READY;
    if (completion->used && locker_uuid_equal(&completion->token, token) && completion->final_ready) {
        status = LOCKER_STATUS_OK;
        out_result->status = completion->final_status;
        out_result->snapshot_revision = completion->revision;
        out_result->length = 0u;
        out_result->json[0] = '\0';
        if (completion->json != NULL) {
            if (completion->length + 1u > sizeof(out_result->json)) {
                status = LOCKER_STATUS_NO_SPACE;
            } else {
                memcpy(out_result->json, completion->json, completion->length + 1u);
                out_result->length = completion->length;
            }
        }
    }
    pthread_mutex_unlock(&core->completion_mutex);
    return status;
}

uint32_t domain_completion_find(locker_core_t *core, const locker_uuid_t *token)
{
    if (core == NULL || token == NULL) {
        return UINT32_MAX;
    }
    uint32_t found = UINT32_MAX;
    pthread_mutex_lock(&core->completion_mutex);
    for (uint32_t i = 0u; i < LOCKER_DOMAIN_COMPLETION_SLOTS; ++i) {
        if (core->completions[i].used && locker_uuid_equal(&core->completions[i].token, token)) {
            found = i;
            break;
        }
    }
    pthread_mutex_unlock(&core->completion_mutex);
    return found;
}

void domain_completion_set_principal(locker_core_t *core, uint32_t slot, const locker_principal_t *principal)
{
    if (core == NULL || slot >= LOCKER_DOMAIN_COMPLETION_SLOTS || principal == NULL) {
        return;
    }
    pthread_mutex_lock(&core->completion_mutex);
    domain_completion_t *completion = &core->completions[slot];
    if (completion->used) {
        completion->principal = *principal;
        completion->has_principal = true;
    }
    pthread_mutex_unlock(&core->completion_mutex);
}

bool domain_completion_take_principal(locker_core_t *core, uint32_t slot, locker_principal_t *out)
{
    if (core == NULL || slot >= LOCKER_DOMAIN_COMPLETION_SLOTS || out == NULL) {
        return false;
    }
    bool ok = false;
    pthread_mutex_lock(&core->completion_mutex);
    domain_completion_t *completion = &core->completions[slot];
    if (completion->used && completion->has_principal) {
        *out = completion->principal;
        ok = true;
    }
    pthread_mutex_unlock(&core->completion_mutex);
    return ok;
}

void domain_completion_reap(locker_core_t *core, uint64_t now_ms)
{
    if (core == NULL) {
        return;
    }
    pthread_mutex_lock(&core->completion_mutex);
    for (uint32_t i = 0u; i < LOCKER_DOMAIN_COMPLETION_SLOTS; ++i) {
        domain_completion_t *completion = &core->completions[i];
        if (!completion->used || !completion->final_ready) {
            continue;
        }
        /* 只回收已经交付过终态的槽：未取走的终态保留到保留期结束，避免调用方
           永远等不到结果；保留期由 core->completion_retention_ms 决定。 */
        if (completion->expires_ms != 0u && now_ms >= completion->expires_ms) {
            free(completion->json);
            memset(completion, 0, sizeof(*completion));
        }
    }
    pthread_mutex_unlock(&core->completion_mutex);
}

bool domain_completion_take_json(locker_core_t *core, uint32_t slot, char *out, size_t out_size,
                                 size_t *out_length, uint64_t *out_revision)
{
    if (core == NULL || slot >= LOCKER_DOMAIN_COMPLETION_SLOTS || out == NULL || out_size == 0u) {
        return false;
    }
    bool ok = false;
    pthread_mutex_lock(&core->completion_mutex);
    domain_completion_t *completion = &core->completions[slot];
    if (completion->used && completion->final_ready && completion->json != NULL &&
        completion->length + 1u <= out_size) {
        memcpy(out, completion->json, completion->length + 1u);
        if (out_length != NULL) {
            *out_length = completion->length;
        }
        if (out_revision != NULL) {
            *out_revision = completion->revision;
        }
        ok = true;
    }
    pthread_mutex_unlock(&core->completion_mutex);
    return ok;
}
