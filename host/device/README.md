# host/device — UART-v1 设备端口（DevicePort）适配器

本模块是**阶段 1 S1-03** 的交付物之一：把 UART-v1 的字节编解码、流式拆帧与 DevicePort 契约实现放在一处。
它不持有任何业务状态，也不提供 HTTP 入口。

- 契约基线：0.2.1（`contracts/contract-source.json`），执行前核对过实际版本，未使用旧副本。
- 状态：S1-03 **IN_PROGRESS**（等独立审计）；本模块不是阶段 1 正式门禁，门禁卡是 S1-07。
- 归属：`docs/architecture/module-ownership.md:25` —— 设备协议负责人；禁止改业务账本、禁止重发可能已发送的 EXECUTE、禁止把 CRC 当身份认证。
- 规格真源：`docs/architecture/uart-v1.md`（帧格式、消息布局、会话/重连屏障规则）；机器真源是 `contracts/uart-layouts.json` 与 `contracts/include/locker_contracts.h`。

## 1. 做什么、不做什么

做：

- UART-v1 帧编解码：magic `4c4b`、version 1、payload_len u16 LE、request_no u32 LE、CRC-16/CCITT-FALSE（覆盖 offset 2 到 payload 末字节，不含 magic 与 CRC 自身）。
- 流式拆帧：单遍扫描、字节级重同步、半帧/粘帧/超长/CRC 错都不越界、不按对端长度分配。
- 11 类请求/响应 body 的逐字段编解码与结构校验（枚举越界、未定义 flag、reserved 非 0、非法 cursor、坏 card_uid、result 非 0 却带 body、HELLO 前缀非 0 等一律 fail closed）。
- 14 个 `locker_device_*` API：`locker_device_{create,bind_sink,open,get_snapshot,execute_once,query_command,fetch_events,ack_persisted,resume_ready,release_context,send_health,time_anchor,close,destroy}`；签名以生成头为准。
- AF_UNIX `SOCK_STREAM` 传输：阶段 1 只接模拟 socket（`locker-device-sim`），`config->simulated == false` 直接 `NOT_READY`。

不做（越界）：

- 不接真实串口、不调真实 HAL、不驱动 GPIO（阶段 2 的 S2-xx）。
- 不判断借还成功、不写业务库、不决定状态迁移（属 S1-04 Core）。
- 不自动重发 EXECUTE；`execute_once` 每物理 command 只发一次，超时后只能 `query_command` 核对。
- 不把 CRC 当作身份认证或抗篡改手段。

## 2. 目录

```text
host/device/
  CMakeLists.txt                       目标 locker_device（STATIC）
  include/locker/device/
    wire.h                             帧常量、frame/stream 结构与拆帧 API（模块私有）
    codec.h                            wire_result 枚举、11 对 request/response union、编解码 API（模块私有）
    transport.h                        AF_UNIX 传输（模块私有）
  src/
    wire.c                             CRC-16 逐位算法、encode_frame、stream_feed
    codec.c                            11 类请求/响应/DEVICE_EVENT 的编解码与校验
    transport.c                        connect/write/read/shutdown
    port.c                             DevicePort 实现：pending 表、reader 线程、镜像状态
```

对外只有 `contracts/include/locker_contracts.h` 里的 DevicePort 原型与 DTO；
`host/device/include/` 下的三个头是协议实现工具头，仅协议适配器、模拟器和正式测试共享，不作为业务模块的产品公开 API。

## 3. 构建接入

根 `CMakeLists.txt:88` 一行 `add_subdirectory(host/device)`。本模块只提供一个目标：

| 目标 | 内容 | 依赖 |
|---|---|---|
| `locker_device` | `codec.c port.c transport.c wire.c` | PUBLIC `locker_common`；PRIVATE `Threads::Threads`；`POSITION_INDEPENDENT_CODE ON`；套 `locker_apply_warnings`（`-Wall -Wextra -Wpedantic`，可加 `-Werror`） |

`-std=c11` 且 `EXTENSIONS OFF`，所以 `transport.c` 顶部显式 `#define _POSIX_C_SOURCE 200809L`。

## 4. 关键实现约定（文档没写死、由本卡裁定的部分）

| 约定 | 内容 | 理由 |
|---|---|---|
| `locker_device_reply_t.message_type` | 带响应标志，即 `request_type \| 0x80`（**待契约负责人裁定**，生成头只声明 `uint8_t`） | 它报告的是**被解码的线上响应类型**，与 sink 回调语义一致；若只放 1..11 会与请求类型号撞车，调用方无法区分方向。本卡不改 `contracts/`，已在 summary §6 与 handoff §12.4 R2-N-4 登记为待裁定项 |
| 响应关联 | pending 保存 session、请求号和类型；HELLO 使用预期 `new_session_id` | 三者全匹配才消费，外来响应不改变镜像或调用 sink |
| 响应结果 | 只有设备 `result=0` 才应用成功 body；错误结果仍投递 | HELLO 失败不建立会话，错误 body 不清空 epoch、boot 或水位 |
| HELLO 身份与能力 | 实际 12 字节 UID 派生 24 位小写 hex，核对 `expected_device_id`；必要能力位为 `0x1f` | 不匹配返回握手 `BAD_PRECONDITION`，不使用配置身份冒充设备；CRC 不提供认证 |
| 输入绑定 | EXECUTE / RESUME / RELEASE 核验 session、epoch、boot、physical_revision；FETCH / ACK 核验设备和 epoch | 不将旧意图或旧游标迁移到当前会话；QUERY 仅收 command_id，历史响应不污染当前会话水位或物理镜像 |
| FETCH 空起点 | 仅 FETCH 接受设备 ID 为空、epoch 为 nil、cursor 为 `(0,0)` 的全空起点；也接受归属完整匹配的起点 | ACK 及 RESUME 的持久游标始终需要设备 ID 和 epoch；半零 cursor 拒绝 |
| `open` 时 `simulated == false` | 返回 `NOT_READY` | 阶段 1 没有物理 UART，如实拒绝而不是假装可用 |
| `get_snapshot` / `time_anchor` 的 request_no | 用调用方给的值 | 契约原型就是这么签的；其余端口用内部自增号 |
| EXECUTE 重发 | 完全没有重发循环 | 结果未知不能自动重放动作（AGENTS.md 纪律） |

