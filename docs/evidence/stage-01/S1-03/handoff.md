# S1-03 交接文档 — UART 字节端口与独立模拟设备

## 1. 一句话结论

`host/device/` 实现了契约 `locker_device_*` 十四个符号（13 个返回 `locker_status_t`，`destroy` 返回 void）：把 `locker_unlock_intent_t` 等 DTO 编成 UART-v1 字节帧、经单 reader 线程流式解析响应与 `DEVICE_EVENT`、按 `(session, request_no, response_type)` 匹配后回调 sink；`host/simulator/` 提供不连真实锁、不碰业务库的独立设备模拟器（含独立 SQLite 设备日志与 0600 AF_UNIX 私有控制面）。19 个 CTest 在 VMware Ubuntu 干净 guest 上全绿（3551 条 C 断言 + 391 条 daemon 检查），ASan/UBSan 与 TSan 零报告。**真实 UART/HAL/GPIO/littlefs 全部 NOT_RUN**。按用户指令本卡只用 VMware guest 作 Linux 证据，不再有 WSL2 证据。

## 2. 目录与文件

```text
host/device/
  CMakeLists.txt                        locker_device (STATIC)
  README.md                             模块边界/约定/已知边界
  include/locker/device/
    wire.h                             帧常量、frame_t、流式解析器（模块私有）
    codec.h                            wire_result、flag/cap、11 对 union、编解码 API（模块私有）
    transport.h                        AF_UNIX 传输层（模块私有）
  src/
    wire.c                             CRC-16/CCITT-FALSE、组帧、拆帧重同步
    codec.c                            11 类请求/响应 + DEVICE_EVENT 显式字节编解码
    transport.c                        connect/read/write/shutdown
    port.c                             契约十四个 `locker_device_*` 符号
host/simulator/
  CMakeLists.txt                        locker_simulator (STATIC) + locker-device-sim (EXE)
  README.md                             明确「不是固件」
  include/locker/simulator/sim.h       引擎 API（生命周期/帧/控制/观测）
  src/sim.c                            状态机 + 独立 SQLite 设备日志
  src/sim_main.c                       daemon：两个 0600 AF_UNIX socket + 控制面
tests/unit/device/                     test_wire/test_codec/test_sim_journal/test_sim_protocol
tests/contract/device/                 test_golden_frames/test_device_port/daemon_smoke.py
docs/evidence/stage-01/S1-03/          本目录证据
```

`host/device/include/locker/device/*.h` 与 `host/simulator/.../sim.h` 是**模块私有头**：`host/device` 的 wire/codec/transport 头不得被其他模块包含；对外的唯一接口是 `contracts/include/locker_contracts.h` 里的 `locker_device_*`。

## 3. 接口调用

### 3.1 契约原型（`contracts/include/locker_contracts.h`，逐字，勿改）

```c
typedef struct locker_device locker_device_t;                       /* :186 */
locker_status_t locker_device_create(locker_device_t **out);        /* :289 */
locker_status_t locker_device_bind_sink(locker_device_t *device,
        const locker_device_sink_t *sink);                          /* :311 */
locker_status_t locker_device_open(locker_device_t *device,
        const locker_device_config_t *config);                      /* :217 */
locker_status_t locker_device_get_snapshot(locker_device_t *device,
        uint32_t request_no);                                       /* :219 */
locker_status_t locker_device_execute_once(locker_device_t *device,
        const locker_unlock_intent_t *intent);                      /* :221 */
locker_status_t locker_device_query_command(locker_device_t *device,
        const locker_uuid_t *command_id);                           /* :223 */
locker_status_t locker_device_fetch_events(locker_device_t *device,
        const locker_event_key_t *after, uint8_t limit);            /* :225 */
locker_status_t locker_device_ack_persisted(locker_device_t *device,
        const locker_event_key_t *cursor);                          /* :227 */
locker_status_t locker_device_resume_ready(locker_device_t *device,
        const locker_ready_guard_t *guard);                         /* :229 */
locker_status_t locker_device_release_context(locker_device_t *device,
        const locker_context_guard_t *guard);                       /* :231 */
locker_status_t locker_device_send_health(locker_device_t *device,
        const locker_health_report_t *report);                      /* :233 */
locker_status_t locker_device_time_anchor(locker_device_t *device,
        uint32_t request_no);                                       /* :295 */
locker_status_t locker_device_close(locker_device_t *device);       /* :291 */
void locker_device_destroy(locker_device_t *device);                /* :293 */
```

返回值语义：这些端口返回的是**「请求是否已发出」**，不是设备的业务结论。设备结论（例如 `command_state`、`result`）通过 sink 的 `reply` 回调带回，`result` 由 `locker_device_reply_t.body.<消息>.result` 携带（wire_result 0..14）。因此 `locker_device_execute_once` 返回 `LOCKER_STATUS_OK` 只表示 EXECUTE 帧已上线，**不代表开锁已执行**。

### 3.2 关键 DTO（逐字）

```c
locker_device_config_t {                     /* :141 */
  bool simulated; char path[108]; char expected_device_id[25];
  uint32_t baud_rate; locker_uuid_t host_instance_id;
  locker_clock_port_t clock;
};
locker_device_sink_t {                       /* :182 */
  void *user;
  locker_status_t (*reply)(void *user, const locker_device_reply_t *reply);
  locker_status_t (*event)(void *user, const locker_device_event_t *event);
};
locker_device_reply_t {                      /* :181 */
  uint32_t request_no; uint8_t message_type;
  uint64_t request_started_host_mono_ms, received_host_mono_ms;
  union { /* 11 个响应 DTO，成员名同消息名 */ } body;
};
locker_unlock_intent_t {                     /* :111 */
  locker_uuid_t operation_id, command_id, session_id, journal_epoch;
  uint64_t expected_boot_id, expected_physical_revision, action_no,
           not_after_mcu_ms;
  uint16_t slot_id; locker_operation_kind_t kind;
};
locker_ready_guard_t {                       /* :112 */
  locker_uuid_t session_id, journal_epoch;
  uint64_t expected_boot_id, expected_physical_revision;
  locker_event_key_t persisted_through;
};
locker_context_guard_t {                     /* :113 */
  locker_uuid_t session_id, journal_epoch, operation_id, command_id;
  uint64_t expected_boot_id, expected_physical_revision;
};
locker_event_key_t {                         /* :57 */
  char device_id[25]; locker_uuid_t journal_epoch;
  uint64_t boot_id, event_seq;
};
locker_device_event_t {                      /* :119 */
  locker_event_key_t key;
  uint64_t mono_ms, physical_revision, detail;
  uint16_t event_type, reason, flags;
  locker_uuid_t operation_id, command_id;
  locker_door_state_t door; locker_item_state_t item;
  uint32_t supply_mv; uint8_t card_uid_len, card_uid[10];
  bool live_transport;
};
```

### 3.3 打开 / 关闭

```c
locker_device_t *device = NULL;
locker_device_config_t config = {0};
config.simulated = true;                       /* stage 1 必须 true */
snprintf(config.path, sizeof config.path, "%s", "/run/locker/uart.sock");
snprintf(config.expected_device_id, sizeof config.expected_device_id, "%s", "01000000000000000000000c");
/* 24 位小写 hex：由设备 12 字节 UID 逐字节展开而来；设备身份不符时 HELLO 结果改判
   BAD_PRECONDITION（port.c:189-201 device_identity_matches）。 */
config.baud_rate = 115200u;
config.host_instance_id = host_uuid;
config.clock = locker_clock_port_real();       /* 业务路径用真实时钟 */

locker_device_create(&device);                 /* 失败置 NULL */
locker_device_bind_sink(device, &sink);        /* 必须在 open 之前 */
locker_device_open(device, &config);           /* 成功时 HELLO 已发出 */
...
locker_device_close(device);                   /* 返回 locker_status_t；停 reader、失效 session */
locker_device_destroy(device);                 /* void；内部先 close，再 destroy mutex + free */
```

`open` 成功后 HELLO 请求已写入 transport，但**响应尚未到达**；`hello_complete` 由 reader 线程在收到 HELLO 响应时置位。因此紧跟 `open` 调用 `get_snapshot` 会得到 `LOCKER_STATUS_NOT_READY`——这是设计行为，调用方应等待第一个 reply 回调后再发后续请求（测试里用 `port_env_open_ready` 等待 HELLO reply）。

### 3.4 执行一次开锁（不自动重发）

```c
locker_unlock_intent_t intent = {0};
intent.operation_id = op; intent.command_id = cmd;
intent.session_id = current_session; intent.journal_epoch = current_epoch;
intent.expected_boot_id = boot; intent.expected_physical_revision = revision;
intent.action_no = watermark + 1u;
intent.not_after_mcu_ms = mcu_now_ms + 8000u;
intent.slot_id = 1u;
intent.kind = LOCKER_OPERATION_KIND_BORROW;
locker_device_execute_once(device, &intent);   /* 只发一次，禁止调用方循环重发 */
```

结果未知时调用方必须走 `locker_device_query_command(device, &command_id)`，**不能**重发 EXECUTE（`uart-v1.md:388`：QUERY 查不到回 `UNKNOWN_RESULT`，不能解释成未执行）。本卡代码里没有任何 EXECUTE 重发循环。

### 3.5 拉取事件 / 确认持久化 / 就绪

```c
locker_event_key_t after = {0};                /* (0,0) 表示从最早开始 */
locker_device_fetch_events(device, &after, 4u);  /* limit 1..4 */
/* reply.body.fetch_events 里有 events[4]、next_after、has_more、gap_before_page */

locker_device_ack_persisted(device, &through);   /* through 不倒退、不越 last_committed */

locker_ready_guard_t guard = {0};
guard.session_id = session; guard.journal_epoch = epoch;
guard.expected_boot_id = boot; guard.expected_physical_revision = revision;
guard.persisted_through = through;
locker_device_resume_ready(device, &guard);      /* CAS 不符回 PHYSICAL_CHANGED */
```

重连顺序固定（`uart-v1.md:408`）：HELLO → GET_SNAPSHOT → FETCH 至屏障 → RK 落库 → ACK_PERSISTED → 核对/释放上下文 → RESUME_READY。

### 3.6 模拟器控制入口（不是产品 API）

`locker-device-sim --uart PATH --control PATH [--journal PATH] [--device-uid HEX12]`，启动后 stdout 打印 `ready device backend: simulator uart=... control=... journal=...`（`sim_main.c:736`）。控制面一行一命令，回复 `ok` 或 `error <detail>`：

| 命令 | 作用 |
|---|---|
| `status` | 16 个 key=value（session/journal_epoch/boot_id/action_watermark/dropped_events/device_mode/journal_state/door/item/command_state/has_active/continuity_ok/first_available/last_committed/ack_persisted/event_count） |
| `door unknown\|closed\|open` | 改变门状态，physical_revision+1；`closed`/`open` 发 DOOR_CLOSED/DOOR_OPENED，`unknown` 发 HEALTH_CHANGED（`sim.c:2352-2356`） |
| `item unknown\|absent\|present` | 改变物品状态，physical_revision+1；`present`/`absent` 发 ITEM_PRESENT/ITEM_REMOVED，`unknown` 发 HEALTH_CHANGED（`sim.c:2389-2393`） |
| `card HEX` | 注入读卡，发 CARD_OBSERVED。文本入口只接受 4/7/10 字节（空串在 `parse_hex_bytes` 就返回 false → `error bad_argument`，`sim_main.c:397`/`:520`）；0 长度（无卡）只能通过 C 引擎 API `locker_sim_control_card`（`sim.c:2402`）调用 |
| `reject_next on\|off` | 下一次 EXECUTE 一次性拒绝 |
| `drop_ack on\|off` | ACK_PERSISTED 被应用但不回帧 |
| `suppress_pulse on\|off` | 跳过 PULSE_STARTED 段（只影响后续动作） |
| `fail_journal on\|off` | 让下一次日志提交 ROLLBACK（拒绝新动作） |
| `inject_gap N` | 注入事件缺口，dropped_events+=N 并锁存 continuity_ok=false |
| `drop_link` | 关闭已接受的 UART 连接（模拟拔线）：主机侧 reader 立即 EOF，设备日志/boot_id/会话都不变，重连即重新 HELLO |
| `restart` | 进程内 close+open（boot_id+1），**不是 MCU 掉电** |
| `cursor B:S` | 回显游标文本（用于脚本构造） |
| `quit` | 退出，exit 0 |

## 4. 参数 / 内存 / 线程 / 销毁

**内存所有权**：所有 DTO 都由调用方持有；`bind_sink` 把 `locker_device_sink_t` 整个结构体**按值复制**进 device（`port.c:574-585`），因此调用方在 bind 之后可以释放自己的 sink 结构体，但被复制的 `sink.user` 指针与两个回调函数必须在 `close` 前保持有效，回调里借用的 `locker_device_reply_t`/`locker_device_event_t` 指针**只在本次回调内有效**，不得保存。编解码器不分配内存，payload 拷进 `locker_wire_frame_t.payload[512]` 栈/结构内数组。

