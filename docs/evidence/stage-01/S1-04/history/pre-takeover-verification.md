# S1-04 验证附录

本文件是 `summary.md` 的验证附录，承载逐文件清单、实现结构、验收项映射、中断矩阵与设计检查明细。主报告按 AGENTS.md 的 P3 规则控制在 10 KB 以内。

## A. 修改文件清单


| 路径 | 行/字节 | 说明 |
|---|---|---|
| `CMakeLists.txt` | 100 / 4200 | **仅加**：`:90 add_subdirectory(host/domain)`、`:97 add_subdirectory(tests/unit/domain)`、`:99 add_subdirectory(tests/integration)`。其余未动 |
| `host/domain/CMakeLists.txt` | 40 / 1110 | 目标 `locker_domain`（STATIC）；PUBLIC `locker_common`+contracts，PRIVATE `locker_storage`/`locker_device`/`Threads::Threads` |
| `host/domain/README.md` | 138 / 11719 | 模块说明：目录、线程模型、生命周期、outbox 与恢复屏障、已知边界 |
| `host/domain/src/domain_internal.h` | 835 / 43298 | `locker_core_t` 全量字段、purpose 枚举、内部原型（模块私有） |
| `host/domain/src/domain_core.c` | 984 / 40264 | 单所有者循环、队列/事件环/应答环、`create/start/stop/destroy/tick/submit/wait_result`、恢复（含 `recover_tools`） |
| `host/domain/src/domain_commands.c` | 2124 / 104443 | 12 个命令/查询处理器、outbox 泵、释放泵、超时矩阵、presence、maintenance、幂等 |
| `host/domain/src/domain_device.c` | 1100 / 51934 | 设备泵（快照/fetch/ack/health/HELLO）、应答落地、事件落地、动作发送、物理冲突检测、快照接收门 |
| `host/domain/src/domain_import.c` | 243 / 11057 | 事件页导入、身份学习、连续性派生、device_state 落库 |
| `host/domain/src/domain_state.c` | 841 / 31101 | 状态投影、响应信封三件套、document 解析、进度发布 |
| `host/domain/src/domain_docs.c` | 508 / 22762 | 受控集合文档写出（operation/loan/tool/device_state/import_cursor/idempotency/outbox/slot_guard/audit） |
| `host/domain/src/domain_operation.c` | 173 / 6086 | 状态迁移表与持久化 |
| `host/domain/src/domain_loan.c` | 181 / 7985 | 借用账本投影（prepare/check/apply/commit_in_memory 四段，与终结同事务） |
| `host/domain/src/domain_repo.c` | 525 / 20270 | 仓储调用封装、completion 槽（token 身份）、恢复页读取 |
| `host/domain/src/domain_change.c` | 173 / 6623 | 事务变更缓冲（`expected/new_snapshot_revision` + mutations，checks 必须在 mutation 之前） |
| `host/domain/src/domain_json.c` / `.h` | 992+108 | 零依赖 JSON 解析/writer（有界、不 malloc） |
| `host/domain/src/domain_text.c` | 363 / 9188 | 文本/UUID/u64 转换、`domain_now`/`domain_now_advance` |
| `host/domain/src/domain_time.c` / `domain_time.h` | 198+21 | RFC3339 UTC 时间格式化/解析 |
| `host/domain/src/domain_time_anchor.c` | 125 / 4816 | 时间锚（与 MCU 时钟对齐协商标量，2 秒节流） |
| `host/domain/src/domain_sha256.c` / `.h` | 156+36 | 幂等 request_hash 用的 SHA-256 |
| `tests/unit/domain/CMakeLists.txt` | 38 / 1495 | 七个 C 测试，CTest 名 `domain.*` |
| `tests/unit/domain/domain_test_support.h` | 1052 / 40043 | 夹具：真实 sqlite 临时库 + 假仓储/假设备/假时钟 |
| `tests/unit/domain/test_domain_text.c` | 371 / 18906 | 文本/UUID/u64 边界（213 检查） |
| `tests/unit/domain/test_domain_json.c` | 473 / 19759 | 解析/writer 边界（159 检查） |
| `tests/unit/domain/test_domain_repo.c` | 629 / 28814 | 仓储封装与 completion 槽（336 检查） |
| `tests/unit/domain/test_domain_state.c` | 1179 / 61136 | 投影/信封/导入/落库（368 检查） |
| `tests/unit/domain/test_domain_device.c` | 1797 / 90564 | 快照/fetch/ack/execute/query/事件/释放/确认依据代际（923 检查） |
| `tests/unit/domain/test_domain_commands.c` | 2080 / 107663 | 12 个处理器 + 超时 + outbox + 幂等 + 审计 + 证明期限（623 检查） |
| `tests/unit/domain/test_domain_core.c` | 800 / 34519 | 生命周期/队列/恢复（217 检查） |
| `tests/integration/CMakeLists.txt` | 51 / 2128 | 一个真实 socket 端到端测试，TIMEOUT 600 |
| `tests/integration/test_core_interrupt_matrix.c` | 1566 / 59029 | 真实 unix socket + 真实 sqlite + 模拟器端到端（95 检查，8 个用例） |

