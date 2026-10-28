> 历史整改附录：保留接手前的问题与修复叙述。当前交付以summary.md、handoff.md和debt.md为准；其中CR-S104-2已由本次接手回修关闭。

# S1-04 缺陷与整改记录（附录）

本文件是 `summary.md` 的附录，记录本卡修复的真实缺陷与跨模块移交。按 AGENTS.md 的 P3 规则，`summary.md` 只保留模板化结论，逐条缺陷史移到这里，避免主报告超长。

总计：**58 项已修缺陷**（#1–#15 开发期暴露、#16–#18 round-1 审计、#19 自查、#20–#34 round-2 审计、#35–#48 round-3 审计、#49 回归自查、#50–#58 round-4 审计 9 项中属实的 8 项），另有 **2 项跨模块移交**（`CR-S104-1`/`CR-S104-2`，见 §7.7）。round-4 的第 9 项 `R3-B-10` 属跨模块，计入移交而非已修。

## 7. 本卡修复的真实缺陷（开发期 15 项）

本卡的实现不是一次写对，以下每一项都是在 VM 实跑中被 ASan/TSan/集成测试暴露出来并修掉的。逐项记录预期、现象、根因、修法，便于审计复核。

| # | 缺陷 | 现象 | 根因 | 修法 |
|---|---|---|---|---|
| 1 | 启动死锁 | 设备永不 READY，`ack_requests` 恒 0 | 快照请求排在会话门之后，会话门又要等快照 | 快照块前移到会话门之前（`domain_device_pump`） |
| 2 | 测试自身越界写 | ASan stack-buffer-overflow | `st_fill_page` 按调用方 `count` 写 `events[4]`，而用例故意传 5 | 填充截断到 `LOCKER_UART_MAX_EVENTS` |
| 3 | 夹具泄漏 15 MB | ASan 9 direct + 54 indirect leaks | 同一 `fixture` 变量「建→用→重建」时未先 free | 插入 `dt_fixture_free`（16 处）并去冗余 |
| 4 | `thread_exited` 数据竞争 | TSan 5 处 | Core 线程写 / 停止方读，普通 bool | 改 `atomic_bool` + acquire/release |
| 5 | 快照游标三方约束 | 设备永不 READY | `(0,0)` 哨兵禁止半零；`EventKey.device_id` 必须 24 位十六进制；ACK 必须带 device_id+epoch | 游标保持全零原点，只补身份与 epoch；`physical.device_id` 从 HELLO 学到的身份补入 |
| 6 | `cursor.continuity_ok` 从不置真 | `OPERATION_CREATE` 恒 NOT_READY（got 5） | 实时路径只有置 false 的写点，唯一置真入口在恢复分支 | `domain_import_page` 成功导入后按「仓储连续 && 无锁存缺口」派生 |
| 7 | 响应信封多一层 `{` | 集成测试全部 `got 8`（IO_ERROR） | `domain_response_data_object` 与自带 `{}` 的投影写手叠用，产生 `{"data":{{` | 拆 `domain_response_end_value`（只写 tail），投影类改用它；列表类删自补 `}` |
| 8 | `locker_uuid_parse` 当布尔用 | 每个正常响应都被判 IO_ERROR | 返回状态枚举，`OK==0`，`!parse(...)` 把成功当失败 | 显式与 `LOCKER_STATUS_OK` 比较 |
| 9 | 无 body 失败应答被静默丢弃 | 五个注入阶段全卡 ACCESS_PENDING | codec 把非零 result 折叠成 18 字节前缀，`command_id`/`operation_id` 全零 → ID 匹配门拦下 | 新增失败应答分支：按 result 映射 `COMMAND_REJECTED`/`UNKNOWN_RESULT`；NOT_READY/BUSY 保留 pending 供查询补救 |
| 10 | 失败 health 应答把会话拉下 | CONFIRM 前 `physical_ready_for_action` 恒假 | `domain_reply_apply` case 5/9/11 不检查 result，body 全零的 `device_mode=0` 被解释成 OFFLINE | 三处都先判 `result==OK` |
| 11 | 查询补救挂在错误的状态分支 | 应答丢失后永不查询 | 补救只在 ACCESS_PENDING 分支，而 `PULSE_STARTED` 事件会先把操作推进 ACCESS_ACTIVE | ACCESS_ACTIVE 分支补同样补救 |
| 12 | FETCH 用视图游标导致跳号 | 仓储 CONFLICT → 误锁缺口 → CONFIRM 挂 | live 事件推进 `cursor.through` 但不落仓储，`after=cursor.through` 返回与仓储水位不相邻的页 | `request_fetch` 的 `after` 改用 `import_cursor` |
| 13 | 测试违反生命周期契约（重启用例） | TSan 7 处（queue destroy vs try_push） | 先 `core_destroy` 再 `device_close`，reader 线程仍可能写已释放队列 | 改为 `core_stop → device_close → core_destroy → device_destroy`（契约原文：close 停止 IO 和回调，线程退出后才允许销毁） |
| 14 | 测试虚拟时钟被真实时钟吞掉 | 30s 的 `OPEN_TIMEOUT` 永不触发 | `it_advance_clock` 每轮从真实 now 重新取基准，净推进 ≈ delta − 真实流逝 | `it_env_t.clock_offset_ms` 累计偏移 |
| 15 | 模拟器 server 只 accept 一次 | 重启后新设备 HELLO 无人接收 | `it_server_main` 单次 accept，旧连接关闭后线程退出 | 拆 `it_server_serve_connection` + 循环 accept |

