# S1-04 round-4 独立审计
结论：FAIL
阻塞项：9 项；非阻塞项：4 项

审计基线：契约 0.2.1 / REVIEW_DRAFT；HEAD=cd4505e；当前工作树，canonical=r203300；active_task=S1-04 / IN_PROGRESS。device backend: simulator。

计数为 7 项尚存阻塞（R3-B-2/3/6/7/10/12/14）及 2 项新增阻塞，去重后 7×P1、2×P2。其余整改没有因缺环境而新增阻塞，但也没有获得本轮动态关闭证明。本报告只写 audit.md，不修改实现、测试、验收、状态索引或其他交付文档。

## round-3 阻塞项复核

本轮两次连接 locker-vm 均超时，已有 SSH 配置的另一地址也超时，故未能运行 Linux C 反例。下表的“部分修”表示看到了具体代码整改，但没有满足提示词“构造失败输入并实际运行后才能判已修”的关闭要求；不把静态检查、开发方日志或 Python 算术复算冒充 C 产品实测。存在确定遗留路径的条目在下一表完整展开。

| 编号 | 判定 | 证据 |
|---|---|---|
| R3-B-1 | 部分修 | host/domain/src/domain_device.c:968–991 原样使用冻结 outbox 字段并核对 session/epoch/boot/revision；domain_commands.c:1317–1327 有 MCU 域过期门。偏移 +500000、stored=503000 的真实发送探针及过期拒发探针 NOT_RUN。证明剩余期限还有独立问题，见 R4-B-1。 |
| R3-B-2 | 部分修，仍阻塞 | domain_device.c:448–456 已为 NO_CHANGE 登记释放；domain_commands.c:1161、1394、1945 有 presence 屏障和释放优先顺序。但设备 active 字段只有写入、没有消费/重建释放的读者；终结后重启遗失释放登记，见遗留表。 |
| R3-B-3 | 部分修，仍阻塞 | domain_device.c:354–385、611–637 增加方向敏感判断与快照接收门；同 boot 旧版本不覆盖。但确认冻结依据不绑定 boot/epoch，且冲突落库失败可绕过，见遗留表。 |
| R3-B-4 | 部分修 | domain_core.c:447–454 已包含 WAIT_CONFIRM；tests/unit/domain/test_domain_core.c:553–559 改为 REVIEW_REQUIRED 断言。成功提交路径的方向正确；失败恢复仍放行，归 R3-B-7，不重复计数。恢复后普通 confirm 的 C 反例 NOT_RUN。 |
| R3-B-5 | 部分修 | domain_commands.c:203、217–243 在同一 change 中追加 operation/loan/idempotency/audit，再一次 commit；失败在 :244–246 返回，命令侧 :874–877 返回无成功 JSON。revision+1 与 domain_change.c:169–171 成功增一静态自洽。选择性 idempotency 提交失败及重放逐字节探针 NOT_RUN，不能据成功用例判关闭。 |
| R3-B-6 | 部分修，仍阻塞 | 审计已并入终结事务，cancel :922–930 填写 OPERATION_CANCELLED。domain_commands.c:1131–1138 仍按字节截断理由；physical_revision=12 时 80 个“中”可被切成非法 UTF-8。真实仓储会拒绝，见遗留表。 |
| R3-B-7 | 部分修，仍阻塞 | domain_device.c:740–767 使用 candidate 后提交；tests/unit/domain/test_domain_device.c:1484–1529 覆盖 BAD_PRECONDITION 失败。但 domain_core.c:454 仍忽略恢复核验提交错误并设置 recovery_ready，见遗留表。PULSE_STARTED / QUERY / 恢复三路径 C 故障探针 NOT_RUN。 |
| R3-B-8 | 部分修 | domain_commands.c:1506–1514 查询已移出状态 switch，:1538–1545 结束等待时清 execute_in_flight；domain_device.c:855–862 处理操作已终结的查询。正式超时用例有 WAIT_CONFIRM 段，但查询丢失/迟到及再次借还实跑 NOT_RUN。 |
| R3-B-9 | 部分修 | domain_commands.c:630 精确比较 tool_revision；contract-source.json:740–743 明确必填。显式“0”与当前“1”的 C 请求反例 NOT_RUN，未标“已修”。 |
| R3-B-10 | 未修，仍阻塞 / P2 | domain_device.c:185、211、250、991 → host/device/src/port.c:489–533 → transport.c:192–239 同步写，仍有 write_lock 和最长 1000ms poll。CR-S104-2 所有权归属成立，验收豁免不成立。summary.md:61、defects.md:88、107 承认未修；不能 DONE。 |
| R3-B-11 | 部分修 | domain_device.c:401–413 区分 UNKNOWN/原基线；domain_commands.c:1046–1051 要求人工 NO_CHANGE 等于可知基线。两个非法输入及合法 NO_CHANGE 的 C 探针 NOT_RUN。不能以这些谓词推导完整证据门已闭合，参见 R3-B-12。 |
| R3-B-12 | 部分修，仍阻塞 | domain_device.c:490–496 会锁存实时跳号，但 :388–438 的证据终结不检查该锁存，跳号后仍能自动 NO_CHANGE 并释放占位。新增用例 test_domain_device.c:1444–1476 只测到 seq=3，未送关门核对终结，见遗留表。 |
| R3-B-13 | 部分修 | domain_state.c:500–510 统一换算；confirm/cancel/resolve 在 domain_commands.c:855、914、1056 用局部 target。rg 检查发现生产命令文件中 5 个加载调用，另有测试调用，不能将“8 个”当成运行覆盖数。取消失败后期限不变及随后超时探针 NOT_RUN。 |
| R3-B-14 | 部分修，仍阻塞 | domain_repo.c:375–389 等待循环与终态读取核对 token；正式槽复用测试 test_domain_repo.c:524–563 只覆盖读侧。domain_core.c:873–874 查询超时仍按裸槽释放，job/finish 不绑定 token，见遗留表。 |

