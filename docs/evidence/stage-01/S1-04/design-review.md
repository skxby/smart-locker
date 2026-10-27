# S1-04 单线程业务核心与恢复核对 — 设计评审报告

> 评审对象：任务卡 S1-04（`docs/stages/stage-01-task-cards.md:67-75`），阶段 1 第 4 张卡。
> 评审性质：**设计评审（design review），不是验收**。本报告为评审输入，**不构成 S1-04 的 DONE 证据或阶段 1 PASS 证据**；S1-04 至今未开工（`active_task=null`），`host/domain/` 目录尚不存在，无实现代码可核。
> 日期：2026-10-26。执行者：s104-design-review（评审协调员，只读）。
> 专家结论由 The Agency 名册两位专家并行给出，见 §2/§3；协调员对其中的关键行号做了**独立抽查复核**，标注在每条的「复核」行。

## 1. 评审输入与证据边界

| 项 | 状态 |
|---|---|
| 专家名册 | `list_experts({division:"engineering"})` 共 74 人，含 **Embedded Linux Driver Engineer** 与 **Privacy Engineer** |
| 召唤方式 | `summon_experts` 一次并行召唤 2 位（≤8），两位均返回 `ok:true`，无失败/不可用 |
| 已读文件 | `docs/stages/stage-01-task-cards.md`（1-37、60-80）、`docs/stages/stage-00-architecture.md`（88-130）、`docs/architecture/internal-interfaces.md`（86-162）、`docs/architecture/module-ownership.md`、`contracts/include/locker_contracts.h`、`contracts/internal-data.schema.json`（x-rules/OutboxRecord/ImportCursorRecord/Session/Card/DeviceEvent）、`host/storage/README.md`、`host/device/README.md`、`host/simulator/README.md`、`host/common/src/log.c`、`host/common/include/locker/common/clock.h` |
| 未读 | `contracts/examples/`、`docs/architecture/uart-v1.md` 全文（专家引用其行号，协调员未逐条复核） |
| 本轮动作 | 仅读文件与写入本报告；**未写代码、未改任何其他文件、未运行 CMake/CTest、未 commit** |

## 2. 专家一：Embedded Linux Driver Engineer（线程/端口/恢复）

### 阻塞级发现（6 条）

**E-B1｜重复动作：outbox 的"发送检查点"没有强制序，且 outbox 记录没有 `command_id`**
- 现象：`docs/architecture/internal-interfaces.md:114` 只有一句文字要求「写串口前先持久化"已尝试发送"检查点」，没有专用端口；唯一可用的事务入口 `locker_repo_commit_change`（`contracts/include/locker_contracts.h:211`）要求 revision+1（`contracts/internal-data.schema.json:5412`），**检查点提交失败的分支未定义**。`OutboxRecord` 字段止于 `attempted_at`，无 `command_id`（`internal-data.schema.json:4136-4204`），但 `locker_unlock_intent_t` 必带 command_id（`locker_contracts.h:111`），`Operation.command_id` 可空（`internal-data.schema.json:357`）。
- 触发路径：检查点提交失败仍调 `locker_device_execute_once`（`locker_contracts.h:221`）→ 重启后 outbox 仍为 `PREPARED`（`internal-data.schema.json:4172`）→ 被当作"未发送"重发；而 `locker_device_query_command` 只按 command_id 查（`locker_contracts.h:223`），此时无键可查。
- 整改：明文规定「检查点未提交成功即禁发并转 REVIEW_REQUIRED」；`PREPARED ≠ 未发送`，须凭 action_no 走 QUERY 定性；outbox 补 `command_id` 由契约负责人裁定。
- 复核：行号属实。补充确认 `locker_contracts.h:220` 注释已写明「每物理 command 一次发送，超时查询，禁止自动重发」，缺的是检查点失败时的实现级约束。

