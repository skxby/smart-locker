# host/domain — 借还业务核心（Core）

本模块是**阶段 1 S1-04** 的交付物，也是主机侧唯一的借还账本写者与状态机所有者。
它把「申请 / 取消 / 确认 / 核验」「有效借用投影」「柜前证明」「outbox 调度」「事件幂等与重启恢复屏障」
放在一个单所有者循环里，对外只经 `contracts/include/locker_contracts.h` 的 Core 端口暴露。

- 契约基线：0.2.1（`contracts/contract-source.json`），执行前核对过实际版本，未使用旧副本。
- 状态：S1-04 **IN_PROGRESS**（等独立审计）。本模块不是阶段 1 正式门禁，门禁卡是 S1-07。
- 归属：`docs/architecture/module-ownership.md:23` —— 业务核心负责人；禁止包含 CivetWeb / SQLite / HAL 头、禁止调用网络或串口阻塞 IO、禁止让客户端指定真实 actor。
- 规格真源：`docs/stages/stage-00-architecture.md:106-128` 的状态表、`docs/architecture/internal-interfaces.md:114` 的发送/重连屏障规则；机器真源是 `contracts/contract-source.json` 与生成的 `contracts/include/locker_contracts.h`。

## 1. 这个模块做什么、不做什么

做：

- 一个 Core 线程拥有全部业务状态与唯一的 SQLite 写连接；其余线程（HTTP、设备 reader、UI）只能经有界队列提交按值复制的 job。
- 借还状态机：REQUESTED → ACCESS_PENDING → ACCESS_ACTIVE → WAIT_CONFIRM →（COMPLETED / NO_CHANGE / FAILED），异常分支进 REVIEW_REQUIRED，用户取消进 CANCELLED。
- 柜前证明（presence）15 秒有效期与一次性消费（`proof_consumed`），过期即失效并进核验。
- outbox 调度：只有 `locker_repo_commit_change` 成功之后才把**本进程实例新创建**的 outbox 交给设备适配器；写串口前先落「已尝试发送」检查点。
- 事件幂等与恢复屏障：按 `journal_epoch` + `(boot_id, event_seq)` 去重推进水位；重启后先核对、可能已发送的动作一律进核验，不恢复发送队列。
- 进度样本：真实 Core 循环完成一次出队+泵之后才发布 instance/counter/sample 副本，`locker_core_get_progress` 非阻塞读取。

不做（越界）：

- 不含 SQLite、CivetWeb、cJSON、HAL 头，也不认识它们的实现类型；仓储与设备都按契约端口借用。
- 启用 `LOCKER_BUILD_NETWORK` 时，S1-05 的登录和令牌认证在 Core 线程执行，使用 libsodium Argon2id 验证口令、SHA-256 摘要比对随机会话令牌；未启用时认证返回 `NOT_READY`。当前尚未接入 HTTP/IPC。
- 不实现 supervisor 健康端口：`locker_core_apply_supervisor_health` 一律返回 `NOT_READY`，归 S4-04；本卡不提供空实现冒充。
- 不接视觉：`vision_policy` 只接受 `DISABLED_FOR_FOUNDATION`，其余返回 `NOT_READY`，归后续视觉卡。
- 不自动重放任何可能已发送的动作；结果未知只查询、不补发。

## 2. 目录

```text
host/domain/
  CMakeLists.txt                 目标 locker_domain（STATIC）
  src/
    domain_internal.h            模块私有总头：常量、结构体、跨文件声明（不对外）
    domain_core.c                生命周期、Core 线程、恢复屏障、对外端口
    domain_commands.c            create/confirm/cancel/resolve、presence、outbox pump、超时
    domain_device.c              设备端口调用、事件/响应应用、物理证据推进
    domain_state.c               持久化记录的读/写、投影、响应与发布
    domain_operation.c           操作工作副本、状态优先级、允许动作
    domain_docs.c                各类 schema 记录（operation/outbox/audit/loan/tool/…）的 JSON 写出
    domain_repo.c                仓储端口薄封装 + 完成槽（completion）管理
    domain_change.c              事务变更缓冲（put/delete/commit）
    domain_import.c              事件导入分页、ACK 水位、设备状态持久化
    domain_loan.c                借用（loan）投影的建/结
    domain_text.c                文本/UUID 工具、状态名与优先级、时间推进
    domain_json.c  domain_json.h          自带的受限 JSON 读写器（不依赖 cJSON）
    domain_sha256.c  domain_sha256.h      SHA-256（请求指纹用）
    domain_time.c  domain_time.h          UTC 时间戳格式化/解析
    domain_time_anchor.c                 时间锚定请求与应用
```

本模块**不提供模块私有公开头**：`CMakeLists.txt:27` 的 `include` 目录是按统一目标模板保留的空位，
对外只有 `contracts/include/locker_contracts.h` 里的 Core 端口与 DTO。
`host/domain/src/` 下的头仅供本模块与 `tests/unit/domain/` 使用，别的模块不得包含。