### 7.1 独立审计 round-1 指出的三项业务缺陷（整改于 r198000）

| # | 缺陷 | 现象 | 根因 | 修法 |
|---|---|---|---|---|
| 16 | **终结与账本分成两个事务** | 提交中途失败会留下「操作 COMPLETED 但 loans 没动」的半截状态：借出侧账上说已借出却没有有效借用，归还侧账上已完成但借用仍 open | `finish_operation` 先提交终结，`command_operation_confirm`/`command_operation_resolve` 再单独提交账本 | `finish_operation` 增 `bool apply_loan`，把 `domain_loan_check` → `domain_operation_persist` → `domain_change_delete("slot_guards","1")` → `domain_loan_apply` 全挂在同一个 change 上，一次 `domain_change_commit`；`domain_loan_commit_in_memory` 只在提交成功后更新内存副本。`domain_loan.c` 拆成 prepare/check/apply/commit_in_memory 四段 |
| 17 | **管理员核验不检查物品** | 物品仍在柜里就能把 BORROW 结案成 COMPLETED，凭空造出一条有效借用，绕开整条物理证据链 | `command_operation_resolve` 只比对 `operation.kind`，不看 `physical.item` | 新增两支：`COMPLETE_BORROW` 要求 `item == ABSENT`、`COMPLETE_RETURN` 要求 `item == PRESENT`，否则 CONFLICT。依据 `interface-expert.md:55` 与卡片「不允许管理员无理由覆盖」 |
| 18 | **`ACCESS_ACTIVE` 未开门缺 30 秒出口** | 锁体卡死/门被顶住/传感器失灵时 `door_opened_ms` 恒 0，操作永久停在 ACCESS_ACTIVE 占着唯一柜格，后续申请全被 BUSY 挡回 | `domain_timeouts` 的 ACCESS_ACTIVE 分支只有 `CLOSE_TIMEOUT`，漏了规格 `stage-00-architecture.md:122`「从脉冲事实起 30 秒未见开门…进入核验」的前半句 | 加 `door_opened_ms == 0 && pulse_started_ms != 0 && now - pulse_started_ms >= LOCKER_WAIT_OPEN_MS` → `require_review("OPEN_TIMEOUT")`；计时起点用脉冲事实 `pulse_started_ms`（三个写入点：`domain_device.c:402`/`:598`/`:655`） |

### 7.2 整改过程中自查出的第四处缺陷

| # | 缺陷 | 现象 | 根因 | 修法 |
|---|---|---|---|---|
| 19 | **`domain_change_check` 在 mutation 之后调用会写出非法 JSON** | r195000 里 `test_domain_commands` 25/527 失败，全部是 `expected status 0, got 3`（INVALID），看不出是 JSON 坏了 | `change_mutations_open` 在第一个 mutation 时已写出 `],"mutations":[` 关闭 checks 数组；此后再 check 会把 check 对象塞进 mutations 数组 → 非法 JSON → 仓储解析失败统一报 INVALID(3) | `domain_change_check` 开头加硬约束 `if (change->mutations_started) { change->broken = true; return false; }`；`finish_operation` 里把 `domain_loan_check` 前移到任何 mutation 之前 |

其中 **1–12、13 的产品侧部分**是真实产品缺陷，**16–19 是独立审计与我方自查共同发现的业务/契约缺陷**，均已修复并在 r198000 验证。**卡片要求的中断矩阵正是靠 #9 才能跑通，#16 是「物理变化与本人确认都满足才完成」这条验收在产品侧的最后一环。**

### 7.3 独立审计 round-2 指出的 15 项阻塞（整改于 r201300）

round-2 审计结论见本目录 `audit-round2.md`（FAIL，14×P1 + 1×P2）。审计用 Linux `memfd` 编译真实探针逐项给出反例，全部属实。以下按审计编号记录修法，每条都有对应回归断言：

