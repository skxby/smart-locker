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

/* observation_flags 位表（docs/architecture/uart-v1.md:50）：
   bit0 SUPPLY_VALID、bit1 PHYSICAL_STABLE、bit2 HISTORY_INCOMPLETE、
   bit3 JOURNAL_DEGRADED、bit4 PULSE_ACTIVE。 */
#define DOMAIN_OBSERVATION_FLAG_SUPPLY_VALID 0x0001u
#define DOMAIN_OBSERVATION_FLAG_PHYSICAL_STABLE 0x0002u
#define DOMAIN_OBSERVATION_FLAG_HISTORY_INCOMPLETE 0x0004u
#define DOMAIN_OBSERVATION_FLAG_JOURNAL_DEGRADED 0x0008u
#define DOMAIN_OBSERVATION_FLAG_PULSE_ACTIVE 0x0010u

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

    /* 进入 WAIT_CONFIRM 时冻结的物理依据（审计 R1-B-4）。
       待确认期间只要出现新的物理事实（门又开、物品离开期望值、物理版本推进、
       物品未知），这一份依据就作废，操作必须立刻进核验——否则「开一次门又
       关回去」就能把已知的物理冲突洗掉，用户照旧确认完成。
       `wait_confirm_basis_valid` 为假表示该操作不是从证据路径进 WAIT_CONFIRM
       （例如恢复出来的），此时按保守规则处理。 */
    bool wait_confirm_basis_valid;
    uint64_t wait_confirm_physical_revision;
    uint8_t wait_confirm_item;
    uint8_t wait_confirm_door;
    /* 冻结依据必须绑定完整代际：physical_revision 只在同一次 boot 内单调
       （internal-interfaces.md:12），换 boot 之后 revision 会从头计数，只比
       revision 大小会把「设备重启后的旧事实」当成「更早的陈旧事实」放过
       （审计 R3-B-3 探针 A：冻结 boot=1/rev=9，送来 boot=2/rev=1 的成功快照
       仍保持 WAIT_CONFIRM）。journal_epoch 同理：换日志世代说明事件流已断开。 */
    uint64_t wait_confirm_boot_id;
    locker_uuid_t wait_confirm_journal_epoch;
    /* 已判定冲突但把「转核验」写进仓储失败时的闭锁标志。
       内存此时仍是 WAIT_CONFIRM，若只看状态就允许 confirm，存储一恢复就能
       直接把一次已知冲突的操作确认完成（审计 R3-B-3 探针 B）。这个标志不落库：
       重启后 WAIT_CONFIRM 一律转核验（domain_recover），不存在窗口。 */
    bool wait_confirm_blocked;

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
    /* 完成槽的令牌。槽号会被回收复用，只有令牌能证明「这个结果属于当初那个
       请求」——等待方超时释放后，同一槽号可能已经被下一个请求占用，作业若还
       按槽号写结果，就会把 A 的应答交到 B 手里（审计 R3-B-14 探针：A 超时释放、
       B 复用同一槽，B 的 wait token 合法，读到的却是 A 的结果）。 */
    locker_uuid_t completion_token;
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
    /* 当前正在执行的作业所持有的完成槽令牌（Core 线程私有）。
       响应写回前用它核对槽的归属：等待方超时释放后槽可能已被复用，
       裸下标写回会把 A 的结果塞进 B 的槽（审计 R3-B-14）。 */
    locker_uuid_t job_token;
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

    /* 设备侧动作上下文的释放（RELEASE_CONTEXT）。终端状态落库后必须通知
       MCU 放掉 has_active，否则下一次 EXECUTE 永远 BUSY、柜子只能用一次
       （审计 R1-B-3）。释放失败只重发/重核，绝不重放旧开锁动作。 */
    bool release_pending; /* 有待释放的上下文 */
    bool release_sent;    /* 已发出请求，等响应 */
    /* CONFLICT（CAS 失败）后不能凭「物理版本变了」就宣布设备侧已放手：
       uart-v1.md:402 要求先重新查询，再用回执里的活动上下文判定。
       置位后释放泵只等新快照、不重发，避免拿旧版本无限重试。 */
    bool release_recheck;
    uint64_t release_ms;  /* 首次登记的宿主单调毫秒 */
    locker_uuid_t release_operation_id;
    locker_uuid_t release_command_id;
    /* 设备侧当前持有的动作上下文（GET_SNAPSHOT 回执）。用途有二：
       ① 释放 CONFLICT 后核对「设备是不是已经放手」；
       ② 重启后内存里的释放登记已经丢失，只能靠它发现设备还挂着上一次
          动作的 has_active，并据此补登记一次释放（审计 R3-B-2 第三段）。 */
    locker_uuid_t device_active_operation_id;
    locker_uuid_t device_active_command_id;
    bool device_active_valid;

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

