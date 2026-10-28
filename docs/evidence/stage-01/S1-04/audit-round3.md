# S1-04 round-3 审计报告

结论：FAIL

阻塞项：14 项；非阻塞项：4 项。

审计日期：2026-10-28。对象为 Windows 当前工作树与 VMware Ubuntu canonical `r201300`；契约实际为 **0.2.1 / REVIEW_DRAFT**，`active_task=S1-04`，状态仍 **IN_PROGRESS**。本轮只写本文件，不整改代码、不修改契约、stage-plan、README、summary、handoff 或其它证据，不 commit、不 push。以下“已修复”仅指对应问题，不表示整卡通
过；跨模块移交不豁免原阻塞。

## round-2 阻塞项逐条复核

| 原编号 | 本轮判定 | 实现与复核证据 | 未闭合部分 |
|---|---|---|---|
| R1-B-1 | 已修复原反例 | `host/domain/src/domain_commands.c:1141,1194,1200`：candidate 提交后才装 active/动作号；VM 探针在武装事务 IO_ERROR 后
为 REQUESTED、outbox=0、action=1，仓储恢复后直接泵送仍 sends=0 | 正式测试仍应固化“失败后再泵送”反例 |
| R1-B-2 | 已修复占位删除 | `host/domain/src/domain_device.c:399,409,412,421`：NO_CHANGE 与 slot_guards 删除同事务，成功后装内存；VM 探针 guard 不存在 | 设备上下文释放仍漏此路径，见 R3-B-2 |
| R1-B-3 | 部分修复，仍阻塞 | `host/domain/src/domain_commands.c:205,1306` 增释放登记与泵；`domain_device.c:813` 处理成功响应 | NO_CHANGE 未登记
释放、释放未完成仍可武装、重启不重建终态待释放上下文；R3-B-2 |
| R1-B-4 | 部分修复，仍阻塞 | `host/domain/src/domain_device.c:354,425` 增冻结依据及事件冲突检查 | GET_SNAPSHOT 未调用冲突检查，confirm 未比较冻结依据；R3-B-3 |
| R1-B-5 | 部分修复，仍阻塞 | `host/domain/src/domain_core.c:169,189` 对恢复入口换算 UTC/mono | 普通 operation_load 仍混用时钟；WAIT_CONFIRM 重
启仍可确认；R3-B-4、R3-B-13 |
| R1-B-6 | 部分修复，仍阻塞 | `host/domain/src/domain_device.c:822` 接锚点响应；`domain_time_anchor.c:49` 周期限制；`domain_commands.c:1163` 持久化 MCU 期限 | 实际 EXECUTE 在 `domain_device.c:851` 重写为主机时刻；R3-B-1 |
| R1-B-7 | 已修复原 UI 造假 | `host/domain/src/domain_device.c:244` 零 UI UUID、progress=0、age=UINT32_MAX、supervisor UNKNOWN；正式
设备单测有对应断言，833 checks PASS | 未把未来 supervisor/UI 实现冒充已完成 |
| R1-B-8 | 部分修复，仍阻塞 | `host/domain/src/domain_commands.c:268` 顶层键排序，`:393` 新增三路由记录；新增同键成功重放测试 | 记录仍另开事务，失败被吞；R3-B-5 |
| R1-B-9 | 部分修复，仍阻塞 | `host/domain/src/domain_core.c:360,428` 恢复工具；`domain_commands.c:566,570` 拒未知工具/错误 ID | 显式版本 0 绕过匹
配；R3-B-9。恢复测试增加 query_calls 不等于已测真实工具恢复内容 |
| R1-B-10 | 部分修复，仍阻塞 | `host/domain/src/domain_commands.c:1017` 已有管理员及理由审计写手 | 业务先提交，审计独立提交且返回值丢弃；取消理由仍丢失；R3-B-6 |
| R1-B-11 | 部分修复，仍阻塞 | `host/domain/src/domain_device.c:733` 查询返回时清在飞标志 | WAIT_CONFIRM 无查询出口、查询也丢失时仍锁死；R3-B-8 |
| R1-B-12 | 核验入口主要修复，投影尚有残项 | `host/domain/src/domain_commands.c:78,951` 核验检查会话和 30s 龄期；`domain_device.c:510` 失败快照清有效性；
`domain_docs.c:34` 写真实/未知龄期 | mode 仍只由 Core phase 得出；R3-N-4。不能把 ProgressReport 的说明直接当 PhysicalSnapshot 的完整来源规则 |
| R1-B-13 | 已修复原两处越权 | `host/domain/src/domain_commands.c:1517,1726` OWNER/ADMIN 与 ADMIN 检查；新增双用户/管理员查询断言，commands 579 checks PASS | 仅确认原 OPERATION_GET / REVIEWS_LIST 反例关闭，不追认全部未来 HTTP 鉴权 |
| R1-B-14 | 未修复，仍阻塞 P2 | `host/domain/src/domain_device.c:185,211,250` 仍同步进入 `host/device/src/port.c:523` → `host/device/src/transport.c:192` | CR-S104-2 是移交记录，不是修复或豁免；R3-B-10 |
| R1-B-15 | 部分修复，仍阻塞 | `host/domain/src/domain_commands.c:157,231,1361` 的终结/核验 helper 与申请超时采用提交后安装 | 设备响应/恢复分支仍先改内存、吞提交
错误；cancel 先覆盖 active；R3-B-7、R3-B-13 |

