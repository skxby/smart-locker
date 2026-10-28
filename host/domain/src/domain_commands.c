#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 业务核心：命令、查询、状态机推进与超时。
 *
 * 状态表真源是 docs/stages/stage-00-architecture.md:106-128；本文件是它唯一的
 * 实现点，全部函数只在 Core 线程执行。几条不可放宽的规则：
 *
 *   - 只有 ACK 不进入开门：ACCEPTED 只说明意图被接纳，ACCESS_ACTIVE 必须等
 *     PULSE_STARTED 事实（apply_execute 里判定）。
 *   - 完成必须同时满足「物理变化」与「本人确认」；只有离位快照不算证据。
 *   - 历史读卡（live_transport == false）永远不能产生柜前证明。
 *   - 终结后释放占位；REVIEW_REQUIRED 保留占位（x-rules 第 6 条）。
 *   - 任何写事务失败都不发送动作：outbox 只有在 commit_change 成功后才交给
 *     设备适配器（internal-interfaces.md:114）。
 */

#include "domain_internal.h"

#include <stdio.h>
#include <string.h>

#include "locker/common/log.h"
/* 幂等记录的有效期（契约 LOCKER_IDEMPOTENCY_DAYS 天）。 */
#define DOMAIN_IDEMPOTENCY_MS (LOCKER_IDEMPOTENCY_DAYS * 24u * 60u * 60u * 1000u)
/*
 * 物理事实的最长可核验龄期。设备的 HEALTH/SNAPSHOT 是秒级刷新，30 秒没刷新
 * 说明会话已经哑了（泵仍在重试但拿不到新事实）。核验改账本必须基于新鲜证据，
 * 不能让断开后的缓存值冒充「现在门是关的」。
 */
#define LOCKER_SNAPSHOT_MAX_AGE_MS 30000u
/* 恢复/列表扫描的最大页数，防止一个损坏库把 Core 卡在循环里。 */
#define DOMAIN_SCAN_PAGES 64u

/* ---------- 通用小工具 ---------- */

static bool kind_matches_state(uint8_t kind, uint8_t state)
{
    (void)state;
    return kind == LOCKER_OPERATION_KIND_BORROW || kind == LOCKER_OPERATION_KIND_RETURN;
}

static uint8_t expected_item_for_kind(uint8_t kind)
{
    return kind == LOCKER_OPERATION_KIND_BORROW ? LOCKER_ITEM_STATE_PRESENT : LOCKER_ITEM_STATE_ABSENT;
}

/* 期望的物品终态：借出后应缺席、归还后应在位。 */
static uint8_t resulting_item_for_kind(uint8_t kind)
{
    return kind == LOCKER_OPERATION_KIND_BORROW ? LOCKER_ITEM_STATE_ABSENT : LOCKER_ITEM_STATE_PRESENT;
}

/*
 * 终结事务里要一并落库的附加记录（R3-B-5 / R3-B-6）。
 *
 * 幂等记录与人工审计都必须和业务结果在**同一个** change 里提交。分成两次提交
 * 时，第一次成功、第二次失败会留下无法自愈的分叉：审计反例实测「只让含
 * idempotency mutation 的那次提交返回 IO_ERROR」，就得到 first=OK、
 * replay=CONFLICT、仓储里已经是 CANCELLED——客户端重试看到互相矛盾的结论；
 * 只让 audits 提交失败，则得到 status=OK、stored_state=COMPLETED、
 * loan_open=1、audits=0，事后完全查不到是谁批的。
 *
 * 一个 change 只有一块缓冲、只能提交一次，所以附加记录必须挂进调用方那个
 * change，不能各开一个。
 *
 * 谁：audit_actor 是**执行核验的那个管理员**，不是原申请人——两者经常不同，
 *     把申请人写进 actor_id 等于伪造授权链。
 * 为什么：audit_reason 是请求里带的原始文本（contract-source.json:1502 要求
 *     留下原因），不再只校验长度就丢掉。
 * 何时/基于什么：occurred_at 用墙钟，reason 里同时带上被核验的物理版本，
 *     这样事后能确认管理员看到的是哪一版柜内事实。
 */
typedef struct domain_finish_extras {
    const locker_command_t *command;       /* 幂等键非 nil 时写 IdempotencyRecord */
    const char *request_hash;              /* 与 command 配套 */
    const locker_principal_t *audit_actor; /* 非空时写 AuditRecord */
    const char *audit_type;                /* AuditRecord.type */
    const char *audit_reason;              /* AuditRecord.reason */
    uint64_t audit_physical_revision;      /* 非 0 时把物理版本并进 reason 尾巴 */
} domain_finish_extras_t;

static locker_status_t manual_audit_append(locker_core_t *core, domain_change_t *change,
                                           const domain_operation_t *operation, const locker_principal_t *actor,
                                           const char *type, const char *reason, uint64_t physical_revision);
/* schema 的 maxLength/minLength 都按**字符**（码点）计，不是字节。理由字段的
   校验和截断都必须走这两个函数，用 strlen 比长度会把合法的多字节理由判错
   （审计 round-5：理由长度校验不符合字符数契约）。 */
static size_t audit_char_count(const char *text, size_t length);
static size_t audit_prefix_bytes(const char *text, size_t length, size_t max_chars);
static locker_status_t idempotency_append(locker_core_t *core, domain_change_t *change,
                                          const locker_command_t *command, const char *request_hash,
                                          const char *response_json);
static void build_operation_response(locker_core_t *core, locker_json_writer_t *writer,
                                     const domain_operation_t *operation, uint64_t revision,
                                     const locker_uuid_t *request_id);

/*
 * 物理事实「可核验」：必须在当前会话里、且采样足够新。
 *
 * physical_ready_for_action 是给**发起新动作**用的（还包括设备模式、日志
 * 健康、门状态）。核验用的门槛不一样：管理员要基于柜内现状改账本，只需要
 * 「这份物理事实是刚采到的」。两者都不能只看缓存值是否曾被填过一次。
 */
static bool physical_evidence_fresh(const locker_core_t *core, uint64_t now)
{
    if (!core->device_session_up || !core->session.valid || !core->device_snapshot_valid) {
        return false;
    }
    if (!core->physical.valid || core->physical.observed_mono_ms == 0u) {
        return false;
    }
    if (now < core->physical.observed_mono_ms) {
        return false; /* 单调时钟回退：样本不可信。 */
    }
    return now - core->physical.observed_mono_ms <= LOCKER_SNAPSHOT_MAX_AGE_MS;
}

/*
 * WAIT_CONFIRM 期间的新物理事实必须作废确认依据。
 *
 * 进 WAIT_CONFIRM 时冻结了当时的 door/item/physical_revision **以及代际
 * （boot_id/journal_epoch）**。此后只要出现任何一种变化——门又开了、物品离开
 * 期望值、物理版本推进、物品变成 UNKNOWN、设备换了 boot 或日志世代——就说明
 * 「用户看到的柜内状态」已经和用户将要确认的那次动作无关了。
 * 以前 confirm 只比当前门/物品和那个旧的 item_transition_seen 布尔，
 * 「开一次门再关回去、物品放回原位」就能把冲突洗掉，用户照旧点确认完成
 * （审计 R1-B-4 探针：开门后仍为 WAIT_CONFIRM，随后可正常 complete）；
 * 只比 revision 大小又漏掉换 boot 后版本号从头计数这一层（审计 R3-B-3 探针 A）。
 *
 * 这个判定同时被两处使用：设备侧每次收到新事实时（domain_device.c 的
 * wait_confirm_conflicted）和 confirm 命令自己的决策点上。只在设备侧判是不够的——
 * 判定与确认之间还可能插入新的快照，确认前的最后一刻必须按当前事实再核一遍。
 *
 * 依据：stage-00-architecture.md:118「每个确认只对应那一组物理证据」、
 * internal-interfaces.md:12/:116。
 */
bool domain_wait_confirm_basis_conflicted(const locker_core_t *core, const domain_operation_t *operation)
{
    if (operation == NULL || !operation->used ||
        operation->state != LOCKER_OPERATION_STATE_WAIT_CONFIRM) {
        return false;
    }
    if (!operation->wait_confirm_basis_valid) {
        /* 恢复出来的操作没有冻结依据：按保守规则，任何新事实都不能被当作确认依据。 */
        return true;
    }
    if (core->physical.boot_id != operation->wait_confirm_boot_id ||
        !locker_uuid_equal(&core->physical.journal_epoch, &operation->wait_confirm_journal_epoch)) {
        /* 换 boot 或换日志世代：冻结依据属于上一代，物理版本会从头计数。 */
        return true;
    }
    if (core->physical.physical_revision > operation->wait_confirm_physical_revision) {
        /* 同一代内物理版本只会随每次门/物品变化递增，因此「更大」是唯一能证明
           「真的又变过」的方向。相等时还要逐字段比对（同一版本理应同值，
           不一致说明数据自身矛盾，同样不能当依据）。 */
        return true;
    }
    if (core->physical.physical_revision == operation->wait_confirm_physical_revision) {
        if (core->physical.item == LOCKER_ITEM_STATE_UNKNOWN || core->physical.item != operation->wait_confirm_item) {
            return true;
        }
        if (core->physical.door != operation->wait_confirm_door) {
            return true;
        }
    }
    /* 更小的物理版本说明这份事实比冻结依据更早（典型来源：GET_SNAPSHOT 是异步的，
       它的采样时刻可能早于已经应用的事件）。陈旧事实既不能作废依据，也不能
       反过来被当成「又变了一次」——那会把刚刚成立的确认判成冲突。 */
    return false;
}

static bool physical_ready_for_action(const locker_core_t *core)
{
    if (!core->device_session_up || !core->session.valid || !core->device_snapshot_valid) {
        return false;
    }
    if (core->session.device_mode != LOCKER_DEVICE_MODE_READY) {
        return false;
    }
    if (!core->cursor.continuity_ok || core->fetch_gap) {
        return false;
    }
    if (core->session.journal_state != LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY) {
        return false;
    }
    if (core->physical.door != LOCKER_DOOR_STATE_CLOSED) {
        return false;
    }
    return true;
}

static void response_finish(locker_core_t *core, uint32_t slot, locker_status_t status, bool has_operation,
                            const locker_uuid_t *operation_id)
{
    if (slot == UINT32_MAX) {
        return;
    }
    /* 槽必须仍属于本次作业：等待方超时后按令牌释放、槽被下一个请求复用时，
       这次写回属于上一个请求。令牌核对在 accept/finish 的锁内完成，
       所以「核对」与「写入」之间不会出现槽被换人的窗口（审计 round-5）。 */
    domain_completion_accept(core, slot, &core->job_token, status, operation_id, has_operation);
    domain_completion_finish(core, slot, &core->job_token, status, NULL, 0u, core->revision);
}

static void response_finish_json(locker_core_t *core, uint32_t slot, locker_status_t status, bool has_operation,
                                 const locker_uuid_t *operation_id, const locker_json_writer_t *writer)
{
    if (slot == UINT32_MAX) {
        return;
    }
    domain_completion_accept(core, slot, &core->job_token, status, operation_id, has_operation);
    if (status != LOCKER_STATUS_OK || writer == NULL || !locker_domain_json_writer_ok(writer)) {
        domain_completion_finish(core, slot, &core->job_token,
                                 status == LOCKER_STATUS_OK ? LOCKER_STATUS_NO_SPACE : status, NULL, 0u,
                                 core->revision);
        return;
    }
    domain_completion_finish(core, slot, &core->job_token, LOCKER_STATUS_OK, writer->buffer, writer->length,
                             core->revision);
}

/* ---------- 占位与生命周期 ---------- */

/* 终结操作：释放占位、清活动副本、发布变化。

   `apply_loan` 为真时把借还账本变更并入**同一个** change：终结与账本要么一起
   成功，要么一起失败。分成两次提交会在中途失败时留下「操作 COMPLETED 但
   loans 没动」的半截状态（借出侧账上说已借出却没有有效借用、归还侧账上已完成
   但借用仍 open），x-rules 第 7 条与 interface-expert.md:29 都要求排除这种情形。

   `extras` 里的幂等记录与人工审计同样并入这一个 change（R3-B-5 / R3-B-6）：
   以前它们各自单开一次提交，业务成功而附加记录失败时，客户端重试会拿到
   与仓储互相矛盾的结论，事后也查不到是谁批的。

   `response_out` 非空时，把「提交后应返回给客户端的那份响应」渲染进去：幂等
   记录要存的就是它，必须与最终返回的逐字节相同，所以只能在这里渲染一次
   （版本按 core->revision + 1u，即本次提交成功后的值）。

   **所有状态跃迁都在局部候选副本上做，提交成功后才写回 core->active。**
   直接在 core->active 上改会留下「库还是旧状态、内存已是终态」的分叉：后续
   超时与占位处理按内存判断就再也不会重试，重启后库里的操作永远挂在半路。
   （R1-B-15） */