### 尚存阻塞项（沿用 round-3 编号，不另计新增）

下列实际行为是逐语句静态推导；除明确说明的 UTF-8 字节复算外，本轮 C 复现均 NOT_RUN。每项提供可执行的最小场景，开发侧应补正式回归后在 VM 复验。

| 编号 / 严重度 | 文件:行 | 期望行为 | 实际行为与影响 | 最小复现步骤 | 依据（文件:行原文）/ 修复建议 |
|---|---|---|---|---|---|
| R3-B-2 / P1 | host/domain/src/domain_device.c:649–654；host/domain/src/domain_commands.c:1396；host/domain/src/domain_core.c:136 | 完成后在释放前崩溃，重连应从已提交终态与 MCU 活动上下文恢复释放，并在完成前拒绝新武装。 | apply_snapshot 只保存 device_active_*；rg 全域源码只有赋值/声明，无消费者。recover_operations 跳过终态，release_pending 默认 false，release_pump 直接返回。MCU 原上下文持续保留；真实设备无法完成新 READY/新动作。仅靠“MCU 会拒绝”是闭锁，没有完成恢复闭环。 | ①真实借出确认事务成功；②暂停 Core 于第一次 RELEASE_CONTEXT 前并重启 Core；③MCU 不重启，GET_SNAPSHOT 报告旧 operation/command；④补取与 ACK 后 tick 多次。断言应发匹配 RELEASE_CONTEXT；当前路径 release_calls 保持 0。增加同会话借→还→再借及此崩溃点的 SQLite+模拟器回归。 | docs/architecture/uart-v1.md:408：“核对操作/释放已处理上下文→RESUME_READY”；stage-01-task-cards.md:73：“正常借还…重启后先核对”。消费设备活动身份，查询其已提交业务终态后补登记释放；未决操作保持核验，不能盲目释放。 |
| R3-B-3 / P1 | host/domain/src/domain_device.c:354–385、611–624；host/domain/src/domain_commands.c:844–849 | 新 boot/epoch 或已知物理冲突使旧确认依据失效；核验提交失败期间也禁止普通确认。 | 冻结依据只比较 revision/item/door，不比较 boot/epoch。换 boot 后低 revision 快照被接纳，但小于冻结版本便不判冲突。另同 boot 新 revision 导致 domain_require_review 提交失败时，内存仍 WAIT_CONFIRM；confirm 从库加载旧记录，只查当前门/物品和 item_transition_seen，不查冻结依据，存储恢复后可直接完成。 | A：正常进入 WAIT_CONFIRM，冻结 boot=1/rev=9，送 boot=2/rev=1、CLOSED/ABSENT 成功快照，检查应转核验，当前仍 WAIT_CONFIRM。B：冻结 rev=9，送 rev=12、同 CLOSED/ABSENT 快照时仅注入 commit=IO_ERROR；恢复提交端口，立即用原 operation_revision confirm。应拒绝，当前判断允许终结。两场景分别补回归；不要只改状态而遗漏失败时的闭锁标志。 | internal-interfaces.md:12：“physical_revision 是同一次 boot 中的…版本”；:116：“按该快照的 boot、physical_revision…任何已知冲突先核验”；stage-00-architecture.md:119：“可能已执行…MCU 重启…REVIEW_REQUIRED”。绑定完整代际，确认决策点重验，持久失败保留不可确认屏障。 |
| R3-B-6 / P2 | host/domain/src/domain_commands.c:980–982、1131–1138；host/storage/src/schema.c:325 | 契约合法理由可提交，审计理由保持合法 UTF-8；事务原子失败不能掩盖合法输入被实现破坏。 | 80 个“中”=240B，可通过 note[257]。physical_revision=12 的后缀为23B，理由被截为233B，最后一个汉字只剩2B，再拼 ASCII 后缀，生成非法 UTF-8；真仓储 utf8_count 返回 false，整个核验事务失败。另 note[257] 把256字符误限为256字节，会拒绝合法的100字中文。 | 在合法 REVIEW_REQUIRED BORROW、物品 ABSENT、physical_revision=12 下，以 reason=80 个“中”执行 COMPLETE_BORROW。字节复算已运行：prefix=233 / suffix=23 / total=256，UTF-8 解码于231–232失败；真实 C+SQLite 提交 NOT_RUN。另以100个“中”检查应为合法请求。 | contract-source.json:789–792：“reason…minLength:5…maxLength:256”；internal-data.schema.json:4287–4290：“reason…maxLength:256”。按 Unicode 字符限长并在完整码点边界构造审计；两个固定 type 长17/19，满足 :4284 的64上限；不要靠删审计或缩小合同输入范围过测。 |
| R3-B-7 / P1 | host/domain/src/domain_core.c:447–458、527–530 | 恢复核验持久化失败应返回错误、保持恢复屏障，并在成功核对前拒绝写命令。 | candidate 避免了内存先跳状态，但恢复仍用 (void) 忽略错误；随后 recovery_ready=true、recovery_done=true、返回 OK，线程据此 phase=READY。库和 active 可仍为 ACCESS_* 或 WAIT_CONFIRM，没有恢复核验成功证明。 | 预置持久 WAIT_CONFIRM（或 ACCESS_PENDING），load/query 均成功，仅恢复转 REVIEW_REQUIRED 的 commit 返回 IO_ERROR；调用 domain_recover。应非 OK 且 recovery_ready=false；当前返回 OK/true。随后恢复存储并检查没有普通确认窗口；与 R3-B-3 的失败闭锁一并测试，但这是独立恢复触发路径。 | stage-00-architecture.md:119：“服务/MCU 重启…REVIEW_REQUIRED，保持占位”；internal-interfaces.md:100：“恢复未完成…不自动发送恢复出来的旧动作”；locker_contracts.h:280 的恢复屏障约定。传播 domain_require_review 返回值，恢复成功标志只能在必需写入均成功后设置。 |
| R3-B-10 / P2 | host/domain/src/domain_device.c:991；host/device/src/port.c:489、523；host/device/src/transport.c:197、216 | Core 只提交有界异步发送意图，背压不在 Core 内等待串口/socket 可写。 | domain_execute_intent 等端口调用同步进入 write_lock、transport_write/poll；单次写最多等待1000ms，锁等待另计。超时/队列/进度同停。CR 的所有权说明真实，但未满足本卡禁止项。 | AF_UNIX 对端完成握手后保持连接不读，填满发送缓冲，再触发 Core 健康/执行发送，观察 Core 循环进度与调用耗时。代码上确定进入同步 poll；本轮背压实测 NOT_RUN，不编造耗时数字。 | module-ownership.md:23：“调用网络/串口阻塞 IO”列为禁止；stage-01-task-cards.md:74：“不调用阻塞网络”；internal-interfaces.md:135：“设备适配器拥有…有界发送队列”。由设备协议负责人和集成负责人修端口及联调；移交不能令 S1-04 DONE。 |
| R3-B-12 / P1 | host/domain/src/domain_device.c:388–438、490–496 | 已知缺口时不能自动宣称完整开关门/始终未变并结束操作；应核验并保占位。 | 跳号确实令 cursor.continuity_ok=false、fetch_gap=true、history_complete=false，但 operation_advance_from_evidence 不读它们。当前物品等于原基线便 NO_CHANGE、删除 slot_guards、登记释放。原“跳号到 WAIT_CONFIRM”也仍能进入该状态，只是普通 confirm 被 physical_ready_for_action 拒绝，不能说证据推进已全修。 | ACCESS_ACTIVE BORROW，基线 PRESENT；送 seq=1 DOOR_OPENED/OPEN/PRESENT；跳过2，送seq=3 DOOR_CLOSED/CLOSED/PRESENT。应 REVIEW_REQUIRED 且保留guard；当前路径 NO_CHANGE 并删除guard。正式新增跳号测试只到seq=3的 ITEM_REMOVED 即结束，须续测闭门两种物品方向。 | stage-00-architecture.md:115–116：“完整开关门/目标变化”“完整开关门且始终未变”；uart-v1.md:370：“存在已知缺口…关联业务进入核验”；stage-01-task-cards.md:74：“不把…快照代替完整…证据”。在任何自动终结/确认前统一校验证据完整性，持久化核验并保占位。 |
| R3-B-14 / P1 | host/domain/src/domain_core.c:865–874；host/domain/src/domain_internal.h:230–234；host/domain/src/domain_repo.c:230–240、280–289 | 查询超时不使尚在途作业的槽被另一请求复用；读、写、释放均须绑定请求身份。 | wait_final 的 token 防错读只保护读取时槽身份；query 超时后仍无条件按下标 release。旧 job 只含 completion 下标，finish 只查 used，故它可把 A 结果写进 B 槽；B 的 wait token 合法，仍读到 A 结果。另 wait后release之间也无身份核对。 | 确定性交错：①acquire A 得slot0，保留未执行 query job；②A等待超时后release(slot0)；③acquire B复用slot0；④执行旧A job/finish(slot0,A_JSON)；⑤以B token wait_final。期望NOT_READY/仅B结果；当前可OK/A_JSON。产品复现可用Core背压延迟队列超过1000ms；本轮C交错探针NOT_RUN。 | internal-interfaces.md:17：“wait_result只按该内部token寻址…不能把两个用户…结果混在一起”；:45：“以私有submission_token等待最多1000ms”；locker_contracts.h:208。job携带token/代号，finish/fail/release锁内核对，或保留超时查询槽到旧job完成后安全回收；补生产query超时回归。 |