**未修改**：`contracts/`、`scripts/`、`host/common/`、`host/storage/`、`host/device/`、`host/simulator/` 的任何源文件，以及其它测试目录。本卡只消费它们的既有接口。**行数统计口径**：按 `\n` 计数（LF，`CR=0`），含文件末尾换行。

## B. 实现结构、处理器与状态机

```text
locker_core_create/start
  └── core_thread_main (唯一所有者线程)
        ├── 队列: DOMAIN_JOB_COMMAND / QUERY / TICK / PRESENCE / MAINTENANCE
        ├── 事件环: locker_device_event_t (256)
        ├── 应答环: locker_device_reply_t (256)
        └── core_run_once:
              1. drain 命令队列
              2. drain 事件环
              3. drain 应答环
              4. core_tick_internal:
                   domain_now_advance → domain_timeouts
                   → domain_release_pump
                   → (READY) [证明新鲜则 operation_arm] → domain_outbox_pump
                   → domain_device_pump
                   → domain_anchor_request
                   → domain_progress_publish
                   → domain_completion_reap
```

- 所有对外 API 都把请求入队后由 Core 线程处理，**没有任何路径在调用者线程直接读改写仓储或设备**。
- `host/http`/`ipc`/`ui` 尚未存在（S1-05/S1-06），因此「客户端、HTTP 工作线程和 UI 不直接写账本或 GPIO」在本卡体现为：Core 是 `host/domain` 内唯一的仓储写者与唯一设备发送者，且这两个动作在同一线程内顺序发生。

### 命令与查询处理器

| 类型 | 处理器 | 关键前置 |
|---|---|---|
| 命令 | `command_operation_create` | `phase==READY`、`tool_valid` 且工具 ID/revision 相符、BORROW 需 `physical_ready_for_action` + `item==PRESENT`、RETURN 需 `loan_open` + 本人 + `item==ABSENT`；幂等键必填 |
| 命令 | `command_operation_confirm` | `state==WAIT_CONFIRM`、`revision` 匹配、重验物理 + `item_transition_seen`、**决策点复核**（冻结依据的 boot/epoch/版本/物品/门仍成立，且未因写库失败而闭锁）；终结 + 账本 + 幂等同一事务 |
| 命令 | `command_operation_cancel` | `revision` 匹配、`reason` 必填（按码点计 1–256 字符）；REQUESTED 直接 CANCELLED 并写 `AuditRecord(OPERATION_CANCELLED)`；终结 + 幂等 + 审计同一事务 |
| 命令 | `command_operation_resolve` | admin peer、`reason` ≥5 字符（按码点计，截断也按码点边界）、`state==REVIEW_REQUIRED`、物理证据新鲜（≤30 s）、resolution 与物品/基线相符；终结 + 账本 + 审计 + 幂等同一事务 |
| 查询 | `query_operation_get` / `query_list` / `query_loans` / `query_events` / `query_reviews` / `query_device_get` / `query_tools` / `query_snapshot` | 只读，`data` 直接是契约资源；`OPERATION_GET` 限本人或 ADMIN，`REVIEWS_LIST` 限 ADMIN，`TOOL_GET` 在工具投影不可知时返 INVALID |