static locker_status_t finish_operation(locker_core_t *core, domain_operation_t *operation, uint8_t state,
                                        const char *reason_code, bool apply_loan,
                                        const domain_finish_extras_t *extras, locker_json_writer_t *response_out)
{
    if (operation == NULL || !operation->used) {
        return LOCKER_STATUS_INVALID;
    }
    domain_operation_t candidate = *operation;
    locker_status_t status = domain_operation_set_state(core, &candidate, state, reason_code);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    bool has_idempotency = extras != NULL && extras->command != NULL &&
                           !locker_uuid_is_nil(&extras->command->idempotency_key);
    locker_json_writer_t local_response;
    locker_json_writer_t *response = response_out;
    if (response == NULL && has_idempotency) {
        response = &local_response;
    }
    if (response != NULL) {
        if (extras == NULL || extras->command == NULL) {
            return LOCKER_STATUS_INVALID;
        }
        build_operation_response(core, response, &candidate, core->revision + 1u, &extras->command->request_id);
    }
    domain_change_t change;
    domain_change_init(core, &change, core->revision, core->revision + 1u);
    /* RepositoryChange 的 checks 段必须在 mutations 之前一次性写完，
       所以账本条件先写、账本变更后写。顺序反了会得到非法 JSON。 */
    char loan_stamp[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    loan_stamp[0] = '\0';
    if (apply_loan && candidate.has_loan) {
        status = domain_loan_check(core, &candidate, &change);
        if (status != LOCKER_STATUS_OK) {
            return status;
        }
    }
    status = domain_operation_persist(core, &change, &candidate);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    if (!domain_change_delete(&change, "slot_guards", "1")) {
        return LOCKER_STATUS_NO_SPACE;
    }
    if (apply_loan && candidate.has_loan) {
        status = domain_loan_apply(core, &candidate, &change, loan_stamp, sizeof(loan_stamp));
        if (status != LOCKER_STATUS_OK) {
            return status;
        }
    }
    if (has_idempotency) {
        status = idempotency_append(core, &change, extras->command, extras->request_hash, response->buffer);
        if (status != LOCKER_STATUS_OK) {
            return status;
        }
    }
    if (extras != NULL && extras->audit_actor != NULL) {
        status = manual_audit_append(core, &change, &candidate, extras->audit_actor, extras->audit_type,
                                     extras->audit_reason, extras->audit_physical_revision);
        if (status != LOCKER_STATUS_OK) {
            return status;
        }
    }
    status = domain_change_commit(core, &change);
    if (status != LOCKER_STATUS_OK) {
        /* 提交失败：内存保持原样，可继续核对/重试。 */
        return status;
    }
    /* 到这里库已经落定，才把候选副本装回内存。 */
    *operation = candidate;
    if (apply_loan && candidate.has_loan) {
        /* 只有提交成功才更新内存副本：提交失败的 change 不会走到这里，
           否则内存会和库分叉。时间戳用事务里写过的同一个值。 */
        domain_loan_commit_in_memory(core, &candidate, loan_stamp);
    }
    /* 终态操作不再占用柜格：活动副本清空，下一次申请可以立即进入。 */
    memset(&core->active, 0, sizeof(core->active));
    /* 设备侧的 has_active 只有 MCU 能清：终端状态落库后必须登记一次
       RELEASE_CONTEXT，否则下一次 EXECUTE 永远 BUSY——柜子只能用一次，
       借出成功不代表能还回去（审计 R1-B-3）。释放失败只重发/重核，
       绝不重放旧开锁动作。 */
    if (candidate.has_command) {
        core->release_pending = true;
        core->release_sent = false;
        core->release_ms = domain_now(core);
        core->release_operation_id = candidate.id;
        core->release_command_id = candidate.command_id;
    }
    domain_publish(core, &candidate.id, "operation", core->revision);
    return LOCKER_STATUS_OK;
}

/* 把操作推进到核验：保留占位（x-rules 6），并持久化原因。

   和 finish_operation 一样，状态跃迁先发生在局部候选副本上：提交失败时内存
   必须保持原来那个待处理状态，下一次处理才能继续重试。以前这里先改内存再
   持久化，失败后内存已经是 REVIEW_REQUIRED，于是下一次调用被「已经是核验」
   短路直接返回 OK——核验结果永远补不进仓储。（R1-B-15） */
locker_status_t domain_require_review(locker_core_t *core, domain_operation_t *operation,
                                      const char *reason_code)
{
    if (operation == NULL || !operation->used) {
        return LOCKER_STATUS_INVALID;
    }
    if (operation->state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED) {
        return LOCKER_STATUS_OK;
    }
    domain_operation_t candidate = *operation;
    locker_status_t status = domain_operation_set_state(core, &candidate, LOCKER_OPERATION_STATE_REVIEW_REQUIRED,
                                                        reason_code);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = domain_persist_operation(core, &candidate);
    if (status != LOCKER_STATUS_OK) {
        /* 提交失败：内存保持原状态，下一次仍会重新尝试核验。 */
        return status;
    }
    *operation = candidate;
    /* 核验必须同步到活动工作副本：调用方可能拿的是刚从仓储读出的局部副本
       （confirm/cancel 就是这样），只改局部副本会让 Core 内存里留着旧状态，
       快照投影、超时推进和后续命令都按过期事实判断。 */
    if (core->active.used && locker_uuid_equal(&core->active.id, &candidate.id)) {
        core->active = candidate;
    }
    domain_publish(core, &candidate.id, "operation", core->revision);
    return LOCKER_STATUS_OK;
}

/* ---------- 幂等 ---------- */

/* 参数规范化：幂等的判据是「同一个意图」，不是「同一串字节」。
   直接哈希 arguments_json 会让 `{"tool_id":"x","kind":"BORROW"}` 与
   `{ "kind": "BORROW", "tool_id": "x" }` 变成两个不同请求——客户端只是重新
   序列化了一次，用户却被拒绝（审计 R1-B-8）。所以先把参数按 key 字典序
   重排，再逐字段喂进哈希，只对语义求指纹。

   这里不拼出完整规范化字符串：单条命令的参数可达 4097 字节，Core 线程栈上
   开 64 KB 缓冲只为算个哈希不值得。改成边渲染边喂哈希，单个值用一个
   4097 字节的栈缓冲（小于任何参数的总体上限，够用）。

   解析失败时返回 false，调用方退回原始文本：非法参数后续必然被 INVALID
   拒掉，不需要在这里判死；但也不能因为解析不了就当成空参数——那会让两条
   不同的坏请求互相重放。 */
static bool hash_arguments(locker_core_t *core, const locker_command_t *command, locker_domain_sha256_t *hash)
{
    size_t length = domain_strnlen(command->arguments_json, sizeof(command->arguments_json));
    if (length == 0u) {
        return false;
    }
    locker_json_document_t doc;
    memset(&doc, 0, sizeof(doc));
    doc.nodes = core->arg_nodes;
    doc.node_capacity = LOCKER_DOMAIN_DOC_NODES;
    doc.members = core->arg_members;
    doc.member_capacity = LOCKER_DOMAIN_DOC_MEMBERS;
    if (!locker_domain_json_parse(&doc, command->arguments_json, length)) {
        return false;
    }
    const locker_json_node_t *root = locker_domain_json_root(&doc);
    if (root == NULL || root->type != LOCKER_JSON_OBJECT) {
        return false;
    }
    /* 契约里命令参数对象最多 5 个键（operation_revision/kind/tool_id/reason/
       resolution），给 32 个余量。超出就不规范化、退回原文：宁可多报一次
       CONFLICT，也不在 Core 线程栈上开跟参数池同规模的数组。 */
    enum { CANONICAL_MAX_MEMBERS = 32 };
    struct canonical_entry {
        char key[64];
        const locker_json_node_t *node;
    };
    struct canonical_entry entries[CANONICAL_MAX_MEMBERS];
    uint32_t count = root->count;
    if (count > (uint32_t)CANONICAL_MAX_MEMBERS) {
        return false;
    }
    /* 成员链顺序与 locker_domain_json_at 一致，key 直接从成员表取。 */
    {
        uint32_t member = root->first;
        uint32_t index = 0u;
        while (member != UINT32_MAX && index < count) {
            const locker_json_member_t *entry = &doc.members[member];
            size_t key_length = entry->key_length;
            if (key_length + 1u > sizeof(entries[index].key)) {
                return false;
            }
            memcpy(entries[index].key, doc.text + entry->key_offset, key_length);
            entries[index].key[key_length] = '\0';
            entries[index].node = &doc.nodes[entry->node];
            member = entry->next;
            index += 1u;
        }
        if (index != count) {
            return false;
        }
    }
    for (uint32_t i = 1u; i < count; ++i) {
        uint32_t j = i;
        while (j > 0u && strcmp(entries[j - 1u].key, entries[j].key) > 0) {
            struct canonical_entry swap = entries[j - 1u];
            entries[j - 1u] = entries[j];
            entries[j] = swap;
            j -= 1u;
        }
    }
    char value_text[4097];
    for (uint32_t i = 0u; i < count; ++i) {
        size_t rendered = 0u;
        if (!locker_domain_json_render(&doc, entries[i].node, value_text, sizeof(value_text), &rendered) ||
            rendered == 0u || rendered >= sizeof(value_text)) {
            return false;
        }
        locker_domain_sha256_update(hash, entries[i].key, strlen(entries[i].key));
        locker_domain_sha256_update(hash, ":", 1u);
        locker_domain_sha256_update(hash, value_text, rendered);
        locker_domain_sha256_update(hash, "\0", 1u);
    }
    return true;
}

static bool build_request_hash(locker_core_t *core, const locker_command_t *command, char *out, size_t out_size)
{
    locker_domain_sha256_t hash;
    locker_domain_sha256_init(&hash);
    char actor[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&command->principal.user_id, actor, sizeof(actor))) {
        return false;
    }
    locker_domain_sha256_update(&hash, actor, strlen(actor));
    locker_domain_sha256_update(&hash, "\0", 1u);
    locker_domain_sha256_update(&hash, command->kind, domain_strnlen(command->kind, sizeof(command->kind)));
    locker_domain_sha256_update(&hash, "\0", 1u);
    char path[LOCKER_DOMAIN_UUID_TEXT + 8u];
    if (domain_uuid_to_text(&command->resource_id, path, LOCKER_DOMAIN_UUID_TEXT)) {
        locker_domain_sha256_update(&hash, path, strlen(path));
    }
    locker_domain_sha256_update(&hash, "\0", 1u);
    /* 语义规范化失败时退回原文（见 hash_arguments 的注释）。 */
    if (!hash_arguments(core, command, &hash)) {
        locker_domain_sha256_update(&hash, command->arguments_json,
                                    domain_strnlen(command->arguments_json, sizeof(command->arguments_json)));
    }
    uint8_t digest[32];
    locker_domain_sha256_final(&hash, digest);
    static const char k_hex[] = "0123456789abcdef";
    if (out_size < 65u) {
        return false;
    }
    for (uint32_t i = 0u; i < 32u; ++i) {
        out[i * 2u] = k_hex[digest[i] >> 4];
        out[i * 2u + 1u] = k_hex[digest[i] & 15u];
    }
    out[64] = '\0';
    return true;
}

/* confirm/cancel/resolve 的幂等记录：把「这条命令产生的完整响应」按请求哈希
   落库，之后同键同参数重放直接返回它（审计 R1-B-8：以前只有 create 写幂等，
   同一个 cancel 发两次，第二次会被状态机判成 CONFLICT，客户端重试反而看到
   互相矛盾的结果）。

   **这条记录必须挂在业务 change 里一起提交**（R3-B-5）。以前这里单开一次提交，
   审计反例「只让含 idempotency mutation 的那次提交返回 IO_ERROR」得到
   first=OK、replay=CONFLICT、仓储里已经是 CANCELLED——同一个请求既成功又被
   判冲突。现在由 finish_operation 在同一个 change 里追加，成功失败完全同步。

   响应体按「提交后的版本」写：change 提交成功会让 core->revision 加一，所以
   调用方要按 core->revision + 1u 渲染这份 JSON。 */