## 新增阻塞项

| 编号 | 严重度 | 文件:行 | 期望行为 | 实际行为 | 最小复现步骤 | 依据（契约/规格文件:行原文）/ 修复建议 |
|---|---|---|---|---|---|---|
| R4-B-1 | P1 | host/domain/src/domain_commands.c:1209–1214、1255–1257 | 冻结 not_after 不超过原柜前证明剩余15秒的保守 MCU 期限；不能在晚武装时刷新资格。 | arm 仅检查 proof 年龄≤15秒，随后无条件 not_after=mcu_low+2000，忽略证明剩余时间。证明接近到期时能再获得约2秒执行窗口；MCU并不知道原证明时刻，只按收到的not_after检查，不能替Core修正。 | 注入 proof_ms=1000；因物理门未就绪延迟武装，到 now=15999 时闭门就绪；给有效新鲜锚点 offset=+500000。预期not_after≤516000；按源码公式本轮Python复算为517999，延长1999ms。在原证明16000截止后、517999前送达MCU仍可能接纳/驱动。此为算术复算，C端到端NOT_RUN。 | uart-v1.md:384：“把柜前证明剩余15秒保守映射成期限”；internal-interfaces.md:133：“UART积压不会刷新旧证明期限”。期限取证明剩余时间和准备预算中较早值，绑定同boot锚点；补14.999s、15s及延迟送达边界回归。 |
| R4-B-2 | P1 | host/domain/src/domain_device.c:307–329、469–553、634–637；host/domain/src/domain_import.c:121、143–160 | 任意已接受物理事实中的 HISTORY_INCOMPLETE 都必须锁存，后续连续页不能洗白，自动业务不得继续。 | 快照路径会看flags bit2，但实时事件完全不消费event->flags，apply_physical_event反而写dropped_events=0。FETCH只复制record->flags，并按gap_before_page/已有fetch_gap派生continuity，忽略事件HISTORY_INCOMPLETE。序号连续并不能说明无丢失，下一份快照到达前有继续确认/武装窗口。 | A：就绪且continuity=true，送同boot相邻seq的普通DOOR_OPENED，flags=0x0004（不必是CONTINUITY_LOST类型），再按顺序送取物/闭门，检查应立即锁存并核验；当前flags未参与判断。B：新Core导入boot1/seq1起的连续页，gap_before_page=0但任一record.flags含0x0004，检查导入后cursor.continuity_ok应false；当前可true。两条C运行均NOT_RUN。 | uart-v1.md:50：“HISTORY_INCOMPLETE是连续性失效锁存；即便event_seq连续也不能认为无丢失”；stage-00-architecture.md:159：“连续性失效锁存独立于event_seq”。在实时/补取/快照所有入口统一锁存与持久化，不以连续seq或新会话覆盖，并测试后续正常页不能洗白。 |