**E-B2｜丢失事件：事件导入游标缺 boot 语义**
- 现象：`ImportCursorRecord` 只有 device_id/journal_epoch/through/continuity_ok（`internal-data.schema.json:4388-4411`），而事实键含 boot_id（`locker_contracts.h:57` 附近，专家引用）。
- 触发路径：MCU 重启 boot+1（`host/simulator/README.md:41`：`restart` = close+open、boot+1、旧会话失效、保留未决上下文）→ 沿用旧游标 → 连续性误判 → 漏导或误判"无缺口"。
- 整改：boot/epoch 变更即判连续性失效并进核验；把 JOURNAL_CHANGED、CURSOR_GAP/REPLAY_BLOCKED 显式映射为 Core 状态；游标按代保存。
- 复核：`ImportCursorRecord` 字段属实；boot_id 语义专家引 `locker_contracts.h:57` 与 `uart-v1.md`，协调员未逐条复核该行，标为「待契约负责人确认」。

**E-B3｜丢失事件：ACK 水位未绑定提交结果**
- 现象：`locker_device_ack_persisted` 只收 cursor（`locker_contracts.h:227`），真实水位来自 `locker_repo_import_events` 的出参 `committed_cursor`（`locker_contracts.h:213`，`internal-interfaces.md:99`）。
- 触发路径：用内存水位或"本批末条"ACK → 越过未提交区间（违反 `internal-interfaces.md:127`「不倒退、不跨缺口、不越已提交记录」）→ MCU 回收明细 → 永久丢事件。
- 整改：ACK 参数只许取 `committed_cursor` 且同代；ACK 失败只重试、不推进内存水位。
- 复核：行号属实，`internal-interfaces.md:99` 明确「事务成功才允许 ACK_PERSISTED」。

**E-B4｜丢失事件：队列满反而打断会话**
- 现象：Core 队列 64、满即 `QUEUE_FULL`、明确不丢关键消息（`locker_contracts.h:17`），但 sink 返回 `NO_SPACE` 时设备侧会锁存、停 reader 并 shutdown（`host/device/README.md:81`）。
- 触发路径：事件突发 → `locker_core_apply_device_event` 返回 NO_SPACE → 会话作废 → 只能 FETCH 补取，而补取顺序未规定即成缺口。
- 整改：事件通道独立额度；或规定 NO_SPACE 时保留 pending、下个 tick 重投、禁止 shutdown；补取固定按 UART-v1 顺序。
- 复核：`locker_contracts.h:17` 与 `host/device/README.md:81` 均属实。

**E-B5｜阻塞 Core：端口调用没有超时预算**
- 现象：Core 单线程同步调 repo/device；device 侧写上限 1000 ms、pending 5000 ms、open 5000 ms（`host/device/README.md:75-76`、`:80`），而 `locker_core_wait_result` 只等 1000 ms（`locker_contracts.h:200`）。
- 触发路径：一次 `execute_once` 占数秒 → `wait_result` 全超时 → 客户端重试放大队列 → 单线程卡死。
- 整改：给出每 tick／每端口超时表；intent 只投适配器队列、由适配器线程执行后回调，Core 不直接写串口；规定 tick 上界与超限降级。
- 复核：行号属实。注意 `module-ownership.md:23` 已禁止 `host/domain/` 调用阻塞 IO，缺的是把该禁令量化成超时预算表。

**E-B6｜重启重放：恢复屏障缺 READY 判定**
- 现象：`locker_repo_load_recovery` 一次返回 64 KB JSON（`locker_contracts.h:147` 附近，专家引用），x-rules 却要求按 collection 分页、且开放请求前在同一 snapshot_revision 内完成必要集合恢复（`internal-data.schema.json:5418`）；`locker_core_start` 只说 remain RECONCILING（`locker_contracts.h:280`），无 READY 条件。
- 触发路径：恢复未完成即接纳 submit → 用未核对账本开门；旧 outbox 被当成待发队列（违反 `internal-interfaces.md:100`「不自动发送恢复出来的旧动作」）。
- 整改：定义 READY 门闩（游标连续、revision 一致、outbox 全部落到 FINISHED/REJECTED/REVIEW_REQUIRED）；恢复期 submit 回 NOT_READY；旧 outbox 只进核验。
- 复核：`locker_contracts.h:280`、`internal-data.schema.json:5418`、`internal-interfaces.md:100` 均属实；`locker_contracts.h:147` 的 64 KB 为专家引用，协调员未逐字复核。