static locker_status_t idempotency_append(locker_core_t *core, domain_change_t *change,
                                          const locker_command_t *command, const char *request_hash,
                                          const char *response_json)
{
    if (command == NULL || locker_uuid_is_nil(&command->idempotency_key) || response_json == NULL ||
        request_hash == NULL) {
        /* 没带幂等键：这条命令不需要可重放记录，不是错误。 */
        return LOCKER_STATUS_OK;
    }
    char idem_key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&command->idempotency_key, idem_key, sizeof(idem_key))) {
        return LOCKER_STATUS_INVALID;
    }
    char expires[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    uint64_t wall = 0u;
    if (locker_clock_port_wall_utc_ms(&core->clock, &wall) != LOCKER_STATUS_OK ||
        !locker_domain_time_format_utc(wall + DOMAIN_IDEMPOTENCY_MS, expires, sizeof(expires))) {
        return LOCKER_STATUS_IO_ERROR;
    }
    if (!domain_change_put_begin(change, "idempotency", idem_key)) {
        return LOCKER_STATUS_NO_SPACE;
    }
    /* path 用 Core 命令名（与 create 一致）：同一把幂等键落在不同资源上时
       不能互相重放，命令名足够区分这三条路由。 */
    domain_write_idempotency(&change->writer, &command->principal.user_id, "POST", command->kind,
                             &command->idempotency_key, request_hash, 200, response_json, expires);
    domain_change_put_end(change);
    return LOCKER_STATUS_OK;
}

/* 一条成功终结/取消/核验的响应体：data 就是 Operation 本身，snapshot_revision
   必须是提交后的真实版本。投影写手自带完整 `{...}`，收尾用 end_value。 */
static void build_operation_response(locker_core_t *core, locker_json_writer_t *writer,
                                     const domain_operation_t *operation, uint64_t revision,
                                     const locker_uuid_t *request_id)
{
    domain_response_begin(writer, core->response_buffer, sizeof(core->response_buffer));
    domain_write_operation_projection(writer, core, operation);
    domain_response_end_value(writer, revision, request_id);
}

/*
 * 幂等重放：同一 actor + 同一 request_hash 的重复意图返回原响应，不产生新
 * operation；哈希不同则 CONFLICT（internal-data.schema.json 的
 * IdempotencyRecord 只存 Core 计算的哈希，Repo 不自行序列化）。
 */
typedef enum domain_idempotency_result {
    DOMAIN_IDEMPOTENCY_NONE = 0,
    DOMAIN_IDEMPOTENCY_REPLAY = 1,
    DOMAIN_IDEMPOTENCY_CONFLICT = 2
} domain_idempotency_result_t;

static domain_idempotency_result_t idempotency_lookup(locker_core_t *core, const locker_command_t *command,
                                                      const char *request_hash, locker_json_writer_t *writer,
                                                      uint64_t *out_revision)
{
    if (locker_uuid_is_nil(&command->idempotency_key)) {
        return DOMAIN_IDEMPOTENCY_NONE;
    }
    char key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&command->idempotency_key, key, sizeof(key))) {
        return DOMAIN_IDEMPOTENCY_CONFLICT;
    }
    domain_doc_t doc;
    if (domain_repo_doc(core, 0, "idempotency", key, &doc) != LOCKER_STATUS_OK) {
        return DOMAIN_IDEMPOTENCY_NONE;
    }
    if (!doc.found || doc.value == NULL) {
        return DOMAIN_IDEMPOTENCY_NONE;
    }
    locker_uuid_t actor;
    if (!domain_json_read_uuid(&doc.doc, domain_json_member(&doc.doc, doc.value, "actor_id"), &actor) ||
        !locker_uuid_equal(&actor, &command->principal.user_id)) {
        return DOMAIN_IDEMPOTENCY_CONFLICT;
    }
    char stored_hash[65];
    if (!domain_json_read_text(&doc.doc, domain_json_member(&doc.doc, doc.value, "request_hash"), stored_hash,
                               sizeof(stored_hash)) ||
        !domain_text_equal(stored_hash, request_hash)) {
        return DOMAIN_IDEMPOTENCY_CONFLICT;
    }
    const locker_json_node_t *response = domain_json_member(&doc.doc, doc.value, "response_json");
    if (response == NULL || response->type != LOCKER_JSON_STRING) {
        return DOMAIN_IDEMPOTENCY_CONFLICT;
    }
    /* response_json 是原样保存的完整响应体（含 data/snapshot_revision/request_id）。 */
    size_t length = 0u;
    if (writer != NULL) {
        locker_domain_json_literal(writer, "");
        if (!locker_domain_json_text(&doc.doc, response, writer->buffer, writer->capacity, &length)) {
            return DOMAIN_IDEMPOTENCY_CONFLICT;
        }
        writer->length = length;
    }
    if (out_revision != NULL) {
        *out_revision = doc.revision;
    }
    return DOMAIN_IDEMPOTENCY_REPLAY;
}

/* ---------- 投影写手 ---------- */

static void write_tool_object(locker_json_writer_t *writer, locker_core_t *core)
{
    if (!core->tool_valid) {
        locker_domain_json_null(writer);
        return;
    }
    core->tool_availability = domain_tool_availability(core);
    domain_write_tool(writer, &core->tool_id, core->tool_name, core->tool_revision, core->tool_availability);
}

static void write_loan_object(locker_json_writer_t *writer, locker_core_t *core)
{
    if (!core->loan_open) {
        locker_domain_json_null(writer);
        return;
    }
    /* 归还操作没有开着的 loan 时也要能回出历史记录，这里只投影当前有效借用。 */
    domain_write_loan(writer, &core->loan_id, &core->loan_tool_id, &core->loan_borrower_id, &core->loan_id, NULL,
                      false, core->loan_opened_at, NULL, false);
}

/* ---------- OPERATION_CREATE ---------- */

static locker_status_t parse_create_request(locker_core_t *core, const locker_command_t *command,
                                            locker_uuid_t *out_tool_id, uint8_t *out_kind, uint64_t *out_tool_revision)
{
    locker_json_document_t doc;
    memset(&doc, 0, sizeof(doc));
    doc.nodes = core->arg_nodes;
    doc.node_capacity = LOCKER_DOMAIN_DOC_NODES;
    doc.members = core->arg_members;
    doc.member_capacity = LOCKER_DOMAIN_DOC_MEMBERS;
    size_t length = domain_strnlen(command->arguments_json, sizeof(command->arguments_json));
    if (length == 0u || !locker_domain_json_parse(&doc, command->arguments_json, length)) {
        return LOCKER_STATUS_INVALID;
    }
    const locker_json_node_t *root = locker_domain_json_root(&doc);
    if (root == NULL || root->type != LOCKER_JSON_OBJECT) {
        return LOCKER_STATUS_INVALID;
    }
    if (!domain_json_read_uuid(&doc, domain_json_member(&doc, root, "tool_id"), out_tool_id)) {
        return LOCKER_STATUS_INVALID;
    }
    if (!domain_json_read_enum_kind(&doc, domain_json_member(&doc, root, "kind"), out_kind)) {
        return LOCKER_STATUS_INVALID;
    }
    if (!domain_json_read_u64(&doc, domain_json_member(&doc, root, "tool_revision"), out_tool_revision)) {
        return LOCKER_STATUS_INVALID;
    }
    return LOCKER_STATUS_OK;
}

static locker_status_t command_operation_create(locker_core_t *core, const locker_command_t *command, uint32_t slot)
{
    locker_uuid_t tool_id;
    uint8_t kind = 0u;
    uint64_t tool_revision = 0u;
    locker_status_t status = parse_create_request(core, command, &tool_id, &kind, &tool_revision);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return status;
    }
    if (core->phase != DOMAIN_PHASE_READY) {
        response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
        return LOCKER_STATUS_NOT_READY;
    }
    /* 本柜只有一个柜格、一件工具：工具必须就是投影出来的那一件，版本必须
       一致。以前写成 `if (core->tool_valid && …)`——只要投影没恢复成功
       （tool_valid=false），这个检查就被跳过，任何 UUID（包括根本不存在的
       工具）都能进账本，`tool_revision` 更是从头到尾没人看（审计 R1-B-9：
       探针在 tool_valid=false 时提交未知工具 + revision=999 返回 OK）。
       工具不可知时只能拒绝，不能用 false 当作「没约束」。 */
    if (!core->tool_valid) {
        response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
        return LOCKER_STATUS_NOT_READY;
    }
    if (!locker_uuid_equal(&tool_id, &core->tool_id)) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    if (tool_revision != core->tool_revision) {
        /* 陈旧或未来的工具版本都可能绕过后续物理证据：revision 是「用户看到的
           那一版工具数据」，必须与当前投影精确相等。
           这里曾经写成 `tool_revision != 0u && …`，给 0 开了「调用方没声明版本」
           的例外——但真源 `contracts/contract-source.json` 的
           `CreateOperationRequest.required` 明确含 `tool_revision`（类型 `U64`），
           服务端要做的是乐观检查，没有任何「不声明版本」的分支；`U64` 允许 0
           只说明 0 是个合法取值，不说明 0 可以跳过比较（审计 R3-B-9：当前
           revision=1 时提交文本 "0" 探针返回 OK）。 */
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    if (core->active.used && !domain_terminal_state(core->active.state)) {
        /* 两申请竞争：后到的明确 SLOT_BUSY，不排队等待。 */
        response_finish(core, slot, LOCKER_STATUS_BUSY, false, NULL);
        return LOCKER_STATUS_BUSY;
    }
    if (kind == LOCKER_OPERATION_KIND_BORROW) {
        if (core->loan_open) {
            response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
            return LOCKER_STATUS_CONFLICT;
        }
        if (!physical_ready_for_action(core) || core->physical.item != LOCKER_ITEM_STATE_PRESENT) {
            response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
            return LOCKER_STATUS_NOT_READY;
        }
    } else {
        /* 归还必须是当前借用人本人，且柜里有物品在位。 */
        if (!core->loan_open) {
            response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
            return LOCKER_STATUS_CONFLICT;
        }
        if (!locker_uuid_equal(&core->loan_borrower_id, &command->principal.user_id)) {
            response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
            return LOCKER_STATUS_INVALID;
        }
        if (!physical_ready_for_action(core) || core->physical.item != LOCKER_ITEM_STATE_ABSENT) {
            response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
            return LOCKER_STATUS_NOT_READY;
        }
    }
    (void)kind_matches_state;

    domain_operation_t operation;
    domain_operation_reset(&operation);
    operation.used = true;
    if (locker_clock_port_new_uuid(&core->clock, &operation.id) != LOCKER_STATUS_OK) {
        response_finish(core, slot, LOCKER_STATUS_IO_ERROR, false, NULL);
        return LOCKER_STATUS_IO_ERROR;
    }
    operation.tool_id = tool_id;
    operation.actor_id = command->principal.user_id;
    operation.kind = kind;
    operation.state = LOCKER_OPERATION_STATE_REQUESTED;
    operation.revision = 1u;
    operation.history_complete = true;
    operation.has_baseline = true;
    operation.baseline = core->physical;
    domain_operation_capture_physical(&operation, &core->physical);
    domain_operation_touch(core, &operation);
    operation.requested_deadline_ms = domain_now(core) + LOCKER_REQUEST_TTL_MS;

    /* 先把响应体拼好：同一事务里还要写幂等记录，重放必须逐字相同。 */
    char key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&operation.id, key, sizeof(key))) {
        response_finish(core, slot, LOCKER_STATUS_IO_ERROR, false, NULL);
        return LOCKER_STATUS_IO_ERROR;
    }
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    /* data 必须就是 Operation 本身（openapi OperationResponse.data → Operation）。
       投影写手 domain_write_operation_projection 自己就写出完整的 `{...}`，
       所以这里既不能再调 domain_response_data_object（会得到 `{"data":{{"id":…`），
       也必须用 domain_response_end_value（它不补 data 对象的 `}`）。 */
    domain_write_operation_projection(&writer, core, &operation);
    domain_response_end_value(&writer, core->revision + 1u, &command->request_id);
    if (!locker_domain_json_writer_ok(&writer)) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }

    char request_hash[65];
    if (!build_request_hash(core, command, request_hash, sizeof(request_hash))) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    char expires[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    uint64_t wall = 0u;
    if (locker_clock_port_wall_utc_ms(&core->clock, &wall) != LOCKER_STATUS_OK ||
        !locker_domain_time_format_utc(wall + DOMAIN_IDEMPOTENCY_MS, expires, sizeof(expires))) {
        response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
        return LOCKER_STATUS_NOT_READY;
    }

    domain_change_t change;
    domain_change_init(core, &change, core->revision, core->revision + 1u);
    if (!domain_change_check(&change, "slot_guards", "1", "ABSENT") ||
        !domain_change_check(&change, "operations", key, "ABSENT")) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }
    status = domain_operation_persist(core, &change, &operation);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    if (!domain_change_put_begin(&change, "slot_guards", "1")) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }
    domain_write_slot_guard(&change.writer, &operation.id);
    domain_change_put_end(&change);
    if (!locker_uuid_is_nil(&command->idempotency_key)) {
        char idem_key[LOCKER_DOMAIN_UUID_TEXT];
        if (!domain_uuid_to_text(&command->idempotency_key, idem_key, sizeof(idem_key))) {
            response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
            return LOCKER_STATUS_INVALID;
        }
        if (!domain_change_put_begin(&change, "idempotency", idem_key)) {
            response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
            return LOCKER_STATUS_NO_SPACE;
        }
        domain_write_idempotency(&change.writer, &command->principal.user_id, "POST", command->kind, 
                                 &command->idempotency_key, request_hash, 201, writer.buffer, expires);
        domain_change_put_end(&change);
    }
    status = domain_change_commit(core, &change);
    if (status != LOCKER_STATUS_OK) {
        /* 事务失败：不留任何内存态，也不发动作。 */
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    core->active = operation;
    domain_publish(core, &operation.id, "operation", core->revision);
    /* 落库后的响应 snapshot_revision 用提交后的真实版本。 */
    locker_json_writer_t final_writer;
    domain_response_begin(&final_writer, core->response_buffer, sizeof(core->response_buffer));
    /* 投影自带完整 `{...}`；用 end_value 收尾，避免多补一层 `}`。 */
    domain_write_operation_projection(&final_writer, core, &operation);
    domain_response_end_value(&final_writer, core->revision, &command->request_id);
    response_finish_json(core, slot, LOCKER_STATUS_OK, true, &operation.id, &final_writer);
    return LOCKER_STATUS_OK;
}