**线程**：`locker_device_open` 起一个 reader 线程（`pthread_create`），它独占 `transport_read`（200 ms `SO_RCVTIMEO`）；所有写方各持 `write_lock` 串行写；状态镜像与 pending 表由 `lock` 保护。sink 回调在 **reader 线程**上调用，因此回调里不能再调用会阻塞等待 reply 的端口（会自锁）。

**pending 表**：`LOCKER_DEVICE_PENDING_MAX = 16`，`pending_add` 满时返回 `LOCKER_STATUS_BUSY`。请求号由 `pending_next_request_no`（`port.c:176-183`）自增：唯一硬边界是 32 位回绕——`next_request_no == UINT32_MAX` 时置 `request_numbers_exhausted` 并返回 0，正常发送路径随即在 `port.c:503-507` 返回 `LOCKER_STATUS_NOT_READY`（不是 `BUSY`；`BUSY` 只来自 pending 表满或发号在途冲突），此后必须 `close`/`open` 建新会话；在途号冲突时由 `pending_add` 在同锁内拒绝，不靠重试次数。超时回收用 `device_now_ms()`（注入时钟），`LOCKER_DEVICE_PENDING_TIMEOUT_MS = 5000` 到点即回收并抬高 `expired_request_floor`，旧号永不复用。`get_snapshot`/`time_anchor` 使用调用方给的 `request_no`（契约签名如此），其余端口用内部自增号。

**销毁顺序**：`locker_device_close` → `opened=false` → `reader_stop=true` → `transport_shutdown`（`shutdown(fd, SHUT_RDWR)`，让阻塞 `recv` 立即返回）→ `pthread_join` → `transport_close` → `stream_reset`；`locker_device_destroy` = `close` + 两个 mutex `destroy` + `free`。**`destroy` 成功后指针失效**（`free` 已发生），调用者必须自行置 NULL，不得再调用任何端口——契约 `locker_contracts.h:292` 只允许「close 并 join 之后 free」，端口不提供已释放指针的探测（重复 `close` 才是幂等的）。

**模拟器销毁**：`locker_sim_destroy`（`sim.c:2010`）内部先调用 `locker_sim_close`（`sim.c:2192`，`sqlite3_close` + 清会话态）再 `free`，因此调用方不必重复 close；`locker_sim_close` 是幂等的（`db == NULL` 直接跳过）。daemon 退出时先关两个 client fd、`close` 两个 listen fd、`unlink` 两个 socket 路径，再 `locker_sim_destroy`。

## 5. 配置 / 依赖 / 接口版本

| 项 | 值 |
|---|---|
| 契约版本 | 0.2.1（`contracts/contract-source.json` 与 `contracts/include/locker_contracts.h` 一致，根 `CMakeLists.txt:41-61` 配置期门禁） |
| UART 帧版本 | `LOCKER_WIRE_VERSION = 1` |
| 波特率 | `config.baud_rate` 只是记录值；AF_UNIX 传输不使用它（真实 UART 未实现） |
| 传输 | AF_UNIX `SOCK_STREAM`；`sun_path` 上限 108 字节（含终止符） |
| 连接超时 | `LOCKER_DEVICE_CONNECT_TIMEOUT_MS = 5000`，每 10 ms 重试；每次 `connect` 都是非阻塞 + `poll` 等到同一个单调截止，accept 队列满也不会卡在阻塞 `connect`（`transport.c:66-140`） |
| 读超时 | `LOCKER_DEVICE_READ_TIMEOUT_MS = 200` |
| 帧上限 | `LOCKER_WIRE_MAX_PAYLOAD = 512`、`LOCKER_WIRE_MAX_FRAME = 524` |
| 事件批量 | FETCH `limit` 1..4，最大响应 469 字节 |
| 脉冲 | `SIM_PULSE_MS = 150`（只写 detail，无 GPIO） |
| 健康窗口 | `SIM_HEALTH_TIMEOUT_MS = 5000` |
| 执行窗口 | `SIM_NOT_AFTER_HORIZON_MS = 15000`、`SIM_PREPARE_DEADLINE_MS = 2000` |
| 事件通知队列 | `SIM_DEVICE_EVENT_QUEUE = 32`（满只丢**实时通知**，事件已 COMMIT，日志不丢；不锁存 continuity，靠 FETCH 补齐，`sim.c:843-861`） |
| 固件版本（模拟） | 0.1.0；capabilities 0x1f |
| 依赖 | 系统 libc/pthread + S1-02 vendored SQLite 3.54.1（Public Domain）；**无新增第三方** |
| 编译 | C11，`EXTENSIONS OFF`，`-Wall -Wextra -Wpedantic -Werror`；`transport.c`/`sim_main.c`/测试顶部显式 `#define _POSIX_C_SOURCE 200809L` |

## 6. 失败路径（逐条）

### 6.1 `locker_device_*` 端口返回码

| 端口 | 返回 `LOCKER_STATUS_*` 的条件 |
|---|---|
| 全部 | `device == NULL` 或 `!created` → `INVALID` |
| `bind_sink` | `sink == NULL` → `INVALID`；已 `open` → `CONFLICT` |
| `open` | 已 open → `CONFLICT`；未 bind sink → `NOT_READY`；clock 无效 → `INVALID`；`path`/`expected_device_id` 为空 → `INVALID`；`simulated == false` → `NOT_READY`；connect 失败 → `IO_ERROR`；`new_uuid` 失败/nil → `IO_ERROR`；`pthread_create` 失败 → `IO_ERROR`；HELLO 组帧/发号失败 → `BUSY`/`IO_ERROR`（并自动 `close`） |
| `get_snapshot`/`execute_once`/`query_command`/`fetch_events`/`ack_persisted`/`resume_ready`/`release_context`/`send_health` | `!opened` 或 `!hello_complete` → `NOT_READY`；参数 NULL → `INVALID` |
| `execute_once` | `slot_id != 1` → `INVALID`；`kind` 不是 BORROW/RETURN → `INVALID` |
| `fetch_events` | `after == NULL` → `INVALID`；`limit == 0` 或 `> 4` → `INVALID` |
| `time_anchor` | `request_no == 0` → `INVALID` |
| 发送路径 | 组帧失败 → `INVALID`；pending 表满 → `BUSY`；写失败 → `IO_ERROR`（并回滚 pending 项） |

注意：`locker_device_close` 返回 `locker_status_t`（`INVALID` = 句柄为 NULL 或从未创建成功；`BUSY` = 在 reader 回调线程内自关闭），**`destroy` 之后指针已释放，任何端口（含 `close`）都不保证返回 `INVALID`**，`locker_device_destroy` 返回 `void` 且 close 失败时不会释放（避免回调线程释放自己正在执行的上下文）。

### 6.2 设备侧 `wire_result`（通过 `reply.body.<msg>.result` 返回，解码器总是返回 `LOCKER_STATUS_OK` 使失败原因可见）

| 码 | 名称 | 触发点（模拟器） |
|---|---|---|
| 0 | OK | 成功 |
| 1 | BAD_VERSION | `sim.c:1918` 帧 version≠1；`sim.c:1114` HELLO `requested_version`≠1 |
| 2 | BAD_SESSION | `sim.c:1936` 非 HELLO 且 session 不匹配；`resume_ready` boot 不符 |
| 3 | NOT_READY | `device_mode == DEGRADED`；`!continuity_ok`；健康心跳超时 |
| 4 | BAD_PRECONDITION | 门未关、物品状态不符、`not_after` 过期/超窗、脉冲中 `release`、`reject_next` |
| 5 | REPLAY_BLOCKED | `action_no <= action_watermark` |
| 6 | ACTION_GAP | `action_no > watermark + 1` |
| 7 | BUSY | 已有活动上下文时再次 `execute` |
| 8 | LOG_UNAVAILABLE | 日志提交失败 / `journal_state == FAULT` / 读页失败 |
| 9 | UNKNOWN_RESULT | `query_command` 查不到该 `command_id` |
| 10 | BAD_PAYLOAD | 字段非法（长度、枚举越界、reserved 非 0、半零游标、`action_no == 0`、`max_count` 越界…） |
| 11 | CURSOR_GAP | ACK/RESUME 的 `through` 超前 `last_committed` 或超前本地 ACK |
| 12 | PHYSICAL_CHANGED | `expected_physical_revision` 不符 |
| 13 | JOURNAL_CHANGED | `journal_epoch` 不符 |
| 14 | UNSUPPORTED | 未知消息类型 |

`result != 0` 时响应帧严格只有 18 字节前缀（`session_id` + `result`），**不带 body**（`uart-v1.md:28`）。

### 6.3 解析器失败路径（不抛异常、不越界）

| 情况 | 行为 |
|---|---|
| magic 不符 | 丢弃 1 字节，`resyncs++`，重新找 magic |
| `payload_len > 512` | 丢弃 1 字节，`length_errors++`、`resyncs++` |
| 帧不完整 | 保留在缓冲区等待更多字节（不分配、不按声明长度预分配） |
| CRC 不符 | 丢弃 1 字节，`crc_errors++`、`resyncs++` |
| handler 返回非 OK | `handler_errors++` 并中止本次 `feed` |
| 缓冲区（524 B）满且无法成帧 | 丢弃 1 字节重同步，不会溢出 |

## 7. 复现命令

### 7.1 VMware 干净 guest（验收主环境）

```bash
# 宿主侧（Windows PowerShell 或 Git Bash）。本卡只用标准工具，不依赖任何临时启动脚本：
#   1) 先重算清单（清单本身在快照里，必须早于打包，否则快照内的清单与源码不一致）
#   2) 打包源码快照（只打源码，排除 build/*.o/*.a/*.so/*.pyc/__pycache__）
#   3) 上传快照与验收脚本到 guest 的 /tmp
bash docs/evidence/stage-01/S1-03/evidence-make-manifest.sh "%REPO%"
tar.exe -czf C:/Users/Public/s103_snapshot.tar.gz --exclude=.git --exclude=build \
        --exclude='build-*' --exclude='*.o' --exclude='*.a' --exclude='*.so' \
        --exclude='*.pyc' --exclude=__pycache__ -C %REPO% .
scp C:/Users/Public/s103_snapshot.tar.gz locker-vm:/tmp/s103_snapshot.tar.gz
scp %REPO%/docs/evidence/stage-01/S1-03/evidence-run-vm.sh locker-vm:/tmp/evidence-run-vm.sh

# guest 侧（VMware Ubuntu 26.04.1 LTS，账号 china）
ssh locker-vm 'bash /tmp/evidence-run-vm.sh'
```

VM 侧脚本会：

1. 守卫：`$HOME/locker-workspace` 必须存在；本次批次目录 `$HOME/locker-workspace/S1-03/batches/<run_id>` 必须**尚不存在**（否则 `SRC_GUARD_FAILED` + exit 2）。脚本里没有任何 `rm -rf`，**活跃源码树 `$HOME/locker-workspace/S1-03/src` 既不被删除也不被当作解包目标**。
2. 解包到批次目录 `batches/<run_id>/src`，`extract.log` 记录 `batch=`、`extract_target=`、`live_tree=`、`live_tree_files_before=`、`live_tree_fingerprint_before=`、`live_tree_fingerprint_after=`、`live_tree_unchanged=yes|no`。
3. 按 `source-manifest.txt` 校验 25 条 sha256 + 字节数（失败 exit 4），再扫描源码树外产物（`SRC_STRAY_ARTIFACTS != 0` 也 exit 4）。
4. 三套构建（普通构建 / ASan+UBSan 构建 / TSan 构建；其中**两套**是消毒器构建）→ `ctest` → 逐测试断言统计 → daemon smoke → 四项设计检查。
5. 逐项 `need_zero` 门禁（configure/build/ctest、普通构建与两套消毒器构建的退出码与报告数、daemon 检查数与失败数、清单与杂散产物、活跃源码树指纹未变、四项设计检查）。逐项 `GATE_OK`/`GATE_FAILED` 只写 `gates.log`；`run-summary.log` 用 `cat "$GATES"` 内联同一份门禁结果并追加 `VM_RUN_VERDICT=PASS|FAIL`（FAIL 时 exit 5）——**verdict 随证据 tgz 一起归档，不只在终端**。
6. 打包 `$HOME/locker-workspace/artifacts/S1-03/s103_vm_evidence_<run_id>.tgz`。

**注意快照内文档版本**：快照在文档最终定稿前打，因此 `batches/<run_id>/src/docs/evidence/.../summary.md` 是上一版；`run_id`、快照 sha256、耗时等数字是跑完后回填的。可核性由 25 个受管源码文件 + `source-manifest.txt`（快照内 `SHACHECK_OK=25`、`SHACHECK_FAILED=0`）保证，文档不在清单内。

