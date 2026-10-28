# S1-04 round-1 审计报告

结论：FAIL

阻塞项：15 项；非阻塞项：3 项。

审计日期：2026-10-27。本报告核对的是整改后 r198000 及当前工作树；按本次指令保留 round-1 标题。仅写本文件，不修改代码、契约、状态、summary、handoff，不 commit、不 push。契约实际为 0.2.1 / REVIEW_DRAFT；机器索引仍为 S1-04 IN_PROGRESS。

## 阻塞项

### R1-B-1 / P1 / 动作事务失败后仍能发送 EXECUTE

- **位置：** `host/domain/src/domain_commands.c:822`、`:839`、`:861`、`:877`（operation_arm / domain_outbox_pump）。
- **触发与复现：** 使用现有 dt 夹具建立 REQUESTED，令 repo->commit_status=IO_ERROR，再提交有效 presence；恢复提交能力后调用 domain_outbox_pump。VM 内存探针实测：首次失败后 active_state=ACCESS_PENDING、sends=0；再次泵送后 stored_state=REQUESTED、active_state=ACCESS_PENDING、sends=1。
- **影响：** operation/command/outbox 的原始事务未成功，后续仅写 SEND_ATTEMPTED 就发送了动作，违反 internal-interfaces.md:98、:114 的先提交后发送规则。
- **修复建议：** 在局部候选副本上构造与提交武装事务，成功后才安装 active/outbox/动作号；失败保持原状态且后续 tick 不得发送。增加“首次事务失败→仓储恢复→再次 tick”正式回归。**阻塞。**

### R1-B-2 / P1 / NO_CHANGE 未释放持久占位

- **位置：** `host/domain/src/domain_device.c:331`、`:345`、`:351`；`host/domain/src/domain_state.c:502`；`host/domain/src/domain_commands.c:430`。
- **触发与复现：** 正常申请并进入 ACCESS_ACTIVE，只开关门、不取物。operation_advance_from_evidence 仅持久化 operation 后清 used，没有删除 slot_guards。VM 探针实测 state=NO_CHANGE、active=0、guard_remains=1。
- **影响：** UI 可显示空闲，但下一次申请的 ABSENT 检查永远冲突；重启也不会修复终态遗留的 guard。现有单测只检查内存 used，没有检查仓储占位和下一次申请。
- **修复建议：** NO_CHANGE 与核验匹配的 guard 删除同事务完成，提交成功后才清内存；用真实 SQLite 覆盖“不取物→再申请”和重启。依据 internal-data.schema.json:5414。**阻塞。**

### R1-B-3 / P1 / 完成后没有释放设备活动上下文，无法连续借还

- **位置：** `host/domain/src/domain_commands.c:103`、`:562`、`:735`；`host/domain/src/domain_device.c:715`（仅处理释放响应，没有请求调用点）。
- **触发与复现：** 运行集成测试的完整借出流程后，在同一设备会话申请归还并提交 presence。全模块没有 locker_device_release_context 调用；模拟器 sim.c:1693 的 has_active 门会对下一次 EXECUTE 返回 BUSY，清除此上下文的正式入口在 sim.c:1860。
- **影响：** 首次借出成功不代表借还闭环可用；现有 test_borrow_flow 在借出后结束，未覆盖紧接着归还。只改主机账本不能释放 MCU 所有的物理关联。
- **修复建议：** 在完成/核验结果落库后，按当前 epoch、boot、operation、command、physical_revision 发起 RELEASE_CONTEXT，核对回执与补取，再准入后续动作；失败只查询/重核，不重放开锁。补同会话借出→归还→再借出集成测试。依据 internal-interfaces.md:129、uart-v1.md:408。**阻塞。**

### R1-B-4 / P1 / WAIT_CONFIRM 期间物理冲突被忽略

- **位置：** `host/domain/src/domain_device.c:331`、`:356`、`:423`；`host/domain/src/domain_commands.c:546`。
- **触发与复现：** 建立 WAIT_CONFIRM，注入新的 DOOR_OPENED，随后重新关门并恢复目标物品状态。VM 探针确认开门后仍为 WAIT_CONFIRM；operation_advance_from_evidence 对该状态立即返回，confirm 只比较当前门/物品和旧 item_transition_seen。
- **影响：** 已知物理冲突可在状态恢复后被抹去，用户仍可完成操作，违反 stage-00-architecture.md:118 和 internal-interfaces.md:116。
- **修复建议：** 保存确认依据的 boot/physical_revision，对等待确认后的相关新物理事实、UNKNOWN、代变化立即进入核验并持久化；新快照也走同一冲突规则。补“开→关恢复原状”和 UNKNOWN 后恢复的回归。**阻塞。**

