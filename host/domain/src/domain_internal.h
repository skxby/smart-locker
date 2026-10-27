#ifndef LOCKER_DOMAIN_INTERNAL_H
#define LOCKER_DOMAIN_INTERNAL_H

/*
 * S1-04 业务核心私有头。
 *
 * 只有 host/domain/src 下的实现文件包含它。对外公开面只有
 * contracts/include/locker_contracts.h 里的 Core 端口与 DTO；本头里的结构、
 * 队列元素和作业类型都不是产品 API，不得被 host/http、host/ipc 或测试直接使用。
 *
 * 线程模型（docs/architecture/module-ownership.md:49-52）：
 *   - 一个 Core 线程拥有全部业务状态与唯一 SQLite 写连接；
 *   - 其他线程只经 submit / query / apply_device_event / apply_device_reply / tick
 *     把**按值复制**的请求放进有界队列，Core 线程是唯一的出队者；
 *   - 设备 sink 回调跑在设备 reader 线程上，只往事件/响应环里 try_push，绝不在
 *     回调里等待 Core（回调返回 NO_SPACE 会让设备侧 shutdown 会话，见
 *     host/device/README.md:81）。环满时锁存溢出标志并返回 OK，由 Core 用
 *     设备日志补取，而不是静默丢弃。
 */

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "locker_contracts.h"

#include "locker/common/bounded_queue.h"
#include "locker/common/clock.h"
#include "locker/common/numeric.h"
#include "locker/common/uuid.h"

#include "domain_json.h"
#include "domain_sha256.h"
#include "domain_time.h"

/*
 * 契约头里的 wire 枚举是「双前缀」命名（LOCKER_WIRE_WIRE_RESULT_OK、
 * LOCKER_WIRE_DEVICE_EVENT_TYPE_PULSE_STARTED）；host/device 侧另有自己的
 * 短名（LOCKER_WIRE_RESULT_OK，见 host/device/include/locker/device/codec.h）。
 * 业务核心不得包含设备模块私有头（module-ownership.md:23），所以在自己这层
 * 定义一组私有别名，指向契约头里的真源名字。这不是新接口，也不改契约。
 */
#define LOCKER_WIRE_RESULT_OK LOCKER_WIRE_WIRE_RESULT_OK
#define LOCKER_WIRE_RESULT_BAD_VERSION LOCKER_WIRE_WIRE_RESULT_BAD_VERSION
#define LOCKER_WIRE_RESULT_BAD_SESSION LOCKER_WIRE_WIRE_RESULT_BAD_SESSION
#define LOCKER_WIRE_RESULT_NOT_READY LOCKER_WIRE_WIRE_RESULT_NOT_READY
#define LOCKER_WIRE_RESULT_BAD_PRECONDITION LOCKER_WIRE_WIRE_RESULT_BAD_PRECONDITION
#define LOCKER_WIRE_RESULT_REPLAY_BLOCKED LOCKER_WIRE_WIRE_RESULT_REPLAY_BLOCKED
#define LOCKER_WIRE_RESULT_ACTION_GAP LOCKER_WIRE_WIRE_RESULT_ACTION_GAP
#define LOCKER_WIRE_RESULT_BUSY LOCKER_WIRE_WIRE_RESULT_BUSY
#define LOCKER_WIRE_RESULT_LOG_UNAVAILABLE LOCKER_WIRE_WIRE_RESULT_LOG_UNAVAILABLE
#define LOCKER_WIRE_RESULT_UNKNOWN_RESULT LOCKER_WIRE_WIRE_RESULT_UNKNOWN_RESULT
#define LOCKER_WIRE_RESULT_BAD_PAYLOAD LOCKER_WIRE_WIRE_RESULT_BAD_PAYLOAD
#define LOCKER_WIRE_RESULT_CURSOR_GAP LOCKER_WIRE_WIRE_RESULT_CURSOR_GAP
#define LOCKER_WIRE_RESULT_PHYSICAL_CHANGED LOCKER_WIRE_WIRE_RESULT_PHYSICAL_CHANGED
#define LOCKER_WIRE_RESULT_JOURNAL_CHANGED LOCKER_WIRE_WIRE_RESULT_JOURNAL_CHANGED
#define LOCKER_WIRE_RESULT_UNSUPPORTED LOCKER_WIRE_WIRE_RESULT_UNSUPPORTED