/* ---------- OPERATION_CONFIRM / CANCEL / RESOLVE ---------- */

static bool parse_revision_request(locker_core_t *core, const locker_command_t *command, uint64_t *out_revision,
                                   char *out_reason, size_t reason_size, bool require_reason, bool *has_reason)
{
    locker_json_document_t doc;
    memset(&doc, 0, sizeof(doc));
    doc.nodes = core->arg_nodes;
    doc.node_capacity = LOCKER_DOMAIN_DOC_NODES;
    doc.members = core->arg_members;
    doc.member_capacity = LOCKER_DOMAIN_DOC_MEMBERS;
    size_t length = domain_strnlen(command->arguments_json, sizeof(command->arguments_json));
    if (length == 0u || !locker_domain_json_parse(&doc, command->arguments_json, length)) {
        return false;
    }
    const locker_json_node_t *root = locker_domain_json_root(&doc);
    if (root == NULL || root->type != LOCKER_JSON_OBJECT) {
        return false;
    }
    if (!domain_json_read_u64(&doc, domain_json_member(&doc, root, "operation_revision"), out_revision)) {
        return false;
    }
    if (out_reason != NULL && reason_size > 0u) {
        out_reason[0] = '\0';
        if (has_reason != NULL) {
            *has_reason = false;
        }
        const locker_json_node_t *reason = domain_json_member(&doc, root, "reason");
        if (reason != NULL && reason->type == LOCKER_JSON_STRING) {
            if (!domain_json_read_text(&doc, reason, out_reason, reason_size)) {
                return false;
            }
            if (has_reason != NULL) {
                *has_reason = true;
            }
        } else if (require_reason) {
            return false;
        }
    }
    return true;
}

static locker_status_t command_operation_confirm(locker_core_t *core, const locker_command_t *command, uint32_t slot,
                                                 const char *request_hash)
{
    uint64_t revision = 0u;
    if (!parse_revision_request(core, command, &revision, NULL, 0u, false, NULL)) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    domain_operation_t operation;
    locker_status_t status = domain_operation_load(core, &command->resource_id, 0, &operation);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    if (!locker_uuid_equal(&operation.actor_id, &command->principal.user_id)) {
        /* 错误用户：不能替别人确认。 */
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    if (operation.revision != revision) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    if (operation.state != LOCKER_OPERATION_STATE_WAIT_CONFIRM) {
        /* 提前确认（还在 ACCESS_ACTIVE/REQUESTED）一律拒绝。 */
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    /* 确认前重验当前物理条件：门必须闭、物品必须已到期望终态、连续性完好。 */
    if (!physical_ready_for_action(core) || core->physical.item != resulting_item_for_kind(operation.kind) ||
        !operation.item_transition_seen) {
        status = domain_require_review(core, &operation, "CONFIRM_PRECONDITION");
        response_finish(core, slot, status == LOCKER_STATUS_OK ? LOCKER_STATUS_CONFLICT : status, false, NULL);
        return status;
    }
    /* 决策点再核一遍冻结依据：设备侧每次收到新事实都会判一次，但「判定」与
       「确认」之间还可能插入一份新快照。只信设备侧那一次，存储失败期间
       （转核验没落库、内存仍 WAIT_CONFIRM）就能把已知冲突直接确认完成
       （审计 R3-B-3 探针 B）。 */
    if (core->active.used && locker_uuid_equal(&core->active.id, &operation.id)) {
        if (core->active.wait_confirm_blocked || domain_wait_confirm_basis_conflicted(core, &core->active)) {
            status = domain_require_review(core, &core->active, "PHYSICAL_CHANGED");
            response_finish(core, slot, status == LOCKER_STATUS_OK ? LOCKER_STATUS_CONFLICT : status, false, NULL);
            return status;
        }
    }
    /* 不要先把仓储副本装进 core->active 再走事务：提交失败时活动工作副本
       已经被这份「重载出来的记录」覆盖，原来那个正在计时的申请连同它的运行时
       截止点一起丢了，占位再也不会自然到期（审计 R3-B-13：取消失败后
       active.deadline 被换成 UTC 量级的截止点，推进 130 s 仍是 REQUESTED、
       used=1）。所有终结都只在局部副本上准备，finish_operation 提交成功后
       才动内存。 */
    domain_operation_t target = operation;
    /* 先把 loan_id 挂到操作上，让 OperationRecord.related_loan_id 与终结同事务落库。 */
    status = domain_loan_prepare(core, &target);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, true, &target.id);
        return status;
    }
    /* 终结、账本、幂等记录同一个事务：任何一步失败都不会留下「已完成但账本
       没动」或「业务已提交但重放会冲突」的分叉。响应体也在事务里按提交后的
       版本渲染一次，落库的那份与返回的那份逐字节相同。 */
    domain_finish_extras_t extras;
    memset(&extras, 0, sizeof(extras));
    extras.command = command;
    extras.request_hash = request_hash;
    locker_json_writer_t writer;
    status = finish_operation(core, &target, LOCKER_OPERATION_STATE_COMPLETED, "CONFIRMED", true, &extras,
                              &writer);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    domain_operation_reset(&target);
    response_finish_json(core, slot, LOCKER_STATUS_OK, true, &operation.id, &writer);
    return LOCKER_STATUS_OK;
}

static locker_status_t command_operation_cancel(locker_core_t *core, const locker_command_t *command, uint32_t slot,
                                                const char *request_hash)
{
    uint64_t revision = 0u;
    /* 理由按**字符**限长（CancelRequest.reason minLength 1 / maxLength 256，
       contract-source.json），不是字节。缓冲区给到整个 arguments_json 的规模，
       免得 100 个汉字（300 字节、合法）在读取阶段就被判成非法输入；
       真正的截断在写审计记录时按码点边界做（审计 R3-B-6）。 */
    char reason[4097];
    bool has_reason = false;
    if (!parse_revision_request(core, command, &revision, reason, sizeof(reason), true, &has_reason) || !has_reason) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    domain_operation_t operation;
    locker_status_t status = domain_operation_load(core, &command->resource_id, 0, &operation);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    if (!locker_uuid_equal(&operation.actor_id, &command->principal.user_id)) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    if (operation.revision != revision) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    if (operation.state == LOCKER_OPERATION_STATE_REQUESTED) {
        /* 还没有任何动作意图：可以安全取消并释放占位。
           同样只在局部副本上终结：core->active 是正在计时的那份工作副本，
           取消提交失败时它必须保持原样（见 R3-B-13 的说明）。 */
        domain_operation_t target = operation;
        /* 取消理由必须落库：CancelRequest.reason 是必填字段
           （contract-source.json 的 schemas.CancelRequest），以前只校验长度就
           丢掉，事后无从知道用户为什么撤单（审计 R3-B-6 后半）。审计与终结、
           幂等记录同一个事务，任何一条写不进去就整体不生效。 */
        domain_finish_extras_t extras;
        memset(&extras, 0, sizeof(extras));
        extras.command = command;
        extras.request_hash = request_hash;
        extras.audit_actor = &command->principal;
        extras.audit_type = "OPERATION_CANCELLED";
        extras.audit_reason = reason;
        locker_json_writer_t writer;
        status = finish_operation(core, &target, LOCKER_OPERATION_STATE_CANCELLED, "CANCELLED", false, &extras,
                                  &writer);
        if (status != LOCKER_STATUS_OK) {
            response_finish(core, slot, status, false, NULL);
            return status;
        }
        domain_operation_reset(&target);
        response_finish_json(core, slot, LOCKER_STATUS_OK, true, &operation.id, &writer);
        return LOCKER_STATUS_OK;
    }
    if (domain_terminal_state(operation.state)) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    /* 可能已执行：取消只能是核验请求，不能声称取消成功（internal-interfaces.md:110）。 */
    status = domain_require_review(core, &operation, "CANCEL_AFTER_ACTION");
    response_finish(core, slot, status == LOCKER_STATUS_OK ? LOCKER_STATUS_CONFLICT : status, false, NULL);
    return status;
}