### 状态机（与 `docs/stages/stage-00-architecture.md` 一致）

```text
REQUESTED ──presence(到柜证明)──> ACCESS_PENDING("ARMED")
    │                                   │
    │ cancel                            │ EXECUTE 应答 PULSE_STARTED 或事件 PULSE_STARTED
    ▼                                   ▼
CANCELLED                          ACCESS_ACTIVE
                                        │
                    门开→门关 + 物品变化（operation_advance_from_evidence）
                                        ▼
                                   WAIT_CONFIRM("PHYSICAL_CONFIRMED")
                                        │ 本人 confirm
                                        ▼
                                   COMPLETED("CONFIRMED")
                  任意状态 ──超时/拒绝/未知──> REVIEW_REQUIRED(<reason_code>)
```

`REVIEW_REQUIRED` 的 reason_code 取值：`COMMAND_REJECTED`、`UNKNOWN_RESULT`、`QUERY_UNRESOLVED`、`OPEN_TIMEOUT`、`CLOSE_TIMEOUT`、`CONFIRM_TIMEOUT`、`CONFIRM_PRECONDITION`、`PHYSICAL_UNKNOWN`、`PHYSICAL_MISMATCH`、`NO_ITEM_CHANGE`（→`NO_CHANGE` 终态）、`PRESENCE_EXPIRED`、`PREPARE_DEADLINE`、`ANCHOR_MISSING`、`SESSION_CHANGED`、`PHYSICAL_CHANGED`、`SEND_FAILED`、`CHECKPOINT_FAILED`、`RECOVERED_AFTER_RESTART`、`REQUEST_EXPIRED`、`CANCEL_AFTER_ACTION`、`EVENT_GAP`。

## C. 验收项逐条映射


