# S1-04 业务核心 — 交接说明

交接日期 2026-10-27。上游 S1-02/S1-03 DONE；下游 S1-05 可立即开始，S1-06 依赖 S1-04 + S1-05。缺陷史与断言明细见 [`summary.md`](pre-takeover-summary.md)、[`defects.md`](../defects.md)、[`verification.md`](pre-takeover-verification.md)。
## 1. 交付物与整卡账

| 类别 | 路径 | 状态 |
|---|---|---|
| 生产代码 | `host/domain/`（19 个 `.c/.h`，9,686 行） | DONE |
| 单元测试 | `tests/unit/domain/`（8 文件 8,381 行，含夹具） | DONE |
| 集成测试 | `tests/integration/test_core_interrupt_matrix.c`（8 用例 95 检查） | DONE |
| 证据 | `docs/evidence/stage-01/S1-04/`（报告/附录/五轮审计/VM 日志） | DONE |
| 验证 | canonical `r204800`，三棵树各 27/27 零报告，合计 **2934** 断言 | DONE |

开工到 `r204800` 约 31 小时墙钟；独立审计 5 轮（round-1 口头给出后宿主重启、round-2/3/4 分别 FAIL 15/14/9 项），累计修 **58 项**真实缺陷，另有 2 项跨模块移交。

## 2. 公开接口（下游只能经这些入口）

声明在 `contracts/include/locker_contracts.h`，实现在 `host/domain/src/domain_core.c`：`locker_core_{create,start,stop,destroy,submit,query,wait_result,tick,get_progress}`、`locker_local_presence_submit`、`locker_local_maintenance_reconcile`。

1. **`submit`/`query` 只入队**，`QUEUED` 不等于业务成功；结果走 `wait_result`。`result.json` 已是完整信封，HTTP 层直接透传，**不要再包一层**。
2. **`wait_result` 超时返回 `NOT_READY`**，不是业务错误——业务拒绝在 `result.status`；上限 `LOCKER_CORE_RESULT_WAIT_MS`（1000 ms）。
3. **`get_progress` 是读锁内副本**，重复读不续命；给 S4-04 用，S1-05 别当健康检查。

## 3. 线程与生命周期契约（HARD REQUIREMENT）

**Core 是全进程唯一的仓储写者与唯一的设备发送者。** 别处不许直接调 `locker_repo_*` / `locker_device_*` 改业务状态；S1-05 的 HTTP 工作线程只能走 `submit`/`query`。

**销毁顺序（TSan 实测）**：`core_stop` → `device_close` → `core_destroy` → `device_destroy`。device 的 sink 上下文就是 Core，必须活到 `device_close` 等完所有回调；**先 destroy Core 再 close device 是真实数据竞争**。

## 4. 内部结构地图

逐文件职责见 [`verification.md`](pre-takeover-verification.md) §A/§B。改代码优先看：`domain_core.c` 的 `domain_handle_tick`/`domain_recover`；`domain_commands.c` 的 `finish_operation`（`extras` 同事务写幂等/审计）、`operation_arm`、`domain_outbox_pump`/`domain_release_pump`、`domain_timeouts`；`domain_device.c` 的 `apply_execute`、`apply_snapshot`（接收门/释放核对/补登记）、`domain_wait_confirm_basis_conflicted`、`operation_advance_from_evidence`；`domain_import.c` 的连续性派生。

## 5. 必须守住的不变量

| # | 不变量（违反后果见 [`defects.md`](../defects.md)） |
|---|---|
| 1 | 状态只能前进，`COMPLETED`(5) 不回 `REVIEW_REQUIRED`(4) |
| 2 | `continuity_ok` 为假即锁存，`HISTORY_INCOMPLETE` 不可洗白（`uart-v1.md:50`） |
| 3 | FETCH 的 `after` 取 `import_cursor`，不取 `cursor.through`——用错则跳号→误锁缺口 |
| 4 | 失败应答无 body：非零 `result` 折叠成 18 字节前缀、ID 全零；`reply_apply` 每 case 先判 `result` |
| 5 | ACK 不是开门：进 `ACCESS_ACTIVE` 只有 EXECUTE 应答 `OK+PULSE_STARTED` 或实时 `PULSE_STARTED` |
| 6 | 终结+账本+幂等+审计同一事务：`loan_check`→`persist`→`delete slot_guards`→`loan_apply`→`extras`→**一次 commit** |
| 7 | `domain_change_check` 必须在任何 mutation 之前（否则 check 落进 mutations→`INVALID(3)`） |
| 8 | 管理员核验核对物品（`BORROW→ABSENT`、`RETURN→PRESENT`），先过 `physical_evidence_fresh`（快照 ≤30 s） |
| 9 | 超时四条：`REQUESTED` 到期→`CANCELLED`；`WAIT_CONFIRM` 120 s→核验；开门 120 s 未关→`CLOSE_TIMEOUT`；从未开门 30 s→`OPEN_TIMEOUT` |
| 10 | 动作与发送分离：`operation_arm` 在 candidate 副本写完并 commit 成功后才装 `active`/outbox |
| 11 | 发送透传冻结的 `outbox_*`；`not_after_ms` 是 **MCU 时钟域**，且不得超过证明剩余时间 |
| 12 | 释放屏障：终结/`NO_CHANGE` 都登记 `release_pending`，释放前 `presence_apply` 返 `BUSY`；`CONFLICT` 只置 `release_recheck`；重启后凭快照补登记 |
| 13 | 快照接收门：同 boot 且版本更小的快照整体丢弃 |
| 14 | `WAIT_CONFIRM` 冻结依据绑 `boot_id`+`journal_epoch`：换 boot 后 revision 重新计数；转核验写库失败置 `wait_confirm_blocked` |
| 15 | 重启后 `WAIT_CONFIRM` 转 `REVIEW_REQUIRED`；设备推进同样 candidate + 单次持久化；恢复核验失败即整段失败 |
| 16 | `completion` 槽号不是身份：写入、等待、释放都必须核对 token |
| 17 | 查询补救与业务状态解耦；放弃等待必须清 `execute_in_flight`；缺口未闭合时证据推进进核验 |
## 6. 响应信封的四件套（最容易写错）