### R1-B-5 / P1 / 恢复截止点混用 UTC 和单调时钟

- **位置：** `host/domain/src/domain_state.c:354`、`:495`、`:497`；`host/domain/src/domain_core.c:143`、`:145`。
- **触发与复现：** 恢复带 UTC updated_at 的 REQUESTED/WAIT_CONFIRM，随后推进单调时钟超过 120 秒。VM 探针：mono=1000，wait_confirm_deadline_ms=1761004920000；推进 130 秒仍为 WAIT_CONFIRM。
- **影响：** UTC epoch 毫秒被当作进程单调截止点，恢复后的申请/确认可能长期不超时，占位不能按规格退出。正常 confirm/cancel 重新加载记录也会引入这一混用。
- **修复建议：** 仅对允许恢复的等待状态用可信墙钟计算剩余有效期，再映射到当前单调时间；可能已执行的 WAIT_CONFIRM 在进程重启时应按主状态表进入核验，不能重建成可直接确认。未知/过期保守处理，补过期、未过期、墙钟不可用及重启回归。**阻塞。**

### R1-B-6 / P1 / TIME_ANCHOR 未接入动作期限

- **位置：** `host/domain/src/domain_device.c:660`、`:745`；`host/domain/src/domain_time_anchor.c:59`、`:92`。
- **触发与复现：** 让主机和 MCU 单调时钟具有不同起点，或使锚点缺失、过期、RTT 超限。domain_reply_apply 没有 TIME_ANCHOR 的 case 10；domain_anchor_apply / domain_anchor_mcu_now 均没有调用方；发送期限直接取 core->now_ms + LOCKER_LOCK_PULSE_MAX_MS。
- **影响：** 主机时间被直接当作 MCU 时间，既可能误拒动作，也可能错误延长命令在 MCU 时间域中的有效性；当前模拟器与主机共用时钟掩盖了问题。
- **修复建议：** 接入锚点响应及 boot/RTT/年龄校验，用保守边界换算冻结的意图期限，无有效样本就拒发；按规定周期请求而非每轮请求。补独立双时钟、过期及重启回归。依据 internal-interfaces.md:131–133。**阻塞。**

### R1-B-7 / P1 / 缺少 UI 时仍伪造新鲜 UI 健康

- **位置：** `host/domain/src/domain_device.c:231`、`:239`、`:241`、`:243`。
- **触发与复现：** 不启动 UI/supervisor，运行 Core 设备泵，观察 send_health 构造的 report：ui_instance_id 等于 Core 实例，ui_progress 等于 Core 计数，ui_age_ms 恒 0。
- **影响：** Core 活着被报告成 UI 活着，违反 S1-04 健康基线及 internal-interfaces.md:154；阶段 1 允许 UI 未知，但不允许伪造 UI 进度。
- **修复建议：** UI 未知使用契约规定的零 UUID / UINT32_MAX age，SERVICE 只携带真实源样本及年龄；保留 supervisor UNKNOWN。增加无 UI 和 Core 停滞时的报文断言。**阻塞。**

### R1-B-8 / P1 / 幂等只持久化 create，且哈希未规范化

- **位置：** `host/domain/src/domain_commands.c:180`、`:203`、`:224`、`:449`、`:558`、`:604`、`:735`、`:1342`。
- **触发与复现：** 用同一幂等键重复成功的 cancel/confirm/resolve；这些处理器没有写 IdempotencyRecord。VM 探针带合法取消理由实测同键取消 first=OK、second=CONFLICT。create 同键仅调整 JSON 字段顺序/空白，也会因直接哈希 arguments_json 而冲突。
- **影响：** 客户端在提交后丢响应时无法取回原结果；同意图重试不满足契约。contract-source.json:1442、:1454、:1501 明确三个操作均幂等；internal-interfaces.md:47 要求 Core 规范化哈希。
- **修复建议：** 所有有副作用命令的结果/幂等记录与业务事务一起提交；按用户、方法、规范路径及规范正文构造哈希，处理有效期；同键异意图冲突，同键同意图返回原响应。补重启后的重试。**阻塞。**

### R1-B-9 / P1 / 未恢复工具投影，也未执行工具版本检查