### 重要但非阻塞
- 并发申请只靠 `slot_guards` 唯一 PUT（`internal-data.schema.json:5414`），CONFLICT→业务码映射未定。
- 单调时钟失败返回 0、与真实 0 不可区分（`host/common/include/locker/common/clock.h:29-38`），而超时判定全靠它。**复核：属实**，`clock.h:29`「returns 0 if the platform call fails」、`:36` 建议把"未推进"当异常。
- `locker_core_query` 与 progress 同走队列（`locker_contracts.h:208-209`），与 1000 ms 等待争用；progress 宜独立双缓冲。
- `reply.message_type` 带 0x80 待裁定（`host/device/README.md:61` 附近），关联逻辑不得依赖该位。
- S1-02 summary 仍标 IN_PROGRESS（`docs/evidence/stage-01/S1-02/summary.md:6`），与 AGENTS.md「S1-02 已 DONE」表述不一致。**复核：属实**，该行明确写「状态：**IN_PROGRESS**（等待最终独立收尾复核）」，而 `docs/stages/stage-01-task-cards.md:3` 写「S1-01、S1-02、S1-03 已完成（DONE）」。**这是真源不一致，需 lead 裁定，不属于 S1-04 设计缺陷。**

### 结论
**不能直接进入实现。** 必须先补四条设计约束：① 检查点失败即禁发、`PREPARED ≠ 未发送`；② ACK 只认 `committed_cursor` 且同代；③ READY 门闩与 outbox 终态；④ 端口超时预算与 tick 上界。①④未决 → 崩溃与突发窗口必然产生第二次 EXECUTE；②③未决 → 必然漏事件或重放旧意图。

## 3. 专家二：Privacy Engineer（令牌/UID/身份暴露面）

### 阻塞级发现（4 条）

**P-B1｜Core 投影没有字段白名单，卡与事件记录会被原样序列化出去**
- 现象：`locker_query_result_t` 只是一个 64 KB 不透明 JSON blob（`locker_contracts.h:118`），Core 是唯一能按 principal 裁剪字段的组件，但契约没有规定投影白名单。x-rules 只为视觉写了「public query projections are sanitized」（`internal-data.schema.json:5422`），对 `cards`/`device_events`/`sessions` **没有对等条款**；`:5416` 只说「会话/密码哈希/UID 行只在内部使用」，未定义"内部"边界与导出限制；S1-04 卡正文（`stage-01-task-cards.md:67-75`）与验收（`:73`）完全无隐私项。
- 触发路径：Core 实现卡片/事件查询时直接序列化 `CardRecord.uid_hex`（`internal-data.schema.json:4043-4045`）与 `DeviceEventRecord.card_uid_hex`（`:4481-4508`）→ HTTP 层拿到明文，`masked_uid` 退化成事后遮挡。
- 整改：S1-04 卡补「投影白名单」硬约束——每个查询只含公开字段集，明文 UID 与 `token_hash` 永不进入 `locker_query_result_t`；x-rules 增加与 `:5422` 对等的条款。
- 复核：`locker_contracts.h:118`、`internal-data.schema.json:5422/5416/4043-4045/4481` 均属实。

**P-B2｜卡 UID 明文被契约冻结，最小暴露无实现路径**
- 现象：`CardRecord.uid_hex` 为明文正则列（`internal-data.schema.json:4043-4045`），`card_uid_index` 以 uid_hex 为 key（`:5413`），读卡事件 `card_uid_hex` 为 required（`:4494-4508`），`locker_device_event_t` 直接带 `uint8_t card_uid[10]`（`locker_contracts.h:119`）。
- 触发路径：S1-04 验收要求"错误用户"判定（`stage-01-task-cards.md:73`），Core 只能拿明文 UID 逐字节比对；一旦落库即形成永久明文身份库。
- 整改：发契约变更请求——`cards` 增加 `uid_hmac`（密钥由 Core 持有），事件表只留 HMAC 或掩码值；比对用 HMAC 等价性，公开面继续用 `masked_uid`。
- 复核：行号属实。**注意：这是契约变更，按 `module-ownership.md:11` 与 `stage-01-task-cards.md:37` 必须由契约负责人统一改真源，业务卡不得自改。**