## 本轮阻塞项

### R3-B-1 / P1 / 实际 EXECUTE 丢弃已冻结的 MCU 期限

- **位置：** `host/domain/src/domain_commands.c:1163,1167,1281`；`host/domain/src/domain_device.c:845,851`。
- **触发与复现：** 给有效锚点偏移 +500000ms、host_now=1000，冻结 outbox_not_after_ms=503000，调用 domain_execute_intent。VM 探针实测 stored=503000，sent=2000。
- **影响：** 新锚点只影响持久意图和泵前检查，线上期限仍处于主机时钟域。独立双时钟会误拒或错误放宽期限；同源时钟测试掩盖它。实际发送还用当前 session/physical 重建 guard，没有完整透传冻结意图。依据 internal-interfaces.md:125,133。
- **修复建议：** 发送与已提交 outbox 同一组不可变 guard/期限，校验冻结 session；锚点缺失/过期/跨 boot 拒发。补正负偏移、传输延迟、样本更新/设备重启后实际 intent 字段断言。**阻塞。**

### R3-B-2 / P1 / 上下文释放没有形成下一次动作的屏障

- **位置：** `host/domain/src/domain_device.c:409,418`；`host/domain/src/domain_commands.c:581,1115,1306,1341`；`host/domain/src/domain_core.c:144,485,487`。
- **触发与复现：** ACCESS_ACTIVE 开关门且物品未变，操作带真实 command_id，事件推进 NO_CHANGE 后泵释放。VM 实测 state=NO_CHANGE、used=0、release_pending=0
、release_calls=0。另在终结落库后、释放完成前提交下一次 presence，武装/发送没有 release_pending 门；core_tick 还先 outbox 后 release。终结提交后进程崩溃也丢失纯内存释放登
记，恢复跳过终态。
- **影响：** MCU has_active 未清，后续 EXECUTE 碰 BUSY；“只开门不取物→再申请”和借还连续使用仍不成立。CONFLICT 也不能证明 MCU 不持有上下文；`:1341` 直接清标志违背 uart-v1.md:411 的 CAS 失败先查询规则。
- **修复建议：** 统一所有已执行终态的释放入口；以设备查询/匹配回执闭合释放并阻止新武装；重启从持久结果及设备活动上下文恢复核对。补同会话借→还→再借、NO_CHANGE→再申请、释放失败/丢回执/终结后重启的真实 SQLite+模
拟器测试。**阻塞。**

### R3-B-3 / P1 / 新快照中的物理冲突绕过 WAIT_CONFIRM 检查