- **位置：** `host/domain/src/domain_core.c:297`；`host/domain/src/domain_commands.c:341`、`:374`、`:1220`。
- **触发与复现：** Core 生产路径没有给 tool_valid/tool_id/tool_revision 赋有效值，也没有恢复 tools 集合；create 仅在 tool_valid 为真时检查 ID，并直接忽略 tool_revision。VM 探针在 tool_valid=false 时提交未知工具和 revision=999，返回 OK。
- **影响：** 正常启动后工具查询为空，客户端无法从查询进入申请；若直接提交 UUID，则任意工具/陈旧版本可进入账本。单测手动初始化工具掩盖了生产路径缺失。
- **修复建议：** 按配置/仓储真源恢复本柜唯一工具并校验版本、状态、柜格绑定；工具不存在或未知时拒绝申请，不能以 false 跳过校验。补真实仓储启动查询→借还、未知 ID 和陈旧版本测试。**阻塞。**

### R1-B-10 / P1 / 人工核验理由和执行管理员没有落库

- **位置：** `host/domain/src/domain_commands.c:673`、`:720`、`:735`；`host/domain/src/domain_docs.c:257`。
- **触发与复现：** 两次分别使用不同管理员/不同 reason 对测试操作做 resolve，查看事务及 audits。reason 只被检查长度，未传给持久化；reason_code 固定为 ADMIN_RESOLVED，actor 仍为原申请人；domain_write_audit 只有定义/声明，没有任何调用方。cancel 的输入理由也未留存。
- **影响：** 无法证明谁基于什么理由覆盖结果，manual_resolution=true 不能替代审计记录；违反任务卡及 contract-source.json:1502 的留原因要求。
- **修复建议：** 将实际管理员、操作、结论、理由、物理版本与时间写入同一终结事务的正式审计记录；提交失败不留半条记录。测试真实查询与重启后的保留情况。**阻塞。**

### R1-B-11 / P1 / EXECUTE 应答丢失后在飞标志永久占用

- **位置：** `host/domain/src/domain_device.c:539`、`:626`、`:732`；`host/domain/src/domain_commands.c:881`、`:1036`。
- **触发与复现：** 丢 EXECUTE 应答，保留 PULSE 事件和 QUERY_COMMAND 应答。execute_in_flight 只在 apply_execute/reset 清除，apply_query_command 不清除；超时清命令又要求 !execute_in_flight。现有 test_interrupt_ack_lost 只断言查询发生、没有重复脉冲，未验证后续操作。
- **影响：** 已核对后的命令仍占发送门，后续合法新命令无法发送；QUERY 返回 PULSE_FINISHED/REJECTED/UNKNOWN_RESULT 状态也缺完整处理。
- **修复建议：** 按命令身份、查询结论和截止点关闭等待状态，完整处理最终命令状态；不把清等待标志变成旧 EXECUTE 可重发。补丢应答→核对→核验/完成→新操作回归。**阻塞。**

### R1-B-12 / P1 / 过期物理缓存仍可用于核验改账

- **位置：** `host/domain/src/domain_commands.c:48`、`:696`；`host/domain/src/domain_device.c:428`；`host/domain/src/domain_docs.c:68`；`host/domain/src/domain_state.c:686`。
- **触发与复现：** 曾取得 CLOSED/ABSENT 快照后断开设备并停止更新，再用缓存 physical_revision 做 COMPLETE_BORROW。resolve 不检查当前会话或采样年龄，只检查缓存值；快照失败不会清 device_snapshot_valid，公开 observed_age_ms 恒 0，设备模式仅按 Core phase 派生。
- **影响：** 离线/过期状态看起来仍 READY 且新鲜，管理员可凭旧物理事实完成账本，不能证明当前门关、物品明确。summary 把该龄期推给后续 Runtime，但本卡的核验判定已经消费它。
- **修复建议：** Core 维护原始采样时间、来源与会话失效，查询按当前时间计算年龄；核验使用可验证的新鲜同代证据，失联/未知拒绝改账。补断线后核验与延迟快照测试。**阻塞。**

### R1-B-13 / P1 / 查询没有落实资源所有者和管理员权限

- **位置：** `host/domain/src/domain_commands.c:1097`、`:1156`、`:1282`、`:1303`。
- **触发与复现：** 用已认证普通用户 B 查询用户 A 的 OPERATION_GET，或提交 REVIEWS_LIST。domain_handle_query 只做 principal 有效性检查，前者无 OWNER/ADMIN 比较，后者无 ADMIN 检查即扫描全部核验操作。
- **影响：** 普通合法会话可以读取他人操作和管理员核验列表，违反 contract-source.json:1424–1429、:1484–1487。HTTP 尚未实现，不能用未来适配层掩盖当前 Core 权限决策缺失。
- **修复建议：** 按冻结的 command/query 绑定在 Core 执行授权，OPERATION_GET 校验所有者或管理员，REVIEWS_LIST 仅管理员；公开快照遵守同等可见性。补两个真实用户和普通用户调用管理查询的测试。**阻塞。**