static locker_status_t command_operation_resolve(locker_core_t *core, const locker_command_t *command, uint32_t slot,
                                                 const char *request_hash)
{
    if (command->principal.role != LOCKER_ROLE_ADMIN) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    locker_json_document_t doc;
    memset(&doc, 0, sizeof(doc));
    doc.nodes = core->arg_nodes;
    doc.node_capacity = LOCKER_DOMAIN_DOC_NODES;
    doc.members = core->arg_members;
    doc.member_capacity = LOCKER_DOMAIN_DOC_MEMBERS;
    size_t length = domain_strnlen(command->arguments_json, sizeof(command->arguments_json));
    if (length == 0u || !locker_domain_json_parse(&doc, command->arguments_json, length)) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    const locker_json_node_t *root = locker_domain_json_root(&doc);
    uint64_t revision = 0u;
    uint64_t physical_revision = 0u;
    uint8_t resolution = 0u;
    char reason[257];
    if (root == NULL || !domain_json_read_u64(&doc, domain_json_member(&doc, root, "operation_revision"), &revision) ||
        !domain_json_read_u64(&doc, domain_json_member(&doc, root, "physical_revision"), &physical_revision) ||
        !domain_json_read_text(&doc, domain_json_member(&doc, root, "resolution"), reason, sizeof(reason))) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    {
        size_t resolution_length = strlen(reason);
        if (!domain_resolution_from_name(reason, resolution_length, &resolution)) {
            response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
            return LOCKER_STATUS_INVALID;
        }
    }
    char note[4097];
    if (!domain_json_read_text(&doc, domain_json_member(&doc, root, "reason"), note, sizeof(note)) ||
        audit_char_count(note, strlen(note)) < 5u) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    domain_operation_t operation;
    locker_status_t status = domain_operation_load(core, &command->resource_id, 0, &operation);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    if (operation.state != LOCKER_OPERATION_STATE_REVIEW_REQUIRED) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    if (operation.revision != revision) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    /* 核验只能匹配原操作类型，不能把借出核验成归还。 */
    if ((resolution == LOCKER_RESOLUTION_COMPLETE_BORROW && operation.kind != LOCKER_OPERATION_KIND_BORROW) ||
        (resolution == LOCKER_RESOLUTION_COMPLETE_RETURN && operation.kind != LOCKER_OPERATION_KIND_RETURN)) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    /* 门必须关闭、物品必须明确：传感器 UNKNOWN 时不允许强制完成。 */
    if (!core->device_snapshot_valid || core->physical.door != LOCKER_DOOR_STATE_CLOSED ||
        core->physical.item == LOCKER_ITEM_STATE_UNKNOWN ||
        core->physical.physical_revision != physical_revision) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    /* 物理事实必须是新鲜的、而且来自当前会话。以前的检查只看内存里那份缓存
       值：设备断开几小时后，device_snapshot_valid 还是 true、door 还是
       CLOSED、item 还是 ABSENT，管理员拿着旧 physical_revision 就能把账本
       改成「已借出」——而这恰恰是最需要证据的时刻（审计 R1-B-12）。
       SNAPSHOT_MAX_AGE_MS 之外、或会话已断，都视为不可核验。 */
    if (!physical_evidence_fresh(core, domain_now(core))) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    /* 管理员核验也不能凭空改账本：结论必须与柜内物品现状相符。
       选 COMPLETE_BORROW 就是宣称「东西已被借出」，那物品必须已经离位；
       选 COMPLETE_RETURN 宣称「东西已还回」，那物品必须在位。
       只检查 operation.kind 而不检查物品，等于允许管理员在物品仍在柜里时
       把一个 BORROW 核验成 COMPLETED，直接凭空造出一条有效借用——这条路径
       绕过的是整条物理证据链，比普通 confirm 更危险。
       依据：docs/architecture/reviews/interface-expert.md:55（COMPLETED =
       物理证据和本人确认同事务核对通过，或管理员**有依据**核验）与
       stage-01-task-cards.md 的「不允许管理员无理由覆盖」。 */
    if (resolution == LOCKER_RESOLUTION_COMPLETE_BORROW &&
        core->physical.item != LOCKER_ITEM_STATE_ABSENT) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    if (resolution == LOCKER_RESOLUTION_COMPLETE_RETURN &&
        core->physical.item != LOCKER_ITEM_STATE_PRESENT) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return LOCKER_STATUS_CONFLICT;
    }
    /* NO_CHANGE 也不能随便选：规格要求「回到原基线」（stage-00-architecture.md:128），
       借出就是物品仍在位、归还就是物品仍离位。只看「不是 COMPLETE_*」就放行，
       等于允许管理员在物品已经离位时把一个 BORROW 核验成 NO_CHANGE，
       账面写着「什么都没发生」而柜子已经空了（审计 R3-B-11 探针②）。
       原基线不可知时同样拒绝：没有基线就没有「回到基线」可谈。 */
    if (resolution == LOCKER_RESOLUTION_NO_CHANGE) {
        const uint8_t baseline_item = operation.has_baseline ? operation.baseline.item : LOCKER_ITEM_STATE_UNKNOWN;
        if (baseline_item == LOCKER_ITEM_STATE_UNKNOWN || core->physical.item != baseline_item) {
            response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
            return LOCKER_STATUS_CONFLICT;
        }
    }
    /* 核验同样只在局部副本上准备：core->active 是正在计时的那份工作副本，
       直接在它上面终结会把「重载出来的记录」覆盖进去，提交失败时原来那个
       申请的运行时截止点就丢了（R3-B-13 的同类缺陷）。 */
    domain_operation_t target = operation;
    target.manual_resolution = true;
    uint8_t terminal = resolution == LOCKER_RESOLUTION_NO_CHANGE ? LOCKER_OPERATION_STATE_NO_CHANGE
                                                                : LOCKER_OPERATION_STATE_COMPLETED;
    bool apply_loan = resolution != LOCKER_RESOLUTION_NO_CHANGE;
    if (apply_loan) {
        status = domain_loan_prepare(core, &target);
        if (status != LOCKER_STATUS_OK) {
            response_finish(core, slot, status, true, &target.id);
            return status;
        }
    }
    /* 人工覆盖必须留下「谁、按什么理由、在什么时刻、基于哪一版物理事实」。
       以前 reason 只被校验长度就丢掉，reason_code 固定 ADMIN_RESOLVED，actor
       仍是原申请人，audits 集合里一条记录都没有——事后完全无法回答是谁批的
       （审计 R1-B-10，contract-source.json:1502 要求留原因）。审计记录现在与
       核验结论、账本变更、幂等记录同一个事务：任一条写不进去就整体不生效，
       不会出现「账已经改了但没有审计」这种事后无法追责的状态（R3-B-6）。 */
    domain_finish_extras_t extras;
    memset(&extras, 0, sizeof(extras));
    extras.command = command;
    extras.request_hash = request_hash;
    extras.audit_actor = &command->principal;
    extras.audit_type = "MANUAL_RESOLUTION";
    extras.audit_reason = note;
    extras.audit_physical_revision = physical_revision;
    locker_json_writer_t writer;
    status = finish_operation(core, &target, terminal, "ADMIN_RESOLVED", apply_loan, &extras, &writer);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    domain_operation_reset(&target);
    response_finish_json(core, slot, LOCKER_STATUS_OK, true, &operation.id, &writer);
    return LOCKER_STATUS_OK;
}

/* ---------- 审计记录 ---------- */

/* schema 的 maxLength 按 Unicode 字符数计（host/storage/src/schema.c:325 用
   utf8_count 统计码点），不是字节数。审计理由必须按码点截断，否则把 240 字节的
   80 个汉字截到 233 字节就会切在一个三字节字符中间，真仓储 utf8_count 返回
   false、整个核验事务失败——合法输入被实现破坏（审计 R3-B-6）。 */
static size_t audit_char_count(const char *text, size_t length)
{
    size_t count = 0u;
    for (size_t index = 0u; index < length; ++index) {
        if (((unsigned char)text[index] & 0xC0u) != 0x80u) {
            count += 1u;
        }
    }
    return count;
}

/* 前 max_chars 个码点占用的字节数；不足 max_chars 个码点时返回整个长度。 */
static size_t audit_prefix_bytes(const char *text, size_t length, size_t max_chars)
{
    size_t count = 0u;
    for (size_t index = 0u; index < length; ++index) {
        if (((unsigned char)text[index] & 0xC0u) == 0x80u) {
            continue; /* 续字节不计数，也不构成可截断边界 */
        }
        if (count == max_chars) {
            return index;
        }
        count += 1u;
    }
    return length;
}

static locker_status_t manual_audit_append(locker_core_t *core, domain_change_t *change,
                                           const domain_operation_t *operation, const locker_principal_t *actor,
                                           const char *type, const char *reason, uint64_t physical_revision)
{
    if (core->repo == NULL || change == NULL || operation == NULL || actor == NULL || reason == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    locker_uuid_t audit_id;
    if (locker_clock_port_new_uuid(&core->clock, &audit_id) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_IO_ERROR;
    }
    char key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&audit_id, key, sizeof(key))) {
        return LOCKER_STATUS_INVALID;
    }
    char occurred[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    uint64_t wall = 0u;
    bool has_occurred = locker_clock_port_wall_utc_ms(&core->clock, &wall) == LOCKER_STATUS_OK &&
                        locker_domain_time_format_utc(wall, occurred, sizeof(occurred));
    /* AuditRecord.reason 上限 256（internal-data.schema.json$defs.AuditRecord），
       把物理版本并进 reason 的尾巴：审计表没有单独的 physical_revision 列，
       而「基于哪一版事实」是判断是否越权最关键的一条。超长就截断原文，
       宁可能读的字少一点，也不能让整条记录落库失败。 */
    const char suffix_prefix[] = " (physical_revision=";
    const char suffix_close[] = ")";
    char suffix[48];
    suffix[0] = '\0';
    size_t suffix_length = 0u;
    if (physical_revision != 0u) {
        int suffix_written = snprintf(suffix, sizeof(suffix), "%s%llu%s", suffix_prefix,
                                      (unsigned long long)physical_revision, suffix_close);
        if (suffix_written < 0 || (size_t)suffix_written >= sizeof(suffix)) {
            return LOCKER_STATUS_NO_SPACE;
        }
        suffix_length = (size_t)suffix_written;
    }
    /* 256 是**字符**上限（internal-data.schema.json $defs.AuditRecord.reason
       maxLength:256，按码点计），不是字节上限。缓冲区必须按「最坏情况一个码点
       占 4 字节」来定尺寸：如果按 256 字节开 char[257]，一个 233 个汉字（699
       字节）的合法理由会整段 memcpy 进 257 字节的栈缓冲，直接踩爆栈
       （审计 round-5 指出的 UTF-8 审计缓冲越界）。后缀是纯 ASCII，字符数等于
       字节数，所以只需给后缀留出它自己的字节数。 */
    char text[256u * 4u + sizeof(suffix)];
    size_t suffix_chars = suffix_length;
    size_t budget_chars = 256u - suffix_chars;
    size_t reason_length = strlen(reason);
    if (audit_char_count(reason, reason_length) > budget_chars) {
        reason_length = audit_prefix_bytes(reason, reason_length, budget_chars);
    }
    memcpy(text, reason, reason_length);
    memcpy(text + reason_length, suffix, suffix_length + 1u);
    if (!domain_change_put_begin(change, "audits", key)) {
        return LOCKER_STATUS_NO_SPACE;
    }
    domain_write_audit(&change->writer, &audit_id, &actor->user_id, &operation->id, type, text,
                       has_occurred ? occurred : NULL, has_occurred, NULL,
                       (uint8_t)LOCKER_TIME_QUALITY_HOST_SYNCED);
    domain_change_put_end(change);
    return LOCKER_STATUS_OK;
}

/* ---------- 柜前证明 ---------- */

static locker_status_t presence_apply(locker_core_t *core, const locker_presence_t *presence, uint8_t proof_source)
{
    if (core->phase != DOMAIN_PHASE_READY) {
        return LOCKER_STATUS_NOT_READY;
    }
    /* 上一次动作的上下文还没在设备侧释放干净时，不许武装新的动作。
       释放未完成意味着 MCU 仍持有 has_active，新 EXECUTE 只会撞 BUSY 并被
       判成「命令被拒 → 核验」，把一次本来完全合法的到柜证明白白烧掉。
       这不是「等一会儿再试」的礼貌，而是同会话连续借还的前置条件
       （审计 R3-B-2：终结落库后、释放完成前提交的 presence 照样武装成功）。 */
    if (core->release_pending) {
        return LOCKER_STATUS_BUSY;
    }
    domain_operation_t operation;
    locker_status_t status = domain_operation_load(core, &presence->operation_id, 0, &operation);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    if (!locker_uuid_equal(&operation.actor_id, &presence->principal.user_id)) {
        /* 证明只能属于操作本人。 */
        return LOCKER_STATUS_INVALID;
    }
    if (operation.revision != presence->operation_revision) {
        return LOCKER_STATUS_CONFLICT;
    }
    if (operation.state != LOCKER_OPERATION_STATE_REQUESTED) {
        /* 已提交动作意图后不再接受新的到柜证明。 */
        return LOCKER_STATUS_CONFLICT;
    }
    if (operation.proof_consumed) {
        /* 一份柜前证明只算一次；过期作废由 operation_arm 的期限分支负责。 */
        return LOCKER_STATUS_CONFLICT;
    }
    operation.proof_source = proof_source;
    operation.proof_ms = domain_now(core);
    operation.proof_consumed = true;
    core->active = operation;
    return LOCKER_STATUS_OK;
}

/*
 * REQUESTED -> ACCESS_PENDING：柜前证明 + 物理前置条件 + 视觉门禁都满足才提交
 * 唯一 command。事务里同时写 operation（含 command_id）、outbox 与占位保持。
 *
 * **arm 的全部字段填充与状态跃迁都在局部候选副本上完成，事务提交成功后才
 * 装回 core->active。** 直接改 core->active 再提交，提交一失败就会出现
 * 「内存里已经是 ACCESS_PENDING 且带 outbox、库里还是 REQUESTED」的分叉：
 * 处置超时或下一次 tick 看到内存状态就允许发送，于是**一个从未落库的动作被
 * 真的发到设备上**（internal-interfaces.md:98/:114 要求先提交后发送）。
 * 审计探针实测该路径：首次失败后 active_state=ACCESS_PENDING、sends=1，
 * 而 stored_state=REQUESTED。（R1-B-1）
 */