- **位置：** `host/domain/src/domain_device.c:354,508,579,770`；`host/domain/src/domain_commands.c:783`。
- **触发与复现：** WAIT_CONFIRM 冻结 physical_revision=7，收到同代 CLOSED/ABSENT 但 revision=8 的成功 GET_SNAPSHOT。VM 实测 physical=8，basis=7，state 仍 WAIT_CONFIRM；apply_snapshot 没有调用 wait_confirm_conflicted。confirm 重新加载记录后也不检查冻结版本/boot。
- **影响：** 经新快照已知的“开门又关回原状”等冲突可被忽略，仍按旧证据确认，违反 internal-interfaces.md:116。注释称快照同规则并非实现事实。
- **修复建议：** 事件和快照统一执行 boot/epoch/physical_revision 冲突判定；confirm 在同一决策点核对冻结依据，持久化失败也不得继续准许确认。补只经快照得知冲突、UNKNOWN 后恢复和同值新版本
用例。**阻塞。**

### R3-B-4 / P1 / 重启后的 WAIT_CONFIRM 可直接完成

- **位置：** `host/domain/src/domain_core.c:172,447`；`host/domain/src/domain_commands.c:778,783`；`tests/unit/domain/test_domain_core.c:538,554`。
- **触发与复现：** 持久化 WAIT_CONFIRM 且 item_transition_seen=true，清 active 后 domain_recover，再以原 revision 确认。VM 探针实测恢复 state=WAIT_CONFIRM、basis_valid=0，confirm=OK、loan_open=1。恢复仅把 ACCESS_PENDING/ACCESS_ACTIVE 转核验；正式测试反而断言恢复 WAIT_CONFIRM。
- **影响：** 可能已执行的操作在服务重启后绕过核验；单纯换算 120s 不能恢复已丢失的确认上下文。违反 stage-00-architecture.md:119 的可能已执行状态重启规则。
- **修复建议：** WAIT_CONFIRM 重启走持久核验屏障，确认依据缺失时拒绝普通确认；修正目前锁定错误预期的测试。补有/无新事件、期限内/外以及核验落库失败重启用例。**阻塞。**

### R3-B-5 / P1 / 幂等记录仍与业务结果分开提交

- **位置：** `host/domain/src/domain_commands.c:393,419,798,812,847,860,987,1007`。
- **触发与复现：** 只令包含 idempotency mutation 的提交返回 IO_ERROR，允许取消业务事务成功，然后重试原键。VM 探针实测 first=OK、replay=CONFLICT，仓储为 CANCELLED。
confirm/resolve 同样在业务完成后另存记录；进程在两次提交间退出也产生相同窗口。
- **影响：** 已提交结果在丢响应后无法可靠取回，违反 internal-interfaces.md:98 的 operation/loan/审计/幂等同事务要求。revision+1 只使成功路径一致，不能解决原子性；store 失
败还被当成功返回。
- **修复建议：** 在同一 change 预构造最终资源与真实目标 revision，将幂等响应和业务提交一次落库；同键重试返回原结果。补第二步故障/重启场景、响应逐字节及 revision 一致性，不只测连续成功。**阻塞。**

### R3-B-6 / P1 / 人工理由审计失败仍可成功覆盖账本

- **位置：** `host/domain/src/domain_commands.c:824,847,987,998,1036,1057`。
- **触发与复现：** 仅 audits 提交返回 IO_ERROR，执行合法 COMPLETE_BORROW。VM 实测 status=OK、stored_state=COMPLETED、loan_open=1、audits=0。原取
消 reason 仍只解析、不持久化。
- **影响：** 业务已生效却没有实际管理员/理由记录；当前 `(void)` 也没有持久登记缺陷。256 字节理由截断后拼 suffix 还可能切断 UTF-8 字符；不能把“检查过原因”视为“保留原因”。依据 internal-interfaces.md:98、contract-source.json 的 resolve 说明。
- **修复建议：** 将真实 actor、结论、完整合法理由及关联信息放进同一终结事务，失败整体不生效；取消原因同样保留。尊重 schema 字符/UTF-8 边界；补失败、重启查询和长中文理由正式回归。**阻塞。**

### R3-B-7 / P1 / 设备状态推进仍先改内存再吞提交错误