### R1-B-14 / P2 / Core 线程同步等待设备写 IO

- **位置：** `host/domain/src/domain_device.c:178`、`:208`、`:245`、`:747`；调用链到 `host/device/src/port.c:523`、`host/device/src/transport.c:192`。
- **触发与复现：** AF_UNIX 对端停止读取、发送缓冲写满时，Core 的快照/健康/动作端口调用进入 transport_write 的 poll 等待及 write_lock；设备端口不是异步入队即返回。可用不读 socket 的正式测试服务端制造背压。
- **影响：** 单所有者循环、超时推进和真实进度一起停顿；有写超时上限仍属于阻塞 IO，违反 module-ownership.md:23 和本卡禁止项。正常可写 socket 的 CTest 未覆盖该条件。
- **修复建议：** 由设备适配器所有者实现有界发送队列，Core 只提交意图并消费回执；共享端口变更走统一负责人。补持续背压时 Core 进度、请求等待和失败闭锁测试。**阻塞。**

### R1-B-15 / P1 / 终结提交失败后内存终态未回滚，超时路径还直接遗忘操作

- **位置：** `host/domain/src/domain_commands.c:109`、`:138`、`:160`、`:955`、`:956`；`host/domain/src/domain_device.c:345`。
- **触发与复现：** 在现有 test_finish_and_loan_same_transaction 的 IO_ERROR 注入后检查 active.state，而不只检查 loan_open；finish_operation 在提交前就把 core->active 改成终态。REQUESTED 到期且提交失败时，domain_timeouts 无条件 reset；require_review 失败后已为 REVIEW_REQUIRED，下次调用直接 OK、不再补持久化。
- **影响：** 数据库仍为待处理状态，内存却完成/遗忘/假核验，后续自动超时与占位处理失效；设备证据推进还会在持久化失败时发布变化。事务原子性修复只覆盖了库内操作和 loan，未覆盖 Core 工作副本。
- **修复建议：** 各状态变化在局部候选副本上完成，提交成功才替换内存、发布与清理；失败保留可继续核对的旧状态。将完成、核验、申请到期、NO_CHANGE 的提交失败都纳入正式回归。**阻塞。**

## 非阻塞项

### R1-N-1 / P2 / 当前交付文档与实际状态不一致

- **位置：** `docs/evidence/stage-01/S1-04/summary.md:6`、`:8`、`:28`、`:30`、`:249`；`docs/evidence/stage-01/S1-04/handoff.md:3`、`:14`；`host/domain/README.md:67`。
- **事实与影响：** summary 提前标 DONE，handoff 称 S1-05 可立即开始；Git 实际正常且仅有基线 cd4505e，不能声称 exit 128；manifest 实际 81 项而非 78；r198000 本地为 6 个日志（sanitizer 日志合并）而非 10 个；summary 的 audit 链接目录错误。summary=31732 B、handoff=12867 B，超过 P3 上限。
- **建议：** 开发侧在整改交接时同步真实状态、证据数量、文件边界和链接并压缩模板；不得把“历史声明的三套绿”写成独立审计通过。当前审计不代改这些文件。**非阻塞文档项，随整改交接处理。**

### R1-N-2 / P2 / simulated 标识仍需契约负责人解决

- **位置：** `host/domain/src/domain_import.c:215`；`host/domain/src/domain_state.c:709`；`docs/evidence/stage-01/S1-04/handoff.md:155`。
- **事实与影响：** 模拟器配置 simulated=true，但 Core 公开和持久化投影均写 false；现有 Core 配置缺少该来源，CR-S104-1 尚未关闭。不能据此把模拟环境表述为实物设备。
- **建议：** 由唯一契约写者统一补充可追溯的后端来源/装配方案，保持生成器维护，不由审计或本卡私加 schema。S1-06 集成前必须落实并测真实输出。**非阻塞契约债务。**

### R1-N-3 / P2 / 既往独立审计来源不能由现存文件证明