static locker_status_t operation_arm(locker_core_t *core)
{
    if (!core->active.used || core->active.state != LOCKER_OPERATION_STATE_REQUESTED) {
        return LOCKER_STATUS_NOT_READY;
    }
    if (!core->active.proof_consumed) {
        return LOCKER_STATUS_NOT_READY;
    }
    uint64_t now = domain_now(core);
    if (core->active.proof_ms == 0u || now < core->active.proof_ms ||
        now - core->active.proof_ms > LOCKER_PRESENCE_TTL_MS) {
        /* 15 秒证明过期：必须重新到柜，不能拿旧证明开门。 */
        core->active.proof_consumed = false;
        core->active.proof_source = 0u;
        (void)domain_require_review(core, &core->active, "PRESENCE_EXPIRED");
        return LOCKER_STATUS_CONFLICT;
    }
    if (core->vision_pre_required) {
        /* REQUIRED 策略需要 PRE_ACCESS 视觉证据；S1-04 只支持
           DISABLED_FOR_FOUNDATION，S2-14 才接入真实视觉门禁。 */
        return LOCKER_STATUS_NOT_READY;
    }
    if (!physical_ready_for_action(core) ||
        core->physical.item != expected_item_for_kind(core->active.kind)) {
        return LOCKER_STATUS_NOT_READY;
    }
    domain_operation_t candidate = core->active;
    locker_uuid_t command_id;
    if (locker_clock_port_new_uuid(&core->clock, &command_id) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_IO_ERROR;
    }
    candidate.command_id = command_id;
    candidate.has_command = true;
    candidate.action_no = core->next_action_no;
    candidate.outbox_present = true;
    if (locker_clock_port_new_uuid(&core->clock, &candidate.outbox_id) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_IO_ERROR;
    }
    candidate.outbox_session_id = core->session.session_id;
    candidate.outbox_journal_epoch = core->session.journal_epoch;
    candidate.outbox_boot_id = core->session.boot_id;
    candidate.outbox_physical_revision = core->physical.physical_revision;
    /* not_after 是**给 MCU 的单调时刻**，不是主机时刻。没有有效锚点样本
       （RTT>200ms / 年龄>3000ms / boot 不一致）就不发动作：主机时间被当成
       MCU 时间可能误拒，也可能把命令有效期错误地拉长（审计 R1-B-6，
       internal-interfaces.md:131-133）。换算取保守下界：宁可期限更短。 */
    uint64_t mcu_low = 0u;
    uint64_t mcu_high = 0u;
    if (domain_anchor_mcu_now(core, &mcu_low, &mcu_high) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_NOT_READY;
    }
    (void)mcu_high;
    /* 期限必须同时受两个上限约束，取较早者（审计 R4-B-1）：
       ① 柜前证明的剩余有效期——证明是「本人此刻在柜前」的唯一凭据，
          15 秒到期后即便动作已经准备好也不能再执行；晚武装（例如门未关好
          拖了十几秒）时若只按准备预算重新给满 2 秒，就等于把一份即将失效的
          证明又续了 2 秒，而 MCU 并不知道原证明时刻，只按收到的 not_after
          放行，没法替主机修正。
       ② 准备预算 LOCKER_PREPARE_DEADLINE_MS——UART 积压不能无限拉长。
       两者都以 mcu_low 为基准换算，duration 是主机单调域时长；锚点已保证
       偏移有界（RTT≤200ms、年龄≤3000ms），直接相加是保守估计。 */
    uint64_t proof_deadline = core->active.proof_ms + LOCKER_PRESENCE_TTL_MS;
    uint64_t proof_remaining = proof_deadline > now ? proof_deadline - now : 0u;
    if (proof_remaining > LOCKER_PREPARE_DEADLINE_MS) {
        proof_remaining = LOCKER_PREPARE_DEADLINE_MS;
    }
    candidate.outbox_not_after_ms = mcu_low + proof_remaining;
    candidate.outbox_state = 1u; /* PREPARED */
    candidate.send_started = false;
    candidate.access_pending_ms = now;

    locker_status_t status = domain_operation_set_state(core, &candidate, LOCKER_OPERATION_STATE_ACCESS_PENDING,
                                                        "ARMED");
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    char operation_key[LOCKER_DOMAIN_UUID_TEXT];
    char outbox_key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&candidate.id, operation_key, sizeof(operation_key)) ||
        !domain_uuid_to_text(&candidate.outbox_id, outbox_key, sizeof(outbox_key))) {
        return LOCKER_STATUS_INVALID;
    }
    domain_change_t change;
    domain_change_init(core, &change, core->revision, core->revision + 1u);
    status = domain_operation_persist(core, &change, &candidate);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    if (!domain_change_put_begin(&change, "outbox", outbox_key)) {
        return LOCKER_STATUS_NO_SPACE;
    }
    domain_write_outbox(&change.writer, &candidate, "PREPARED", NULL);
    domain_change_put_end(&change);
    status = domain_change_commit(core, &change);
    if (status != LOCKER_STATUS_OK) {
        /* 提交失败：内存仍是 REQUESTED 且没有 outbox，泵不会发送任何东西。 */
        return status;
    }
    /* 落库成功才安装：此刻的 active 与仓储一致。 */
    core->active = candidate;
    core->next_action_no += 1u;
    domain_publish(core, &candidate.id, "operation", core->revision);
    return LOCKER_STATUS_OK;
}

/* ---------- outbox 泵 ---------- */

/*
 * 只有本次进程实例新建的 outbox 项才允许发送；发送前先持久化
 * 「已尝试发送」检查点，检查点提交失败就绝不发送（internal-interfaces.md:114）。
 */