本轮实测（round-9 非阻塞整改后的 canonical run）：run_id=`20261026-095548`、`VM_RUN_VERDICT=PASS`，快照 sha256=`07d7ab6e83b034512df21ca4cff4af70b111251a57dce6c832f8757ca2987f38`（`84512317` B），批次目录 `$HOME/locker-workspace/S1-03/batches/20261026-095548/src`。活跃树 `$HOME/locker-workspace/S1-03/src` 在运行前后各取一次指纹（`live_tree_fingerprint_before=`/`_after=` 均为 `9638dec8117214532e316760a50c6291378bb56445b46ec9402bb9c29ace9aeb`，`live_tree_unchanged=yes`，`live_tree_files_before=`/`_after=` 均为 `1159`），**前后指纹相同才写 `GATE_OK live-tree-unchanged`**，单次计数不再作为证据。完整数字见 `summary.md` §5.1。

### 7.2 手工最小复现

```bash
cmake -S . -B /tmp/locker-build -DCMAKE_BUILD_TYPE=Debug \
      -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
cmake --build /tmp/locker-build -j4
ctest --test-dir /tmp/locker-build --output-on-failure

# 只跑本卡：
ctest --test-dir /tmp/locker-build -R "device\.|contract\.device\." --output-on-failure
```

### 7.3 daemon 手工冒烟

```bash
/tmp/locker-build/host/simulator/locker-device-sim \
  --uart /tmp/sim.sock --control /tmp/sim.ctl --journal /tmp/sim.db &
printf "status\n" | socat - UNIX-CONNECT:/tmp/sim.ctl
printf "door open\n" | socat - UNIX-CONNECT:/tmp/sim.ctl
printf "inject_gap 3\n" | socat - UNIX-CONNECT:/tmp/sim.ctl
printf "restart\n" | socat - UNIX-CONNECT:/tmp/sim.ctl
printf "quit\n" | socat - UNIX-CONNECT:/tmp/sim.ctl
```

### 7.4 mutation

```bash
python3 -B docs/evidence/stage-01/S1-03/mutation-batch.py --check   # 只校验 56 个点位 old 唯一
python3 -B docs/evidence/stage-01/S1-03/mutation-batch.py           # 全跑，写 mutation.log
```

### 7.5 四项设计检查（只证明设计一致性）

```bash
python3 -B scripts/check_contracts.py
python3 -B scripts/check_stage_plan.py
python3 -B scripts/check_vision_contracts.py
python3 -B scripts/check_feasibility.py
```

## 8. 再生成步骤

```bash
# 1) 清单与快照（两个 helper 都接收仓库根作为参数，不写死任何挂载路径）
bash docs/evidence/stage-01/S1-03/evidence-make-manifest.sh "<repo-root>"
bash docs/evidence/stage-01/S1-03/evidence-make-snapshot.sh "<repo-root>" "<snapshot.tar.gz>"

# 2) 在 VMware guest 上重跑唯一一类 Linux 证据（脚本自己建批次目录，不删活跃源）
scp "<snapshot.tar.gz>" locker-vm:/tmp/s103_snapshot.tar.gz
scp docs/evidence/stage-01/S1-03/evidence-run-vm.sh locker-vm:/tmp/evidence-run-vm.sh
ssh locker-vm 'bash /tmp/evidence-run-vm.sh'

# 3) 重跑 mutation（同样从批次目录解包，不动活跃源）
python -B docs/evidence/stage-01/S1-03/mutation-batch.py --src "<repo-root>"
```

清单范围固定为：根 `CMakeLists.txt` + `host/device/` + `host/simulator/` + `tests/unit/device/` + `tests/contract/device/`（25 个文件）。新增文件必须同步进 `evidence-make-manifest.sh` 的 `find` 范围，否则 VM 端清单校验会少条目。

两个 helper 与 `evidence-run-vm.sh` 一样不写死任何挂载路径：仓库根与输出路径都由参数给出，因此命令不依赖特定机器的目录布局。`evidence-run-vm.sh` 只跑在 VMware guest 上，它创建 `$HOME/locker-workspace/S1-03/batches/<run_id>/` 并拒绝复用已存在批次。

## 9. 证据索引（`docs/evidence/stage-01/S1-03/`）

| 文件 | 内容 |
|---|---|
| `summary.md` | 本卡实施报告（9 章） |
| `handoff.md` | 本文档 |
| `audit.md` | round-1 独立审计（FAIL，B-1～B-16 / N-1～N-9），历史记录不改 |
| `audit-round2.md` | round-2 独立审计（FAIL，R2-B-1～R2-B-6 / R2-N-1～R2-N-4），整改对照见 §12.4 |
| `audit-round3.md` | round-3 独立审计（FAIL，R3-B-1～R3-B-3 / R3-N-1～R3-N-4），整改对照见 §12.5 |
| `audit-round4.md` | round-4 独立审计（FAIL，R4-B-1 / R4-N-1～R4-N-4），整改对照见 §12.6 |
| `audit-round5.md` | round-5 独立审计（FAIL，R5-B-1 / R5-N-1～R5-N-3），整改对照见 §12.7 |
| `audit-round6.md` | round-6 独立审计（FAIL，R6-B-1 / R6-N-1～R6-N-3），整改对照见 §12.8 |
| `audit-round6-prompt.txt` / `audit-round6-runner.ps1` / `audit-round6-run.log` / `audit-round6-codex-output.txt` | round-6 审计的输入、启动器、运行记录与原始转录（审计范围、25 文件指纹、整改声明原文），随卡归档以证明审计不是黑箱 |
| `audit-round7.md` | round-7 独立审计（FAIL，R7-B-1 / R7-B-2 阻塞 + R7-N-1 / R7-N-2 非阻塞），整改对照见 §12.9 |
| `audit-round7-prompt.txt` / `audit-round7-runner.ps1` / `audit-round7-run.log` / `audit-round7-codex-output.txt` | round-7 审计的输入、启动器、运行记录与原始转录 |
| `audit-round8.md` | round-8 独立审计（FAIL，R8-B-1 阻塞 + R8-N-1 / R8-N-2 非阻塞），整改对照见 §12.10 |
| `audit-round8-prompt.txt` / `audit-round8-runner.ps1` / `audit-round8-run.log` / `audit-round8-codex-output.txt` | round-8 审计的输入、启动器、运行记录与原始转录 |
| `audit-round9-prompt.txt` / `audit-round9-runner.ps1` | round-9 审计的输入与启动器（同一份 25 文件指纹与整改声明，可独立复现本轮审计） |
| `source-manifest.txt` | 25 个受管文件 sha256 + 字节数；清单自身 sha256 与字节数见 summary §1 |
| `mutation.log` | **56 点位**变异结果（VM 活跃树全量跑，`CAUGHT 51 / 56`、`CAUGHT_TIMEOUT 2`（m49、m51）、`CAUGHT_TOTAL 53 / 56`、`NOT_CAUGHT 3`、`BUILD_FAILED 0`，逐点位见 summary §5.4；日志 208276 B / sha256 `d4fa3c31…3376` / 1649 LF）；每点位未过滤的完整 CTest stdout 归档在同目录 `mutation-raw-ctest.tgz`（**23286 B**，sha256=`e1d4986a4d5bc6a08e0537d4fbae8c38d6805d80204639b21cfe2c209ecfa8ca`，**57 个条目** = `raw/` + 56 个点位日志），R6-N-2 整改 |
| `mutation-batch.py` | 变异脚本（`--check` 只校验锚点唯一性；full 模式逐点位构建+CTest，build 失败记为 BUILD_FAILED 不算 CAUGHT） |
| `evidence-make-manifest.sh` / `evidence-make-snapshot.sh` / `evidence-run-vm.sh` | 证据生成与验收脚本 |
| `check_contracts.json` / `check_stage_plan.json` / `check_vision_contracts.json` / `check_feasibility.json` | 四项设计检查的结构化输出 |
| `vm-s103-configure.log` / `-build.log` / `-ctest.log` / `-unit-tests.log` / `-daemon-smoke.log` | VMware canonical run 的构建与测试 |
| `vm-s103-asan-{configure,build,ctest}.log` / `vm-s103-tsan-{configure,build,ctest}.log` | VMware 消毒器 |
| `vm-s103-source-verify.log` / `vm-s103-manifest.sha256` / `vm-s103-environment.log` / `vm-s103-run-summary.log` / `vm-s103-extract.log` / `vm-s103-design-checks.log` / `vm-s103-gates.log` | VMware 快照校验、环境、汇总、设计检查与 17 项门禁（17 项 `GATE_OK` 只写在 `vm-s103-gates.log`；`gates_log=` 引用、`GATE_OK` 全文与末行 `VM_RUN_VERDICT=` 追加在 `vm-s103-run-summary.log`；`SSH_EXIT` 只存在于启动器终端输出，不写进任何 `vm-s103-*.log`） |

**Linux 证据只有一类**：VMware 干净 guest（验收主环境）。按用户指令本卡不再使用 WSL2 证据；daemon smoke 是同一次 VM 运行里的进程级用例，不替代 C 测试，也不被 C 测试替代。

## 10. 未实现项 / 未验证项

| 项 | 状态 | 说明 |
|---|---|---|
| 真实 UART（115200 8N1、DMA、字节间隔、线路噪声、断线） | **NOT_RUN** | stage 1 只做 AF_UNIX 字节流；`baud_rate` 仅记录不生效 |
| STM32 HAL / 交叉编译 / Keil | **NOT_RUN** | 本卡是主机侧；`firmware/` 未改 |
| GPIO 限时脉冲硬截止 | **NOT_RUN** | `sim_finish_pulse`（`sim.c:1571`）是模拟器的确定性定时转换（`SIM_PULSE_MS` 150 ms），不是硬件截止；真实硬截止仍需阶段 2 的定时器证据 |
| MCU / littlefs 掉电可靠性 | **NOT_RUN** | `restart`（`sim.c:2208`）是进程内 close+open；SQLite ≠ littlefs |
| CRC 抗篡改 / 身份认证 | **NOT_RUN** | `uart-v1.md:21` 明确 CRC 不承担认证；本卡未加加密 |
| 事件通知队列溢出后的恢复 | **按设计不锁存** | `sim_queue_push`（`sim.c:851`）队列满（32）只丢实时通知，事件仍已提交到日志；`continuity_ok` 不受影响，不需要恢复动作 |
| `c_declaration_syntax`（pycparser） | **NOT_RUN** | 两项检查器都把 `c_declaration_syntax` 标为 `NOT_RUN (optional pycparser unavailable)`；不把 parse-only 当编译链接 |
| 硬件 profile | **UNVERIFIED** | 沿用契约状态 |

## 11. 临时残留

本轮在 `C:/Users/Public/` 产生的临时脚本与产物（`s103_*.sh`、`s103_*.out`、`s103_iter.ps1`、`s103_canon.ps1`、`s103_mut_vm.sh`、`s103_snapshot.tar.gz`、`s103_vm_ev*.tgz`）在交付前删除；WSL2 专用证据脚本 `evidence-run-wsl.sh` 与 16 个 `wsl-*.log`、`wsl-run-id.txt` 已按用户指令删除。

正式保留：`docs/evidence/stage-01/S1-03/` 下的 3 个证据脚本（`evidence-make-manifest.sh`、`evidence-make-snapshot.sh`、`evidence-run-vm.sh`）、`mutation-batch.py`、`source-manifest.txt`、`mutation.log`、4 个设计检查 JSON、17 个 `vm-s103-*.log`（含 `vm-s103-gates.log`）+ `vm-s103-manifest.sha256`（共 18 个 `vm-s103-*` 文件）、`summary.md`、`handoff.md`、`audit.md`、`audit-round2.md`、`audit-round3.md`、`audit-round4.md`、`audit-round5.md`、`audit-round6.md`、`audit-round6-prompt.txt`、`audit-round6-runner.ps1`、`audit-round6-run.log`、`audit-round6-codex-output.txt`、`audit-round7-prompt.txt`、`audit-round7-runner.ps1`、`audit-round7-run.log`、`audit-round7-codex-output.txt`、`audit-round7.md`、`audit-round8-prompt.txt`、`audit-round8-runner.ps1`、`audit-round8-run.log`、`audit-round8-codex-output.txt`、`audit-round8.md`、`audit-round9-prompt.txt`、`audit-round9-runner.ps1`、`mutation-raw-ctest.tgz`，共 53 个文件（round-9 审计的 run.log/codex-output/report 三件由审计方写入后为 56 个）。`tests/contract/device/daemon_smoke.py` 是正式测试，不是临时脚本。

## 12. 审计整改对照（round-1 FAIL → 本轮整改）