| # | 审计项 | 缺陷 | 修法 | 回归断言 |
|---|---|---|---|---|
| 20 | R1-B-1 | 动作事务提交失败后仍会发出 EXECUTE：`commit_status=IO_ERROR` 恢复后泵仍把操作推进到 `ACCESS_PENDING` 并 `sends=1`，而仓储里仍是 `REQUESTED` | `operation_arm` 把全部字段填充与状态跃迁做在 `domain_operation_t candidate = core->active;` 上，`domain_change_commit` 成功后才安装 `core->active`、递增 `next_action_no`、发布 | `test_domain_commands.c` 的 arm 失败段：失败后 `state==REQUESTED && !has_command`，pump 不再发 |
| 21 | R1-B-2 | `NO_CHANGE` 不释放持久占位：只持久化 operation 就清内存 `used`，`slot_guards` 仍留着 → 下次申请永远冲突，重启也不修 | `operation_advance_from_evidence` 在同一 change 里 `domain_change_delete(&change,"slot_guards","1")`；内存侧 `candidate.used=false` 但**保留终态**（不再 `memset` 抹掉） | `test_domain_device.c` 的 NO_CHANGE 用例断言 `state==NO_CHANGE`、`reason==NO_ITEM_CHANGE`，且后续申请可成功 |
| 22 | R1-B-3 | 完成后从不发 `RELEASE_CONTEXT`，模拟器 `has_active` 门会让下一次 EXECUTE 永远 BUSY → 柜子只能用一次 | `finish_operation` 登记 `release_pending`；新增 `domain_release_pump`（`domain_commands.c`）在会话可用时发 `locker_device_release_context`，超时重发、`CONFLICT` 表示设备侧已不持有则清除；`domain_reply_apply` case 11 清标志；`core_tick_internal` 在 outbox 泵之后调用 | `test_core_interrupt_matrix` 的 `test_borrow_flow` 连续借还；`test_domain_commands.c` 的 release 段 |
| 23 | R1-B-4 | `WAIT_CONFIRM` 期间物理冲突被忽略：注入 `DOOR_OPENED` 仍停 `WAIT_CONFIRM`，因为 `operation_advance_from_evidence` 对该状态立即 return | `domain_operation_t` 增 `wait_confirm_basis_valid`/`wait_confirm_physical_revision`/`wait_confirm_item`/`wait_confirm_door` 四字段，进 `WAIT_CONFIRM` 时冻结；新增 `wait_confirm_conflicted` 在 `apply_physical_event` 末尾与 `domain_event_apply` 推进前比对，冲突则 `domain_require_review("PHYSICAL_CHANGED")` | `test_domain_device.c` 与 `test_domain_commands.c` 的 `PHYSICAL_CHANGED` 段 |
| 24 | R1-B-5 | 恢复截止点混用 UTC 与单调时钟：带 UTC `updated_at` 的记录恢复后 `wait_confirm_deadline_ms=1761004920000`，推进 130 秒仍不超时 | `domain_core.c` 新增 `deadline_in_monotonic(wall_start, duration, wall_ok, now_wall, now_mono)`，把墙钟起点换算成单调域；`recover_operations` 的 REQUESTED/WAIT_CONFIRM 两处都用它 | `test_domain_core.c::test_recover_operations` 断言改为 `fixture.core->now_ms + ...` |
| 25 | R1-B-6 | `TIME_ANCHOR` 完全没接入：`domain_reply_apply` 无 case 10，`domain_anchor_apply`/`domain_anchor_mcu_now` 无调用方，`outbox_not_after_ms` 直接取主机单调时钟 | `domain_reply_apply` 新增 case 10；`operation_arm` 用 `domain_anchor_mcu_now` 把 `PREPARE_DEADLINE_MS` 换算到 MCU 时间域，无锚点返回 `NOT_READY`；`domain_outbox_pump` 按锚点换算判过期（两侧下界都越界才拒发），无锚点报 `ANCHOR_MISSING`；`domain_anchor_request` 加 2 秒节流 | `test_domain_commands.c` 的 `ANCHOR_MISSING`/`PREPARE_DEADLINE` 两段；夹具 `dt_core_ready` 补锚点样本 |
| 26 | R1-B-7 | 无 UI 时伪造新鲜 UI 健康：`send_health` 填 `ui_instance_id=Core 实例`、`ui_progress=Core 计数`、`ui_age_ms=0` | 改为零 UUID + `ui_progress=0` + `ui_age_ms=UINT32_MAX`，`supervisor_state=UNKNOWN` | `test_domain_device.c` 的 health 上报段 |
| 27 | R1-B-8 | 幂等只对 create 生效且哈希未规范化：同键 cancel/confirm/resolve 第二次返回 CONFLICT；create 只改 JSON 字段顺序或空白也冲突 | `build_request_hash` 改为对**规范化后的参数**求哈希（`hash_arguments` 解析 `arguments_json`，按成员键字典序逐项 `locker_domain_json_render` 后喂 SHA-256，空白与顺序不再影响结果；解析失败才退回原文）；新增 `store_idempotent_response`，confirm/cancel/resolve 三个成功分支都写 `IdempotencyRecord`（`method="POST"`、`path=command->kind`、`request_hash` 同上）。**版本号语义**：幂等记录本身是一次提交（`domain_change_commit` 使 `core->revision += 1`），所以响应体先按 `core->revision + 1u` 预写、写失败再按真实 `revision` 重写，保证「落库那份」与「返回客户端那份」逐字节相同 | `test_domain_commands.c::test_action_idempotency`（confirm/cancel/resolve 各三段：同键重放响应逐字节相同且 `commit_calls`/`revision` 不变、换 request_id 不影响、换参数 CONFLICT） |
| 28 | R1-B-9 | 工具投影从不恢复、版本不校验：`tool_valid` 恒 false 让 create 跳过 ID 检查，未知工具 + `tool_revision=999` 返回 OK | `domain_core.c` 新增 `recover_tools`（读 `tools` 集合；多于一件启用工具则 `tool_valid=false` 拒绝），接入 `domain_recover`；`command_operation_create` 三段校验（不可知→NOT_READY、ID 不符→INVALID、revision 不符→CONFLICT） | `test_domain_commands.c::test_create` 三段用例；`test_domain_core.c` 的 recovery 断言 |
| 29 | R1-B-10 | 人工核验的理由与执行管理员未落库：`reason` 只查长度、`reason_code` 固定 `ADMIN_RESOLVED`、actor 仍是原申请人、`domain_write_audit` 无调用方 | 新增 `record_manual_audit`，写 `AuditRecord`（`MANUAL_RESOLUTION`，reason 原文按 256 截断并附 `(physical_revision=N)`），resolve 成功后调用；写审计失败不回滚业务事务 | `test_domain_commands.c` 的 resolve 审计段 |
| 30 | R1-B-11 | EXECUTE 应答丢失后 `execute_in_flight` 永久占用：`apply_query_command` 不清它，而超时清命令又要求它为假 → 后续合法命令发不出 | `apply_query_command` 入口先清 `execute_in_flight`；失败分支改 `pending_command_valid=false` 并对未决操作 `domain_operation_set_state(...,REVIEW_REQUIRED,"QUERY_UNRESOLVED")` | `test_domain_device.c` 的查询补救段；`test_core_interrupt_matrix::test_interrupt_ack_lost` |
| 31 | R1-B-12 | 过期物理缓存仍可用于核验改账：resolve 不查会话与采样龄期；快照失败不清 `device_snapshot_valid`；公开 `observed_age_ms` 恒 0 | 新增 `physical_evidence_fresh`（要求会话在线、快照有效、龄期 ≤ `LOCKER_SNAPSHOT_MAX_AGE_MS`=30s），resolve 前置校验；`apply_snapshot` 失败时清 `device_snapshot_valid`/`physical.valid`；`domain_write_physical` 增 `now_ms` 参数，按当前单调时钟算真实龄期，未采样或时钟回拨写 `UINT32_MAX` | `test_domain_commands.c` 的 freshness 段；`test_domain_state.c` 的龄期三态（250 / 4294967295 × 2） |
| 32 | R1-B-13 | 查询未落实所有者/管理员权限：普通用户 B 可查用户 A 的 `OPERATION_GET`，也可调 `REVIEWS_LIST` | `query_operation_get` 加 `privileged \|\| actor==principal.user_id`，否则 INVALID（不区分不存在/无权）；`REVIEWS_LIST` 非 ADMIN 一律 INVALID | `test_domain_commands.c::test_query_dispatch` 的三个越权用例 |
| 33 | R1-B-14 | Core 线程同步等待设备写 IO，背压时整条循环停顿（**P2、跨模块**） | 记录为跨模块问题移交契约负责人：`host/domain` 侧只经 `locker_device_*` 端口，端口本身是同步语义（`host/device/src/transport.c:192-239` 的 `poll` 等待上限 1000 ms 属 S1-03 模块边界）。本卡可改目录只有 `host/domain/`、`tests/unit/domain/`、核心相关 `tests/integration/`，**不越界改 `host/device`**，登记为 `CR-S104-2`（建议由设备协议负责人实现有界发送队列，Core 只提交意图并消费回执） | 未在本卡修，见 §8 |
| 34 | R1-B-15 | 终结提交失败后内存终态未回滚；REQUESTED 到期提交失败时无条件 reset；`require_review` 失败后已被置 `REVIEW_REQUIRED`，下次调用直接 OK 不再补持久化 | `finish_operation` 与 `domain_require_review` 都改为**局部候选副本**：`domain_operation_t candidate = *operation;` → 全部改动做在副本上 → `domain_change_commit` 成功后才 `*operation = candidate` 并同步 `core->active`；`domain_timeouts` 只在 `finish_operation` 返回 OK 时才 `domain_operation_reset` | `test_domain_commands.c::test_finish_and_loan_same_transaction`（`commit_status=IO_ERROR` 时 revision 不前进、`!loan_open`，恢复后重试成功） |