- **位置：** `host/domain/src/domain_device.c:477,480,651,666,682,695,740,751`；`host/domain/src/domain_core.c:449,451`。
- **触发与复现：** 库里 ACCESS_PENDING，对 EXECUTE 的 BAD_PRECONDITION 回复注入 commit_status=IO_ERROR。VM 实测 active=REVIEW_REQUIRED、stored=ACCESS_PENDING、publish_calls=1。PULSE_STARTED、QUERY 回复和恢复转核验同样直接改 active，再忽略持久化结果。
- **影响：** helper 的 candidate 修复没有覆盖所有调用链；内存/数据库/发布版本再次分叉。内存已是核验时 domain_require_review 短路 OK，不能自动补库；恢复还可在失败后设置 recovery_ready。
- **修复建议：** 所有响应/事件/恢复跃迁统一提交候选，再安装及发布；提交失败保留可重试或显式故障闭锁状态。补上述各入口 IO_ERROR→恢复→再核对的数据库/内存/发布联合断言。**阻塞。**

### R3-B-8 / P1 / 丢 EXECUTE 应答后的等待仍可能永久锁住

- **位置：** `host/domain/src/domain_commands.c:1374,1391,1417,1446`；`host/domain/src/domain_device.c:715,733`。
- **触发与复现：** EXECUTE 应答丢失，但门/物品事件在 5s 内已推进 WAIT_CONFIRM。VM 探针推进到 7000ms 后 query_calls=0、execute_in_flight=1、pending=1。查
询仅在 ACCESS_PENDING/ACCESS_ACTIVE 发起；若 QUERY 自身也丢失，超时清 pending 仍要求 !execute_in_flight。操作终结后 apply_query_command 又因 !used 提前返回。
- **影响：** 后续 outbox 永久撞在飞门；仅证明“ACK 丢失时发生了一次查询、不重复脉冲”不能证明下一次业务可用。
- **修复建议：** 命令等待生命周期独立于业务状态，所有可能已执行状态均有有界查询/核验收敛；结果未知只阻断旧命令重发，不无限占用新命令槽。补快速到 WAIT_CONFIRM、查询丢失/迟到、完成/核验后再次操作。**阻塞。**

### R3-B-9 / P1 / 显式 tool_revision=0 可绕过版本检查

- **位置：** `host/domain/src/domain_commands.c:574`；`contracts/contract-source.json:726`。
- **触发与复现：** 当前工具 revision=1，提交必填 tool_revision 文本“0”。VM 探针 result=OK。代码给 0 加了“兼容旧客户端”的未授权例外。
- **影响：** 不匹配版本仍可占柜并进入动作链；真源要求提交版本并做乐观检查，没有“不声明版本”的分支。
- **修复建议：** 精确匹配当前版本，不私增兼容语义；补 0/陈旧/未来版本及真实仓储恢复后的工具内容测试。**阻塞。**

### R3-B-10 / P2 / 跨模块阻塞 IO 仍违反本卡禁止项

- **位置：** `host/domain/src/domain_device.c:185,211,250,854`；`host/domain/src/domain_commands.c:1330`；`host/device/src/port.c:523`；`host/device/src/transport.c:192,222`。
- **触发与复现：** AF_UNIX 对端保持连接但不读，缓冲耗尽，Core 发送调用进入 write_lock 与最长 1000ms 的 poll。当前调用链仍存在；本轮未新增背压运行测试。
- **影响：** 单所有者循环的超时、队列和进度同步停顿，违反 module-ownership.md:23 及 S1-04 “不调用阻塞网络”。CR-S104-2 说明所有权限制，不构成验收豁免，也不能改列已修缺陷。
- **修复建议：** 由设备协议所有者/统一集成负责人实现有界非阻塞发送队列并交付，Core 消费异步结果；增加持续背压下的进度与等待上限回归后再复审。**阻塞。**

### R3-B-11 / P1 / UNKNOWN 和人工 NO_CHANGE 被误当原基线

- **位置：** `host/domain/src/domain_device.c:389,390,409`；`host/domain/src/domain_commands.c:939,964,976`；`docs/stages/stage-00-architecture.md:128`。
- **触发与复现：** ACCESS_ACTIVE 已见开门，关门事件 item=UNKNOWN，VM 实测直接 NO_CHANGE、used=0。另 BORROW 核验时当前 ABSENT、原基线 PRESENT，管理员选择 NO_C
HANGE，VM 实测返回 OK、落 NO_CHANGE。
- **影响：** “不是目标值”不等于“始终未变化且回到原基线”；未知/物品已离位却释放占位、账本保持不变，造成物理与账本不一致。
- **修复建议：** 明确区分目标、原基线、UNKNOWN/矛盾三类；未知或证据不完整进核验；管理员 NO_CHANGE 也校验方向对应原基线。补借/还两向 UNKNOWN、变位后恢复、人工反向物品状态测试。**阻塞。**