## 非阻塞项

| 编号 / 严重度 | 文件:行 | 期望行为 | 实际行为 / 复核判定 | 最小复现 / 证据 | 依据 / 建议 |
|---|---|---|---|---|---|
| R3-N-1 / N-P2 | docs/evidence/stage-01/S1-04/handoff.md:3、9–15、86；summary.md:65；defects.md:5、88；verification.md:29、182–184 | 当前文档匹配状态、覆盖和可复现正式入口；数字有原始依据。 | 部分修：summary保持IN_PROGRESS，handoff补“约26小时/3轮”。仍把未修CR及多项未闭合问题纳入“49项已修”，handoff笼统称下游可立即开始；仍依赖/tmp/full-verify.sh。域源码实测9415行、单测目录8150行，handoff写9020/8016；集成1566行匹配verification。verification称假仓储夹具含真实sqlite也不符。26小时无起点日志，不能认证为实测。 | Python按LF重数19个src .c/.h；9个单测目录文件（含CMake/夹具）；summary=10174B、handoff=8190B，在10KiB/8KiB内。test_borrow_flow:1215–1276仍只借出，没有RETURN；release_requests只有统计没有连续借还断言。2887仅为所列8数的算术和，canonical CTest日志无逐二进制断言数，未独立验证各数。 | AGENTS.md文档同步/P3/P5；stage-plan的S1-05依赖S1-04。单独端口冻结后的鉴权预研按任务卡:35可做，但不能把未通过的业务当成可依赖交付。更新事实、正式复验入口、覆盖矩阵和准确度量，临时.s104-tmp由开发收尾清理。 |
| R3-N-2 / N-P2 | host/domain/src/domain_state.c:721–722；host/domain/src/domain_import.c:215 | 模拟后端应有可追溯的真实标识。 | 未修，保留CR-S104-1。公开与持久化投影仍false，证据文档明确simulator；本轮未据此冒称硬件通过。 | 静态追踪domain_write_device_projection及domain_write_device_state的false实参；未做产品投影C实跑。 | internal-interfaces.md:135：“模拟器…显式返回simulated=true”。契约唯一写者闭合后端来源，域模块不私改schema；保留为待裁定债务。 |
| R3-N-3 / N-P2 | docs/evidence/stage-01/S1-04/audit-round2-runner.ps1:1、8–13；audit-round3-runner.ps1:1、8–13；audit-round2-run.log:1–4；audit-round3-run.log:1–4 | 每轮真实归档prompt/runner/run.log/原始转录，并能证实对应轮次和指定模型推理档。 | 部分修：round-2 runner现在存在，UTF-8 BOM；两轮四件文件名均存在，不能继续说“缺runner”。但runner逐字节相同，仍读写通用文件，未显式固定模型/high；两轮run.log也逐字节相同，START/END均20:55:02–21:08:53，不能证明两个独立轮次。提示词提到的audit-round3-rawoutput.txt未见。 | SHA256前缀：两runner均cc786a8d9c749717；两run.log均00eaacf01a74dfee。原始转录分别1521202B/675139B，hash不同，证明有不同内容，不证明这份run.log属于round-3。 | AGENTS.md逐轮四件套/固定模型要求。编排方保留真实来源与轮次；历史缺失如实标记，不补造。当前仅有报告写权限，审计不代补归档。 |
| R3-N-4 / N-P2 | host/domain/src/domain_state.c:696–704、710 | Device.mode反映设备/会话健康，Core业务线程恢复完成不能替代设备READY。 | 未修。phase==READY直接映射Device READY，session失效、快照失败、设备RECONCILING均未参与。 | 静态输入phase=READY、session.valid=false时，函数仍返回READY；未做C产品投影实跑。 | uart-v1.md:48：OFFLINE是主机观察投影；stage-00-architecture.md:90–95四种状态分开。按真实设备状态派生，保持未知；不拿这个显示修复替代动作门禁。 |