审计同时列出的三项非阻塞问题处理结果：

- **R1-N-1**（summary 提前标 DONE、Git 状态描述、manifest 文件数、sanitizer 日志形态、文档超 P3 上限）：已在本次报告修订中全部纠正——状态改为「三套跑完才标 DONE」，Git 描述改为「已初始化仓库，`core.autocrlf=false`」，manifest 明确 81 项，sanitizer 日志形态如实写成「configure/build/ctest 三段合并一份」，`summary.md` 压到 10,203 B、`handoff.md` 压到 8,184 B（P3 上限 10 KB / 8 KB）。
- **R1-N-2**（`device_state.simulated` 仍写 false）：保留为契约缺口 `CR-S104-1`，见 §7.7。
- **R1-N-3**（无 `audit-run.log` 等四件套）：每轮审计都留四件套。round-2 已归档为 `audit-round2.md` / `audit-round2-prompt.txt` / `audit-round2-codex-output.txt`（1.5 MB 完整转录）/ `audit-round2-run.log`（`EXIT=0`、`START=2026-10-27T20:55:02`、`END=2026-10-27T21:08:53`、`MINUTES=13.8`）；round-3 用同一 `audit-runner.ps1` 产出 `audit.md` / `audit-prompt.txt` / `audit-codex-output.txt` / `audit-run.log`。