#define LOCKER_DEVICE_EVENT_BOOT LOCKER_WIRE_DEVICE_EVENT_TYPE_BOOT
#define LOCKER_DEVICE_EVENT_PULSE_STARTED LOCKER_WIRE_DEVICE_EVENT_TYPE_PULSE_STARTED
#define LOCKER_DEVICE_EVENT_PULSE_FINISHED LOCKER_WIRE_DEVICE_EVENT_TYPE_PULSE_FINISHED
#define LOCKER_DEVICE_EVENT_DOOR_OPENED LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_OPENED
#define LOCKER_DEVICE_EVENT_DOOR_CLOSED LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_CLOSED
#define LOCKER_DEVICE_EVENT_ITEM_REMOVED LOCKER_WIRE_DEVICE_EVENT_TYPE_ITEM_REMOVED
#define LOCKER_DEVICE_EVENT_ITEM_PRESENT LOCKER_WIRE_DEVICE_EVENT_TYPE_ITEM_PRESENT
#define LOCKER_DEVICE_EVENT_CARD_OBSERVED LOCKER_WIRE_DEVICE_EVENT_TYPE_CARD_OBSERVED
#define LOCKER_DEVICE_EVENT_SUPPLY_CHANGED LOCKER_WIRE_DEVICE_EVENT_TYPE_SUPPLY_CHANGED
#define LOCKER_DEVICE_EVENT_HEALTH_CHANGED LOCKER_WIRE_DEVICE_EVENT_TYPE_HEALTH_CHANGED
#define LOCKER_DEVICE_EVENT_JOURNAL_FAULT LOCKER_WIRE_DEVICE_EVENT_TYPE_JOURNAL_FAULT
#define LOCKER_DEVICE_EVENT_COMMAND_ACCEPTED LOCKER_WIRE_DEVICE_EVENT_TYPE_COMMAND_ACCEPTED
#define LOCKER_DEVICE_EVENT_CONTEXT_RELEASED LOCKER_WIRE_DEVICE_EVENT_TYPE_CONTEXT_RELEASED
#define LOCKER_DEVICE_EVENT_CONTINUITY_LOST LOCKER_WIRE_DEVICE_EVENT_TYPE_CONTINUITY_LOST
#define LOCKER_DEVICE_EVENT_UI_RESTART_REQUEST LOCKER_WIRE_DEVICE_EVENT_TYPE_UI_RESTART_REQUEST

#define LOCKER_DOMAIN_UUID_TEXT 37u
#define LOCKER_DOMAIN_U64_TEXT 21u
#define LOCKER_DOMAIN_TIMESTAMP_TEXT 32u
#define LOCKER_DOMAIN_KEY_TEXT 194u
#define LOCKER_DOMAIN_COLLECTION_TEXT 33u

/* 单页设备事件上限：uart-layouts.json 的 max_event_count。 */
#define LOCKER_DOMAIN_EVENT_PAGE_MAX 4u

/* 在途设备请求：Core 发出的请求号 -> 用途，回复按 request_no 匹配。 */
#define LOCKER_DOMAIN_PENDING_SLOTS 8u
/* 完成槽：HTTP/IPC 用 submission_token 取结果，最长保留 completion_retention_ms。 */
#define LOCKER_DOMAIN_COMPLETION_SLOTS 32u
/* 事件证据里最多保留的源事件键（schema: Evidence.source_events maxItems 32）。 */
#define LOCKER_DOMAIN_SOURCE_EVENTS 32u
/* 单个 RepositoryChange 的 mutations 上限（schema: maxItems 32）。 */
#define LOCKER_DOMAIN_MAX_MUTATIONS 32u
#define LOCKER_DOMAIN_MAX_CHECKS 32u

/* 单文档查询结果缓冲：一个 OperationRecord 记录约 2 KB，留 4 倍余量。 */
#define LOCKER_DOMAIN_DOC_BYTES 8192u
/* 分页查询结果缓冲：契约允许 100 条文档 + 65535 字节上限。 */
#define LOCKER_DOMAIN_PAGE_BYTES 65536u
#define LOCKER_DOMAIN_DOC_NODES 768u
#define LOCKER_DOMAIN_DOC_MEMBERS 768u
#define LOCKER_DOMAIN_PAGE_NODES 2048u
#define LOCKER_DOMAIN_PAGE_MEMBERS 2048u

/* ---------- 设备物理镜像 ---------- */

typedef struct domain_physical {
    bool valid;
    char device_id[25];
    locker_uuid_t journal_epoch;
    uint64_t boot_id;
    uint64_t physical_revision;
    uint8_t door; /* locker_door_state_t */
    uint8_t item; /* locker_item_state_t */
    bool supply_valid;
    uint32_t supply_mv;
    uint64_t observed_mono_ms; /* 收到该事实时的宿主单调毫秒 */
    uint8_t time_quality;      /* locker_time_quality_t */
    bool continuity_ok;
    uint64_t dropped_events;
    locker_event_key_t durable_cursor;
} domain_physical_t;

/* ---------- 操作工作副本（只由 Core 线程持有） ---------- */