### R3-B-12 / P1 / 实时事件跳号仍能声称完整并进入确认

- **位置：** `host/domain/src/domain_device.c:438,442,447,451,494,503`；`host/device/src/port.c:354,386`。
- **触发与复现：** 同 epoch/boot 输入实时 seq=1 开门、seq=3 离位、seq=4 关门，跳过 seq=2。VM 探针实测 continuity=1、fetch_gap=0、history_complete=1、
state=WAIT_CONFIRM。DevicePort 只校验会话/解码，不替 Core 检查事件序号连续。
- **影响：** 缺失的事件可能含物理冲突，Core 却提前宣称完整证据；后续 FETCH 尚未补齐期间可确认。当前 domain_import_page 的连续性派生不能追认未提交的实时水位。
- **修复建议：** 检测相邻键/跨 boot 屏障，在证据推进前锁存已知缺口并阻止确认；以落库连续水位证明完整性。补跳号、延迟补页、缺口页、跨 boot 和补齐前并发确认回归。**阻塞。**

### R3-B-13 / P1 / 取消失败仍把 UTC 截止点安装进 active

- **位置：** `host/domain/src/domain_state.c:493,495,497`；`host/domain/src/domain_commands.c:831,846,847,1356`。
- **触发与复现：** REQUESTED 的正确单调截止为 121000ms，取消前从库加载操作，终结提交注入 IO_ERROR。VM 实测 status=IO_ERROR、mono=1000、active.deadline=1761004920000；恢复仓储并推进 130s 后仍 REQUESTED、used=1。
- **影响：** 只修 recover_operations 没有修普通加载；cancel 在调用 candidate helper 前已覆盖 active，失败破坏原超时状态，占位不能正常退出。与 R3-B-7 的设备响应分支是独立触
发路径。
- **修复建议：** 保持单调截止的统一恢复/加载规则，在整个命令成功前不覆盖活动工作副本；确认/核验附加字段也在局部副本准备。补取消失败→超时、墙钟未知/过期/回拨和反复重试。**阻塞。**

### R3-B-14 / P1 / completion 槽复用可交付另一请求结果

- **位置：** `host/domain/src/domain_core.c:791,796,874,875`；`host/domain/src/domain_repo.c:364,400,447,460`。
- **触发与复现：** wait_result 分别加锁执行 find(token) 和 wait_final(slot)，两者间旧终态到期被 reap、新请求复用槽。VM 确定性交错探针实测 found_a=0、slot_b=0、token_equal=0，但等待旧槽返回 OK / OTHER_USER_B。另 locker_core_query 超时后直接 release，尚在队列/处理中的旧 job 仍只保存槽号，可完成已经复用的新槽。
- **影响：** 锁保护了单步内存访问，却没有保护请求身份/槽生命周期，可能把他人的查询或业务结果交给旧调用者；TSan 全绿不会检测这种逻辑竞态。
- **修复建议：** 完成和等待均绑定不可复用 token/代号，锁内重新校验身份；查询超时不能让仍在途 job 写入可重用裸槽。补排队超过 1s、终态过期边界与两个不同用户并发复用的确定性回归。**阻塞。**

## 非阻塞项

### R3-N-1 / P2 / 交付文档仍提前声明已修与可开下游

- **位置：** `docs/evidence/stage-01/S1-04/summary.md:7,65,72`；`handoff.md:3,9,81`；`defects.md:5,47,64`；`verification.md` 
§C/§D/§G。
- **事实与影响：** summary 保持 IN_PROGRESS 是正确的，P3 大小也合格（summary 10203B、handoff 8184B）；但“15 项全部整改/34 项已修”、把未修 CR-S104-2 计入已修、handoff “S1-05 可立即开始”均不符本轮反例。集成 test_borrow_flow 到借出即结束，没有真实连续归还；域源码当前合计 9346 行，集成文件 1553 行，旧行数不再准确。handoff 未记录整卡实际耗时及审计
轮数，并依赖 /tmp 临时脚本作复核入口。
- **建议：** 开发侧按本轮结论更新当前文档、映射真实测试覆盖、给出可复现正式入口及度量；不得由审计代改或重定义 DONE。**非阻塞文档项。**