### 7.4 独立审计 round-3 指出的 14 项阻塞（整改于 r203300）

round-3 结论见本目录 `audit.md`（FAIL，13×P1 + 1×P2）。审计同样用 Linux `memfd` 编译真实探针给出反例，其中 12 项是对 round-2 整改的**再深入**（"已修"只到表面），2 项（R3-B-14、R3-B-9）是全新发现。以下按审计编号记录：

| # | 审计项 | 缺陷 | 修法 | 回归断言 |
|---|---|---|---|---|
| 35 | R3-B-1 | 实际 EXECUTE 丢弃已冻结的 MCU 期限：`not_after` 存储 503000（MCU 域）但发送 2000（主机域），因为发送时重采当前 session/physical 重建 guard | `domain_execute_intent` 原样透传 `outbox_{session_id,journal_epoch,boot_id,physical_revision,not_after_ms}`；新增三道门：`not_after==0`→NOT_READY、会话/epoch/boot 不符→CONFLICT、`physical_revision` 不符→CONFLICT | `test_domain_device.c::test_execute_guards`（`last_intent.not_after_mcu_ms == operation.outbox_not_after_ms` 等 4 条透传断言） |
| 36 | R3-B-2 | 上下文释放没有形成下一次动作的屏障：`NO_CHANGE` 后 `release_pending=0`、`release_calls=0`；释放完成前可提交下一次 presence；tick 顺序先 outbox 后 release；崩溃丢失纯内存释放登记 | ①`operation_advance_from_evidence` 的 `!changed` 分支也登记 `release_pending`；②`domain_handle_tick` 把 `domain_release_pump` 排到 `domain_outbox_pump` **之前**；③`presence_apply` 在 `release_pending` 时返 `BUSY`；④`apply_snapshot` 用设备上报的 `active_operation_id` 闭合释放（nil 或不属于本次→清标志；仍持有且 `release_recheck`→刷新 guard 重发一次）；⑤`CONFLICT` 只置 `release_recheck`，不重发旧版本 | `test_domain_device.c::test_release_after_no_change`（5 段：登记→成功→CONFLICT 只查→快照刷新后重发→快照无 active 则清除）；`test_domain_commands.c::test_presence_blocked_while_releasing` |
| 37 | R3-B-3 | 新快照中的物理冲突绕过 `WAIT_CONFIRM`：冻结 `revision=7`，收到 revision=8 的成功快照后 `physical=8`、`basis=7`，状态仍 `WAIT_CONFIRM` | `apply_snapshot` 末尾调用 `wait_confirm_conflicted`；`wait_confirm_conflicted` 改为方向敏感（只有版本**更大**或同版本下物品/门变化才算冲突） | `test_domain_device.c::test_snapshot_confirm_conflict`（快照 revision=12 → `REVIEW_REQUIRED`+`PHYSICAL_CHANGED`，且库里同样落库） |
| 38 | R3-B-4 | 重启后的 `WAIT_CONFIRM` 可直接完成：持久化 `WAIT_CONFIRM`+`item_transition_seen=true`，重启恢复后以原 revision 确认 → `loan_open=1` | `recover_operations` 把 `WAIT_CONFIRM` 与 `ACCESS_*` 一样转 `REVIEW_REQUIRED`，`reason_code="RECOVERED_AFTER_RESTART"`，并清 `wait_confirm_deadline_ms` | `test_domain_core.c::test_recover_operations`（恢复后 `REVIEW_REQUIRED` + `RECOVERED_AFTER_RESTART` + deadline 为 0） |
| 39 | R3-B-5 | 幂等记录仍与业务结果分开提交：只让含 idempotency mutation 的提交失败 → `first=OK` 但 `replay=CONFLICT`，仓储里已是 `CANCELLED` | `finish_operation` 增 `const domain_finish_extras_t *extras`，在同一 `domain_change_t` 内追加 `idempotency_append` 与 `manual_audit_append`，最后**一次** `domain_change_commit`；响应体在 commit 前按 `core->revision + 1u` 渲染，失败则整笔不生效 | `test_domain_commands.c::test_action_idempotency`（三段：重放逐字节相同 + `commit_calls`/`revision` 不变 + 换参数 CONFLICT） |
| 40 | R3-B-6 | 人工理由审计失败仍可成功覆盖账本：只让 audits 提交失败 → `status=OK`、`stored_state=COMPLETED`、`audits=0`；取消理由仍只解析不持久化 | 审计并入 `finish_operation` 同一事务（见 #39）；`manual_audit_append` 把 reason 按 256 截断并附 `(physical_revision=N)`；cancel 也写 `AuditRecord(type=OPERATION_CANCELLED, reason=取消理由)` | `test_domain_commands.c::test_resolve` 与 `test_cancel` 的 `audits` 集合断言（type/actor/reason 三项） |
| 41 | R3-B-7 | 设备状态推进先改内存再吞提交错误：`BAD_PRECONDITION` 回复 + `commit_status=IO_ERROR` → 内存 `REVIEW_REQUIRED`、库里仍 `ACCESS_PENDING`、已 publish | `device_advance_persisted`（`domain_device.c`）改为 candidate 副本 + `domain_persist_operation`，成功才 `*operation=candidate`、同步 `core->active`、publish | `test_domain_device.c::test_reply_commit_failure`（失败时内存与库都保持 `ACCESS_PENDING`；恢复后同样应答才转核验） |
| 42 | R3-B-8 | 丢 EXECUTE 应答后的等待仍可能永久锁住：门/物品事件 5 s 内推进到 `WAIT_CONFIRM` 后，查询挂在 `ACCESS_*` 分支永远排不上 → `query_calls=0`、`execute_in_flight=1` | 查询补救从 `domain_timeouts` 的两个 case 里抽出、移到 switch 之后与业务状态解耦；放弃等待块去掉 `&& !execute_in_flight`，改为清 `execute_in_flight` 并对未决操作 `domain_require_review("QUERY_UNRESOLVED")` | `test_domain_commands.c::test_timeouts` 的 `WAIT_CONFIRM` 丢应答段（`query_calls==1` → 再推进 → `QUERY_UNRESOLVED`） |
| 43 | R3-B-9 | 显式 `tool_revision=0` 可绕过版本检查：当前 revision=1、提交 `"0"` → OK | `command_operation_create` 改为精确比较 `tool_revision != core->tool_revision → CONFLICT`（删掉 `!= 0u &&` 的"兼容旧客户端"例外） | `test_domain_commands.c::test_create`（`tool_revision=999`→CONFLICT、`=1`→OK） |
| 44 | R3-B-10 | 跨模块阻塞 IO（**P2**）：AF_UNIX 对端保持连接但不读 → Core 发送进入 `write_lock` + 最长 1000 ms poll | 同 R1-B-14，本卡不越界改 `host/device`；`CR-S104-2` 说明所有权限制，**不构成验收豁免** | 未在本卡修，见 §7.7 |
| 45 | R3-B-11 | `UNKNOWN` 和人工 `NO_CHANGE` 被误当原基线：①`item=UNKNOWN` 的事件 → 直接 `NO_CHANGE`；②BORROW 核验时当前 `ABSENT`、原基线 `PRESENT`，管理员选 `NO_CHANGE` → OK | ①`operation_advance_from_evidence` 先判 `item == UNKNOWN` → `REVIEW_REQUIRED("PHYSICAL_UNKNOWN")`；②`!changed` 且（无基线/基线为 UNKNOWN/当前物品≠基线）→ `REVIEW_REQUIRED("PHYSICAL_MISMATCH")`；③`command_operation_resolve` 的 `NO_CHANGE` 也要「等于原基线且基线可知」 | `test_domain_device.c::test_event_item_unknown_and_mismatch`（两条 reason_code 断言） |
| 46 | R3-B-12 | 实时事件跳号仍能声称完整：同 epoch/boot 输入 seq=1,3,4（跳过 2）→ `continuity_ok=1`、`history_complete=1`、进 `WAIT_CONFIRM` | `domain_event_apply` 加跳号检查：同 boot 且水位非 0 且 `event_seq != through.event_seq + 1` → `cursor.continuity_ok=false`、`fetch_gap=true`、`active.history_complete=false` | `test_domain_device.c::test_event_sequence_gap`（seq=1 应用 → seq=3 三标志翻转） |
| 47 | R3-B-13 | 取消失败仍把 UTC 截止点安装进 active：取消前从库加载操作、终结提交失败 → `status=IO_ERROR` 但 `active.deadline=1761004920000` | ①`domain_operation_load` 内部做墙钟→单调换算（`domain_deadline_in_monotonic`），覆盖全部 8 个入口；②confirm/cancel/resolve 都不再 `core->active = operation`，改为局部 `target` 副本 | `test_domain_state.c::test_operation_load`（过期 `updated_at` → `now + TTL`；起点在未来 → `now + duration`）；`test_domain_commands.c` 的 confirm/cancel/resolve 失败段 |
| 48 | R3-B-14 | completion 槽复用可交付另一请求结果：`wait_result` 分别加锁 `find(token)` 与 `wait_final(slot)`，两者间旧终态被 reap、新请求复用槽 → 等待旧槽返回 `OK / OTHER_USER_B` | `domain_completion_wait_final` 增 `const locker_uuid_t *token` 参数，等待循环与终态判定都必须 token 相等，否则 `NOT_READY` | `test_domain_repo.c::test_completion_slot_reuse`（槽复用后旧 token 得 `NOT_READY` 且 `result.length==0`，新 token 仍拿到 B 的结果） |