`audit.md`（round-1）结论 **FAIL**，给出 16 项阻塞（B-1～B-16）与 9 项非阻塞（N-1～N-9）。下表逐条对照审计指出的问题、本轮整改落点（当前源码真实行号）与回归证据。审计机当时声明 C 端 configure/build/CTest/sanitizer/mutation 全部 NOT_RUN（它没有 WSL/SSH 能力），因此下表的「回归证据」列全部指向本轮在 VMware guest 上真实跑出来的结果（各轮整改后分别重跑，最新为 round-9 前开发方自查后的 run_id=20261026-054252，见 §9 证据索引）。

### 12.1 阻塞项 B-1～B-16

| 编号 | 审计指出的问题 | 整改落点 | 回归证据 |
|---|---|---|---|
| B-1 | 响应未按 session 匹配：旧 session 的合法成功响应可消费新会话 pending 并改写镜像 | `port.c:127-142` `pending_take` 四条件（used + request_no + request_type + session_id）；`port.c:408-409` 以被解码响应的 `hello.session_id` 参与匹配 | `test_device_port.c:1366` `test_response_session_and_error_mirror`；`daemon_smoke.py:561-568` 旧 session 得 `BAD_SESSION` |
| B-2 | 协议错误响应被当成功应用：18 B 的 BUSY 响应仍清空 epoch/boot/revision/watermark | `port.c:417-419` 只有 `matched && result == 0` 才 `apply_response`；失败响应只投递不改镜像 | 同 `test_device_port.c:1366` 后半段（注入 BUSY 18 B 后镜像不变）；`daemon_smoke.py:245-254` 零 ACK 快照不得给 READY |
| B-3 | 发送适配器把旧意图/守卫静默换到当前 session | `port.c:711-722` `execute_once` 校验 intent 的 session/epoch/boot/revision 四元组，不符返回 `CONFLICT` 且不发字节；`port.c:761-774` `device_cursor_matches`（FETCH 的 `after` 归属、ACK 的 `through` 归属）；`port.c:797` / `port.c:824` 两处 CONFLICT | `test_device_port.c:1457` `test_bound_dtos_rejected_without_writes`；`test_device_port.c:1774` `test_old_intent_after_close_open_rejected_without_write` |
| B-4 | HELLO 未核设备身份：任意非空字符串都能通过 | `port.c:185-198` `device_identity_matches`：12 字节 UID 派生 24 位小写 hex 与 `expected_device_id` 比对，且必需能力位必须齐全；`port.c:410-416` 身份不符时清零 body、保留 session、结果改 `BAD_PRECONDITION` | `test_device_port.c:1423` `test_hello_identity_capabilities_and_failure`；`daemon_smoke.py:150` 部分解析 UID 被拒 |
| B-5 | 在途请求号可重复、无回收、回绕直接跳 1，丢 16 个响应后永久 BUSY | `port.c:37` 5000 ms 有界回收；`port.c:112-124` `pending_expire` 回收并把 request_no 抬到 `expired_request_floor`；`port.c:145-173` `pending_add` 拒重复号/拒低于 floor/满 16 拒；`port.c:176-183` `pending_next_request_no` 到 UINT32_MAX 后置 exhausted 并返回 0，要求新会话 | `test_device_port.c:1550` `test_pending_timeout_and_request_wrap`；`test_device_port.c:1841` `test_pending_entry_matches_response_type` |
| B-6 | sink 背压只终止 reader，后续 execute/health 仍写 socket | `port.c:202-215` `device_sink_result` 锁存 `reconcile_required` + `reader_stop` + 清 pending + `transport_shutdown`；后续请求在 `port.c:493-499`/`port.c:524-531` 一律 `NOT_READY`，只有明确 close/open 恢复 | `test_device_port.c:1512` `test_sink_backpressure_stops_writes`（BUSY 与 NO_SPACE 两轮，断言三个 API 都 NOT_READY 且帧计数不增） |
| B-7 | 恢复/健康守卫缺失：不 FETCH/ACK/RESUME、不发 HOST_HEALTH 也能 EXECUTE；now_ms 只取一次 | `sim.c:1378-1417` `sim_handle_resume_ready` 全屏障（epoch/boot/revision/游标结构/ACK 上界/`recovery_boundary`/has_active/pulse/journal/door/item UNKNOWN/health）；`sim.c:1371-1376` `sim_health_fresh`（5000 ms 窗口 + core_progress 非零 + age 未超）；`sim.c:1813-1838` HOST_HEALTH 校验 host_instance 与计数倒退；`sim.c:1729-1771` 驱动前后重采时并重查 session/READY/health/door/revision | `test_sim_protocol.c:1163` `test_recovery_requires_contiguous_fetch_and_fresh_core`；`test_sim_protocol.c:801` `test_health_timeout_rules`；`test_sim_journal.c:732` `test_deadline_rechecked_after_prepare_and_result` |
| B-8 | 模拟脉冲永不结束：`pulse_active` 恒真，RELEASE_CONTEXT 永久 BAD_PRECONDITION | `sim.c:1571-1595` `sim_finish_pulse`：`SIM_PULSE_MS`（150 ms）到点后先关断再转换 `PULSE_FINISHED`，转换失败转 `sim_latch_pulse_uncertainty`；`sim.c:1547-1569` 锁存 UNKNOWN_RESULT | `test_sim_protocol.c:919` `test_pulse_activity_rules`（BORROW/RETURN 重复循环）；`test_sim_journal.c:635` `test_failed_pulse_finish_latches_durable_gap` |
| B-9 | `action_no` 用跨会话全局主键；重复参数比较不完整；返回拼接新关联 | `sim.c:282-289` DDL 改为 `sim_commands_v2 PRIMARY KEY(session_id, action_no)` 并从旧表迁移；`sim.c:1639-1660` existing 分支全参比对（session/operation/command/not_after/boot/revision/kind/slot）后返回**原记录**；`sim.c:1661-1668` `command_id` 仍全局唯一查重 | `test_sim_protocol.c:1231` `test_full_intent_dedup_and_new_session_watermark`；`test_sim_journal.c:349` `test_action_watermark_and_query_survive_restart` |
| B-10 | 重启丢未决上下文；RELEASE 无持久清空检查点；door/item 凭空变 CLOSED/PRESENT | `sim.c:2053-2116` `sim_restore_active` 原子恢复活动意图（无 `active_command` 时回退查 `sim_commands_v2` 最新行；PULSE_STARTED 转 UNKNOWN_RESULT）；`sim.c:2162-2167` 重启后 door/item 置 UNKNOWN；`sim.c:1856-1904` RELEASE 必须事务提交成功才回 OK | `test_sim_journal.c:557` `test_release_checkpoint_survives_process_reopen`；`test_sim_journal.c:812` `test_suppressed_intent_restores_without_replay`；`daemon_smoke.py` 重启后 `door=0`/`item=0` |
| B-11 | `drop_ack` 只覆盖 type=4，未覆盖 EXECUTE 接纳响应丢失 | `sim.c:2435` 独立 `drop_execute_ack` 注入；`sim.c:2285-2289` 在 `handle_frame` 回复阶段隐藏 EXECUTE 回复，与 `suppress_pulse` 完全独立 | `test_sim_protocol.c:1307` `test_execute_ack_loss_is_independent_of_pulse_suppression`；`test_device_port.c:1580`；`daemon_smoke.py:272-297` QUERY 解不确定且 PULSE_STARTED/PULSE_FINISHED 计数相等 |
| B-12 | 通知队列满被混同关键日志丢失（锁存 continuity 且不持久化） | `sim.c:843-861` `sim_queue_push`：无会话只落日志、队列满（32）只丢实时通知，事件已 COMMIT，注释明确它不是连续性故障；`sim.c:945` `sim_continuity_flags` 不复位水位 | `test_sim_protocol.c:977` `test_event_queue_saturation`（补 pop 断言：恰好 32 次成功、第 33 次 NOT_READY、`dropped_events == 0`）；`test_sim_protocol.c:1030` `test_device_event_queue` |
| B-13 | 已知缺口未反馈到分页/ACK；`fail_journal` 开关可掩盖真实事务失败 | `sim.c:1318-1323` gap 公式：**只要 `continuity_ok` 被锁存为 false，任何页都报 `gap_before_page=1`**（另有 after 早于 first_available 的分支），不再区分「带内可见缺口」与「静默缺口」——round-2 审计 R2-B-3 指出后者没有规格依据，`silent_gap` 标志已整体删除；`sim.c:1356` ACK 在缺口时 `CURSOR_GAP`；`sim.c:2462` `inject_gap` 持久写 `dropped_events`/`continuity_ok=0`；`sim.c:2494` `fail_journal` 关闭不清真实 FAULT | `test_sim_protocol.c:508-517`（`inject_gap(2)` 后从 `first_available` 取页仍 `gap_before_page=1`）；`test_sim_journal.c:635`（gap=1、ACK CURSOR_GAP、重启后仍 continuity=false/dropped=1）；`test_sim_journal.c:402` `test_gap_latches_history_incomplete`；`test_sim_journal.c:782` `test_real_sql_fault_cannot_be_cleared_by_injection_switch` |
| B-14 | daemon CRC 重同步与请求号入口不符合帧规则（按不可信 payload_len 切帧会吞掉合法帧；0 号请求先产生副作用） | `sim_main.c:232-248` `service_read_uart` 改用 `locker_wire_stream_feed` 逐字节重同步；`sim_main.c:193-230` `service_on_frame` 先重建 header+CRC，`locker_sim_handle_frame` 返回非 OK 时无副作用返回；`sim.c:1915-1917`/`sim.c:2266-2268` 两处 0 号请求拒绝；`sim.c:1918`/`sim.c:1921` 头部版本与类型校验在 decode 之前 | `daemon_smoke.py:193-204`（0 号无副作用、非零 HELLO 前缀 BAD_PAYLOAD、未知 type UNSUPPORTED）；`daemon_smoke.py:213-215` 损坏长度重同步到内嵌完整 HELLO；`daemon_smoke.py:585-593` 前导垃圾后 resync |
| B-15 | 「4 个 NOT_CAUGHT 全部等价」结论不成立 | 不再宣称整体等价：m13/m32/m39 经重写锚点后已 **CAUGHT**；剩余 3 个 NOT_CAUGHT（m24/m28/m31）逐个给出可证论证，写入 `summary.md` §5.4；m7/m22 补正式断言后转 CAUGHT | 本轮 VM 全量（**56 点位**）：**CAUGHT 51 / 56**、CAUGHT_TIMEOUT 2（m49 连接截止、m51 daemon 卡死）、合计 53 / 56、NOT_CAUGHT 3、BUILD_FAILED 0（`mutation.log`，sha256 `d4fa3c31…3376`，208276 B / 1649 行；历史版本 51 点位 / 47 CAUGHT / `1cc351b7…ff6f`、53 点位 / 49 CAUGHT / `27958b11…fa51a`、round-9 前 56 点位 / 646 条 / `02dde78c…16e8c` 均已作废） |
| B-16 | 复验脚本删除活跃源码、校验失败不阻止继续；变异工作区写在 `$HOME` 直属 | `evidence-run-vm.sh`：每次运行解包到 `$WS/S1-03/batches/<run_id>/src`，批次目录已存在即 `SRC_GUARD_FAILED` exit 2，脚本内**无任何 `rm -rf`**，活跃树 `$WS/S1-03/src` 不受影响；清单或 `SRC_STRAY_ARTIFACTS` 非 0 即 exit 4；结尾 `need_zero()` 逐项校验：逐项 `GATE_OK`/`GATE_FAILED` 写 `gates.log`，`run-summary.log` 内联同一份门禁结果并追加 `VM_RUN_VERDICT`，FAIL exit 5。`mutation-batch.py`：源根优先级 `--src` → `$S103_SRC` → 默认；沙箱落 `build/S1-03/mutation/<run>`、日志落 `logs/S1-03/mutation/<run>`；build 失败记 BUILD_FAILED 不算 CAUGHT，有 BUILD_FAILED 时 exit 3 | 本轮 `SSH_EXIT=0`（该退出码只出现在终端，未归档；归档证据以 `run-summary.log` 的 `VM_RUN_VERDICT` 为准）、`VM_RUN_VERDICT=PASS`；mutation 全量 BUILD_FAILED 0 |

### 12.2 非阻塞项 N-1～N-9