void domain_outbox_pump(locker_core_t *core)
{
    domain_operation_t *operation = &core->active;
    if (!operation->used || !operation->outbox_present) {
        return;
    }
    if (operation->state != LOCKER_OPERATION_STATE_ACCESS_PENDING || operation->send_started ||
        core->execute_in_flight) {
        return;
    }
    if (!core->device_session_up || !core->session.valid) {
        return;
    }
    uint64_t now = domain_now(core);
    /* outbox_not_after_ms 在 MCU 时钟域里（见 operation_arm）。拿主机时钟直接
       比会得出荒谬结论：两个时钟起点不同，可能立刻「过期」，也可能永远不过期。
       必须换算后再比；换算不出来（锚点过期）就当作已过期——宁可不发。 */
    if (operation->outbox_not_after_ms != 0u) {
        uint64_t mcu_low = 0u;
        uint64_t mcu_high = 0u;
        if (domain_anchor_mcu_now(core, &mcu_low, &mcu_high) != LOCKER_STATUS_OK) {
            (void)domain_require_review(core, operation, "ANCHOR_MISSING");
            return;
        }
        if (mcu_low > operation->outbox_not_after_ms && mcu_high > operation->outbox_not_after_ms) {
            /* 两侧下界都越界才算真的过期：只在最坏情况下过期不足以拒发。 */
            (void)domain_require_review(core, operation, "PREPARE_DEADLINE");
            return;
        }
    }
    if (core->session.boot_id != operation->outbox_boot_id ||
        !locker_uuid_equal(&core->session.journal_epoch, &operation->outbox_journal_epoch)) {
        (void)domain_require_review(core, operation, "SESSION_CHANGED");
        return;
    }
    if (core->physical.physical_revision != operation->outbox_physical_revision) {
        (void)domain_require_review(core, operation, "PHYSICAL_CHANGED");
        return;
    }
    /* 检查点：先把 outbox 置 SEND_ATTEMPTED 并持久化，再发。 */
    char outbox_key[LOCKER_DOMAIN_UUID_TEXT];
    if (!domain_uuid_to_text(&operation->outbox_id, outbox_key, sizeof(outbox_key))) {
        (void)domain_require_review(core, operation, "OUTBOX_KEY");
        return;
    }
    char attempted[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    uint64_t wall = 0u;
    if (locker_clock_port_wall_utc_ms(&core->clock, &wall) != LOCKER_STATUS_OK ||
        !locker_domain_time_format_utc(wall, attempted, sizeof(attempted))) {
        (void)domain_require_review(core, operation, "CHECKPOINT_CLOCK");
        return;
    }
    domain_change_t change;
    domain_change_init(core, &change, core->revision, core->revision + 1u);
    if (!domain_change_put_begin(&change, "outbox", outbox_key)) {
        (void)domain_require_review(core, operation, "CHECKPOINT_SPACE");
        return;
    }
    domain_write_outbox(&change.writer, operation, "SEND_ATTEMPTED", attempted);
    domain_change_put_end(&change);
    if (domain_change_commit(core, &change) != LOCKER_STATUS_OK) {
        /* 检查点没落库：禁止发送，进核验。 */
        (void)domain_require_review(core, operation, "CHECKPOINT_FAILED");
        return;
    }
    operation->outbox_state = 2u; /* SEND_ATTEMPTED */
    operation->send_started = true;
    operation->send_started_ms = now;

    locker_status_t status = domain_execute_intent(core, operation);
    if (status != LOCKER_STATUS_OK) {
        if (status == LOCKER_STATUS_BUSY) {
            return;
        }
        /* 端口本身拒绝：结果未知，进核验而不是重发。 */
        (void)domain_require_review(core, operation, "SEND_FAILED");
    }
}

/* ---------- 设备上下文释放 ---------- */

/*
 * 把「设备侧动作上下文已结束」这件事告诉 MCU。
 *
 * 为什么必须有：模拟器/固件把一次已接受的动作记成 has_active，新的 EXECUTE
 * 会直接回 BUSY，直到主机发 RELEASE_CONTEXT（sim.c:1693 的门、sim.c:1860 的
 * 入口）。以前 Core 只处理释放响应、从不发请求，于是第一次借出成功后，同一
 * 会话里的归还永远发不出去——「借出来」和「还得回去」之间断了一环
 * （审计 R1-B-3，internal-interfaces.md:129、uart-v1.md:408）。
 *
 * 释放只清 MCU 的动作上下文：不动动作水位、不动已提交事件、不清
 * HISTORY_INCOMPLETE，也不重放旧 EXECUTE（uart-v1.md:402）。发送失败只重试
 * 释放本身，绝不因此重放开锁。
 */
void domain_release_pump(locker_core_t *core)
{
    if (!core->release_pending) {
        return;
    }
    if (!core->device_session_up || !core->session.valid) {
        return; /* 会话断了：等重连后重新核对，期间不发任何动作。 */
    }
    uint64_t now = domain_now(core);
    if (core->release_recheck) {
        /* CAS 失败过：只等新快照判定设备是否已放手（apply_snapshot 里做），
           不重发同一个旧版本请求。uart-v1.md:402 明确禁止无限按旧版本重试。
           快照由 domain_device_pump 周期取（500 ms 节流），这里不重复计时。 */
        return;
    }
    if (core->release_sent) {
        /* 已发出：等响应，超时就再发一次（释放是幂等的：设备侧 already_released
           回 1 表示本来就已释放，不需要区分是谁清的）。 */
        if (now >= core->release_ms && now - core->release_ms < LOCKER_HOST_TIMEOUT_MS) {
            return;
        }
    }
    locker_context_guard_t guard;
    memset(&guard, 0, sizeof(guard));
    guard.session_id = core->session.session_id;
    guard.journal_epoch = core->session.journal_epoch;
    guard.expected_boot_id = core->session.boot_id;
    guard.expected_physical_revision = core->physical.physical_revision;
    guard.operation_id = core->release_operation_id;
    guard.command_id = core->release_command_id;
    locker_status_t status = locker_device_release_context(core->device, &guard);
    if (status == LOCKER_STATUS_OK) {
        core->release_sent = true;
        core->release_ms = now;
        return;
    }
    if (status == LOCKER_STATUS_NOT_READY || status == LOCKER_STATUS_BUSY) {
        return; /* 端口还不可用：下一轮再试，不算失败。 */
    }
    /* CONFLICT（CAS 失败）：会话/物理版本已经和请求里的 guard 对不上，
       设备侧到底还持不持有这条上下文，主机并不知道。以前这里直接清掉
       release_pending，等于**假设**「版本变了 = 已经释放」——那只是猜测，
       设备可能仍挂着 has_active，后续 EXECUTE 全部 BUSY，而且再也没有任何
       重试入口（审计 R3-B-2）。现在改为进入「只查询」状态：等下一份快照的
       active_operation_id 给出确凿答案，再决定是闭合还是补发。 */
    if (status == LOCKER_STATUS_CONFLICT) {
        core->release_recheck = true;
        core->release_sent = false;
        core->release_ms = now;
    }
}

/* ---------- 超时推进 ---------- */

void domain_timeouts(locker_core_t *core)
{
    uint64_t now = domain_now(core);
    domain_operation_t *operation = &core->active;
    if (operation->used) {
        switch (operation->state) {
        case LOCKER_OPERATION_STATE_REQUESTED:
            if (operation->requested_deadline_ms != 0u && now >= operation->requested_deadline_ms) {
                /* 120 秒无动作意图：取消并释放占位。
                   只有取消事务真的提交成功才清活动副本——提交失败时操作仍挂在
                   库里，内存清空会让后续 tick 再也看不到它，占位永远不释放、
                   库里的操作永远停在 REQUESTED。（R1-B-15） */
                if (finish_operation(core, operation, LOCKER_OPERATION_STATE_CANCELLED, "REQUEST_EXPIRED",
                                     false, NULL, NULL) == LOCKER_STATUS_OK) {
                    domain_operation_reset(operation);
                }
            }
            break;
        case LOCKER_OPERATION_STATE_ACCESS_PENDING:
            /* 脉冲事实起 30 秒未见开门 -> 核验。 */
            if (operation->access_pending_ms != 0u && now - operation->access_pending_ms >= LOCKER_WAIT_OPEN_MS) {
                (void)domain_require_review(core, operation, "OPEN_TIMEOUT");
            }
            break;
        case LOCKER_OPERATION_STATE_ACCESS_ACTIVE:
            /* 脉冲已起但门一直没开：**必须有这个出口**。
               stage-00-architecture.md:122 写的是「从脉冲事实起 30 秒未见开门、
               开门后 120 秒未关门，进入核验」——两句都要实现。只挂
               door_opened_ms 的 CLOSE_TIMEOUT 时，后半句被满足，前半句漏了：
               脉冲起来、门没开（锁体卡死、门被外力顶住、传感器失灵），
               door_opened_ms 永远是 0，操作会永久停在 ACCESS_ACTIVE 占着唯一
               柜格占位，所有后续申请都被 BUSY 挡回去——这是「没有确定结果」，
               违背本卡验收项「各业务超时和 UNKNOWN 输入均有确定结果」。
               计时起点用 pulse_started_ms（脉冲事实），不是 access_pending_ms。 */
            if (operation->door_opened_ms == 0u && operation->pulse_started_ms != 0u &&
                now - operation->pulse_started_ms >= LOCKER_WAIT_OPEN_MS) {
                (void)domain_require_review(core, operation, "OPEN_TIMEOUT");
                break;
            }
            if (operation->door_opened_ms != 0u && now - operation->door_opened_ms >= LOCKER_WAIT_CLOSE_MS) {
                (void)domain_require_review(core, operation, "CLOSE_TIMEOUT");
            }
            break;
        case LOCKER_OPERATION_STATE_WAIT_CONFIRM:
            if (operation->wait_confirm_deadline_ms != 0u && now >= operation->wait_confirm_deadline_ms) {
                (void)domain_require_review(core, operation, "CONFIRM_TIMEOUT");
            }
            break;
        default:
            break;
        }
    }
    /* 查询补救必须与业务状态解耦：只要还有一条「可能已执行」的命令没有定论，
       就必须有界核对（uart-v1.md:408「结果未知必须 QUERY_COMMAND」）。以前这段
       分别挂在 ACCESS_PENDING / ACCESS_ACTIVE 两个分支里，于是事件流先把操作
       推进 WAIT_CONFIRM（5 秒内就能发生）之后查询永远排不上；查询自己再丢一次，
       execute_in_flight 与 pending 就双锁死，后续任何新命令都发不出去
       （审计 R3-B-8：推进到 7000 ms 后 query_calls=0、execute_in_flight=1）。 */
    if (core->pending_command_valid && !core->pending_command_queried && core->pending_command_ms != 0u &&
        now - core->pending_command_ms >= LOCKER_HOST_TIMEOUT_MS) {
        if (domain_query_command(core, &core->pending_command_id) == LOCKER_STATUS_OK) {
            core->pending_command_queried = true;
            /* 查询发出后重新起算等待窗口。不重起算的话，同一个 tick 里
               既发出查询又立刻丢弃待确认命令，回复永远对不上，等于把
               「只查一次」退化成「查了就忘」。 */
            core->pending_command_ms = now;
        }
    }
    /* 在途请求超时：清槽，让泵重新发起（回复迟到时按 request_no 找不到，忽略）。 */
    for (uint32_t i = 0u; i < LOCKER_DOMAIN_PENDING_SLOTS; ++i) {
        domain_pending_t *slot = &core->pending[i];
        if (!slot->used) {
            continue;
        }
        if (slot->started_ms != 0u && now - slot->started_ms >= LOCKER_HOST_TIMEOUT_MS) {
            uint8_t purpose = slot->purpose;
            memset(slot, 0, sizeof(*slot));
            if (purpose == DOMAIN_PURPOSE_SNAPSHOT) {
                core->snapshot_in_flight = false;
            } else if (purpose == DOMAIN_PURPOSE_FETCH) {
                core->fetch_in_flight = false;
            } else if (purpose == DOMAIN_PURPOSE_ACK) {
                core->ack_in_flight = false;
            }
        }
    }
    /* 只有「已经查过一次、且查询后的窗口也走完」才放弃等待：还没查就清掉
       会白白丢掉一次可用的只读核对机会。放弃等待是有界收敛的终点——命令结果
       已经无法核实，业务必须进核验，同时在飞标志与待确认槽都要释放，
       否则「结果未知」会变成永久占用新命令槽（审计 R3-B-8 后半）。 */
    if (core->pending_command_valid && core->pending_command_queried && core->pending_command_ms != 0u &&
        now - core->pending_command_ms >= LOCKER_HOST_TIMEOUT_MS) {
        core->pending_command_valid = false;
        core->pending_command_queried = false;
        core->execute_in_flight = false;
        if (core->active.used && !domain_terminal_state(core->active.state)) {
            (void)domain_require_review(core, &core->active, "QUERY_UNRESOLVED");
        }
    }
}

/* ---------- 设备事件 / 响应 ---------- */

void domain_handle_device_event(locker_core_t *core, const locker_device_event_t *event, uint32_t completion)
{
    if (atomic_exchange(&core->event_overflow_flag, false)) {
        core->event_overflow = true;
        core->fetch_gap = true;
        core->cursor.continuity_ok = false;
    }
    if (event->event_type == LOCKER_DEVICE_EVENT_CARD_OBSERVED && event->live_transport &&
        event->card_uid_len != 0u && event->card_uid_len <= sizeof(core->last_card.uid)) {
        /* 历史补传（live_transport == false）只补记录，绝不产生柜前证明。 */
        core->last_card.valid = true;
        core->last_card.mono_ms = domain_now(core);
        core->last_card.uid_len = event->card_uid_len;
        memcpy(core->last_card.uid, event->card_uid, event->card_uid_len);
        core->last_card.user_resolved = false;
    }
    domain_event_apply(core, event);
    if (completion != UINT32_MAX) {
        domain_completion_fail(core, completion, LOCKER_STATUS_OK);
    }
}

void domain_handle_device_reply(locker_core_t *core, const locker_device_reply_t *reply, uint32_t completion)
{
    if (atomic_exchange(&core->reply_overflow_flag, false)) {
        core->reply_overflow = true;
        core->fetch_gap = true;
    }
    domain_reply_apply(core, reply);
    if (completion != UINT32_MAX) {
        domain_completion_fail(core, completion, LOCKER_STATUS_OK);
    }
}

/* ---------- 认证 ---------- */

void domain_handle_auth(locker_core_t *core, const domain_job_t *job)
{
    (void)core;
    (void)job;
    /* 真实凭据校验归 S1-05；见 locker_core_authenticate 的说明。 */
    if (job->completion != UINT32_MAX) {
        domain_completion_accept(core, job->completion, &job->completion_token, LOCKER_STATUS_NOT_READY, NULL,
                                 false);
        domain_completion_finish(core, job->completion, &job->completion_token, LOCKER_STATUS_NOT_READY, NULL, 0u,
                                 core->revision);
    }
}

/* ---------- 查询 ---------- */

static locker_status_t query_operation_get(locker_core_t *core, const locker_query_t *query, uint32_t slot)
{
    domain_operation_t operation;
    locker_status_t status = domain_operation_load(core, &query->resource_id, 0, &operation);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return status;
    }
    /* 资源归属检查（contract-source.json:1424-1429）：操作属于发起人本人，
       管理员可以看全部。没有这一步，任何已认证的普通会话只要猜到/拿到
       Operation id 就能读到别人的借用记录（审计 R1-B-13）。
       不区分「不存在」和「无权」：两者都返回 INVALID，避免用状态码探测他人的
       id 是否存在。 */
    bool privileged = query->principal.role == LOCKER_ROLE_ADMIN;
    if (!privileged && !locker_uuid_equal(&operation.actor_id, &query->principal.user_id)) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    /* 投影自带完整 `{...}`；用 end_value 收尾，避免多补一层 `}`。 */
    domain_write_operation_projection(&writer, core, &operation);
    domain_response_end_value(&writer, core->revision, &query->request_id);
    response_finish_json(core, slot, LOCKER_STATUS_OK, true, &operation.id, &writer);
    return LOCKER_STATUS_OK;
}

static void scan_operations(locker_core_t *core, locker_json_writer_t *writer, uint32_t *written, bool reviews_only)
{
    char after[LOCKER_DOMAIN_KEY_TEXT];
    after[0] = '\0';
    for (uint32_t page = 0u; page < DOMAIN_SCAN_PAGES; ++page) {
        domain_doc_t doc;
        locker_status_t status = domain_repo_page(core, "operations", after[0] == '\0' ? NULL : after, 100u, &doc);
        if (status != LOCKER_STATUS_OK) {
            return;
        }
        const locker_json_node_t *documents =
            domain_json_member(&doc.doc, locker_domain_json_root(&doc.doc), "documents");
        if (documents == NULL || documents->type != LOCKER_JSON_ARRAY) {
            return;
        }
        for (uint32_t i = 0u; i < documents->count; ++i) {
            const locker_json_node_t *document = locker_domain_json_at(&doc.doc, documents, i);
            const locker_json_node_t *value = domain_json_member(&doc.doc, document, "value");
            if (value == NULL) {
                return;
            }
            domain_operation_t operation;
            if (!domain_operation_from_record(&doc.doc, value, &operation)) {
                return;
            }
            if (reviews_only && operation.state != LOCKER_OPERATION_STATE_REVIEW_REQUIRED) {
                continue;
            }
            if (*written > 0u) {
                locker_domain_json_literal(writer, ",");
            }
            domain_write_operation_projection(writer, core, &operation);
            *written += 1u;
            if (*written >= 100u) {
                return;
            }
        }
        const locker_json_node_t *next =
            domain_json_member(&doc.doc, locker_domain_json_root(&doc.doc), "next_after_key");
        if (next == NULL || next->type != LOCKER_JSON_STRING) {
            return;
        }
        size_t length = 0u;
        if (!locker_domain_json_text(&doc.doc, next, after, sizeof(after), &length)) {
            return;
        }
    }
}

static locker_status_t query_list(locker_core_t *core, const locker_query_t *query, uint32_t slot, bool reviews)
{
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    if (!domain_response_data_object(&writer)) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }
    locker_domain_json_key(&writer, "items");
    locker_domain_json_literal(&writer, "[");
    uint32_t written = 0u;
    if (reviews) {
        scan_operations(core, &writer, &written, true);
    }
    locker_domain_json_literal(&writer, "]");
    if (!reviews) {
        /* loans / events 分页游标：首版一次返回全部，未读完由后续卡接续。 */
        locker_domain_json_literal(&writer, ",\"next_cursor\":");
        locker_domain_json_null(&writer);
    }
    /* 不在这里补 `}`：data 对象的关闭由 domain_response_end 负责。 */
    domain_response_end(&writer, core->revision, &query->request_id);
    response_finish_json(core, slot, LOCKER_STATUS_OK, false, NULL, &writer);
    return LOCKER_STATUS_OK;
}

static locker_status_t query_loans(locker_core_t *core, const locker_query_t *query, uint32_t slot)
{
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    if (!domain_response_data_object(&writer)) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }
    locker_domain_json_key(&writer, "items");
    locker_domain_json_literal(&writer, "[");
    if (core->loan_open) {
        write_loan_object(&writer, core);
    }
    locker_domain_json_literal(&writer, "],\"next_cursor\":");
    locker_domain_json_null(&writer);
    /* 不在这里补 `}`：data 对象的关闭由 domain_response_end 负责。 */
    domain_response_end(&writer, core->revision, &query->request_id);
    response_finish_json(core, slot, LOCKER_STATUS_OK, false, NULL, &writer);
    return LOCKER_STATUS_OK;
}