void domain_write_physical(locker_json_writer_t *writer, const domain_physical_t *physical, uint64_t now_ms);
void domain_write_evidence(locker_json_writer_t *writer, const domain_operation_t *operation, uint64_t now_ms);
void domain_write_operation(locker_json_writer_t *writer, const domain_operation_t *operation, uint64_t now_ms);
void domain_write_operation_record(locker_json_writer_t *writer, const domain_operation_t *operation,
                                  const locker_uuid_t *host_instance_id, uint64_t now_ms);
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
                               const locker_uuid_t *active_command, bool has_active_command,
                               uint64_t now_ms);
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
/* 终端状态落库后通知 MCU 释放动作上下文（RELEASE_CONTEXT）；不发则下一次
   EXECUTE 永远 BUSY（审计 R1-B-3）。 */
void domain_release_pump(locker_core_t *core);
void domain_timeouts(locker_core_t *core);
void domain_fetch_pump(locker_core_t *core);

/* 完成槽（线程安全） */
uint32_t domain_completion_acquire(locker_core_t *core, locker_uuid_t *out_token);
void domain_completion_release(locker_core_t *core, uint32_t slot);
/* 按令牌释放：只有槽里装的仍是这个令牌时才清槽。等待方超时后调用它，
   绝不能用裸下标——那时槽可能已经被下一个请求复用，按下标释放会直接
   抹掉别人的在途结果（审计 R3-B-14）。 */
void domain_completion_release_owned(locker_core_t *core, uint32_t slot, const locker_uuid_t *token);
/* 槽当前是否仍由这个令牌代表。Core 线程在派发作业前记下作业的令牌，
   写结果前再核对一次：期间等待方可能已经超时释放、槽被别的请求复用，
   此时这次写入属于「上一个请求」，必须丢弃而不是塞给新请求。 */
bool domain_completion_token_matches(locker_core_t *core, uint32_t slot, const locker_uuid_t *token);
/* 写结果的两个入口都带令牌，**在锁内**完成「核对身份 + 写入」：
   以前是先在锁外 token_matches 再在锁内 accept/finish，两次取锁之间槽可能被
   回收并被下一个请求复用，核对就白做了（审计 round-5：完成槽检查与写入不原子）。
   token 为 NULL 表示调用方没有身份可核（设备环的收尾路径，槽号本来就是
   UINT32_MAX 之外的占位），此时只按 used 判断。 */
void domain_completion_accept(locker_core_t *core, uint32_t slot, const locker_uuid_t *token,
                              locker_status_t status, const locker_uuid_t *operation_id, bool has_operation);
void domain_completion_finish(locker_core_t *core, uint32_t slot, const locker_uuid_t *token,
                              locker_status_t status, const char *json, size_t length, uint64_t revision);
locker_status_t domain_completion_wait_accept(locker_core_t *core, uint32_t slot, uint32_t timeout_ms,
                                              locker_status_t *out_status, locker_uuid_t *out_operation,
                                              bool *out_has_operation);
/* 等待终态时**必须**带上 submission_token**：槽号会在保留期结束后被回收、
   再被别的请求复用，只用槽号等待会把别人的结果当成自己的交出去
   （审计 R3-B-14：find 与 wait 之间旧槽被 reap、新请求复用同一槽，
   等待旧槽却拿到 OK / OTHER_USER_B）。token 在锁内逐次核对，槽一换人立刻返回
   NOT_READY，让调用方按契约用幂等键重查。 */