| 编号 | 审计意见 | 处理 |
|---|---|---|
| N-1 | 控制面注入的事件不立即推送到 UART | 已修：`sim_main.c:776`/`:805` 每轮循环调用 `service_flush_events` |
| N-2 | 控制面解析过松（尾随垃圾、非法 cursor/hex 被接受） | 已修：`sim_main.c:335` `parse_cursor`、`:392` `parse_hex_bytes`、`:594-618` `discarding_line` 拒绝尾随字节与超长行 |
| N-3 | transport 无写超时，对端不读可永久阻塞 | 已修：`transport.c:134-170` `poll` + 绝对截止时间，超时返回失败而不是无限阻塞 |
| N-4 | `sqlite3_step` 返回值/BLOB 长度/u64 溢出未处理 | 已修：事务与写入路径的 step 结果经 `sim_sql_ok` 判定；两个只读 `SELECT COUNT(*)` 计数辅助（`sim.c:734`、`sim.c:2637`）在非 `SQLITE_ROW` 时返回 0 而非静默继续；`sim_u64_text` 20 位补零、`sim_u64_from_text` 拒绝溢出；事件 detail 长度按列宽截断 |
| N-5 | handoff 的函数签名/所有权/端口计数过期 | 已修：本文件 §3 与 §4 按当前头文件重写（`locker_device_close` 返回 `locker_status_t`、`locker_device_destroy` 返回 void、`locker_sim_destroy` 内部关库） |
| N-6 | summary 的 `depends_on` 与行号不准 | 已修：`summary.md:40` `depends_on=["S1-01"]`；行号按当前树重算 |
| N-7 | 无 Python3 时静默少注册测试 | 已修：`tests/contract/device/CMakeLists.txt` 缺 Python3 直接 `FATAL_ERROR` |
| N-8 | 测试夹具固定 `/tmp` 路径并 unlink 他人文件 | 已修：`device_test_support.h` 路径带 pid + 计数器，只 unlink 自己创建的文件 |
| N-9 | 启动日志未标注后端 | 已修：`sim_main.c:736` 输出 `ready device backend: simulator` |

### 12.3 round-1 自测阶段另外修复的真实缺陷

round-1 之后又发现并修掉下列问题（都不是放宽断言）：

| # | 类别 | 位置 | 内容 |
|---|---|---|---|
| 1 | 测试缺陷 | `tests/contract/device/daemon_smoke.py:519-541` | `inject_gap` 之后必须把 `CONTINUITY_LOST` 的 `DEVICE_EVENT` 从链路读掉，否则事件帧滞留会把后续 4 条断言读串（这不是产品缺陷，是测试没消费异步帧） |
| 2 | 测试缺陷 | `test_sim_protocol.c` / `test_sim_journal.c` / `test_device_port.c` | 三处分页 `do { } while (has_more)` 无上界；m43 变异时 `has_more` 永真导致 1113 万行输出、`mutation.log` 涨到 1.7 GB。改为带上界 `LOCKER_TEST_PAGE_LIMIT` 的 `for` 循环并断言未超界 |
| 3 | 测试缺口 | `test_device_port.c` `test_pending_entry_matches_response_type` | 补用例：同 request_no、不同类型、同 session 的响应必须被忽略（m7 由 NOT_CAUGHT 转 CAUGHT） |
| 4 | 测试缺口 | `test_sim_protocol.c` `test_event_queue_saturation` | 补队列满的 pop 断言：恰好 32 次成功、第 33 次 NOT_READY、`dropped_events == 0`（m22 由 NOT_CAUGHT 转 CAUGHT） |
| 5 | 脚本缺陷 | `mutation-batch.py` | 12 个锚点因源码被重写而失效；按当前源码逐字重写，`--check` 48/48 OK。变异 new 侧统一保留变量引用（`&& 0` / `|| 1`），避免 `-Werror` 下未使用变量造成编译失败被误记为 CAUGHT |
| 6 | 脚本缺陷 | `evidence-run-vm.sh` | 6 处 `\${...}` 转义残留会让门禁读到字面量并误判 `SSH_EXIT=4`；已全部改为 `${...}`，`bash -n` 通过 |
| 7 | 测试缺陷 | `test_sim_journal.c` `test_release_checkpoint_survives_process_reopen` | round-2 为 R2-B-4 新增的 `detail<>0` 断言把 **TEXT 列**和整数 `0` 比较：`detail` 以 20 位补零十进制 TEXT 存储，SQLite 的 TEXT 恒大于 INTEGER，故该条件永远为真、断言必然失败（实测 `expected 0, got 1`）。改为与 `'00000000000000000000'` 比较并另加 `CAST(detail AS INTEGER)` 断言（`test_sim_journal.c:613-617`）；产品代码本身已正确写 0（`sim.c:1893-1894`） |

### 12.4 round-2 审计（R2-B-1～R2-B-6 / R2-N-1～R2-N-4）整改

| 编号 | 审计意见（文件:行） | 整改落点 | 回归证据 |
|---|---|---|---|
| R2-B-1 | `evidence-run-vm.sh:6/:16/:17/:27` 的 SRC 守卫恒真且 `rm -rf "$SRC"` 会删活跃源码 | 脚本改为每次解包到 `$WS/S1-03/batches/<run_id>/src`；批次目录已存在即 `SRC_GUARD_FAILED` exit 2；**脚本内已无任何 `rm -rf`**；`extract.log` 记录 `live_tree_fingerprint_before/after=$WS/S1-03/src` | `grep -c 'rm -rf' evidence-run-vm.sh` = 0；`extract.log` 的 `live_tree_fingerprint_before/after=` 与 `run-summary.log` 的 `live_tree_files_before=`（round-2 时点值 1155，当前 1158）记录活跃树；`source-verify.log` 的 `SRC_FILES=`（round-2 时点值 1155，当前 1166）统计的是**批次解包树**，不是活跃树 |
| R2-B-2 | `sim.c:1927-1933` decode 失败早退时 reply 仍用**当前** session，旧 session 的坏帧拿到新 session 的错误响应 | 新增 `sim_echo_session()`：`locker_sim_handle_frame` 在 `sim_reply_init` 前先按帧 payload 首 16 字节取请求 session；未知 type 的 `UNSUPPORTED` 与 dispatch 前就被拒的帧都回显它 | `daemon_smoke.py` 旧 session 的 `GET_SNAPSHOT` 断言 `result=2`、`payload_len=18`（`daemon_smoke.py:561-568`）；回显请求 session 的逐字节断言由 round-3 的 R3-N-3 补齐（`daemon_smoke.py` `payload[0:16] == GOLDEN_SNAPSHOT_PAYLOAD[0:16]`、`test_sim_protocol.c` 的 `memcmp(response.hello.session_id, frame+HEADER_SIZE)`） |
| R2-B-3 | `sim.c:1318-1323` 对 `inject_gap` 的带内缺口给豁免（gap=0），规格无此豁免 | 删除整套 `SIM_META_SILENT_GAP` 机制；gap 公式改为「`continuity_ok` 已锁存 false 即报 1」，另有 after 早于 first_available 的分支 | `test_sim_protocol.c` `test_fetch_pagination`：`inject_gap(2)` 后从 `first_available` 取页断言 `gap_before_page=1`、`first_available=(1,1)`、`event_count=3` |
| R2-B-4 | `sim.c:1893-1894` `CONTEXT_RELEASED` 的 `detail` 传了 `action_no`，违反 uart-v1.md:46/:352「detail 不适用必须 0」 | `sim_transition(..., CONTEXT_RELEASED, 0u, 0u, true)`，第 5 参 detail 改 0 | `test_sim_journal.c` `test_release_checkpoint_survives_process_reopen` 新增两条断言（`:613-617`）：`SELECT COUNT(*) FROM sim_events WHERE event_type=13 AND detail <> '00000000000000000000'` = 0，以及 `SELECT CAST(detail AS INTEGER) FROM sim_events WHERE event_type=13 LIMIT 1` = 0。注意首版误把 TEXT 列与整数 0 比较（`detail<>0` 恒真），已按 §12.3 第 7 行改正 |
| R2-B-5 | `port.c:105/:119/:175` pending 超时用 `clock_gettime` 真时钟，注入时钟无法驱动，测试真等 5.3 秒 | 删除 `device_deadline_now()`；`pending_expire`(`port.c:112`) / `pending_add`(`port.c:171`) 改用 `device_now_ms(device)`（注入时钟） | `test_device_port.c` `test_pending_timeout_and_request_wrap`(:1618) 改为 `env.clock.monotonic_ms += PORT_PENDING_TIMEOUT_MS + 1u` 触发回收（不再真等 5.3 s）；新增 `test_real_clock_smoke`(:1589) 用 `locker_clock_port_real()` 走完 open→GET_SNAPSHOT→close，满足 `stage-01-task-cards.md:123`「集成测试至少保留一项真实时钟烟测」 |
| R2-B-6 | `handoff.md:125/:200/:202/:221` 四处与实现不符（device id 样例、17 次发号、destroy 后 INVALID、队满锁存 continuity） | §3.3 样例改 `01000000000000000000000c`；§4 发号改为「唯一硬边界是 `UINT32_MAX` 回绕，冲突在同锁内拒绝」；§4 销毁改为「destroy 成功后指针失效，调用者置 NULL，不得再调用」；§5 队满改为「只丢实时通知，事件已 COMMIT」 | 本文档 §3.3 / §4 / §5 已改；对应实现 `port.c:176-183`、`port.c:981-992`、`sim.c:843-861` |
| R2-N-1 | `host/device/README.md`、`host/simulator/README.md` 的 run_id 过期 | 两个 README 改为指向本轮 canonical run（见 §7.1） | `grep -n 'run_id' host/*/README.md` |
| R2-N-2 | `VM_RUN_VERDICT`/`SSH_EXIT` 只打终端，未进任何 `vm-s103-*.log` | `evidence-run-vm.sh` 结尾把逐项 `GATE_OK`/`GATE_FAILED` 写 `gates.log`，并把 `VM_RUN_VERDICT` 追加进 `run-summary.log`，然后才打包 tgz | `vm-s103-run-summary.log` 现在包含 `gates_log=`、`GATE_OK ...` 与 `VM_RUN_VERDICT=` 行 |
| R2-N-3 | `mutation-batch.py:347/:348/:356` 把 CTest 汇总行算进断言失败、分母只取第一个测试 | 用 `assertion_re = ^FAIL [^:()]+:\d+: ` 只统计断言行，`summary_re` 收集每个红测试的 `(n of m)`，分母改为全部红测试分母之和；每个红测试单独打 `red_test` 行 | `mutation.log` 每个点位改为 `assertion_failures=N assertions red across K test binaries (denominator=M)` + 逐测试 `red_test` 行 |
| R2-N-4 | `port.c:429` 的 `reply.message_type` 带 `0x80` 只是本卡裁定，生成头未声明 | 本卡不改生成物；已在 `host/device/README.md` §4 记为「待契约负责人裁定项」，并在 summary §6 单列 | 文档记录，不改 `contracts/` |

### 12.5 round-3 审计（R3-B-1～R3-B-3 / R3-N-1～R3-N-4）整改