static locker_status_t query_device_get(locker_core_t *core, const locker_query_t *query, uint32_t slot)
{
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    /* 设备投影自带完整 `{...}`；用 end_value 收尾，避免多补一层 `}`。 */
    domain_write_device_projection(&writer, core);
    domain_response_end_value(&writer, core->revision, &query->request_id);
    response_finish_json(core, slot, LOCKER_STATUS_OK, false, NULL, &writer);
    return LOCKER_STATUS_OK;
}

static locker_status_t query_tools(locker_core_t *core, const locker_query_t *query, uint32_t slot, bool single)
{
    if (single && (!core->tool_valid || !locker_uuid_equal(&query->resource_id, &core->tool_id))) {
        /* 工具投影不可知时，「这件工具存在吗」答不上来：一律 INVALID，
           而不是回一个 name=null 的空壳冒充存在。 */
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return LOCKER_STATUS_INVALID;
    }
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    if (single) {
        /* TOOL_GET 的 data 就是 Tool 本身（openapi ToolResponse.data → Tool）；
           write_tool_object 已写出完整 `{...}` 或 null，所以不调 data_object，
           并用 end_value 收尾（它不补 `}`）。 */
        write_tool_object(&writer, core);
        domain_response_end_value(&writer, core->revision, &query->request_id);
        response_finish_json(core, slot, LOCKER_STATUS_OK, false, NULL, &writer);
        return LOCKER_STATUS_OK;
    }
    if (!domain_response_data_object(&writer)) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }
    locker_domain_json_key(&writer, "items");
    locker_domain_json_literal(&writer, "[");
    if (core->tool_valid) {
        write_tool_object(&writer, core);
    }
    /* 不在这里补 `}`：data 对象的关闭由 domain_response_end 负责。 */
    locker_domain_json_literal(&writer, "]");
    domain_response_end(&writer, core->revision, &query->request_id);
    response_finish_json(core, slot, LOCKER_STATUS_OK, false, NULL, &writer);
    return LOCKER_STATUS_OK;
}

static locker_status_t query_snapshot(locker_core_t *core, const locker_query_t *query, uint32_t slot)
{
    locker_json_writer_t writer;
    domain_response_begin(&writer, core->response_buffer, sizeof(core->response_buffer));
    if (!domain_response_data_object(&writer)) {
        response_finish(core, slot, LOCKER_STATUS_NO_SPACE, false, NULL);
        return LOCKER_STATUS_NO_SPACE;
    }
    locker_domain_json_key(&writer, "device");
    domain_write_device_projection(&writer, core);
    locker_domain_json_literal(&writer, ",\"tools\":[");
    if (core->tool_valid) {
        write_tool_object(&writer, core);
    }
    locker_domain_json_literal(&writer, "],\"active_operations\":[");
    if (core->active.used && !domain_terminal_state(core->active.state)) {
        domain_write_operation_projection(&writer, core, &core->active);
    }
    locker_domain_json_literal(&writer, "],\"loans\":[");
    if (core->loan_open) {
        write_loan_object(&writer, core);
    }
    /* 不在这里补 `}`：data 对象的关闭由 domain_response_end 负责。 */
    locker_domain_json_literal(&writer, "]");
    domain_response_end(&writer, core->revision, &query->request_id);
    response_finish_json(core, slot, LOCKER_STATUS_OK, false, NULL, &writer);
    return LOCKER_STATUS_OK;
}

void domain_handle_query(locker_core_t *core, const domain_job_t *job)
{
    const locker_query_t *query = &job->body.query;
    uint32_t slot = job->completion;
    locker_status_t status = domain_principal_check(core, &query->principal);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return;
    }
    if (strcmp(query->kind, LOCKER_CORE_DEVICE_GET) == 0) {
        query_device_get(core, query, slot);
    } else if (strcmp(query->kind, LOCKER_CORE_SNAPSHOT_GET) == 0) {
        query_snapshot(core, query, slot);
    } else if (strcmp(query->kind, LOCKER_CORE_TOOLS_LIST) == 0) {
        query_tools(core, query, slot, false);
    } else if (strcmp(query->kind, LOCKER_CORE_TOOL_GET) == 0) {
        query_tools(core, query, slot, true);
    } else if (strcmp(query->kind, LOCKER_CORE_OPERATION_GET) == 0) {
        query_operation_get(core, query, slot);
    } else if (strcmp(query->kind, LOCKER_CORE_LOANS_LIST) == 0) {
        query_loans(core, query, slot);
    } else if (strcmp(query->kind, LOCKER_CORE_REVIEWS_LIST) == 0) {
        /* 核验队列是管理员视图（contract-source.json:1484-1487）：普通用户拿到它
           等于能看到别人所有待核验的操作和原因码（审计 R1-B-13）。 */
        if (query->principal.role != LOCKER_ROLE_ADMIN) {
            response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
            return;
        }
        query_list(core, query, slot, true);
    } else if (strcmp(query->kind, LOCKER_CORE_EVENTS_LIST) == 0) {
        /* 阶段 1 的 18 个仓储集合里没有 events：公开事件流由审计/设备事件派生，
           归 S1-05 适配层；这里返回空页而不是伪造历史。 */
        query_list(core, query, slot, false);
    } else {
        /* SESSION、USERS、CARD、VISION 四类命令归 S1-05 / S2：不空实现冒充。 */
        response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
    }
}

/* ---------- 命令 ---------- */

void domain_handle_command(locker_core_t *core, const domain_job_t *job)
{
    const locker_command_t *command = &job->body.command;
    uint32_t slot = job->completion;
    locker_status_t status = domain_principal_check(core, &command->principal);
    if (status != LOCKER_STATUS_OK) {
        response_finish(core, slot, status, false, NULL);
        return;
    }
    if (core->phase != DOMAIN_PHASE_READY) {
        response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
        return;
    }
    /* 幂等：同一 key + 同一哈希返回原响应，不产生新 operation。 */
    char request_hash[65];
    if (strcmp(command->kind, LOCKER_CORE_OPERATION_CREATE) == 0 && locker_uuid_is_nil(&command->idempotency_key)) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return;
    }
    if (!build_request_hash(core, command, request_hash, sizeof(request_hash))) {
        response_finish(core, slot, LOCKER_STATUS_INVALID, false, NULL);
        return;
    }
    locker_json_writer_t replay;
    locker_domain_json_writer_init(&replay, core->response_buffer, sizeof(core->response_buffer));
    domain_idempotency_result_t idem = idempotency_lookup(core, command, request_hash, &replay, NULL);
    if (idem == DOMAIN_IDEMPOTENCY_CONFLICT) {
        response_finish(core, slot, LOCKER_STATUS_CONFLICT, false, NULL);
        return;
    }
    if (idem == DOMAIN_IDEMPOTENCY_REPLAY) {
        response_finish_json(core, slot, LOCKER_STATUS_OK, false, NULL, &replay);
        return;
    }
    if (strcmp(command->kind, LOCKER_CORE_OPERATION_CREATE) == 0) {
        command_operation_create(core, command, slot);
    } else if (strcmp(command->kind, LOCKER_CORE_OPERATION_CONFIRM) == 0) {
        command_operation_confirm(core, command, slot, request_hash);
    } else if (strcmp(command->kind, LOCKER_CORE_OPERATION_CANCEL) == 0) {
        command_operation_cancel(core, command, slot, request_hash);
    } else if (strcmp(command->kind, LOCKER_CORE_OPERATION_RESOLVE) == 0) {
        command_operation_resolve(core, command, slot, request_hash);
    } else {
        response_finish(core, slot, LOCKER_STATUS_NOT_READY, false, NULL);
    }
}

void domain_handle_presence(locker_core_t *core, const domain_job_t *job)
{
    const locker_presence_t *presence = &job->body.presence.presence;
    locker_status_t status = domain_principal_check(core, &presence->principal);
    if (status != LOCKER_STATUS_OK) {
        return;
    }
    status = presence_apply(core, presence, 2u /* LOCAL_UI */);
    if (status != LOCKER_STATUS_OK) {
        return;
    }
    /* 到柜证明立即尝试推进到 ACCESS_PENDING；物理条件不满足就留在 REQUESTED。 */
    status = operation_arm(core);
    if (status == LOCKER_STATUS_OK) {
        domain_outbox_pump(core);
    }
}

void domain_handle_maintenance(locker_core_t *core, const domain_job_t *job)
{
    const locker_maintenance_t *request = &job->body.maintenance.maintenance;
    locker_status_t status = domain_principal_check(core, &request->principal);
    if (status != LOCKER_STATUS_OK) {
        return;
    }
    if (request->principal.role != LOCKER_ROLE_ADMIN) {
        return;
    }
    /* 维护核验不是普通确认的别名：不改写操作历史、不发脉冲、不清缺口。 */
    if (core->phase != DOMAIN_PHASE_READY || !core->device_snapshot_valid) {
        return;
    }
    if (core->physical.door != LOCKER_DOOR_STATE_CLOSED || core->physical.item == LOCKER_ITEM_STATE_UNKNOWN) {
        return;
    }
    if (core->physical.physical_revision != request->physical_revision ||
        core->revision != request->snapshot_revision) {
        return;
    }
    /* 只解除可以解除的 hold：重新做一次 CAS 放行，让设备回到 READY。 */
    locker_ready_guard_t guard;
    memset(&guard, 0, sizeof(guard));
    guard.session_id = core->session.session_id;
    guard.journal_epoch = core->session.journal_epoch;
    guard.expected_boot_id = core->session.boot_id;
    guard.expected_physical_revision = core->physical.physical_revision;
    guard.persisted_through = core->import_cursor;
    (void)locker_device_resume_ready(core->device, &guard);
}

void domain_handle_tick(locker_core_t *core, uint64_t tick_ms)
{
    domain_now_advance(core, tick_ms);
    domain_timeouts(core);
    /* 先释放上一轮动作的上下文，再考虑发送新动作。
       顺序反过来时，终结落库后、释放完成前的那次 tick 会先让 outbox 泵看到
       「没有在飞命令、状态也允许」，于是新动作撞上设备侧还没清掉的 has_active，
       直接被 BUSY 顶回核验（审计 R3-B-2：core_tick 先 outbox 后 release）。
       释放是幂等的只读收尾，先做不会推迟任何业务。 */
    domain_release_pump(core);
    if (core->phase == DOMAIN_PHASE_READY) {
        /*
         * 到柜证明与物理前置条件可能错开：证明提交时门磁还没稳定，arm 会留在
         * REQUESTED。这里在证明仍新鲜的窗口内继续尝试，过期由 arm 自己转
         * REVIEW_REQUIRED，保证「15 秒证明过期」有确定出口而不是永远挂着。
         */
        if (core->active.used && core->active.state == LOCKER_OPERATION_STATE_REQUESTED &&
            core->active.proof_consumed) {
            (void)operation_arm(core);
        }
        domain_outbox_pump(core);
    }
    domain_device_pump(core);
    domain_anchor_request(core);
    domain_progress_publish(core);
    domain_completion_reap(core, core->now_ms);
}

void domain_handle_job(locker_core_t *core, const domain_job_t *job)
{
    /* 本次作业持有的完成槽令牌：下面每个处理器写结果前都会核对槽的归属，
       只有令牌仍对得上才写（审计 R3-B-14）。设备事件/应答作业没有等待方，
       令牌保持全零，写回时必然不匹配——它们本来就不该写完成槽。 */
    core->job_token = job->completion_token;
    switch ((domain_job_kind_t)job->kind) {
    case DOMAIN_JOB_COMMAND:
        domain_handle_command(core, job);
        break;
    case DOMAIN_JOB_QUERY:
        domain_handle_query(core, job);
        break;
    case DOMAIN_JOB_AUTH:
        domain_handle_auth(core, job);
        break;
    case DOMAIN_JOB_PRESENCE:
        domain_handle_presence(core, job);
        break;
    case DOMAIN_JOB_MAINTENANCE:
        domain_handle_maintenance(core, job);
        break;
    case DOMAIN_JOB_TICK:
        domain_handle_tick(core, job->body.tick_ms);
        break;
    case DOMAIN_JOB_DEVICE_EVENT:
        domain_handle_device_event(core, &job->body.event, job->completion);
        break;
    case DOMAIN_JOB_DEVICE_REPLY:
        domain_handle_device_reply(core, &job->body.reply, job->completion);
        break;
    case DOMAIN_JOB_STOP:
    default:
        break;
    }
}