### R3-N-2 / P2 / simulated=false 的契约债务仍开放

- **位置：** `host/domain/src/domain_state.c:710`；`host/domain/src/domain_import.c:215`；`docs/evidence/stage-01/S1-04/defects.md:77`。
- **事实与影响：** Core 公开/持久投影仍写 false，实际后端为 simulator；CR-S104-1 只是登记。文档已明确模拟来源，未冒充硬件通过。
- **建议：** 契约唯一写者在 S1-06 装配前闭合后端来源和正式输出测试，禁止本卡私改生成物。**非阻塞契约债务。**

### R3-N-3 / P2 / 独立审计归档尚未满足逐轮四件套

- **位置：** `docs/evidence/stage-01/S1-04/defects.md:71`；`audit-runner.ps1:1`；本目录 round-2 文件清单。
- **事实与影响：** round-2 报告、prompt、约 1.5MB 原始转录、含 START/END/EXIT 的 run.log 已存在；但没有 audit-round2-runner.ps1，通用 runner 仍标 round-1，通用 run.log 与 round-2 时间内容相同，不能替代逐轮归档。当前 round-3 的最终转录/结束日志须由真正启动器完成后归档，不能在本报告里预认完成。
- **建议：** 编排方保留真实轮次、模型/推理档、BOM runner、原始转录及退出码；历史缺失如实记缺失。本轮授权仅写 audit.md，不补造四件套。**非阻塞留痕项。**

### R3-N-4 / P2 / 设备 mode 仍由 Core phase 冒充

- **位置：** `host/domain/src/domain_state.c:684,698`；`host/domain/src/domain_commands.c:78,951`。
- **事实与影响：** Core 为 READY 就公开设备 READY，即使当前 session 不可用、快照失败或实际设备处于 RECONCILING。核验新增加的 freshness 门已阻止原“旧缓存改账”反例，但显示仍可能误导
调用端；两者不能合称全部修复。
- **建议：** 从真实会话/设备模式/快照健康派生设备投影，测试失联与重连阶段；保留未知，不重置旧样本年龄。**非阻塞投影项。**

## 已执行检查与证据

| 检查 | 本轮实际结果及边界 |
|---|---|
| Git 基线/修改边界 | git status/log/diff 成功，HEAD=cd4505e；没有上一卡..本卡提交区间。审查当前 diff、未跟踪集成测试、验收与相关契约；不把不存在的提交说成已审。当前 tracked diff 19 文件（含用户已有 AGENTS.md 变更），contracts 与 host/common/storage/device/simulator 无 diff |
| 真源及范围 | 核对入口、S1-04 卡、内部接口/串口/状态表/ownership/traceability、机器契约及生成声明；清点 host/domain 21 文件、单测 9 文件、集成 2 文件。根构建 add_subdirectory 位于 :90/:97/:99，依赖本地已有模块，无新下载 |
| VMware 环境 | ssh -o BatchMode=yes locker-vm 成功；Ubuntu x86_64 / Linux 7.3.0-rc5。实际检查存在 batches/{r194000,r198000,r201100,r201300}；未使用 WSL |
| canonical 源码 | source-manifest.txt 实际 81 项，对当前 Windows 文件全部 SHA-256 匹配；在 r201300/src 执行 sha256sum -c -，exit=0、81/81 OK。仅证明清单覆盖文件一致，不保证清单外文件或旧批次同源 |
| 本轮普通构建/CTest | cmake --build ~/locker-workspace/S1-04/batches/r201300/build -j2，exit=0；ctest --test-dir 同目录 --output-on-failure，exit=0、27/27、19.74s。没有把 Windows 不能编译当成 Linux NOT_RUN |
| 本轮直接执行断言计数 | text 213 / json 159 / repo 311 / state 364 / device 833 / commands 579 / core 216 / integration 93，合计 2768；8 个可执行文件全部 exit=0。集成的 BUSY 诊断为负向用例输出，不能按 printf 命中当残留 |
| 历史 sanitizer 证据读取 | r201300-{configure,asan,tsan}.log 均 :17 CONFIGURE_EXIT=0、:146 BUILD_EXIT=0、:147 WARNINGS=0、:205 27/27、:208 CTEST_EXIT=0、:209 SANITIZER_REPORTS=0；environment :9–11 三行四元组全 0。ASan/UBSan、TSan 是开发侧同源历史实跑，本轮未重跑，不把日志读取说成审计实
跑 |
| 本轮设计检查 | python -B scripts/check_contracts.py、check_stage_plan.py、check_vision_contracts.py 各 exit=0/PASS。仅设计一致性；硬件 profile UNVERIFIED、阶段门禁未通过 |
| 本轮反例探针 | 经 SSH stdin，在 guest 用 Python memfd + gcc 编译链接 r201300 Debug locker_domain/common 静态库；复用正式 dt 夹具并针对 idempotency/audits 提交选择性注入失败。4 组探针编译/执行均 exit=0；实测结果已逐项写入 R3-B-1..9、11..14 与复核表。未改生产/正式测试，未生成磁盘探针、构建或日志文件；探针不是已交付正式回归 |
| 临时残留检查 | 检查 printf/fprintf 与现有 .s104-tmp。生产 snprintf 是正式 JSON/文本构造；测试 stderr 是失败诊断，不凭关键词判残留。本轮只新增/覆盖 audit.md，未产生磁盘临时
脚本；开发侧已有 .s104-tmp 不由只读审计清理，交接前仍须开发方清理 |