| 编号 | 审计意见（文件:行） | 整改落点 | 回归证据 |
|---|---|---|---|
| R3-B-1 | `sim.c:1534` 把 `COMMAND_ACCEPTED` 的 `detail` 绑死为 20 位零文本、`sim.c:1765` 把 `pulse_checkpoint.detail` 置 0，违反 uart-v1.md:345「COMMAND_ACCEPTED detail 为 action_no」，同一 action_no 会留下 type12/detail1 与 type12/detail0 两条矛盾记录 | `sim_cancel_unstarted_body` 新增 `char detail[21]`，用 `sim_u64_text(sim->active_intent.action_no, detail)` 绑第 4 个参数（`sim.c:1519-1538`）；失败分支 `sim->pulse_checkpoint.detail = request->action_no`（`sim.c:1765`），内存态与落库态一致 | `test_sim_journal.c` `test_deadline_rechecked_after_prepare_and_result`（`:744`）新增三条断言：type12 行数 ≥1、`detail<>'00000000000000000001'` 的行数为 0、`detail='00000000000000000001'` 的行数 ≥1 |
| R3-B-2 | `handoff.md:360` 用 `bash` 调 `mutation-batch.py`（Python 脚本） | §8 再生成步骤改为 `python -B docs/evidence/stage-01/S1-03/mutation-batch.py --src "<repo-root>"` | `python -m py_compile` exit 0；`--check` 输出 `# CHECK_SUMMARY points=48 bad=0` |
| R3-B-3 | 提交给审计的 `mutation.log` sha256 摘要只有 63 位十六进制 | round-3 整改后重跑的版本完整值是 `e3f477eac9b1d997d81a2e6428b1153da97bb9e65f9270f255b0416547ba98ad`（183170 B / 1417 行）；round-4 整改后按新统计口径重跑的版本是 `ecba26eb1788c9a1d6e932e38ce2cab828baa7ee7ff3e752f21c4202f50bd6e5`（185389 B / 1418 行，已作废）；round-5 整改后（新增 m49 点位 + 分类顺序修正）的版本是 `8a7083bdd5648554043b0f9b5f8f221440eabffd17ca6fdc95208b7bcab8a146`（186270 B / 1426 行，已作废）；round-6 整改后（新增 m50/m51 点位 + 每点位保留完整 CTest stdout）为 `1cc351b7dd6ef7cab1b821e13a63f1d1ddafe9ad3cbac716619220b20d1eff6f`（197711 B / 1527 行，已作废）；round-9 前自查后（56 点位）的版本是 `02dde78cf442c60016bed5cb75b1753a9030d4c392321d0859ef410a01616e8c`（208118 B / 1648 行，已作废）；round-9 非阻塞整改后重跑的**当前版**是 `d4fa3c310b4bf667de070ccf759c3c9b66ed899fda8b8e70d60ccea77ace3376`（208276 B / 1649 行），与 `summary.md` §5.4 一致；输入侧笔误已澄清 | `certutil -hashfile` / Node `createHash` 复算一致 |
| R3-N-1 | `port.c:429` 的 `reply.message_type` 带 `0x80` 未经契约负责人裁定 | 本卡不改生成物；`host/device/README.md` §4 与 `summary.md` §6 继续登记为「待契约负责人裁定项」 | 文档记录，`contracts/` 未改动 |
| R3-N-2 | handoff 七处措辞与实现不符（bind_sink「借用」、发号耗尽→BUSY、close INVALID 含义、gates.log 含 verdict、SRC_FILES 当活跃树、所有 step 经 sim_sql_ok、旧 `detail<>0` SQL） | §4 内存所有权改为「按值复制 sink 结构，只借用 user 与函数指针」；§4 发号耗尽改 `NOT_READY`（`port.c:503-507`）；§6.1 注明 destroy 后不保证 INVALID；§9 分开 `gates.log` 与 `run-summary.log` 的职责并声明 `SSH_EXIT` 只在终端；§12.4 R2-B-1 改为「`live_tree_files` 记录活跃树、`SRC_FILES` 是批次树」；§12.2 N-4 改为「事务/写入路径经 sim_sql_ok，两个只读计数辅助非 ROW 时返回 0」；§12.3 第 7 行改引 `test_sim_journal.c:613-617` | 逐条按当前源码行复核（`port.c:574-585`、`port.c:503-507`、`port.c:949-992`、`sim.c:734`、`sim.c:2637`） |
| R3-N-3 | 整改表声称的两个「回显请求 session」断言实际不存在 | 补齐正式断言：`daemon_smoke.py` 旧 session 拒绝处新增 `reply["payload"][0:16] == GOLDEN_SNAPSHOT_PAYLOAD[0:16]`；`test_sim_protocol.c` `test_malformed_hello_prefix_and_zero_number_have_no_side_effect` 新增 `memcmp(response.hello.session_id, frame+HEADER_SIZE)`；`test_execute_rules` 的 slot/kind 负例补 `operation_id`，使失败原因真的是 slot_id/kind 而不是 codec 的 nil-UUID 拒绝；PULSE_ACTIVE 断言改为读真实 GET_SNAPSHOT 的 `snapshot.flags`（原来用 `locker_sim_continuity_ok()` 是恒真式） | 上述四处断言在 VM canonical 运行中执行；daemon 检查数随新增断言上升 |
| R3-N-4 | `wire.c:90-91` 放行 `DEVICE_EVENT\|RESPONSE_FLAG`=0xc0，与 `wire.h` 声明的合法集合不一致 | `locker_wire_encode_frame` 删除 0xc0 例外，response 类型必须是 `request_type\|RESPONSE_FLAG` | `test_wire.c` `test_encode_rejects_bad_arguments` 新增两条 `0xc0` 必须 `INVALID` 的断言（request_no=1 与 0 各一条） |

### 12.6 round-4 审计（R4-B-1 / R4-N-2～R4-N-4）整改

| 编号 | 审计意见（文件:行） | 整改落点 | 回归证据 |
|---|---|---|---|
| R4-B-1 | §8 再生成入口仍逐字是 `bash docs/evidence/stage-01/S1-03/mutation-batch.py`，与 R3-B-2 的整改声明冲突；审计把脚本交给 guest `bash -n` 实测 rc=2 | §8 两处入口统一为 `python -B docs/evidence/stage-01/S1-03/mutation-batch.py --src "<repo-root>"`；全文再无「`bash` + `.py`」组合 | `grep -n 'bash.*mutation-batch' handoff.md` 无命中；`python -B docs/evidence/stage-01/S1-03/mutation-batch.py --check --src .` 输出 `# CHECK_SUMMARY points=48 bad=0`、exit 0 |
| R4-N-2 | daemon 检查数写 249，`vm-s103-daemon-smoke.log:11/:13` 实为 250（round-4 时点值） | `summary.md` 五处、本文档 §1、`host/simulator/README.md` 全部改为 **250**；round-6 新增 journal 路径用例后为 **286**（见 §12.8），round-7 新增路径别名用例后为 **318**（见 §12.9），round-8 新增悬空链接用例后为 **366**（见 §12.10），round-9 前自查补链接拼写与跳数用例后当前值为 **391**（见 §12.11） | 当时以原始日志为准复算：`DAEMON_CHECKS=250`、`PASS ... (250 checks)`；后续时点值 286 / 318，round-8 时点 `DAEMON_CHECKS=366`，round-9 前自查后当前 `DAEMON_CHECKS=391`、`PASS ... (391 checks)`（`vm-s103-daemon-smoke.log`）；文档内剩余 `249` 仅 `sim.c:2494` 行号 |
| R4-N-3 | m43 段的 `assertion_failures` 把「有完整汇总的 3 个二进制」写成全部失败来源 | `mutation-batch.py` 输出改为 `assertion_failures=N assertion lines red; complete_summaries=K binaries; denominator=M checks in those summaries only`；`summary.md` §5.4 表头说明分子/分母口径不同，并加 m43 专段（分母 1253 只覆盖打印了完整汇总的 3 个二进制） | round-4 版（647 行 = 566 + `test_device_port` 81 行，185389 B / 1418 行 / sha256 `ecba26eb…d6e5`）**已作废**；round-5 版（186270 B / 1426 行 / `8a7083bd…a146`）**亦已作废**；round-6 整改后 51 点位重跑版 `1cc351b7…ff6f`（197711 B / 1527 行）**亦已作废**；round-9 前自查后 56 点位重跑的版本为 `02dde78c…16e8c`（208118 B / 1648 行，已作废）；round-9 非阻塞整改后重跑的**当前版**为 `d4fa3c31…3376`（208276 B / 1649 行），其 m43 段为 `assertion_failures=647 … complete_summaries=3 binaries; denominator=1253 checks in those summaries only`（按文件复算 210 + 354 + 2 + 81 = 647），与本节及 `summary.md` §5.4 一致 |
| R4-N-4① | `host/device/README.md:80` 说 pending「按实际单调时间」，实际用 `config.clock` | 改为「按注入的 `config.clock` 单调时间 5000 ms 有界回收（生产注入真实时钟，测试注入替身时钟；见 `port.c:112`/`:171`）」 | 与 `port.c:112`/`:171` `device_now_ms` 一致 |
| R4-N-4② | `summary.md:288` 称 `port.c:127-142` 用 `continue`，该函数无字面 continue；「陈旧条目复用号」不能豁免在途号唯一性 | 改为「不消费、继续扫描（单层 `for` 循环，命中三条件才 `*out = *entry; entry->used = false`）」，并写明在途号唯一性由 `pending_add`（`port.c:145` 起同锁查重）保证 | `grep -n 'continue' host/device/src/port.c` 在 `pending_take` 范围内无命中 |
| R4-N-4③ | `evidence-run-vm.sh:15/:17` 先 `mkdir -p "$BUILD" "$LOGS" "$ART"` 再检查 `[ ! -d "$WS" ]`，守卫恒假 | `mkdir` 移到守卫之后；守卫失败信息直接写 stderr（此时 `$LOGS` 尚未创建） | 脚本第 15-19 行顺序；`bash -n` 通过 |
| R4-N-4④ | 本文档 §7.1 用 `live_tree_files` 单次计数声称活跃树未被触碰 | 脚本新增 `LIVE=$WS/S1-03/src` 与 `live_tree_fp()`（`find -type f -printf '%P %s\n' | sort | sha256sum`，目录不存在输出 `absent`），解包前取 `LIVE_BEFORE`、全部测试与设计检查之后取 `LIVE_AFTER`，写 `live_tree_fingerprint_before/after` 与 `live_tree_unchanged=yes|no`，并把 **`live-tree-unchanged` 纳入 `need_zero` 同级的门禁**（不等即 `GATE_FAILED` + exit 5） | 新 run 的 `extract.log` 与 `run-summary.log` 同时含 before/after 指纹与 `GATE_OK live-tree-unchanged`；§7.1 改述为「前后指纹相同」。**注：round-5 R5-N-2 指出该版指纹只哈希路径+大小，已改为哈希文件内容并前移采样点，见 §12.7** |
| R4-N-4⑤ | 本文档称 verdict 写进 `gates.log` 与 `run-summary.log` 两个日志，实际 `gates.log` 只有 `GATE_OK` 行；历史 `SSH_EXIT` 无 transcript | §7.1 第 5 步改为「逐项 `GATE_OK`/`GATE_FAILED` 只写 `gates.log`；`run-summary.log` 用 `cat "$GATES"` 内联同一份结果并追加 `VM_RUN_VERDICT`」；§12.1 B-16 行注明 `SSH_EXIT` 只在终端、归档以 `VM_RUN_VERDICT` 为准 | 与脚本第 271-275 行一致 |


### 12.7 round-5 审计（R5-B-1 / R5-N-1～R5-N-3）整改

| 编号 | 审计意见（文件:行） | 整改落点 | 回归证据 |
|---|---|---|---|
| R5-B-1 | 连接截止不能打断阻塞 `connect()`：`transport.c:76` 用阻塞 socket、`:83` 直接 `connect()`，只有返回后才在 `:118-120` 检查截止；guest 实测 accept 队列填满时 `locker_device_transport_connect(...,5000)` 5400 ms 未返回，父进程 accept 后才返回 status=0 / 5406 ms，违反 `transport.h:36-38`「Retries for at most timeout_ms of real time」 | `transport_try_connect` 改为「非阻塞 connect + poll 到同一单调截止 + 恢复阻塞模式」：`transport.c:69-78` 新增 `transport_connect_result(int fd)`（`getsockopt(SO_ERROR)`）；`:109-113` `fcntl(F_GETFL)` + `F_SETFL, flags\|O_NONBLOCK`；`:130-136` 遇 `EINPROGRESS`/`EINTR` 用 `poll(&writable,1u,(int)(deadline_ms-now_ms))` 等可写；`:137` 用 SO_ERROR 判定；`:143-147` 把 `flags & ~O_NONBLOCK` 清回阻塞（read 依赖 `SO_RCVTIMEO`）；`locker_device_transport_connect` 把 deadline 传进每次尝试并在 `INVALID` 时立即返回。文件 203 → 267 行 | `tests/contract/device/test_device_port.c` 新增 `test_connect_deadline_when_accept_queue_full()`（`:1802-1874`）：`listen(listener,1)` + 非阻塞 connect 填满 accept 队列，断言 700 ms 截止内返回 `IO_ERROR`、耗时 < 3000 ms、`!connected`；`accept()` 腾一格后同一调用必须成功（证明截止路径不留死锁）。断言 369 → **389**，VM canonical 实测 `PASS contract.device.test_device_port (389 checks)` |
| R5-N-1 | `reply.message_type` 的方向位（`port.c:429` 写 `request_type\|0x80`）仍待契约负责人裁定（`contracts/include/locker_contracts.h:181` 只有 `uint8_t message_type`） | 本卡不改生成物；延续 R2-N-4 / R3-N-1 / R4-N-1 的登记口径，继续在 `host/device/README.md` §4、`summary.md` §6 记为「待契约负责人裁定项」 | 文档记录，`contracts/` 未改动 |
| R5-N-2 | `evidence-run-vm.sh:21-28` 的 `live_tree_fp()` 只做 `find -printf '%P %s'`（路径+大小，不读内容），等长改写不可见；`:47` 的计数在解包之后采样，不能称「解包前计数」 | `live_tree_fp()` 改为 `( cd "$LIVE" && find . -type f -print0 \| LC_ALL=C sort -z \| xargs -0 -r sha256sum \| sha256sum )`（哈希**内容**）；新增 `live_tree_files()`，`LIVE_FILES_BEFORE`/`LIVE_BEFORE` 一起前移到解包之前；结尾 `LIVE_FILES_AFTER` 与 `LIVE_AFTER` 双条件相等才 `LIVE_UNCHANGED=yes`，`live-tree-unchanged` 门禁保持独立 | 新 run `extract.log`/`run-summary.log` 含 `live_tree_files_before=_after=1158`、`live_tree_fingerprint_before=_after=c1fb320a…3a6a`、`live_tree_unchanged=yes`、`GATE_OK live-tree-unchanged` |
| R5-N-3 | 本文档三处控制入口措辞与实现不符：`:178` 启动行、`:185` `card HEX` 长度、`:183-184` `door`/`item unknown` 的事件类型 | §3.6 启动行改为 `ready device backend: simulator uart=... control=... journal=...`（`sim_main.c:736`）；`door`/`item` 行补注 `unknown` 发 `HEALTH_CHANGED`（`sim.c:2352-2356` / `:2389-2393`）；`card HEX` 行改为「文本入口只接受 4/7/10 字节（空串在 `parse_hex_bytes` 即 false → `error bad_argument`，`sim_main.c:397`/`:520`）；0 长度只能走 C 引擎 API `locker_sim_control_card`（`sim.c:2402`）」；§5 错误表「连接超时」行补「每次 connect 都是非阻塞 + poll 到同一单调截止（`transport.c:66-140`）」 | 逐条按当前源码行复核；`host/device/README.md:75` 同步补同一句 |