## 3. 构建接入

根 `CMakeLists.txt:90` 一行 `add_subdirectory(host/domain)`，`:97` 一行 `add_subdirectory(tests/unit/domain)`。
本模块只提供一个目标：

| 目标 | 内容 | 依赖与设置 |
|---|---|---|
| `locker_domain` | 15 个 `src/*.c` | PUBLIC `locker_common` + 契约头目录；PRIVATE `Threads::Threads` 与 `src`；`POSITION_INDEPENDENT_CODE ON`；套 `locker_apply_warnings`（`-Wall -Wextra -Wpedantic`，可加 `-Werror`） |

`-std=c11` 且 `C_EXTENSIONS OFF`，所以用到 POSIX 线程与 `clock_gettime` 的源文件在顶部显式 `#define _POSIX_C_SOURCE 200809L`。

## 4. 关键实现约定（文档没写死、由本卡裁定的部分）

| 约定 | 内容 | 理由 |
|---|---|---|
| 状态只进不退 | `domain_operation_set_state` 用 `domain_state_rank` 比较：`to_rank > from_rank` 才允许，终态不可再迁 | 已经见过 PULSE_STARTED（ACCESS_ACTIVE）之后迟到的 COMMAND_ACCEPTED 不能把状态拉回 ACCESS_PENDING |
| 待发送命令不可取消 | ACCESS_PENDING / ACCESS_ACTIVE 的允许动作只有 `WAIT` | 命令可能已被接纳，此时取消会把结果变成未知 |
| 证明一次性 | 校验通过后置 `proof_consumed`；`armed` 前要求 `proof_consumed` 为真且 `now - proof_ms <= LOCKER_PRESENCE_TTL_MS`(15 s) | 同一份柜前证明不能开两次门、也不能留到下一槽复用 |
| 检查点先于发送 | outbox 先写 `SEND_ATTEMPTED` 并提交，失败即 `require_review("CHECKPOINT_FAILED")` 且**绝不发送** | 部分字节也算结果未知，不能出现「发了但没记」 |
| 发送前四项一致性 | boot_id、journal_epoch、physical_revision、`not_after_ms`(2000 ms) 任一不符就进核验 | 旧会话/旧物理状态下的意图不得迁移到新会话 |
| 超时只查询 | `send_started` 且超过 `LOCKER_HOST_TIMEOUT_MS`(5000 ms) 未终局时发一次 `query_command`，同一命令只查一次 | 事务语义要求「不自动重发 EXECUTE」，查询后重新起算等待窗口 |
| 终态由证据定 | 只有「见过开门 + 门已关 + 物品状态符合方向」才进 WAIT_CONFIRM；门关但物品没变进 NO_CHANGE | 「有当前离位快照」不等于开门/物品变化的完整证据 |
| 重启一律进核验 | 恢复出的 ACCESS_PENDING / ACCESS_ACTIVE 直接置 REVIEW_REQUIRED + `RECOVERED_AFTER_RESTART` 并落库 | `internal-interfaces.md:114`：不恢复发送队列 |
| action_no 单调 | 恢复后 `next_action_no = action_watermark + 1`，为 0 时取 1 | 模拟设备要求 `action_no == watermark + 1`，复用旧号会被 REPLAY_BLOCKED |
| 单活动操作 | `recover_operations` 只接第一个非终态操作，其余留在仓储 | `slot_guards` 只有一个 key `"1"`，与本阶段的单柜格物理模型一致 |
| 环满不打断设备 | 设备 sink 在 reader 线程只 `try_push`，环满置原子溢出标志并返回 `OK` | 返回 `NO_SPACE` 会让设备适配器 shutdown 并锁存不可用；溢出由 Core 补取 |
| 私有 wire 别名 | `domain_internal.h:38-75` 用 `#define` 把契约里的 wire 枚举映射到本地名 | 核心不得包含 `host/device` 的私有头 |
| 无明文身份外泄 | 投影与日志只写 `actor_id` 等 UUID，不写 UID 明文、token_hash 或 `arguments_json` 原文 | 契约与脱敏要求 |

## 5. 构建与测试

```bash
# 构建目录必须在源码树之外；VM 上的 canonical 布局是
# $HOME/locker-workspace/build/S1-04/<批次>，源码在 .../S1-04/batches/<批次>/src。
build="${HOME}/locker-workspace/build/S1-04/manual"
cmake -S . -B "$build" \
      -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
cmake --build "$build" -j2
ctest --test-dir "$build" --output-on-failure
```

`-DLOCKER_BUILD_TESTS=ON` 是必需的，否则根 CMake 不会 `enable_testing()`，CTest 报 `No tests were found`。
开发期只想编一个测试文件时加 `-DLOCKER_DOMAIN_TESTS_FILTER=<name>`（见 `tests/unit/domain/CMakeLists.txt:21`）。