**非阻塞（R3-N-1..N-4）**：R3-N-1 文档提前声明已修 → 本轮 summary/handoff/defects/verification 全部改为如实表述（含「round-4 未跑」）；R3-N-2 `simulated=false` 保留 `CR-S104-1`；R3-N-3 四件套逐轮归档；R3-N-4 设备 mode 仍由 Core phase 冒充 → 保留为已知缺口（见 summary §7）。

### 7.5 独立审计 round-4 指出的 9 项阻塞（整改于 r204700）

round-4 结论见 `audit-round4.md`（FAIL，7×P1 + 2×P2 + 4 项非阻塞）。该轮审计期间 VM 两次连接超时，全部 C 探针 NOT_RUN，判定为静态推导 + Python 算术复算；因此本轮整改**全部补了正式回归断言**，不靠审计重跑来证明。9 项中 8 项属实并已修，第 9 项 `R3-B-10` 属跨模块（计入 §7.7 移交）。

| # | 审计项 | 缺陷 | 修法 | 回归断言 |
|---|---|---|---|---|
| 50 | R3-B-2（尚存） | `device_active_*` 只有赋值没有消费者：终结落库后崩溃重启，内存 `release_pending` 全丢而 MCU 仍持 has_active → `release_calls` 永远 0，柜子只能再用一次 | `apply_snapshot` 在 `accept_physical` 后、`release_pending` 为假时，若快照上报的 `active_operation_id` 非 nil 且不是当前 active 的操作，就自动登记 `release_pending`（带设备给的 `active_command_id`）；释放只通知放手，不重放开锁动作 | `test_domain_device.c::test_release_after_restart`（恢复后无登记 → 快照带 active → 登记 → `release_pump` 发出且 guard 版本取自快照） |
| 51 | R3-B-3（尚存） | 冻结依据只比 revision/item/door，不绑 boot/epoch：换 boot 后 revision 从头计数，rev=1 被当成「更早的陈旧事实」放过；另同 boot 新 revision 导致转核验写库失败时内存仍 `WAIT_CONFIRM`，confirm 从库加载旧记录后可直接完成 | ①`domain_operation_t` 增 `wait_confirm_boot_id`/`wait_confirm_journal_epoch`/`wait_confirm_blocked`；②判定抽成 `domain_wait_confirm_basis_conflicted`（非 static，供 confirm 复用）：boot/epoch 不符即冲突，版本更大才冲突，更小视为陈旧；③`wait_confirm_conflicted` 判冲突时先置 `wait_confirm_blocked=true` 再转核验；④confirm 在 `physical_ready_for_action` 后做**决策点复核**，闭锁或依据冲突一律 CONFLICT | `test_domain_device.c::test_confirm_basis_generation`（冻结 rev=9/boot=1，收到 boot=2/rev=1 的成功快照 → `REVIEW_REQUIRED`+`PHYSICAL_CHANGED`） |
| 52 | R3-B-6（尚存） | 80 个汉字（240 B）的合法理由被截到 233 B，切在三字节字符中间 → 真仓储 `utf8_count` 返回 false、整个核验事务失败；另 `note[257]` 把 256 **字符**误当 256 **字节** | 新增 `audit_char_count`/`audit_prefix_bytes` 按**码点**计数与截断；resolve 的 `note` 与 cancel 的 `reason` 缓冲放大到 4097，建读阶段不再按字节判非法；`manual_audit_append` 按 `256 - 后缀字符数` 截原文再拼 ASCII 后缀 | `test_domain_commands.c::test_resolve` 的 `audits` 断言（`MANUAL_RESOLUTION` + `physical_revision=7` + actor 动态比对）；`test_cancel` 的 `OPERATION_CANCELLED` |
| 53 | R3-B-7（尚存） | 恢复用 `(void)domain_require_review(...)` 忽略错误，随后 `recovery_ready=true; recovery_done=true; return OK` → 线程据此进 READY，核验从未落库 | `recover_operations` 检查返回值，非 OK 直接 `return status`；`recovery_ready`/`recovery_done` 只在成功后置，失败时 phase 停在 RECOVERING | `test_domain_core.c::test_recover_operations`（`repo->commit_status=IO_ERROR` 时 `domain_recover` 返回 IO_ERROR 且不置 ready） |
| 54 | R3-B-12（尚存） | 跳号确实置了 `continuity_ok=false`/`fetch_gap=true`/`history_complete=false`，但 `operation_advance_from_evidence` **不读它们**：当前物品等于原基线就 NO_CHANGE、删占位、登记释放 | `operation_advance_from_evidence` 在 `ACCESS_ACTIVE` 门之后、证据判定之前加缺口门：`!cursor.continuity_ok \|\| fetch_gap \|\| !operation->history_complete` → `domain_require_review("HISTORY_INCOMPLETE")` 并直接返回 | `test_domain_device.c::test_event_sequence_gap`（seq=1 → seq=3 三标志翻转）；`test_event_history_incomplete`（锁存后即使物品符合期望终态也进核验） |
| 55 | R3-B-14（尚存） | `wait_final` 的 token 只保护读侧；`query` 超时后仍按裸下标 `release`，旧 job 只含 completion 下标、`finish` 只查 `used` → 可把 A 的结果写进 B 槽 | ①`domain_job_t` 与 `locker_core_t` 各增令牌字段；②新增 `domain_completion_release_owned`/`domain_completion_token_matches`；③`submit`/`query` 在 enqueue 前写 `job.completion_token`，超时后按令牌释放；④`domain_handle_job` 把令牌装进 `core->job_token`，`response_finish`/`response_finish_json` 写结果前核对 | `test_domain_repo.c::test_completion_release_owned`（错令牌不释放、正确令牌释放、槽复用后旧令牌无效、参数守卫）；`test_completion_slot_reuse` |
| 56 | R4-B-1（新增） | arm 只检查证明年龄 ≤15 s，随后无条件 `not_after = mcu_low + 2000`，忽略证明剩余时间 → 证明接近到期时仍能再获约 2 秒执行窗口（复算：期望 ≤516000，实际 517999） | `operation_arm` 取 `proof_remaining = min(proof_deadline - now, LOCKER_PREPARE_DEADLINE_MS)`，`outbox_not_after_ms = mcu_low + proof_remaining` | `test_domain_commands.c::test_arm_deadline_bounded_by_proof`（t=15000 时 `not_after == 15000 + 1000` 且严格小于「无条件 2 秒」的上界，落库值与发送值一致） |
| 57 | R4-B-2（新增） | 实时事件完全不消费 `event->flags`，`apply_physical_event` 反而写 `dropped_events=0`；FETCH 只复制 `record->flags`、按 `gap_before_page` 派生 continuity，忽略事件级 HISTORY_INCOMPLETE → 序号连续即可声称完整 | ①`domain_event_apply` 读 `event->flags` bit2，置 `continuity_ok=false`/`fetch_gap=true`/`history_complete=false`；②`apply_physical_event` 删掉 `dropped_events=0`（事件不带累计值，写死 0 等于抹掉已知丢失）；③`domain_import.c` 本页任一事件带 bit2 即锁存缺口 | `test_domain_device.c::test_event_history_incomplete`（seq=2 带 bit2 → 三标志翻转，seq=3 符合期望仍进核验） |
| 58 | R4-B-2（配套） | `operation_advance_from_evidence` 的缺口门与 ①③ 必须联动，否则锁存了也没人读（与 #54 同一处代码、不同触发入口：实时事件 vs 补取页面） | 同 #54 的缺口门；三入口（实时事件 flags、补取页 flags、快照 flags）任一成立都走同一道门 | 同 #54 |