typedef struct domain_operation {
    bool used;
    locker_uuid_t id;
    locker_uuid_t tool_id;
    locker_uuid_t actor_id;
    locker_uuid_t command_id;
    bool has_command;
    uint8_t kind;  /* locker_operation_kind_t */
    uint8_t state; /* locker_operation_state_t */
    uint64_t revision;
    char reason_code[65];
    char created_at[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    char updated_at[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    uint64_t created_ms;
    uint64_t updated_ms;

    bool has_baseline;
    domain_physical_t baseline;

    bool history_complete;
    bool door_open_seen;
    bool item_transition_seen;
    bool manual_resolution;
    bool has_physical;
    domain_physical_t physical;
    locker_event_key_t source_events[LOCKER_DOMAIN_SOURCE_EVENTS];
    uint32_t source_event_count;

    bool has_loan;
    locker_uuid_t related_loan_id;
    uint8_t proof_source; /* 0 = null, 1 = CARD, 2 = LOCAL_UI */
    bool proof_consumed;
    /* 柜前证明被 Core 接纳的时刻（单调毫秒）。证明有效期 LOCKER_PRESENCE_TTL_MS，
       过期后必须重新到柜，不能拿旧证明开门（契约 :237 与主规格 15 秒时效）。 */
    uint64_t proof_ms;

    /* 运行时计时（单调毫秒） */
    uint64_t requested_deadline_ms;
    uint64_t access_pending_ms;
    uint64_t pulse_started_ms;
    uint64_t door_opened_ms;
    uint64_t wait_confirm_deadline_ms;

    bool outbox_present;
    locker_uuid_t outbox_id;
    uint64_t action_no;
    uint8_t outbox_state; /* 0 = 无, 否则 OutboxRecord.state 序号 */
    bool send_started;
    uint64_t send_started_ms;
    /* OutboxRecord 的其余冻结字段（契约 schema 没有 command_id，命令标识保存在
       OperationRecord.operation.command_id 上；发送意图从这里与那里拼出来）。 */
    locker_uuid_t outbox_session_id;
    locker_uuid_t outbox_journal_epoch;
    uint64_t outbox_boot_id;
    uint64_t outbox_physical_revision;
    uint64_t outbox_not_after_ms;
} domain_operation_t;

/* ---------- 仓储访问辅助 ---------- */

typedef struct domain_doc {
    bool found;
    uint64_t revision;
    locker_json_document_t doc;
    const locker_json_node_t *value; /* documents[0].value，未找到为 NULL */
} domain_doc_t;

/* ---------- RepositoryChange 构造 ---------- */

typedef struct domain_change {
    locker_json_writer_t writer;
    char *buffer;
    size_t capacity;
    uint32_t checks;
    uint32_t mutations;
    bool mutations_started;
    bool broken;
} domain_change_t;

/* ---------- 作业 ---------- */

typedef enum domain_job_kind {
    DOMAIN_JOB_COMMAND = 1,
    DOMAIN_JOB_QUERY = 2,
    DOMAIN_JOB_AUTH = 3,
    DOMAIN_JOB_PRESENCE = 4,
    DOMAIN_JOB_MAINTENANCE = 5,
    DOMAIN_JOB_TICK = 6,
    DOMAIN_JOB_DEVICE_EVENT = 7,
    DOMAIN_JOB_DEVICE_REPLY = 8,
    DOMAIN_JOB_STOP = 9
} domain_job_kind_t;

typedef struct domain_job {
    uint8_t kind;
    uint32_t completion; /* 完成槽下标；UINT32_MAX 表示无 */
    union {
        locker_command_t command;
        locker_query_t query;
        locker_auth_request_t auth;
        locker_device_event_t event;
        locker_device_reply_t reply;
        uint64_t tick_ms;
        struct {
            locker_peer_t peer;
            locker_presence_t presence;
        } presence;
        struct {
            locker_peer_t peer;
            locker_maintenance_t maintenance;
        } maintenance;
    } body;
} domain_job_t;

typedef struct domain_event_slot {
    locker_device_event_t event;
    uint32_t completion;
} domain_event_slot_t;

typedef struct domain_reply_slot {
    locker_device_reply_t reply;
    uint32_t completion;
} domain_reply_slot_t;

/* ---------- 完成槽 ---------- */

typedef struct domain_completion {
    bool used;
    bool accept_ready;
    bool final_ready;
    locker_uuid_t token;
    locker_status_t accept_status;
    locker_status_t final_status;
    bool has_operation;
    locker_uuid_t operation_id;
    bool has_principal;
    locker_principal_t principal; /* 只用于 AUTH 作业的结果回传 */
    uint64_t revision;
    char *json; /* malloc，NULL 表示无结果体 */
    size_t length;
    uint64_t expires_ms;
} domain_completion_t;

/* 在途请求用途 */
typedef enum domain_purpose {
    DOMAIN_PURPOSE_NONE = 0,
    DOMAIN_PURPOSE_SNAPSHOT = 1,
    DOMAIN_PURPOSE_FETCH = 2,
    DOMAIN_PURPOSE_EXECUTE = 3,
    DOMAIN_PURPOSE_QUERY_COMMAND = 4,
    DOMAIN_PURPOSE_ACK = 5,
    DOMAIN_PURPOSE_RESUME = 6,
    DOMAIN_PURPOSE_RELEASE = 7,
    DOMAIN_PURPOSE_HEALTH = 8,
    DOMAIN_PURPOSE_ANCHOR = 9
} domain_purpose_t;

typedef struct domain_pending {
    bool used;
    uint32_t request_no;
    uint8_t purpose;
    uint64_t started_ms;
    bool has_command;
    locker_uuid_t command_id;
} domain_pending_t;

/* 当前设备会话：只由 Core 线程维护，来自 HELLO 或快照响应。 */
typedef struct domain_session {
    bool valid;
    locker_uuid_t session_id;
    locker_uuid_t journal_epoch;
    uint64_t boot_id;
    char device_id[25]; /* 12 字节 UID 的 24 位小写十六进制 */
    uint8_t device_mode;
    uint8_t journal_state;
    uint32_t capabilities;
    uint64_t action_watermark;
} domain_session_t;

/* ---------- Core ---------- */

typedef enum domain_phase {
    DOMAIN_PHASE_INIT = 0,
    DOMAIN_PHASE_RECOVERING = 1,
    DOMAIN_PHASE_READY = 2,
    DOMAIN_PHASE_STOPPED = 3
} domain_phase_t;

typedef struct domain_card_observation {
    bool valid;
    uint64_t mono_ms;
    uint8_t uid_len;
    uint8_t uid[10];
    bool user_resolved;
    locker_uuid_t user_id;
} domain_card_observation_t;

typedef struct domain_cursor_state {
    bool valid;
    locker_uuid_t journal_epoch;
    char device_id[25];
    locker_event_key_t through;
    bool continuity_ok;
} domain_cursor_state_t;

struct locker_core {
    bool initialized;
    locker_core_config_t config;
    locker_repo_t *repo;
    locker_device_t *device;
    locker_publisher_t *publisher;
    char cabinet_name[257];
    locker_uuid_t instance_id;
    locker_clock_port_t clock;
    uint32_t completion_retention_ms;
    bool vision_pre_required;
    bool vision_post_required;

    pthread_t thread;
    bool thread_running;
    /* 线程退出标志由 Core 线程写、停止方读，必须走原子；普通 bool 跨线程
       读写是 data race（TSan 在 domain_core.c:443 与 :597 实测命中）。 */
    atomic_bool thread_exited;
    atomic_bool stop_requested;
    bool stop_requested_joined;

    pthread_mutex_t mutex; /* 保护 stop 标志与 loop 条件变量 */
    pthread_cond_t cond;

    locker_bounded_queue_t queue;
    domain_job_t *queue_storage;

    locker_bounded_queue_t event_ring;
    domain_event_slot_t *event_storage;
    locker_bounded_queue_t reply_ring;
    domain_reply_slot_t *reply_storage;
    /* 设备回调跑在 reader 线程：环满只置原子溢出标志，由 Core 线程消费成
       缺口状态（普通 bool 跨线程写会构成 data race）。 */
    atomic_bool event_overflow_flag;
    atomic_bool reply_overflow_flag;

    pthread_mutex_t completion_mutex;
    pthread_cond_t completion_cond;
    domain_completion_t completions[LOCKER_DOMAIN_COMPLETION_SLOTS];

    pthread_mutex_t progress_mutex;
    locker_progress_t progress;
    bool progress_valid;

    /* ---- 以下字段只由 Core 线程访问 ---- */
    uint64_t now_ms;
    uint64_t revision;
    uint8_t phase;
    bool event_overflow;
    bool reply_overflow;
    bool device_session_up;
    bool device_snapshot_valid;
    domain_session_t session;
    domain_physical_t physical;
    domain_cursor_state_t cursor;
    domain_operation_t active;
    domain_card_observation_t last_card;
    locker_uuid_t pending_command_id;
    bool pending_command_valid;
    uint64_t pending_command_ms;
    /* 超时后只查询不重发（契约 :221）：查询发出后置位，避免每个 tick 反复查。 */
    bool pending_command_queried;
    domain_pending_t pending[LOCKER_DOMAIN_PENDING_SLOTS];
    uint32_t request_no_next;
    bool recovery_done;
    locker_status_t recovery_status;
    bool recovery_ready;
    uint64_t action_watermark;
    /* 下一个可用的主机动作号：只增，恢复时抬到已见水位之上。 */
    uint64_t next_action_no;
    bool fetch_in_flight;
    bool snapshot_in_flight;
    bool ack_in_flight;
    locker_event_key_t ack_cursor;
    bool ack_cursor_valid;
    /* 仓储已提交的连续水位：补取与 ACK 都只能用它，不能用内存里的
       cursor.through（后者随 live 事件前进，尚未落库）。 */
    locker_event_key_t import_cursor;
    bool import_cursor_valid;
    bool import_continuity;

    /* 主机 <-> MCU 单调时钟换算（internal-interfaces.md:133）。
       只有 RTT<=200ms、年龄<=3000ms 且 boot 一致的样本才可用于动作期限。 */
    bool anchor_valid;
    uint64_t anchor_boot_id;
    int64_t anchor_offset_low_ms;  /* mcu - host 的下界 */
    int64_t anchor_offset_high_ms; /* mcu - host 的上界 */
    uint64_t anchor_sampled_ms;    /* 采样时刻（宿主单调毫秒） */
    uint64_t last_anchor_ms;       /* 上次发出 TIME_ANCHOR 的宿主单调毫秒 */

    /* ---- 设备泵与投影（Core 线程私有） ---- */
    uint64_t snapshot_requested_ms;
    uint64_t last_health_ms;
    uint64_t last_fetch_ms;
    uint64_t last_pump_ms;
    bool fetch_gap;
    bool execute_in_flight;
    locker_uuid_t tool_id;
    char tool_name[65];
    uint64_t tool_revision;
    uint8_t tool_availability;
    bool tool_valid;
    bool tool_enabled;
    /* 有效借用投影：由已提交的 loans 记录重建，不由客户端断言。 */
    bool loan_open;
    locker_uuid_t loan_id;
    locker_uuid_t loan_tool_id;
    locker_uuid_t loan_borrower_id;
    locker_uuid_t loan_borrow_operation_id;
    char loan_opened_at[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    uint64_t progress_counter;
    uint64_t last_progress_ms;

    /* arguments_json 就地解析用的节点/成员池（Core 线程独占）。 */
    locker_json_node_t arg_nodes[LOCKER_DOMAIN_DOC_NODES];
    locker_json_member_t arg_members[LOCKER_DOMAIN_DOC_MEMBERS];

    /* 仓储 DTO 缓冲放在 Core 结构里而不是栈上：locker_repo_result_t 与
       locker_recovery_t 各有 64 KB，Core 线程栈不承担这个体积。 */
    locker_repo_result_t repo_result;
    locker_recovery_t recovery;
    bool persisted_state_valid;
    char persisted_device_id[25];
    uint64_t persisted_boot_id;
    uint64_t persisted_physical_revision;
    uint8_t persisted_mode;
    char page_buffer[LOCKER_DOMAIN_PAGE_BYTES];
    locker_json_node_t page_nodes[LOCKER_DOMAIN_PAGE_NODES];
    locker_json_member_t page_members[LOCKER_DOMAIN_PAGE_MEMBERS];
    char doc_buffer[2][LOCKER_DOMAIN_DOC_BYTES];
    locker_json_node_t doc_nodes[2][LOCKER_DOMAIN_DOC_NODES];
    locker_json_member_t doc_members[2][LOCKER_DOMAIN_DOC_MEMBERS];
    char change_buffer[LOCKER_DOMAIN_PAGE_BYTES];
    char response_buffer[LOCKER_DOMAIN_PAGE_BYTES];
};

/* ---------- 文本与枚举工具 ---------- */

size_t domain_strnlen(const char *text, size_t max_length);
bool domain_text_copy(char *destination, size_t destination_size, const char *source);
bool domain_text_equal(const char *left, const char *right);
bool domain_uuid_to_text(const locker_uuid_t *uuid, char *out, size_t out_size);
bool domain_uuid_from_text(const char *text, size_t length, locker_uuid_t *out);
bool domain_uuid_text_valid(const char *text, size_t length);
const char *domain_state_name(uint8_t state);
const char *domain_kind_name(uint8_t kind);
const char *domain_role_name(uint8_t role);
const char *domain_door_name(uint8_t door);
const char *domain_item_name(uint8_t item);
const char *domain_mode_name(uint8_t mode);
const char *domain_time_quality_name(uint8_t quality);
const char *domain_action_name(uint8_t action);
const char *domain_proof_name(uint8_t proof);
const char *domain_outbox_state_name(uint8_t state);
const char *domain_availability_name(uint8_t availability);
bool domain_state_from_name(const char *text, size_t length, uint8_t *out_state);
bool domain_kind_from_name(const char *text, size_t length, uint8_t *out_kind);
bool domain_role_from_name(const char *text, size_t length, uint8_t *out_role);
bool domain_resolution_from_name(const char *text, size_t length, uint8_t *out_resolution);
bool domain_terminal_state(uint8_t state);
bool domain_state_rank(uint8_t state, uint8_t *out_rank);
uint8_t domain_state_from_wire_rank(uint8_t rank);

/* 单调时间与单调钳制：now 只增不减。 */
uint64_t domain_now(locker_core_t *core);
uint64_t domain_now_advance(locker_core_t *core, uint64_t candidate);

/* ---------- 仓储访问 ---------- */

locker_status_t domain_repo_doc(locker_core_t *core, int slot, const char *collection, const char *key,
                                domain_doc_t *out);
locker_status_t domain_repo_page(locker_core_t *core, const char *collection, const char *after_key,
                                 uint16_t limit, domain_doc_t *out);
locker_status_t domain_repo_refresh_revision(locker_core_t *core);

/* ---------- RepositoryChange ---------- */

void domain_change_init(locker_core_t *core, domain_change_t *change, uint64_t expected, uint64_t next);
bool domain_change_check(domain_change_t *change, const char *collection, const char *key,
                         const char *condition);
bool domain_change_put_begin(domain_change_t *change, const char *collection, const char *key);
void domain_change_put_end(domain_change_t *change);
bool domain_change_delete(domain_change_t *change, const char *collection, const char *key);
locker_status_t domain_change_commit(locker_core_t *core, domain_change_t *change);

/* ---------- 文档构造（写入仓储的 value） ---------- */

void domain_write_physical(locker_json_writer_t *writer, const domain_physical_t *physical);
void domain_write_evidence(locker_json_writer_t *writer, const domain_operation_t *operation);
void domain_write_operation(locker_json_writer_t *writer, const domain_operation_t *operation);
void domain_write_operation_record(locker_json_writer_t *writer, const domain_operation_t *operation,
                                  const locker_uuid_t *host_instance_id);
void domain_write_outbox(locker_json_writer_t *writer, const domain_operation_t *operation,
                         const char *state, const char *attempted_at);
void domain_write_audit(locker_json_writer_t *writer, const locker_uuid_t *id, const locker_uuid_t *actor,
                        const locker_uuid_t *operation, const char *type, const char *reason,
                        const char *occurred_at, bool has_occurred, const locker_event_key_t *source,
                        uint8_t time_quality);
void domain_write_slot_guard(locker_json_writer_t *writer, const locker_uuid_t *operation_id);
void domain_write_import_cursor(locker_json_writer_t *writer, const locker_uuid_t *epoch,
                                const char *device_id, const locker_event_key_t *through,
                                bool continuity_ok);
void domain_write_device_state(locker_json_writer_t *writer, const char *cabinet_name,
                               const char *contract_version, bool simulated, uint8_t mode,
                               const domain_physical_t *physical, uint64_t snapshot_revision,
                               const locker_uuid_t *active_command, bool has_active_command);
void domain_write_loan(locker_json_writer_t *writer, const locker_uuid_t *id, const locker_uuid_t *tool_id,
                       const locker_uuid_t *borrower_id, const locker_uuid_t *borrow_operation,
                       const locker_uuid_t *return_operation, bool has_return, const char *opened_at,
                       const char *closed_at, bool has_closed);
void domain_write_tool(locker_json_writer_t *writer, const locker_uuid_t *id, const char *name,
                       uint64_t revision, uint8_t availability);
void domain_write_tool_record(locker_json_writer_t *writer, const locker_uuid_t *id, const char *name,
                              uint64_t revision, bool enabled);
void domain_write_idempotency(locker_json_writer_t *writer, const locker_uuid_t *actor, const char *method,
                              const char *path, const locker_uuid_t *key, const char *request_hash,
                              int response_status, const char *response_json, const char *expires_at);

/* 事件键 -> JSON */
void domain_write_event_key(locker_json_writer_t *writer, const locker_event_key_t *key);

/* 相位 -> DeviceMode（RECONCILING/READY/OFFLINE）。 */
uint8_t domain_core_device_mode(const locker_core_t *core);

/* ---------- 状态机 ---------- */

void domain_operation_reset(domain_operation_t *operation);
void domain_operation_allowed_actions(const domain_operation_t *operation, uint8_t *out, uint32_t *count);
void domain_operation_touch(locker_core_t *core, domain_operation_t *operation);
locker_status_t domain_operation_persist(locker_core_t *core, domain_change_t *change,
                                         const domain_operation_t *operation);
locker_status_t domain_operation_set_state(locker_core_t *core, domain_operation_t *operation,
                                           uint8_t state, const char *reason_code);
void domain_operation_add_source_event(domain_operation_t *operation, const locker_event_key_t *key);
void domain_operation_capture_physical(domain_operation_t *operation, const domain_physical_t *physical);

/* ---------- 各功能文件入口（全部在 Core 线程执行） ---------- */

void domain_handle_job(locker_core_t *core, const domain_job_t *job);
void domain_handle_command(locker_core_t *core, const domain_job_t *job);
void domain_handle_query(locker_core_t *core, const domain_job_t *job);
void domain_handle_auth(locker_core_t *core, const domain_job_t *job);
void domain_handle_presence(locker_core_t *core, const domain_job_t *job);
void domain_handle_maintenance(locker_core_t *core, const domain_job_t *job);
void domain_handle_device_event(locker_core_t *core, const locker_device_event_t *event, uint32_t completion);
void domain_handle_device_reply(locker_core_t *core, const locker_device_reply_t *reply, uint32_t completion);
void domain_handle_tick(locker_core_t *core, uint64_t tick_ms);
locker_status_t domain_recover(locker_core_t *core);
void domain_outbox_pump(locker_core_t *core);
void domain_timeouts(locker_core_t *core);
void domain_fetch_pump(locker_core_t *core);

/* 完成槽（线程安全） */
uint32_t domain_completion_acquire(locker_core_t *core, locker_uuid_t *out_token);
void domain_completion_release(locker_core_t *core, uint32_t slot);
void domain_completion_accept(locker_core_t *core, uint32_t slot, locker_status_t status,
                              const locker_uuid_t *operation_id, bool has_operation);
void domain_completion_finish(locker_core_t *core, uint32_t slot, locker_status_t status,
                              const char *json, size_t length, uint64_t revision);
locker_status_t domain_completion_wait_accept(locker_core_t *core, uint32_t slot, uint32_t timeout_ms,
                                              locker_status_t *out_status, locker_uuid_t *out_operation,
                                              bool *out_has_operation);
locker_status_t domain_completion_wait_final(locker_core_t *core, uint32_t slot, uint32_t timeout_ms,
                                             locker_query_result_t *out_result);
/* 取 AUTH 终态的 principal；没有则返回 false。 */
bool domain_completion_take_principal(locker_core_t *core, uint32_t slot, locker_principal_t *out);
bool domain_completion_take_json(locker_core_t *core, uint32_t slot, char *out, size_t out_size,
                                 size_t *out_length, uint64_t *out_revision);
/* 按 submission_token 找完成槽；找不到返回 UINT32_MAX。 */
uint32_t domain_completion_find(locker_core_t *core, const locker_uuid_t *token);
/* AUTH 作业回传 principal（只在认证成功时调用）。 */
void domain_completion_set_principal(locker_core_t *core, uint32_t slot, const locker_principal_t *principal);
/* 回收超过保留期的完成槽（Core 线程定期调用）。 */
void domain_completion_reap(locker_core_t *core, uint64_t now_ms);

/* ---------- 命令 / 查询共用 ---------- */

/* 复检 principal：会话存在、未撤销、未过期，用户存在且 enabled，角色一致。
   契约 :287 要求「Validate current session/user in core; recheck session at
   command execution」，因此每个命令执行前都要走一次，不能只信 receipt 时的快照。 */
locker_status_t domain_principal_check(locker_core_t *core, const locker_principal_t *principal);

/* 把 OperationRecord 文档反序列化成工作副本（恢复期直接按文档构造用）。 */
bool domain_operation_from_record(const locker_json_document_t *doc, const locker_json_node_t *root,
                                  domain_operation_t *out);
/* 把 import_cursors 记录载入 Core 的连续水位（首个有效记录生效）。 */
bool domain_import_cursor_from_record(const locker_json_document_t *doc, const locker_json_node_t *root,
                                      locker_core_t *core);
bool domain_json_read_enum_device_mode(const locker_json_document_t *doc, const locker_json_node_t *node,
                                       uint8_t *out);

/* 读取 operations/<id> 并反序列化成工作副本（含 evidence/baseline/loan 关联）。 */
locker_status_t domain_operation_load(locker_core_t *core, const locker_uuid_t *id, int slot,
                                      domain_operation_t *out);
/* 单文档提交：把 operation 写回仓储并推进 revision。 */
locker_status_t domain_persist_operation(locker_core_t *core, domain_operation_t *operation);

/* 公开投影（openapi.json 的 Operation / Loan / Tool / Device 组件）。 */
void domain_write_operation_projection(locker_json_writer_t *writer, locker_core_t *core,
                                       const domain_operation_t *operation);
void domain_write_tool_projection(locker_json_writer_t *writer, locker_core_t *core);
uint8_t domain_tool_availability(const locker_core_t *core);
void domain_write_device_projection(locker_json_writer_t *writer, locker_core_t *core);

/* ---------- 设备桥接（Core 线程） ---------- */

uint32_t domain_request_no(locker_core_t *core, uint8_t purpose);
void domain_pending_clear(locker_core_t *core, uint8_t purpose);
bool domain_pending_take(locker_core_t *core, uint32_t request_no, uint8_t *out_purpose);
void domain_device_reset(locker_core_t *core);
void domain_device_drain(locker_core_t *core);
void domain_device_pump(locker_core_t *core);
void domain_event_apply(locker_core_t *core, const locker_device_event_t *event);
void domain_reply_apply(locker_core_t *core, const locker_device_reply_t *reply);
/* 把 sink 绑到设备适配器；必须在 device_open 之前调用（契约 :311）。 */
locker_status_t domain_device_bind(locker_core_t *core);
/* 发送一次解锁意图；只由 outbox 泵调用，绝不重发同一物理 command。 */
locker_status_t domain_execute_intent(locker_core_t *core, domain_operation_t *operation);
/* 只查询、不重发：用于发送后结果未知。 */
locker_status_t domain_query_command(locker_core_t *core, const locker_uuid_t *command_id);

/* ---------- 时间锚（Core 线程） ---------- */

/* 发一次 TIME_ANCHOR；无设备或已有在途锚请求时返回 NOT_READY。 */
locker_status_t domain_anchor_request(locker_core_t *core);
/* 应用 TIME_ANCHOR 响应：只有 RTT 与年龄都合格、boot 一致才更新换算区间。 */
void domain_anchor_apply(locker_core_t *core, const locker_device_reply_t *reply);
/* 估算当前 MCU 单调毫秒区间；无有效锚返回 NOT_READY（fail closed，不猜）。 */
locker_status_t domain_anchor_mcu_now(const locker_core_t *core, uint64_t *out_low, uint64_t *out_high);

/* ---------- 有效借用投影（Core 线程） ---------- */

/* 终结前把 loan_id 挂到操作上：借出新建，归还复用当前有效借用的 id。 */
locker_status_t domain_loan_prepare(locker_core_t *core, domain_operation_t *operation);
/* 终结之后写账本：借出新建 Loan，归还关闭原记录（借用者与 tool 保持原值）。 */
locker_status_t domain_loan_apply(locker_core_t *core, domain_operation_t *operation);

/* 进度源样本发布（非阻塞、只增）。 */
void domain_progress_publish(locker_core_t *core);

/* 失败收尾：完成槽直接置失败，不产生结果体。 */
void domain_completion_fail(locker_core_t *core, uint32_t slot, locker_status_t status);

/* ---------- JSON 取值小工具（domain_state.c 导出，命令层复用） ---------- */

const locker_json_node_t *domain_json_member(const locker_json_document_t *doc,
                                             const locker_json_node_t *object, const char *key);
bool domain_json_read_uuid(const locker_json_document_t *doc, const locker_json_node_t *node,
                           locker_uuid_t *out);
bool domain_json_read_u64(const locker_json_document_t *doc, const locker_json_node_t *node, uint64_t *out);
bool domain_json_read_text(const locker_json_document_t *doc, const locker_json_node_t *node, char *out,
                           size_t out_size);
bool domain_json_read_int(const locker_json_document_t *doc, const locker_json_node_t *node, int64_t *out);
bool domain_json_read_bool(const locker_json_document_t *doc, const locker_json_node_t *node, bool *out);
bool domain_json_read_enum_state(const locker_json_document_t *doc, const locker_json_node_t *node,
                                 uint8_t *out);
bool domain_json_read_enum_kind(const locker_json_document_t *doc, const locker_json_node_t *node,
                                uint8_t *out);
bool domain_json_read_door(const locker_json_document_t *doc, const locker_json_node_t *node, uint8_t *out);
bool domain_json_read_item(const locker_json_document_t *doc, const locker_json_node_t *node, uint8_t *out);
bool domain_json_read_time_quality(const locker_json_document_t *doc, const locker_json_node_t *node,
                                   uint8_t *out);

/* 恢复期分页读取（同一 snapshot_revision 内完成必要集合恢复，x-rules 10）。 */
locker_status_t domain_repo_recovery_page(locker_core_t *core, const char *collection, const char *after_key,
                                         uint16_t limit, domain_doc_t *out);

/* 设备日志页导入：新事件去重 + 连续游标同事务，成功后推进 import_cursor 与 ACK。 */
locker_status_t domain_import_page(locker_core_t *core, const locker_wire_fetch_events_response_t *page);

/* 把当前 import_cursor 记为待 ACK 的水位。 */
void domain_ack_refresh(locker_core_t *core);

/* 设备快照落地成 device_state 记录（只在内容变化时提交）。 */
locker_status_t domain_persist_device_state(locker_core_t *core);

/* 设备身份学习：HELLO 的 12 字节 device_uid 或 live 事件的 24 位 hex。
   device_id 是 DeviceId/EventKey 的必填字段，但 FETCH_EVENTS 的 wire 游标只有
   boot/seq，所以 Core 必须自己记住握手得到的身份，不能猜也不能写占位值。 */
void domain_learn_device_id(locker_core_t *core, const char *device_id);
void domain_learn_device_uid(locker_core_t *core, const uint8_t uid[12]);

/* 响应构造（Core 线程） */
void domain_response_begin(locker_json_writer_t *writer, char *buffer, size_t capacity);
bool domain_response_data_object(locker_json_writer_t *writer);
void domain_response_end(locker_json_writer_t *writer, uint64_t revision,
                         const locker_uuid_t *request_id);

/* 发布结果 */
void domain_publish(locker_core_t *core, const locker_uuid_t *operation_id, const char *resource,
                    uint64_t revision);

#endif /* LOCKER_DOMAIN_INTERNAL_H */