**P-B3｜命令参数是不透明字符串，脱敏器键名表覆盖不到**
- 现象：`locker_command_t.arguments_json[4097]` 原样承载请求正文（`locker_contracts.h:116`），而敏感键表只有 token/password/carduid/rawuid/uid 等（`host/common/src/log.c:126-131`），**不含 `params`/`arguments`/`body`/`payload`**。`internal-interfaces.md:83` 有「原始敏感 params 禁止出现在日志」的规则，但没有执行机制。
- 触发路径：Core 在 submit 失败路径记录 `arguments=<原文>` → `key_is_sensitive_n`（`log.c:158-182`）不命中 → 整段正文明文落盘。
- 整改：S1-04 明令禁止把 `arguments_json` 作为字段值记录（只记长度/摘要/命令名），或在 `log.c` 表中补 `arguments`/`params` 后缀项。
- 复核：`locker_contracts.h:116` 与 `log.c:126-135` 属实。

**P-B4｜证据与事件目录没有保留期和擦除路径**
- 现象：`AuditRecord` 结构良好（`internal-data.schema.json:4255-4326`，无 UID/token），但带 `card_uid_hex` 的 `DeviceEventRecord` 无 `expires_at`；全库唯一保留期是幂等记录 30 天（`locker_contracts.h:21` `LOCKER_IDEMPOTENCY_DAYS 30u`）。
- 触发路径：S1-04 的"恢复核对"要求长期保留事件与证据（`stage-01-task-cards.md:75`）→ 证据目录变成无期限身份库。
- 整改：给 `device_events`/`vision_evidence` 定保留期与自动清理作业；审计保留"谁在何时对哪个 operation 做了什么"，不保留 UID 原文。
- 复核：`locker_contracts.h:21` 属实；专家引用的 `docs/architecture/reviews/interface-expert.md:29` 协调员未复核。

### 重要但非阻塞
- **日志后缀表缺 uid**：等值表含 `"uid"`（`log.c:130`），后缀表（`log.c:133-136`）只有 carduid/rawuid，且后缀匹配走原键 → `reader_uid`、`subject_uid` 不命中。**复核：属实**，S1-04 若用这类字段名记录身份会泄漏。
- `Session.token` 的 `writeOnly: false`（`contracts/openapi.json:3454`），Session 对象任何序列化都带明文令牌；建议改 true。**复核：属实。**
- `token_hash` 是裸 SHA-256 无 salt（`internal-data.schema.json:4017-4019`，正则 `^[0-9a-f]{64}$`），与 S1-05 卡要求的 Argon2id 不一致；`LOCKER_SESSION_HOURS 8u`（`locker_contracts.h:22`）为固定绝对过期，无轮换与空闲超时。**复核：两处行号属实。**
- **正面项**：`locker_principal_t` 只持 `session_id` UUID 而非明文 token（`locker_contracts.h:115`），`locker_core_authenticate` 后 Core 不驻留凭据；重启恢复列表不含 sessions（`internal-interfaces.md:100`）；`log.h:31-34` 已明示"防御纵深非数据流证明"的边界。**复核：`locker_contracts.h:115`、`internal-interfaces.md:100` 属实。**

### 结论
**可以进入实现，但必须先补齐三条隐私约束，否则 S1-04 会把明文身份固化进 DB 与证据，S1-05 只能做事后遮挡。** ① Core 查询投影字段白名单（P-B1），写进 S1-04 验收项；② 卡 UID 的 HMAC 最小化契约变更（P-B2），在实现比对逻辑前完成；③ 事件/证据保留期与擦除作业（P-B4）。P-B3 与 uid 后缀缺口可在 S1-04 内以代码约束解决，不必等契约。会话生命周期（8 h 绝对过期 + 执行前重验 + 重启不恢复）当前设计可接受，不阻塞。