**非阻塞（R3-N-1..N-4）**：R3-N-1 文档口径 → 本轮 summary 改为「58 项真实缺陷 + 2 项移交」，行数按实测（`host/domain` 9,686 / `tests/unit/domain` 8,381 / 集成 1,566）重写，handoff 记录整卡耗时与审计轮数；R3-N-2 `simulated=false` 保留 `CR-S104-1`；R3-N-3 四件套逐轮归档并补 `audit-round2-runner.ps1`/`audit-round3-runner.ps1`/`audit-round4-runner.ps1`；R3-N-4 设备 mode 仍由 Core phase 冒充 → 保留为已知缺口（summary §7）。

### 7.6 回归自查发现的产品缺陷（#49，整改于 r203300）

| # | 缺陷 | 触发 | 修法 | 回归断言 |
|---|---|---|---|---|
| 49 | 晚到的旧快照把刚成立的新事实回退，导致 `WAIT_CONFIRM` 被误判 `PHYSICAL_CHANGED` | `test_borrow_flow`：Core 进 `WAIT_CONFIRM` 后，一个采样时刻早于已应用事件的 GET_SNAPSHOT 应答到达 → `physical` 被覆盖成旧值 → `wait_confirm_conflicted` 判冲突 → 转核验，后续 confirm 与 loans 查询全失败（TSan 树偶发，普通/ASan 树不复现） | `apply_snapshot` 增接收门：`!physical->valid \|\| boot_id 不同 \|\| snapshot->physical_revision >= physical->physical_revision` 才接受；physical 赋值、设备 active 记录、释放核对全部移进该门内。物理版本随每次门/物品变化严格递增（`host/simulator/src/sim.c:2351`、`:2388`），所以同 boot 下更小版本必然是旧应答 | `test_domain_device.c::test_snapshot_apply` 新增三段（同 boot 更大版本接受 / 同 boot 更小版本整体丢弃 / 换 boot 即使版本更小也接受）；集成 `test_borrow_flow` 稳定 |