### 验收与中断证据的实际覆盖

| 验收组 | 已有实际证据 | 审计判断 |
|---|---|---|
| 正常借还、只开门不取物 | test_borrow_flow 只完成借出；NO_CHANGE 事件单测 | 连续借还/释放链未闭合；R3-B-2，不可整项 PASS |
| 提前确认、错误用户、两个申请竞争 | commands 与 integration 正式负向断言、本轮 PASS | 已有证据，不推导未覆盖权限分支 |
| 15 秒证明、业务超时、UNKNOWN | 现有单测及模拟拒绝/超时矩阵 PASS | 时钟域失败恢复、UNKNOWN 物品反例仍在；R3-B-11/13 |
| ACK、物理变化加本人确认 | ACCEPTED 不推进，正常借出物理链测试 PASS | 快照冲突、序号缺口、重启 WAIT_CONFIRM 仍可绕过；R3-B-3/4/12 |
| 历史读卡不开锁、旧动作不重放 | 历史读卡负向及 ACCESS_PENDING 重启用例 PASS | 不等于全部重启恢复通过；WAIT_CONFIRM 反例见 R3-B-4 |
| 准备→发送→ACK→脉冲→落库 | 模拟器 BEGIN/INTENT/EVENT/COMMIT/RESULT 五阶段、丢应答/抑制脉冲/主机重启测试 PASS；本轮另测提交失败与重试 | 模拟器日志故障点不等于每个主机提交/进程崩溃点
；双事务窗口、丢查询与下一次操作尚缺正式回归 |
| 健康基线与并发结果 | UI 未知字段、源进度正式测试 PASS | completion 身份复用是逻辑竞态；R3-B-14，不能由 TSan 零报告排除 |

## NOT_RUN 与阻塞边界

- **本轮 NOT_RUN：** sanitizer 重跑（按 P2 复用同源历史证据）、真实背压运行、连续借→还→再借全链、修复后的正式新增回归；缺少后两者的门禁证据不能通过措辞补齐。普通 Debug/CTest、设计检查和上述探针已
实际运行。
- **硬件 NOT_RUN/UNVERIFIED：** 真实 STM32/UART/GPIO、锁脉冲硬截止、MCU 掉电、RK 存储介质掉电、相机/RKNN；阶段 1 的 device backend 为 simulator，以上不作为
本轮实现失败的伪理由，也不冒称已通过。
- **直接阻塞判定：** 14 项均为具体可触发代码/事务/生命周期缺陷，R3-B-10 保持原 P2 阻塞；其余为 P1。需要设备所有者配合的部分已明确，不授权审计越界修改，也不允许以 CR 归档替代修复。S1-04 不能标 DONE，不能据本轮绿测试开启依赖卡；独立复验全部阻塞项关闭后才可交接。