## 4. 汇总：阻塞级发现与整改优先级

**阻塞级合计 10 条**（E-B1..E-B6 六条 + P-B1..P-B4 四条）。按"不修就一定出事"排序：

| 序 | 编号 | 主题 | 后果 | 责任归属 |
|---|---|---|---|---|
| 1 | E-B1 | 发送检查点无强制序、outbox 无 command_id | 重复开锁动作 | 契约负责人 + Core |
| 2 | E-B3 | ACK 水位未绑定 committed_cursor | 永久丢事件 | Core + Storage |
| 3 | E-B6 | 恢复屏障缺 READY 门闩 | 未核对账本开门 / 重放旧意图 | Core |
| 4 | E-B5 | 端口调用无超时预算 | 阻塞 Core 单线程 | Core + Device |
| 5 | E-B2 | 导入游标缺 boot 语义 | 漏导事件 / 缺口误判 | Core + Storage |
| 6 | E-B4 | 队列满打断设备会话 | 事件补取缺口 | Core + Device |
| 7 | P-B1 | Core 投影无字段白名单 | 明文 UID/token_hash 出网 | Core（卡内可解决） |
| 8 | P-B2 | 卡 UID 明文被契约冻结 | 永久明文身份库 | 契约变更 |
| 9 | P-B3 | arguments_json 绕过脱敏 | 请求正文明文落盘 | Core（卡内可解决） |
| 10 | P-B4 | 事件/证据无保留期与擦除 | 证据目录变身份库 | 契约 + Core |

**关键观察：E-B1、E-B2、P-B2、P-B4 四条都指向同一件事——真源（`contracts/`）缺字段或语义，而 S1-04 是这些字段的第一个消费者。** 按 `module-ownership.md:11`、`stage-01-task-cards.md:37`，业务卡不得自改契约，因此这四条必须先由契约负责人裁定，否则 S1-04 开工即会在"按契约写"和"按正确性写"之间二选一。

## 5. 建议 lead 采取的下一步（仅建议，未执行）

1. **开工前先冻结四条设计约束**：检查点失败即禁发且 `PREPARED≠未发送`；ACK 只认 `committed_cursor` 且同代；READY 门闩与 outbox 终态；每端口超时预算表。写进 S1-04 卡的"产出/验收"，否则实现无判据。
2. **对 E-B1/E-B2/P-B2/P-B4 提契约变更请求**（位置/冲突/受影响调用方/建议），交契约负责人统一改 `contract-source.json` 并重新生成，不在 S1-04 里绕过。
3. **把 P-B1/P-B3 降级为 S1-04 卡内硬约束**（投影白名单 + 禁止记录 `arguments_json`），不需要等契约。
4. **顺带裁定一处真源不一致**：`docs/evidence/stage-01/S1-02/summary.md:6` 仍为 IN_PROGRESS，而 `docs/stages/stage-01-task-cards.md:3` 与 AGENTS.md 写 S1-02 DONE。与 S1-04 设计无关，但影响"下一张卡"的可信度。
5. **本报告不作为任何 PASS 证据**；S1-04 的真实 DONE 仍需状态转移场景矩阵与实际测试结果（`stage-01-task-cards.md:74`），且设备侧全部为模拟，不得表述为硬件验收。

## 6. 已知限制

- 专家引用的 `docs/architecture/uart-v1.md` 具体行号、`docs/architecture/reviews/interface-expert.md:29`、`locker_contracts.h:57`、`:147`、`:61`(device README) 未由协调员逐条复核，已在对应条目注明。
- `host/domain/` 尚不存在，无实现代码可核；本评审只针对契约与设计文档。
- 两位专家均为外部 AI 角色（The Agency 名册），其结论属**评审输入**，不替代项目内的独立审计轮次（S1-04 的 audit 轮应另行安排）。