## 已核实通过项

- 完整读取本轮UTF-8 audit-prompt.txt；按其只写audit.md的边界执行，未改业务、正式测试、契约、索引、脚本或Git历史，未commit/push，未跨卡。
- 本机实际运行 python -B scripts/check_contracts.py、python -B scripts/check_stage_plan.py、python -B scripts/check_vision_contracts.py，三者exit=0、status=PASS。仅证明设计一致性；输出明确hardware_profile=UNVERIFIED、runtime_or_hardware_tested=false。
- source-manifest.txt逐行SHA-256复算：81项，当前Windows文件81/81匹配、0缺失/0不符。VM侧清单本轮无法复算，不声称两侧已独立复核同源。
- 实际读取三份vm-s104-r203300-{configure,asan,tsan}.log：各:17 CONFIGURE_EXIT=0、:146 BUILD_EXIT=0、:147 WARNINGS=0、:205 27/27、:208 CTEST_EXIT=0、:209 SANITIZER_REPORTS=0；每份实际27条Passed。environment.log:9–11为PLAIN/ASAN/TSAN各0 0 0 0，:7–8为02:18:34–02:20:20。这些是开发方归档的历史运行证据，本轮没有重跑普通/ASan/TSan。
- 所列断言数213+159+323+368+900+612+217+95确为2887；这仅核实加法，没有核实各二进制实际执行计数。19项前置加7域单测加1集成与27项CTest数量自洽。
- Git核查：HEAD仍cd4505e，没有可用“上一卡..本卡提交”区间。当前tracked diff为21文件，2724增/426删，包含用户AGENTS.md。根CMake当前diff仅:99新增tests/integration接入；:90/:97域接入已在基线。contracts及host/common/storage/device/simulator无本轮tracked diff。不把它说成已审不存在的提交范围。
- 对照所要求的5个新增设备用例、commands改动和夹具：预置已知工具/合法锚点/physical_revision=7是单测前置，不构成生产放宽；release_status注入补了本地CONFLICT测试；恢复WAIT_CONFIRM改断言符合规格；槽复用读侧token测试方向正确。它们未覆盖本报告的重启释放、跨boot、失败闭锁、真UTF-8仓储、查询超时写侧身份等场景，不能外推全项通过。test_reply_commit_failure还手动重置pending并重新送同应答，不等于生产会自动恢复。
- 静态核对finish_operation同一change最终一次commit、失败不安装candidate；类型OPERATION_CANCELLED/MANUAL_RESOLUTION分别19/17字符，均小于64。没有凭这些静态事实关闭R3-B-5/6的动态要求。
- 实际rg检查host/domain的printf/fprintf直接输出无命中；snprintf是正式JSON/文本构造，测试stderr为失败诊断，不当成debug残留删除。已有.s104-tmp为开发侧临时目录，本审计不清理。
- 读取r194000/r198000/r201100/r201300历史日志的退出标记和测试汇总，不能把旧批次PASS替代r203300修复反例；design-review.md保留为历史设计输入，不代替本轮验收。