- **位置：** `docs/evidence/stage-01/S1-04/audit-codex-output.txt:1`；`docs/evidence/stage-01/S1-04/handoff.md:15`；`AGENTS.md` 的逐轮审计归档规则。
- **事实与影响：** 当前 audit-codex-output.txt 为 0 字节；本卡没有 audit-roundN 四件套，也未见所述 audit-run.log。可读到设计评审输入，但没有可核实的 round-1 正式原始转录，不能替它追认审计来源。
- **建议：** 编排方按真实轮次保存 prompt、BOM runner、含 START/END/EXIT 的 run.log 和原始输出；既往缺失明确记为缺失。本轮仅获授权写 audit.md，不补造历史产物。**非阻塞留痕项。**

## 已核实的整改与证据

- **既往 A/B/C/D：** 当前 finish_operation 已将 operation、guard 删除和 loan 组合在同一个 change；loan 条件在 mutations 前写入，domain_change_check 已拒绝 mutation 后追加检查；resolve 已检查借出 ABSENT/归还 PRESENT；ACCESS_ACTIVE 已有基于 pulse_started_ms 的 30 秒未开门出口（domain_commands.c:112、:704、:715、:998）。这些实现改动存在，但 R1-B-1/B-15 等未解决，不能据此给整卡 PASS。
- **Git 与范围：** %REPO% 下 git status --short 本轮 exit=0；HEAD=cd4505e。没有上一卡到本卡的提交区间，故结合当前 diff、已有 S1-04 源码、未跟踪集成测试及验收/契约审查。未清理或修改已有 .s104-tmp、开发侧文件和证据。
- **源码一致性：** source-manifest.txt 实际 81 项，逐项 SHA-256 对当前 Windows 文件及 VMware guest 的 `~/locker-workspace/S1-04/batches/r198000/src` 均为 81/81 匹配。该匹配只证明所列文件一致，不证明清单覆盖全部项目，也不能给历史 r194000 追授最终源码身份。
- **环境：** ssh locker-vm 成功，guest 为 Ubuntu x86_64 / Linux 7.3.0-rc5；使用现有 r198000 源码及同批次、位于 src 外的 build。缓存确认 Debug、LOCKER_BUILD_TESTS=ON、LOCKER_WARNINGS_AS_ERRORS=ON。没有使用 WSL。
- **本轮实跑：** `cmake --build ~/locker-workspace/S1-04/batches/r198000/build -j2` exit=0；`ctest --test-dir ~/locker-workspace/S1-04/batches/r198000/build --output-on-failure` exit=0，27/27，19.27 秒。三条 `python -B scripts/check_{contracts,stage_plan,vision_contracts}.py` 分别 exit=0/PASS；设计检查不证明业务或硬件通过。
- **额外反例：** 通过 SSH stdin 使用既有 dt 夹具及当前 Debug 静态库，在 Linux memfd 中编译/执行审计探针，编译/运行 exit=0，无新增磁盘源码或测试文件。复现 B-1、B-2、B-4、B-5、B-8、B-9；上述条目给出夹具条件、调用顺序和观察值。探针用于发现反例，不充当正式回归，开发侧必须落正式测试。
- **历史测试证据：** 已读取 r194000 及 r198000 日志；r198000 普通、ASan+UBSan、TSan 日志均记载 27/27 / CTEST_EXIT=0。sanitizer 本轮未重跑，不能称为本轮独立 sanitizer PASS；全部绿的现有测试未覆盖上述反例。测试改动中 revision 由两次提交变一次、publish 只保留 operation 的断言调整，不能证明缺失的 loan/工具变化通知已经满足消费者要求。
- **边界：** Core 使用统一契约端口，未引入 CivetWeb/SQLite/HAL 实现类型或新增外部依赖；既有依赖锁仍为固定版本，未下载 master 或改第三方库。真实账本与设备驱动所有权不能代替状态机、权限及故障路径的验收。

## 未运行与限制

- **NOT_RUN：** 本轮没有重跑 ASan/UBSan/TSan，没有重新配置全新 build；使用指纹匹配的现有 Debug build 完成普通构建和 CTest。Sanitizer 历史日志与本轮执行严格区分。
- **NOT_RUN：** RK3568/ARM、STM32、真实 UART/GPIO、锁脉冲硬截止、掉电持久性、真实相机/RKNN及整柜门禁。硬件 profile 仍 UNVERIFIED，不能用模拟器或静态检查替代。
- **未补正式测试：** 审计未修改测试或业务代码；本报告中的反例需开发侧补正式 regression，完整整改后再独立复验。原有 27/27 是现有测试结果，不是所有验收项通过。
- **交付判断：** 本卡继续整改，不应标 DONE，不应据此启动依赖卡。本文只报告问题、证据及限制，不执行状态更新、交接或版本提交。