locker_status_t domain_completion_wait_final(locker_core_t *core, uint32_t slot, const locker_uuid_t *token,
                                             uint32_t timeout_ms, locker_query_result_t *out_result);
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
/* 把「墙钟 UTC 起点 + 时长」的期限换算到单调时间域（domain_core.c）。
   起点是库里的 OperationRecord.updated_at（Unix epoch 毫秒，量级 1e12），
   而 Core 全部计时比较都在单调毫秒（量级 1e6）上做。直接把两者相加得到的
   截止点永远大于 now，重启/重载后挂着的申请与待确认操作就再也不会超时
   （审计 R1-B-5、R3-B-13）。换算规则：
   - 墙钟不可用或起点为 0：无法判断是否过期，按「立即到期」处理（保守）；
   - 已经超过期限：剩余按 0，立即到期；
   - 起点在未来（墙钟回拨过）：按完整时长算，不放大成更长的等待。 */
uint64_t domain_deadline_in_monotonic(uint64_t wall_start_ms, uint64_t duration_ms, bool wall_ok,
                                      uint64_t now_wall_ms, uint64_t now_mono_ms);
/* 单文档提交：把 operation 写回仓储并推进 revision。 */
locker_status_t domain_persist_operation(locker_core_t *core, domain_operation_t *operation);
/* 把操作推进到 REVIEW_REQUIRED 并持久化原因（保留占位，x-rules 6）。
   提交失败时内存保持原状态，调用方下次仍可重试。设备证据路径也要用，
   所以不能只留在 domain_commands.c 里。 */
locker_status_t domain_require_review(locker_core_t *core, domain_operation_t *operation,
                                      const char *reason_code);
/* WAIT_CONFIRM 的冻结依据是否已经失效（门/物品/版本/代际任一处变化）。
   设备侧收到新事实时与 confirm 决策点共用同一份判定，避免两处各写一份漂移。 */
bool domain_wait_confirm_basis_conflicted(const locker_core_t *core, const domain_operation_t *operation);
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
/* 第一阶段：把账本条件（ABSENT/EXISTS）写进调用方的事务缓冲。**必须在任何
   put/delete 之前**调用——RepositoryChange 的 checks 段要在 mutations 之前
   一次性写完，晚写会把 checks 混进 mutations 得到非法 JSON。 */
locker_status_t domain_loan_check(locker_core_t *core, const domain_operation_t *operation,
                                  domain_change_t *change);
/* 第二阶段：把账本变更写进同一事务缓冲（借出新建 Loan，归还关闭原记录，
   借用者与 tool 保持原值）。**不提交**——调用方把它和操作终结放进同一个
   change 里提交，否则「操作已 COMPLETED 但账本没动」这种半截状态会在中途
   失败时留下。写出的时间戳经 out_stamp 回传，供提交成功后更新内存副本，
   保证 loans 文档里的 opened_at/closed_at 与 core->loan_opened_at 不分叉。 */
locker_status_t domain_loan_apply(locker_core_t *core, const domain_operation_t *operation,
                                  domain_change_t *change, char *out_stamp, size_t stamp_size);
/* 账本事务提交成功后的内存副本更新（借出建、归还清）。 */
void domain_loan_commit_in_memory(locker_core_t *core, const domain_operation_t *operation,
                                  const char *timestamp_text);

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
/* 收尾「调用方已写出完整 JSON 值」的信封：不补 data 对象的 `}`。
   投影写手（operation/device/tool 对象）自带 `{…}`，必须用这个而不是
   domain_response_end，否则会多一层大括号写成非法 JSON。 */
void domain_response_end_value(locker_json_writer_t *writer, uint64_t revision,
                               const locker_uuid_t *request_id);
void domain_response_end(locker_json_writer_t *writer, uint64_t revision,
                         const locker_uuid_t *request_id);

/* 发布结果 */
void domain_publish(locker_core_t *core, const locker_uuid_t *operation_id, const char *resource,
                    uint64_t revision);

#endif /* LOCKER_DOMAIN_INTERNAL_H */