### 12.8 round-6 审计（R6-B-1 / R6-N-1～R6-N-3）整改

| 编号 | 审计意见（文件:行） | 整改落点 | 回归证据 |
|---|---|---|---|
| R6-B-1 | 默认 journal 路径静默截断后可等于 control socket 路径：`sim_main.c:681-683` 默认分支 `(void)snprintf(config.journal_path, sizeof(config.journal_path), "%s.journal", control_path)` 忽略返回值；control 恰 107 字节时截断后 journal 与 control 同名，`locker_sim_open` 先建库、随后 `listen_unix` 的 `sim_main.c:78 (void)unlink(path)` 删掉数据库换成 socket，进程仍打印 `ready`，`restart` 返回 `error io_error`，退出后只剩 `-wal`/`-shm`；100 字节路径静默变成 `.journa`。容量定义 `sim.h:33 LOCKER_SIM_PATH_SIZE 108u` | `sim_main.c` 新增 `static bool socket_path_fits(const char *path)`（`:61-66`，`strlen(path) < sizeof(address.sun_path)`）；`main` 改为「先校验、再建资源」：`:690` 对 uart/control 两个 socket 路径统一校验（失败 `socket path too long` + `return 2`，发生在 `locker_sim_open` 与 `listen_unix` 之前）；默认 journal 分支改为 `int written = snprintf(...)` 并检查 `written < 0 || (size_t)written >= sizeof(config.journal_path)`（`:703`，`journal path too long` + `return 2`）；新增撞名守卫 `:713`（journal 与任一 socket 同路径 → `journal path must differ from the socket paths` + `return 2`） | `tests/contract/device/daemon_smoke.py:316` 新增 `test_daemon_journal_path_limits`（36 条检查）：99 B control + 默认 journal 必须启动并 restart/quit 成功；100 B 与 107 B control + 默认 journal 必须 exit 2 且不建 control/uart socket 与 `.journal`/`.journa`；107 B control + 显式短 journal 必须启动并 restart/quit 成功；显式 journal 与 control 同名必须 exit 2 且不建任何文件。VM 实测（round-6 时点）`DAEMON_CHECKS=286` / `DAEMON_FAILURES=0`；round-7 整改后为 **318**、round-9 前自查后为 **391**，并打印 `journal_limit_100_exit=2 stderr=journal path too long`、`journal_limit_107_exit=2`、`journal_collision_exit=2 stderr=journal path must differ from the socket paths`。mutation 新增 m50（`sim_main.c:703`，daemon 红 7）与 m51（`sim_main.c:713`，daemon 红 4）均 CAUGHT |
| R6-N-1 | 原生 `reply.message_type` 方向位仍待契约负责人裁定（`contracts/include/locker_contracts.h:181` 只有 `uint8_t message_type`；`port.c:429` 填 `request_type\|0x80`；`host/device/README.md:61` 已登记） | 本卡不改生成物；延续 R2-N-4 / R3-N-1 / R4-N-1 / R5-N-1 的登记口径 | 文档记录，`contracts/` 未改动 |
| R6-N-2 | `mutation.log` 只写筛选后的断言/摘要，没有 `***Timeout` 原行、120.10 秒或被超时的具体测试名，m49 段无法仅凭原件独立复核「某测试在 120.10 s 被 CTest 杀掉」 | `mutation-batch.py` 新增 `RAW_DIR = <LOGDIR>/raw`，每个点位在 ctest 前后记 `started_ctest`/`ctest_seconds`，把**未过滤**的完整 CTest stdout + 退出码 + 秒数写入 `raw/<point>.log`；`mutation.log` 每个点位在 `patch=OK ... ctest_seconds=%.2f raw=raw/<point>.log` 之后逐条追加 `ctest_evidence <含 ***Timeout/***Failed/Timeout 的原行>`；汇总段新增 `# RAW_CTEST_STDOUT <RAW_DIR>` | 本轮 VM 全量跑已产出 `logs/S1-03/mutation/20261026-011500/raw/` 下 51 个点位日志；`mutation.log` 的 m49 段含 `***Timeout` 原行与 `ctest_seconds` |
| R6-N-3 | 文档历史与当前数字混写：`summary.md:278` 仍叙述 647 条 / 185389 B / 1418 行；`summary.md:201` 把 mutation 运行号 234500 当源码批次；`summary.md:6` 称全部在同一最终快照跑完（实际 canonical 与 mutation 分两次跑）；`summary.md:144` 称 16 项经 need_zero（实际 15 次调用 + live-tree + design 共 17）；`handoff.md:304` 称「三套消毒器」（实际普通构建 + 两套消毒器）；`handoff.md:497` 一行同时含旧 647/81 与当前 646/80 | `summary.md:278` 改为「历史值 …（round-4 输入）与 …（round-5 输入）；当前值（round-6 整改后 51 点位重跑）为 646 条 / 分母 1253 / 197711 B / 1527 行」；`summary.md:201` 改为「round-6 整改后在 VM 批次树 `20261026-010653/src`、`S103_RUN=20261026-011500` 重跑」；状态行改为分列 canonical run 与 mutation 跑次；`summary.md:144` 改为 15 次 `need_zero` 调用 + live-tree-unchanged + design-checks 共 17 项门禁；`handoff.md:304` 改为「普通构建 + ASan/UBSan + TSan 三套构建」；`handoff.md:497` 拆为「round-4 版（已作废）」与「round-6 最终版 646 / 80」 | 本轮全库 grep 复核，无残留旧值（除明确标注为历史/作废者） |

### 12.9 round-7 审计（R7-B-1 / R7-B-2 / R7-N-1 / R7-N-2）整改

| 编号 | 审计意见（文件:行） | 整改落点 | 回归证据 |
|---|---|---|---|
| R7-B-1 | 路径守卫只比较**原始字符串**：`sim_main.c:713` 用 `strcmp(config.journal_path, uart_path)` 与 `strcmp(..., control_path)`，`journal=D/./c` 与 `control=D/c` 比较不等却指向同一目录项。于是 `locker_sim_open` 先建库，`listen_unix`（`sim_main.c:87` 的 `(void)unlink(path)`）再把这条目录项删掉换成 socket，进程仍打印 `ready`，`restart` 返回 `error io_error`，退出后只剩 `-wal`/`-shm`。另一分支 `control=D/j.db-wal`、`journal=D/j.db`：控制 socket 占住 WAL 名，`restart` 返回 `ok` 但控制路径变回普通 WAL 文件，新连接 errno=111 | `sim_main.c` 新增 `#define SIM_MAIN_CANON_SIZE 4096u`（`:46`）与 `static bool canonical_entry(const char *path, char *out, size_t out_size)`（`:76-122`）：`realpath` 命中直接用；`ENOENT` 时取父目录 `realpath` 加 basename 拼出「将来会命中的条目」，空串/无父目录/超长一律 `false`。新增 `static bool paths_are_distinct(...)`（`:128-164`）规范化三者后比较：uart 与 control 不等、journal 不等于任一 socket、`<journal>-wal` 不等于任一 socket、`<journal>-shm` 不等于任一 socket。`main` 的原 `strcmp` 守卫替换为 `sim_main.c:815` 的统一守卫（`paths must name distinct filesystem entries` 加 `return 2`），位置在全部长度检查之后、`locker_sim_create`(`:828`)/`locker_sim_open`(`:832`)/两次 `listen_unix`(`:844`/`:850`) 与 `ready` 打印(`:865`) 之前 | `daemon_smoke.py` 新增 `test_daemon_path_aliasing`（`:468-569`）：`alias_control`（`./c.sock`）、`alias_uart`、`wal_collision`（`control=j.db-wal`）、`shm_collision`（`control=j.db-shm`）、`same_sockets`、`same_alias`、`same_alias_sockets` 七例必须 exit 2 且**该用例目录内不产生任何条目**；正对照 `ok`（`journal=./j.db` 但确实不同条目）必须 `ready` 加 `restart ok` 加 `quit ok` 加 exit 0。检查数 286 → **318**，VM 实测 `DAEMON_CHECKS=318` / `DAEMON_FAILURES=0`。mutation 新增 m52（`sim_main.c:120` 规范化回退改成直接返回原串）与 m53（`sim_main.c:157` 的 `-wal`/`-shm` 比较加 `&& 0`），并把 m51 改锚到新守卫 `sim_main.c:815` |
| R7-B-2 | 未检查 `uart` 与 `control` 是否同一 socket 目录项：`--uart D/c --control D/c --journal D/j.db` 时第二次 `listen_unix`（`sim_main.c:749`）先 unlink 第一个 UART socket 再 bind 控制 socket，两个 listen 都成功并打印 `ready`，但按配置连 UART 的客户端实际进入文本控制解析器，黄金 HELLO 收到 ASCII `error bad_argument` 而非 `0x81` 帧 | 同一 `paths_are_distinct`（`sim_main.c:151-153` 的 `strcmp(uart, control) == 0` 即返回 false）同时覆盖 `same_sockets`/`same_alias`/`same_alias_sockets` 三例；拒绝发生在任何 socket 创建之前，因此不会留下被 unlink 的 UART 条目 | `daemon_smoke.py:525-528` 三例；正对照仍要求两个 socket 分别可达（`restart`/`quit` 走 control，HELLO 走 uart） |
| R7-N-1 | `reply.message_type` 的方向位仍待契约负责人裁定（`contracts/include/locker_contracts.h:181` 只有 `uint8_t message_type`；`port.c:429` 填 `request_type` 按位或 `0x80`；`host/device/README.md:61` 已登记）。延续 R2-N-4 / R3-N-1 / R4-N-1 / R5-N-1 / R6-N-1 | 本卡不改生成物，继续登记为待裁定项 | 文档记录，`contracts/` 未改动 |
| R7-N-2 | 文档现值漂移：`summary.md` 范围表仍写 `host/simulator/README.md` 67 行/6620 B、`sim_main.c` 827 行/27930 B；`:133` 散列缩写以 `e067` 结尾但原日志以 `10a5` 结尾；`:203` 仍称「该段没有保留完整 CTest 输出」但 51 份 raw 已可核对；`handoff.md:513` 活跃树 `e0a1d88b…e067` 不对应当前日志 | 范围表改为 69/7999 与 956/32920；全部 canonical 数字换到 run_id=`20261026-032231`（快照 `bbf1f72b…145f`、`live_tree_fingerprint_before=_after=c1fb320a…3a6a`、`SRC_FILES=1166`、10.08 s / 11.79 s / 12.67 s）；§5.4 增加「每点位 raw 已归档」与 `mutation-raw-ctest.tgz` 索引；`handoff.md:513` 改为当前指纹 | 本轮批量替换脚本实测 `missing []`；证据目录逐个列名复核（round-7 整改后为 48 个文件，其中 18 个 `vm-s103-*` 已全部换成 run_id=20261026-032231 的日志）。**以下为 round-7 时点值，已被 §12.10 的当前值取代**：69/7999、956/32920、`c1fb320a…3a6a`、`SRC_FILES=1166`、10.08 s / 11.79 s / 12.67 s |

### 12.10 round-8 审计（R8-B-1 / R8-N-1 / R8-N-2）整改