| 卡片验收项 | 实现位置 | 测试证据 |
|---|---|---|
| 正常借还 | `command_operation_create` → `operation_arm` → `apply_execute` → `operation_advance_from_evidence` → `command_operation_confirm` → `domain_loan_apply` | `integration.test_core_interrupt_matrix::test_borrow_flow`（create→REQUESTED→presence→ACCESS_PENDING/ARMED→EXECUTE→开门取物关门→WAIT_CONFIRM→CONFIRM→COMPLETED/CONFIRMED→loan `closed_at:null`）；`test_domain_commands.c` 的 create/confirm 用例 |
| 只开门不取物 | `operation_advance_from_evidence` 的 `expected_item_for` 未变 → `NO_CHANGE("NO_ITEM_CHANGE")` | `test_domain_device.c::test_event_cursor_rules` + `test_domain_commands.c` 的 NO_CHANGE 断言 |
| 提前确认 | `command_operation_confirm:514-518` `state != WAIT_CONFIRM` → CONFLICT | `integration::test_wrong_principal`（断言 `result.status == CONFLICT`）；`test_domain_commands.c` 的提前 confirm 用例 |
| 错误用户 | `command_operation_confirm:505-509` `actor_id != principal.user_id` → INVALID；`command_operation_resolve` 要求 `verified_admin_peer` | `integration::test_wrong_principal`；`test_domain_commands.c` 的 actor 不符用例 |
| 两个申请竞争 | 单所有者线程 + `idempotency_lookup` + 仓储事务，第二条返回 BUSY | `integration::test_competing_requests`（第二条 create 必 BUSY，被拒不留痕）；`test_domain_commands.c::test_create_success` 的 BUSY 段 |
| 15 秒证明过期 | `presence.proof` TTL 15s → `require_review("PRESENCE_EXPIRED")` + CONFLICT；动作期限另受**证明剩余时间**约束（`not_after = mcu_now + min(证明剩余, 2 s)`），证明快到期时不会再多给 2 秒 | `test_domain_commands.c` 的 presence 过期用例；`test_arm_deadline_bounded_by_proof`（t=15000 时 `not_after == 15000+1000`） |
| 各业务超时 | `domain_timeouts`：REQUESTED 120s → `REQUEST_EXPIRED`；ACCESS_PENDING 30s → `OPEN_TIMEOUT`；ACCESS_ACTIVE 未开门 30s → `OPEN_TIMEOUT`、开门后 120s → `CLOSE_TIMEOUT`；WAIT_CONFIRM 120s → `CONFIRM_TIMEOUT` | `integration::test_interrupt_pulse_suppressed`（30s → OPEN_TIMEOUT）；`test_domain_commands.c::test_timeouts`（四条超时 + 查询补救 + 槽复位） |
| UNKNOWN 输入有确定结果 | EXECUTE 应答 `UNKNOWN_RESULT`/`LOG_UNAVAILABLE` → `REVIEW_REQUIRED("UNKNOWN_RESULT")`；无 body 失败应答按 result 映射 | `integration::test_interrupt_before_pulse` 的 5 个注入阶段（BEGIN/INTENT/EVENT/COMMIT/RESULT 全部 → `REVIEW_REQUIRED/UNKNOWN_RESULT`）；`test_domain_device.c::test_execute_response` 的 unknowns 段 |
| 仅 ACK 不进入开门 | `apply_ack` 只推进 ACK 水位，不触碰操作状态；`apply_execute` 只在 `result==OK && command_state==PULSE_STARTED` 时推进 ACCESS_ACTIVE | `test_domain_device.c::test_execute_response` 的 ACCEPTED 不推进段（`!execute_in_flight`、`publish_calls==0`）；`integration::test_boot_and_session` 的 `ack_requests>=1` 与仍 RECONCILING 断言 |
| 物理变化与本人确认都满足才完成 | `command_operation_confirm:519-525` 同时要求 `physical_ready_for_action`、`item == resulting_item_for_kind`、`item_transition_seen` | `integration::test_borrow_flow`（缺任一项则 `CONFIRM_PRECONDITION`）；`test_domain_commands.c` 的 confirm-precondition 用例 |
| 历史读卡不开柜 | `domain_event_apply` 对 `!live_transport` 事件只锁存缺口、不推进操作（`live_transport` 由 `host/device/src/port.c:387` 置真，仅实时帧为真） | `test_domain_device.c::test_event_cursor_rules` 的 `live_transport=false` 段（`!continuity_ok && fetch_gap`，水位仍推进但操作不动） |
| 重启后先核对，可能执行过的动作不重放 | `domain_recover` 顺序 `refresh_revision → operations → loans → tools → import_cursor → device_state`；ACCESS_PENDING/ACCESS_ACTIVE/**WAIT_CONFIRM** 恢复为 `REVIEW_REQUIRED("RECOVERED_AFTER_RESTART")`，且**恢复核验落库失败即整体失败**（不置 `recovery_ready`）；`next_action_no = action_watermark + 1`；终结后崩溃时凭快照上报的 `active_operation_id` 补登记释放 | `integration::test_interrupt_restart_barrier`（重启后 `REVIEW_REQUIRED/RECOVERED_AFTER_RESTART`、`execute_requests` 不变）；`test_domain_core.c::test_recover_operations`；`test_domain_device.c::test_release_after_restart` |
| 健康基线接口 | `domain_progress_publish` 每 tick 发布 instance/counter/sample 副本；`locker_core_get_progress` 非阻塞读取锁内副本 | `test_domain_core.c` 的 progress 用例（同 counter 重取不续命） |


## D. 中断位置矩阵

「准备→发送→ACK→脉冲→落库」中断位置矩阵

卡片要求「对中断位置记录预期和结果」。矩阵用模拟器的五阶段故障注入（`locker_sim_control_fail_next`：BEGIN/INTENT/EVENT/COMMIT/RESULT）实现，每个阶段让模拟器在该步写日志失败并回 `LOG_UNAVAILABLE`：

| 中断位置 | 模拟器返回 | Core 的预期与实测 | 是否重放 | 证据 |
|---|---|---|---|---|
| BEGIN（事务开始前） | `LOG_UNAVAILABLE` | `REVIEW_REQUIRED("UNKNOWN_RESULT")` | 否 | `test_interrupt_before_pulse` stage=BEGIN |
| INTENT（命令写入前） | `LOG_UNAVAILABLE` | 同上 | 否 | stage=INTENT |
| EVENT（事件写入前） | `LOG_UNAVAILABLE` | 同上 | 否 | stage=EVENT |
| COMMIT（提交） | `LOG_UNAVAILABLE` | 同上 | 否 | stage=COMMIT |
| RESULT（结果状态更新） | `LOG_UNAVAILABLE` | 同上 | 否 | stage=RESULT |
| 应答丢失（`drop_execute_ack`） | 请求已执行但应答不回 | 5s 后 Core 发 `QUERY_COMMAND` 核对；`pulses` 保持 1，`execute_requests` 不增 | **否**（只查询不重发） | `test_interrupt_ack_lost` |
| 脉冲被抑制（`suppress_pulse`） | 命令 `ACCEPTED` 但不发脉冲 | 停在 ACCESS_PENDING，30s 后 `REVIEW_REQUIRED("OPEN_TIMEOUT")` | 否 | `test_interrupt_pulse_suppressed` |
| 主机重启（借出已武装） | — | 恢复为 `REVIEW_REQUIRED("RECOVERED_AFTER_RESTART")`，不重发 EXECUTE | **否** | `test_interrupt_restart_barrier` |

五个注入阶段在 r192000 之前全部失败（Core 拿不到 body 全零的失败应答里的 `command_id`/`operation_id`，被 ID 匹配门静默拦下），修复后全部通过——这是本卡的一个真实产品缺陷，详见 §7。

### 5.5 事件连续性派生（本卡的关键不变量）

`physical_ready_for_action` 要求 `cursor.continuity_ok && !fetch_gap`；`operation_advance_from_evidence` 在 `ACCESS_ACTIVE` 门之后、证据判定之前再加同一道缺口门（`!continuity_ok || fetch_gap || !history_complete` → `REVIEW_REQUIRED("HISTORY_INCOMPLETE")`），否则「锁存了缺口但没人读」等于没锁。这两个位的来源必须闭合：

- `domain_import_page` 成功导入后，只有「仓储判定连续（`import_continuity`）」且「本会话没有已锁存缺口（`!fetch_gap`）」才把 `cursor.continuity_ok` 置真；否则一律锁死（`HISTORY_INCOMPLETE` 一旦成立不得被后续连续页洗白，`uart-v1.md:50`）。
- **三个独立入口都要看 `observation_flags` bit2**：实时事件 `event->flags`（`domain_event_apply`）、补取页里任一事件的 `record->flags`（`domain_import_page`）、快照 `snapshot->flags`（`apply_snapshot`）。只做快照那一路时，设备报「我丢了事件」主机照旧按序号连续推进到 `WAIT_CONFIRM`。
- `device_state.physical.continuity_ok` 与 `durable_cursor` 同步取同一份事实，不允许「动作准入读真、落库写假」。实时事件不带累计丢失数，所以 `apply_physical_event` **不写** `dropped_events`（写死 0 等于每来一条事件就抹掉「已知丢过 N 条」）。
- FETCH 的 `after` 取**仓储水位 `import_cursor`**，不是视图游标 `cursor.through`。live 事件只推进 `cursor.through` 不落仓储，若用视图游标取 `after`，sim 会返回与仓储水位不相邻的页 → 仓储 CONFLICT → 误锁缺口。这是本卡修掉的第二个真实缺陷（§7）。


## E. 三项设计检查明细


| 检查 | 结果 |
|---|---|
| `python scripts/check_contracts.py` | `"status": "PASS"`，contract_version 0.2.1，public_routes 24、internal_ports 82、private_rpc_methods 28、resolved_schema_refs 453、uart_requests 11、`hardware_profile: "UNVERIFIED"`、`business_or_hardware_tested: false` |
| `python scripts/check_stage_plan.py` | `"status": "PASS"`，task_cards 45、active_task S1-04、`implementation_cards_done: 3`、`runtime_or_hardware_tested: false` |
| `python scripts/check_vision_contracts.py` | `"status": "PASS"`，vision_schema_refs 58、`camera_owned: false`、`model_inference_tested: false`、`hardware_tested: false` |

三项检查在 round-4 整改完成、canonical 推进到 `r204800` 后重跑，结果与上表一致，输出原件已按同一次运行覆盖。

这三项**只验证设计产物一致性**，不证明业务实现或真机可用。输出原件见本目录 `check_contracts.json`（1064 B）、`check_stage_plan.json`（642 B）、`check_vision_contracts.json`（1148 B）。

## F. 契约与真源指纹（开工时与收尾时一致）

本卡全程未修改下列任何文件。开工时从 `contracts/contract-source.json` 读取契约版本 0.2.1 与实际一致，未用旧副本；收尾时重算 SHA-256 仍与开工时相同，证明整改只落在授权目录内。**唯一例外**：`AGENTS.md` 由用户在本卡期间追加了「全自动开发-审计循环」与「版本控制」两节，其指纹因此与开工时不同（`8dc09b1c…` / 11511 B），不属本卡改动。

| 文件 | SHA-256 | 字节 |
|---|---|---|
| `contracts/contract-source.json` | `14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f` | 66553 |
| `contracts/include/locker_contracts.h`（生成物） | `34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a` | 39925 |
| `contracts/internal-data.schema.json` | `e7b8695c49115e2994cf126dc5ab629e5c82460e70d4c292b19576f55c60e386` | 129569 |
| `contracts/openapi.json`（生成物） | `94eb16430c075ca5285d4bf729b4517e30c7d0d3259d554c96880e9eabf70e61` | 161956 |
| `contracts/uart-layouts.json` | `28fb958dc8905a55912be16fefeb5f13d1f8c0104008cba1e6adffee26cac0bd` | 15245 |
| `docs/architecture/internal-interfaces.md` | `a5b3cdc41c05006e83edc970c46c15f79644adb8fe071b71953f239a64c1c2cf` | 29792 |
| `docs/architecture/uart-v1.md` | `5bb2204f291b259f7b28cc7a2a1473352d5f1d0d501032168924d664c5e3ed8f` | 26022 |
| `docs/architecture/module-ownership.md` | `b182fe6e424b605fd4cad1073c85178588ee474ca8ab6f38be84e3129f72bb3f` | 10135 |
| `docs/stages/stage-01-task-cards.md` | `6a84204513ece4deb9fa1ec632580378eecb44a063b7487ba9b864c3e7016473` | 16511 |
| `config/development/stage-plan.json` | `ebef6ddc531ef44532e725589eb6aa49d8cf7bebd6ea39b67604e8acc94a02ee` | 24809 |
| `README.md` | `11d053f43d352699629563d03aa5da9daa7dbf8c379e29be67f5abec5a7fda3c` | 8905 |
| `AGENTS.md`（用户在本卡期间追加两节） | `8dc09b1c7d2eec5e8d5cbbdeead3ff017aecbeb23ad723fa197f921f4cfb90ff` | 11511 |

本卡源码的逐文件 SHA-256 见同目录 `source-manifest.txt`（81 项，覆盖 `host/domain`、`host/common`、`host/storage`、`host/device`、`host/simulator`、`tests/unit/domain`、`tests/integration`、`contracts/include` 与根 `CMakeLists.txt`）。Windows 工作树与 VM `batches/r204800/src` 两侧清单一致。

## G. 构建与测试口令、逐测试断言数

**VMware guest（唯一 Linux 验收环境）**，`run_id=r204800`，源码经 `tar` 快照（`s104src.tar`）传到 VM 独立批次目录，同一份源码连跑三棵树：

```text
SRC=$HOME/locker-workspace/S1-04/batches/r204800/src
LOGS=$HOME/locker-workspace/logs/S1-04/r204800
# 普通树
cmake -S "$SRC" -B "$SRC/../build" -DCMAKE_BUILD_TYPE=Debug \
      -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
cmake --build "$SRC/../build" -j2
(cd "$SRC/../build" && ctest --output-on-failure)
# ASan + UBSan（独立 build-asan 目录，其余同上）
-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
# TSan（独立 build-tsan 目录，其余同上）
-DCMAKE_C_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g"
```

三棵树由同一份脚本 `.s104-tmp/full-verify.sh` 在同一 run 目录内连续跑完，源码只解包一次，各自独立 build 目录与全量 CTest；`environment.log` 记录 `PLAIN`/`ASAN`/`TSAN` 三行 `configure build ctest reports` 退出码四元组，本轮三行都是 `0 0 0 0`。

**逐测试断言数（直接执行测试二进制，run=r204800）**

| 测试 | 检查数 |
|---|---|
| `domain.test_domain_text` | 213 |
| `domain.test_domain_json` | 159 |
| `domain.test_domain_repo` | **336**（+13：completion token、槽复用与按令牌释放） |
| `domain.test_domain_state` | **368**（+4：龄期三态；+4：时钟域换算） |
| `domain.test_domain_device` | **923**（+90：快照冲突、未知物品、跳号、提交失败、释放屏障、快照接收门、确认依据代际、重启补登记、HISTORY_INCOMPLETE 锁存） |
| `domain.test_domain_commands` | **623**（+44：动作幂等、审计、释放门、查询补救、工具版本、证明期限上界） |
| `domain.test_domain_core` | **217** |
| `integration.test_core_interrupt_matrix` | **95**（+2：泵计数等待） |
| **S1-04 合计** | **2934** |

另有 `common.*`（6 项）、`storage.*`（6 项）、`device.*`（4 项）、`contract.device.*`（3 项）共 19 项前置卡测试，本卡未修改其源码。

**负向对照（证明回归测试有效，非事后追认）**

| 缺陷 | 回退后观测 | 结论 |
|---|---|---|
| 启动死锁（快照请求排在会话门之后） | `test_domain_device.c:243/248 expected 3, got 2`、`:1210 expected 1, got 0`、`:1211 fixture.core->snapshot_in_flight` → 4 of 829 失败 | 单测确实覆盖启动路径 |
| `domain_change_check` 顺序（自查 #19） | 25 of 527 失败，全部 `expected status 0, got 3`（INVALID） | 非法 JSON 会被测试捕获 |
| 快照游标半零（只补 `device_id` 不补 `boot_id`） | `r154000` 15 of 21 checks failed，全 CONFLICT | `device_cursor_matches` 的三方约束被覆盖 |
| 晚到旧快照回退（#49） | `r203200` TSan 树 `test_borrow_flow` 3 of 95 失败（`WAIT_CONFIRM`/`COMPLETED`/`closed_at:null`），普通与 ASan 树不复现 | 只在真实时序下暴露，单测补三段接收门断言 |
| 确认依据不绑代际（#51） | **NOT_RUN**（本轮未做回退实验） | 断言已就位（`test_confirm_basis_generation`），负向对照留待后续补 |
| 释放补登记（#50） | **NOT_RUN**（本轮未做回退实验） | 断言已就位（`test_release_after_restart`），负向对照留待后续补 |