## 5. 线程与生命周期

- `locker_device_create` 只分配与初始化两个 mutex、transport 与 stream，不建线程。
- `locker_device_bind_sink` 必须在 `open` 之前调用；适配器复制回调表，`sink.user` 借用到 `close` 成功返回。回调里的 DTO 指针只在本次回调有效，需要保留时复制。回调在 reader 线程执行；回调内 `close` 停收停发并返回 `BUSY`，`destroy` 不释放正在执行的对象，外部线程须再次关闭并销毁。
- `locker_device_open` 成功时：connect（10 ms 重试，总超时 5000 ms；每次尝试都是非阻塞 `connect` + `poll` 到同一单调截止，accept 队列满也不会卡在阻塞 `connect`）→ 生成 `new_session_id` → 起**一个** reader 线程 → 发 HELLO（type 1）。`opened` 之后再次 `open` 返回 `CONFLICT`。
- reader 线程独占 `read`；写、shutdown 和最终 close 使用同一 `write_lock`，join 期间释放此锁供 reader 收尾。写使用非阻塞 send + poll，总上限 1000 ms 实际单调时间；失败锁存需要核对并 shutdown。read 超时 200 ms，EOF 或错误使会话不可用。connected 使用 C11 原子。
- `locker_device_close`：`opened=false` → 置 `reader_stop` → 清 pending → `shutdown(fd, SHUT_RDWR)` 打断阻塞 read → `pthread_join` → 关 fd → reset stream。不重放任何已发 EXECUTE。
- `locker_device_destroy` 在 `close` 成功后销毁 mutex 并释放对象；回调线程不能销毁自身执行上下文。
- 上限为 16 个在途请求；同锁内检查任意类型的请求号唯一并占位，重复号或满表返回 `BUSY`。内部号大于所有此前分配的号。到 `UINT32_MAX` 后必须 close/open 建新会话，拒绝回绕。
- pending 按注入的 `config.clock` 单调时间 5000 ms 有界回收（生产注入真实时钟，测试注入替身时钟；见 `port.c:112`/`:171`）；超时不伪造回复、不判定未执行、不重发 EXECUTE。到期号及更低的号在本会话中禁止重用，防止迟到响应命中新请求；仍可用新号 QUERY / FETCH 核对。应用自己的请求超时仍表示结果未知。
- sink 返回 `BUSY`、`NO_SPACE` 或其他拒绝时，锁存需要重新核对、停止 reader 并 shutdown。此后所有请求（包括动作和健康）返回 `NOT_READY`。适配器不替 Core ACK 未交付事实；明确 close/open 后通过 FETCH / QUERY 恢复，并重新完成持久化、健康和 RESUME 屏障。尚未处理的流字节不视为已交付事实。

## 6. 已知边界（不伪造）

| 项 | 状态 | 说明 |
|---|---|---|
| 真实 UART / USB 转串口 | **NOT_RUN** | 阶段 1 只有 AF_UNIX 模拟 socket；`simulated=false` 直接拒绝 |
| 真实 MCU / HAL | **不属于本模块** | 属阶段 2；本模块只消费字节 |
| CRC 抗篡改 | **不声称** | `uart-v1.md:21` 明确 CRC 不承担身份认证 |
| Linux 编译 / C 测试 / sanitizer | **PASS**（VMware Ubuntu 26.04.1 LTS；run_id 见 `docs/evidence/stage-01/S1-03/vm-s103-run-summary.log` 首行，不在此处写死以免与证据脱节） | 19/19 CTest、`-Werror` 零警告、ASan+UBSan 0 报告、TSan 0 命中；日志 `docs/evidence/stage-01/S1-03/vm-s103-*.log` |
| 32 位 / ARM 交叉编译 | **NOT_RUN** | 本模块只构建 x86_64 主机侧；无 32 位/ARM 交叉工具链，不声称可移植性 |

## 7. 测试与证据

- 单元测试：`tests/unit/device/{test_wire,test_codec,test_sim_journal,test_sim_protocol}.c`
- 契约测试：`tests/contract/device/{test_golden_frames,test_device_port}.c`（黄金帧逐字节、真 socket 端到端）
- 实施报告：`docs/evidence/stage-01/S1-03/summary.md`；交接说明：`docs/evidence/stage-01/S1-03/handoff.md`；源码清单：`source-manifest.txt`；变异日志：`mutation.log`。
- 独立审计：`docs/evidence/stage-01/S1-03/audit.md`（round-1，FAIL）与其后的整改记录。当前调用行为以生成头、本 README 和源码为准。