| 编号 | 审计意见（文件:行） | 整改落点 | 回归证据 |
|---|---|---|---|
| R8-B-1 | 末级**悬空符号链接**绕过路径身份守卫：`realpath(journal)` 对目标尚不存在的链接返回 `ENOENT`，round-7 的 `canonical_entry` 回退只解析父目录再拼回链接名，从未解析链接目标（`sim_main.c:86` 空串检查、`:90` `realpath` 成功分支、`:120` ENOENT 回退 `snprintf`、`:139`/`:142`/`:146` 三处调用、`:815` 守卫）。于是 `locker_sim_open`（`:832`）先按链接创建目标主库及其 sidecar，`listen_unix`（`:844`/`:850`，实际 unlink 在 `:181`）再把同一条目录项删掉换成 socket：进程仍打印 `ready`，`restart` 返回 `error io_error`，退出后只剩 `-wal`/`-shm`；`control=j.db-wal` 时反过来，控制 socket 占住 WAL 名，重连 errno=111 | `sim_main.c` 新增 `#define SIM_MAIN_LINK_HOPS 8u`（`:49`，超过即拒绝，避免环自旋）；把原 ENOENT 回退拆成 `static bool canonical_missing(const char *path, char *out, size_t out_size)`（`:77-114`，`snprintf(out, out_size, "%s/%s", parent_real, name)` 在 `:110`）；新增 `static bool read_link_target(const char *link_path, char *out, size_t out_size)`（`:116-159`，`readlink` 后按链接所在目录解析相对目标）；重写 `canonical_entry`（`:167-210`）：`realpath` 命中即用（`:185`）→ `errno != ENOENT` 拒绝（`:189`）→ `lstat` 不存在则 `canonical_missing`（`:192`/`:196`）→ 非链接拒绝（`:198`）→ `hops += 1u` 超限拒绝（`:202`）→ `read_link_target` 后 `memcpy(current, next, ...)` 继续（`:205-208`）；新增 `clashes_with_sockets`（`:213-216`）；`paths_are_distinct`（`:225-269`）新增 `journal_spelling` 缓冲，四项 canonical 检查后先比 `uart` 与 `control`（`:245`），再按**链接目标**与**字面拼写**两种形态比 socket（`:248-250`），最后双循环拼 `-wal`/`-shm` 两种 base 各比一次（`:252-267`，判定在 `:263`）。注释写明理由：SQLite 沿链接创建**目标**文件，`-wal`/`-shm` 却追加在**传入的名字**上 | `daemon_smoke.py` 新增 `test_daemon_dangling_links`（`:469-599`，在 `test_daemon_journal_path_limits` 末尾调用）：`rejected` 闭包断言不超时、exit 2、stderr 含 `distinct filesystem entries`、**用例目录条目集合与建链前完全相同**（不创建也不删除）、每条链接仍是链接且 `readlink` 值不变；九例 `link_to_control`(j.db→c.sock)、`link_to_uart`、`link_absolute`（绝对目标）、`link_chain`（两跳 j.db→mid→c.sock）、`link_parent`（`here`→`.` 后 journal=`here/c.sock`）、`link_wal`/`link_shm`（socket 落在链接目标的 sidecar 上）、`link_loop`（j.db↔j2 环）、正对照 `link_ok`（j.db→real.db 必须 `ready` + `restart ok` + `quit ok` + exit 0 + `real.db` 存在 + 链接仍在）。daemon 检查数 318 → **366**；VM 实测 `DAEMON_CHECKS=366` / `DAEMON_FAILURES=0`，九条拒绝行全部 `exit=2` 且 stderr 一致。mutation 新增 m54（`sim_main.c:205`，跟链接改回直接 `canonical_missing`）、m55（`sim_main.c:248`，只查链接目标不查字面拼写）、m56（`sim_main.c:202`，hop 上限加 `&& 0`），并把 m52 改锚到 `:110`、m53 重写为 `sim_main.c:263` 的 `clashes_with_sockets` 条件 |
| R8-N-1 | `reply.message_type` 的方向位仍待契约负责人裁定（`contracts/include/locker_contracts.h:181` 只有 `uint8_t message_type`；`host/device/src/port.c:429` 填 `request_type` 按位或 `0x80`；`host/device/README.md:61` 已登记）。延续 R7-N-1 / R6-N-1 / R5-N-1 / R4-N-1 / R3-N-1 / R2-N-4 | 本卡不改生成物，继续登记为待裁定项 | 文档记录，`contracts/` 未改动 |
| R8-N-2 | 文档现值漂移五处：`handoff.md:5` 仍写 daemon 286（原始日志 366 的前一时点值 318）；`handoff.md:304` 仍称「三套消毒器」（实际是普通构建 + 两套消毒器构建）；`handoff.md:534` 写 README 69/7999（实际 71 行）；`summary.md:282` 在同一当前 §5.4 内把 51 点位 / 197711 B / 1527 行称「当前值」；`mutation-batch.py:148` 的 m22 文案仍称队满「latches continuity」，与 old/new 实际改的 `return` 不符 | ①`:5` 改为 366；②`:304` 改为「普通构建与两套消毒器构建的退出码与报告数」；③本行（§12.9 R7-N-2）证据列改写为 round-7 时点值并说明已被 §12.10 取代；④`summary.md:282` 把 51 点位版本标为「round-6 版」历史值并指向 §5.4；⑤`mutation-batch.py` m22 文案改为 `sim.c:851 full live queue drops the live copy, journal kept` | 本轮批量替换脚本实测 `missing []`；`summary.md` / `handoff.md` 全库 grep 复核，残留旧值均带「round-N 时点值」「已作废」限定；`mutation-batch.py --check --src .` 实测 `# CHECK_SUMMARY points=56 bad=0` |
### 12.11 round-9 前的开发方自查整改（m55 死代码 / m56 测试缺口 / 锚点重算）

round-8 审计（结论 FAIL，1 阻塞 / 2 非阻塞）后留下的 5 个 NOT_CAUGHT 里，`m55_link_spelling_ignored` 与 `m56_link_hop_limit` 无法用「等价性」解释，属真实测试缺口。开发方在交 round-9 前自查并整改，未等审计开单：

| 项 | 问题（文件:行） | 整改落点 | 回归证据 |
|---|---|---|---|
| 自查-1：m56 是真缺口 | `sim_main.c:202` 的 `if (hops > SIM_MAIN_LINK_HOPS) return false;` 没有任何用例能触发——既有九例最深只到 2 跳，删掉上限后行为完全相同，变异体 0 红 | `tests/contract/device/daemon_smoke.py` 新增 `link_hops` 用例：10 个链接 `j.db→h0→h1→…→h9→nowhere`，断言 exit 2 + `distinct filesystem entries` + 目录条目集合不变 | 变异 m56（给上限加 `&& 0`）后 `DAEMON_FAILURES=4`，目录里出现 `nowhere`/`nowhere-wal`/`nowhere-shm` 与 `h0..h9`，证明上限被删后守卫不再拒绝 |
| 自查-2：m55 原点位不可达 | 原点位锚 `sim_main.c:248` 的 `clashes_with_sockets(journal_spelling, uart, control)`。`canonical_entry` 第一步就跑 `realpath`，而 `realpath` **永不返回末级是符号链接的路径**，所以 socket 的规范路径不可能等于 journal 的字面拼写，该子句恒假 | 删除该不可达子句（`sim_main.c:248-255` 改为一段解释注释 + `if (clashes_with_sockets(journal, uart, control)) return false;`），保留 sidecar 的双形态循环（`sim_main.c:256-271`）；m55 重定向为 `m55_link_spelling_sidecars`，锚 `sim_main.c:257` 的 `bases[1] = journal_spelling;`（变异改为 `bases[1] = journal;`） | 新增 `link_spelling_wal` / `link_spelling_shm` 两例（journal=`j.db`→`real.db`，control 分别取 `j.db-wal` / `j.db-shm`）：journal 的字面拼写带 sidecar 与 socket 同条目时必须拒绝。变异体 `DAEMON_FAILURES=8` |
| 自查-3：mutation 锚点行号 | 53 个点位里 46 个 `ref` 的 `文件:行` 与 `old` 片段实际所在行不符（例：m44 标 `sim_main.c:86` 实际在 193），审计无法按标注行复核 | 按「`old` 在源码中唯一出现处」重算全部 `ref`，再随本轮源码变化重算 `sim_main.c` 的 9 个锚点（m44 `:302`、m45 `:475`、m46 `:704`、m47 `:429`、m48 `:400`、m50 `:910`、m51 `:924`、m52 `:110`、m53 `:267`） | `python -B docs/evidence/stage-01/S1-03/mutation-batch.py --check --src .` → `# CHECK_SUMMARY points=56 bad=0`，且每个 `CHECK_OK` 行打印的 `文件:行` 与 `old` 片段实际位置一致 |

受管文件随之变化：`host/simulator/src/sim_main.c`（36600→36909 B / 1061→1065 行）、`tests/contract/device/daemon_smoke.py`（51589→52617 B / 1054→1076 行（1075 个 LF，末行无换行），新增 `link_spelling_wal`/`link_spelling_shm`/`link_hops` 三例，daemon 检查 366→**391**）、`host/simulator/README.md`（8476→8584 B，:22 冲突类别从五类改回四类并说明 sidecar 按两种形态各查一次，:69 检查数 391）。25 个受管文件合计 620932 B，`source-manifest.txt` sha256=`3218c11e7a53d5320e6f6588aa702f00a7f4f93a8924247b04e6789fbd7cdfb6`。

**round-9 审计 PASS 后的非阻塞整改（R9-N-2 / R9-N-3 / R9-N-4）**：`sim_main.c` 的 `paths_are_distinct` 两处注释与 `host/simulator/README.md:22`、`tests/contract/device/daemon_smoke.py:562` 原先都用「SQLite 把 `-wal`/`-shm` 追加在它收到的字面名字上」解释 sidecar 双形态检查；审计在 guest 实测 `j -> real` 只产生 `real-wal`/`real-shm`，这个理由是错的（vendored `host/storage/third_party/sqlite/sqlite3.c:47604-47633` 的 `unixFullPathname` 解析链接）。四处文案统一改为「解析后的目标是数据库文件实际成为的条目，字面拼写是**本模块自己的保守限制、不是 SQLite 要求**」，实现与测试预期不变。`mutation-batch.py` 重算 8 个陈旧 `ref`（m44 `:302`、m45 `:475`、m46 `:704`、m47 `:429`、m48 `:400`、m50 `:910`、m51 `:924`、m53 `:267`），并让 `--check` 真正比较 `ref` 与实测行号（不符即打印 `CHECK_REF_MISMATCH` 且 `bad>0`），不再只打印动态 anchor。`summary.md:185` 的 `sim_main.c:62/86` 改为 278/302，`:205` 的 m49 逐字引用改回 120.08 秒（round-9 非阻塞整改后重跑得 120.11 秒，见 §13），`:270` 补上 m51 是 0 红超时。`tests/contract/device/__pycache__/` 已删除，`evidence-make-manifest.sh` 的 `find` 增加 `! -name '*.pyc' ! -path '*/__pycache__/*'`。整改后 25 个受管文件合计 **621200 B**，`source-manifest.txt` 2794 B / sha256=`7af887a219586547f7798dd20602070f94230aec3c99d0919a4e38a2fd56860e`；canonical run `20261026-054252` 因 `sim_main.c`、`host/simulator/README.md`、`daemon_smoke.py` 变化而作废；重跑后的 run_id 为 `20261026-095548`，56 点位 mutation 在批次树 `batches/20261026-095548/src` 上重跑（`S103_RUN=20261026-100500`），数字见 §13。

## 13. 下一步

1. **round-9 独立审计已 PASS（0 阻塞 / 4 非阻塞）**，报告 `docs/evidence/stage-01/S1-03/audit-round9.md`；审计机（gpt-6.1-sol / high，15.1 分钟）本轮实际跑 Windows 四项设计检查、25 受管文件与 56 锚点/56 份 raw 独立复算、ssh 到 guest 核验 98 源文件与 18 份 canonical 原件、六个既有 C 测试二进制（1834 checks）、daemon 路径专项（32 + 73 checks）、六个生产 C 文件 `-fsyntax-only -Werror`；全量 configure/build/CTest/ASan/TSan/56 点 mutation 声明为 NOT_RUN，未伪造。
2. round-9 的 4 项非阻塞已按 §12.11 末段整改：R9-N-2 文案理由、R9-N-3 的 8 个 `ref` 与三处文档引用、R9-N-4 的 `__pycache__` 与清单生成器排除规则。R9-N-1（`reply.message_type` 方向位）依 `docs/architecture/module-ownership.md:11` 交契约负责人裁定，本卡不改生成物。
3. 因为 `sim_main.c`、`host/simulator/README.md`、`tests/contract/device/daemon_smoke.py` 与 `mutation-batch.py` 都变了，canonical run `20261026-054252` 与 `source-manifest.txt` 的 `3218c11e…dfb6` **作废**；已按流程重算 manifest（`7af887a2…860e`）、重跑 VMware canonical（`20261026-095548`，`VM_RUN_VERDICT=PASS`）与 56 点位 mutation（`S103_RUN=20261026-100500`，CAUGHT 51 / CAUGHT_TIMEOUT 2 / NOT_CAUGHT 3 / BUILD_FAILED 0），据此本卡已置 **DONE**。
4. 收尾动作（已执行）：S1-03 置 DONE、`report` 指向 `docs/evidence/stage-01/S1-03/summary.md`、`active_task` 置 null、`recommended_next` 置 S1-04；阶段 1 仍为 IN_PROGRESS、`gate_result` 仍 NOT_RUN（门禁卡 S1-07）。本卡 DONE ≠ 阶段 PASS。
5. 不 commit、不 push（仓库本身也不是 Git 仓库，`git status` exit 128）。