## 环境与 NOT_RUN

- Windows本机只执行只读Git/文本/指纹检查、三项设计检查及两个内存中的Python字节/算术复算。没有创建探针文件、临时数据库、socket、编译树或脚本。
- 实际连接命令：ssh -o BatchMode=yes -o ConnectTimeout=10 locker-vm ...，及后续ConnectTimeout=5复试，均exit=1：“connect to host 192.0.2.10 port 22: Connection timed out”。已有配置地址192.0.2.11同样超时。不修改SSH配置、不启动/重建VM、不使用WSL。
- 本轮NOT_RUN：VM源码指纹复核、CMake/Debug重建、全量CTest重跑、8个二进制断言复算、全部14项C反例/故障注入、连续借→还→再借真实链、持续背压、ASan/UBSan/TSan重跑。没有把“Windows无法运行”或VM不可达本身列为产品阻塞；FAIL来自上面9条具体代码/合同缺陷。
- UTF-8字节复算和期限算术复算均非产品测试；其真实C+SQLite/模拟器运行仍NOT_RUN。其余最小复现为从当前代码推导的待执行正式回归，报告没有声称已运行。
- 硬件NOT_RUN/UNVERIFIED：真实STM32/UART/GPIO、锁脉冲硬截止、MCU/RK介质掉电、相机/RKNN。阶段1只审simulator，不要求用本轮替代后续硬件门禁。
- 本次不宣称覆盖所有路径/所有合法JSON响应，不把既有CTest绿灯作为完整合同证明。R3-B-5的选择性事务失败、response_json逐字节/提交版本核对等仍需运行补证。
- S1-04不能DONE，阶段1不能PASS。设备IO需要所有者配合，但CR归档不豁免验收；其余整改应一次性覆盖本报告全部阻塞及失败路径，再独立复审。没有执行整改、交接或开启下一卡。