本模块贡献 7 个 CTest（`domain.test_domain_text` / `_json` / `_repo` / `_state` / `_device` / `_commands` / `_core`），
每个测试文件一个可执行、一个 CTest 用例，非零退出是唯一失败信号，单例 `TIMEOUT 120`。
断言数与双环境结果见 `docs/evidence/stage-01/S1-04/summary.md`。

## 6. 线程与生命周期

- `locker_core_create` 只分配、初始化队列/完成槽/mutex/cond，不建线程、不碰仓储。
- `locker_core_start` 先 `domain_device_bind`（把设备 sink 接到事件环与响应环），再起**一个** Core 线程；线程入口先跑 `domain_recover`，成功才把 phase 置 `READY`，失败保持 `RECOVERING` 并让所有写命令回 `NOT_READY`。
- 队列语义：`locker_core_submit` 申请完成槽后投 job，满载返回 `BUSY`；回执给调用方的是 `token`，结果必须用 `locker_core_wait_result` 按 token 取，**token 不重用**。
- `locker_core_query` 是唯一会阻塞的对外入口，最多等 `LOCKER_CORE_RESULT_WAIT_MS`(1000 ms)。
- `locker_core_get_progress` 只取 `progress_mutex` 下的副本，**非阻塞**、不经过 Core 等待队列；Core 尚未发布过样本时返回 `NOT_READY`，不合成自增计数。
- `locker_core_stop` 置停止标志 + 投 `DOMAIN_JOB_STOP` + 轮询 `thread_exited`（每 5 ms，上限 `timeout_ms` 或 `LOCKER_HOST_TIMEOUT_MS`）。**超时不 terminate 线程，返回 `BUSY`** —— 超时不等于取消。
- `locker_core_destroy` 若对象仍在运行会先 `locker_core_stop`，成功后才释放；契约 `:285` 要求适配器回调全部停止之后才可以 destroy。
- 设备 sink 回调在设备的 reader 线程执行，只做有界 `try_push`；Core 线程是唯一出队应用者。

## 7. 已知边界（不伪造）

| 项 | 状态 | 说明 |
|---|---|---|
| 真实 MCU / 串口 / GPIO | **NOT_RUN** | 阶段 1 只用 `locker-device-sim` 的 AF_UNIX 通道；锁脉冲硬截止属阶段 2 |
| 真实身份校验（Argon2id） | **IMPLEMENTED** | S1-05 提供登录、token哈希、角色/启用复检、原会话撤销及用户/卡管理；最终整卡验证见S1-05报告 |
| supervisor 健康端口 | **NOT_IMPLEMENTED** | 恒返回 `NOT_READY`，S4-04 负责；不用空实现冒充 |
| 视觉融合 | **NOT_IMPLEMENTED** | 只接受 `DISABLED_FOR_FOUNDATION`，其余 `NOT_READY` |
| 多柜格并发操作 | **NOT_IMPLEMENTED** | `slot_guards` 只有 key `"1"`，本阶段单柜格 |
| 32 位 / ARM 交叉编译 | **NOT_RUN** | 只在 x86_64 Linux 验证 |
| 断电持久性 | **NOT_RUN** | 只验证到 SQLite 事务语义，不做掉电注入 |

## 8. 相关文档

- 实施报告与证据：`docs/evidence/stage-01/S1-04/summary.md`
- 调用约定/失败路径/复现方法：`docs/evidence/stage-01/S1-04/handoff.md`
- 设计评审输入（非验收）：`docs/evidence/stage-01/S1-04/design-review.md`
- 状态表与架构：`docs/stages/stage-00-architecture.md:106-128`
- 发送与重连屏障规则：`docs/architecture/internal-interfaces.md:114`、`:201-205`
- 模块边界：`docs/architecture/module-ownership.md:23`、`:49-53`
- 契约真源与生成物：`contracts/contract-source.json`、`contracts/include/locker_contracts.h`

S1-05 操作适配需要区分幂等冲突与版本冲突：Core 在幂等键异意图时保留 CONFLICT 状态，并在结果 JSON 返回契约错误信封 IDEMPOTENCY_CONFLICT 和本次 request_id；不会写仓储或创建新操作。成功重放仍返回原响应。

S1-05 的用户/卡管理、审计和幂等结果由Core同事务写入；账号停用或角色变化撤销原会话，重新启用不恢复旧token。最后启用管理员受保护。用户上限100、每账号会话上限16，过期或撤销记录在该账号下次登录时清理。loans 查询返回持久化历史并按借用者过滤，events 从设备事实与人工审计投影；分页与当前快照版本绑定。HTTP/IPC只消费公开端口，命令字段在Core再次校验。