两套写法二选一：**A** 逐字段写 data → `domain_response_begin` + `domain_response_data_object` + 键值 + `domain_response_end`；**B** data 是自带 `{}` 的完整对象（投影写手）→ `domain_response_begin` + `domain_write_*_projection` + `domain_response_end_value`。**混用得到 `{"data":{{`，非法 JSON**（本卡为此付出 8 个集成用例全红）。新增返回 `result.json` 的用例先过 `dc_envelope_ok`（断言顶层恰好三个成员）。

## 7. 夹具与集成测试陷阱

- `dt_core_device_ready()` 置 `continuity_ok`、`import_cursor_valid=false`、工具投影、锚点与 `physical_revision=7`；最后一项是 `test_import_page`/`test_fetch_response` 的前置，改它会连锁破坏 7 处。
- `dv_arm_operation()` 不设 `command_id`/`outbox_*`，`test_execute_guards` 类用例要自己补齐，否则 `NOT_READY`；`dv_event()` 的 `physical_revision = 7 + seq`。
- `dc_seed_active()` 进 `WAIT_CONFIRM` 时按当前 physical 补冻结依据，先改 `physical.item` 再 seed，反过来 confirm 判 `CONFLICT`。
- 假设备 `release_status` 可注入 `CONFLICT`；`repo->commit_status` 是唯一能注入提交失败的入口。同一 `fixture` 多次 `dt_fixture_init` 前必须 `dt_fixture_free`（否则泄漏 15 MB）。
- `it_advance_clock(delta)` 用 `clock_offset_ms` 累计后调 `tick`，不要改回 `it_now_ms()+delta`（虚拟时钟退化，30 s `OPEN_TIMEOUT` 触发不了）；`it_server_main` 循环 accept。
- 不要在 arm 后立刻断言 `execute_requests`：EXECUTE 由泵在下一 tick 发出，用 `it_wait_until(it_counter_reached, ...)` 等计数（本卡三次假红）。区分 `result.status`（业务）与返回 `status`（传输）。

## 8. 本卡刻意没做的事

| 项 | 为什么 | 归属 |
|---|---|---|
| `locker_core_apply_supervisor_health` | 卡片归 S4-04，不做空实现冒充 | S4-04 |
| `device_state.simulated` 写 `false` | 无该字段的写入真源（`CR-S104-1`） | S1-06 回填 |
| Core 线程同步等设备写 IO | 修复落在 `host/device`（`CR-S104-2`） | 设备协议负责人 |
| 视觉相关接口 | 卡片说 S2-14；`vision_policy` 固定 `DISABLED_FOR_FOUNDATION` | S2-14 |
| 管理员无理由覆盖 / 自动重放旧 EXECUTE | 卡片禁止；重启一律进 `REVIEW_REQUIRED` | — |

## 9. 给 S1-05 的提醒

1. `locker_command_t` 自带 `principal`——鉴权后填真实身份，**不接受客户端自称的 `actor_id`**。
2. `locker_local_presence_submit` 要求 `peer.verified_ui_peer` 且 `presence.operation_revision` 等于当前 revision；IPC 层保证 `SO_PEERCRED` 一致。
3. `locker_local_maintenance_reconcile` 要求 `peer.verified_admin_peer`，只在 `phase==READY` 且物理稳定时成功，异步无直接结果。

## 10. 复核方式

在 VMware guest 跑 `sh /tmp/full-verify.sh <新 run_id>`：同一份源码连跑普通树 / ASan+UBSan / TSan，日志写 `~/locker-workspace/logs/S1-04/<run>/`。判定：三棵树都 `CONFIGURE_EXIT=0`、`BUILD_EXIT=0`、`WARNINGS=0`、`CTEST_EXIT=0`、`SANITIZER_REPORTS=0`、`0 tests failed out of 27`。