### 7.7 跨模块移交（不在本卡可改目录）

| CR | 冲突 | 影响 | 建议 | 依赖 |
|---|---|---|---|---|---|
| `CR-S104-1` | `device_state.simulated` 在 `contracts/internal-data.schema.json` 中是必填字段，但 `locker_core_config_t` 没有任何来源能说明后端是不是模拟器，Core 只能写 `false` | 公开与持久化投影把模拟后端报成真实设备；审计 R1-N-2 已指出不能据此把模拟环境表述为实物设备 | 由契约负责人决定是给 `locker_core_config_t` 增加后端来源字段，还是把该字段改为可空；在此之前所有文档一律写「本卡证据来自模拟器」 | 等待部分：该字段语义；可继续部分：本卡其余实现 |
| `CR-S104-2` | `host/device/src/port.c` 的 `device_send_request` 在持有 `write_lock` 时同步调用 `locker_device_transport_write`（`transport.c:192` 的 `poll` 上限 1000 ms），背压时 Core 单所有者循环整段停顿 | 超时推进与真实进度一起被推迟；违反 `module-ownership.md:23` 与 S1-04 卡「不调用阻塞网络」 | 由设备协议负责人在 `host/device` 内实现有界发送队列，Core 只提交意图并消费回执；补「持续背压下 Core 进度、请求等待、失败闭锁」的正式测试 | 等待部分：端口语义；可继续部分：Core 侧调用点已全部经 `locker_device_*`，改端口不需要动 `host/domain` |