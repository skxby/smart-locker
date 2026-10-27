# S1-03 round-4 独立审计
结论：FAIL
阻塞项：1 项；非阻塞项：4 项

日期：2026-10-25；工作副本：%REPO%；契约：0.2.1 / REVIEW_DRAFT。审计只写本报告，不修改实现、测试、契约、生成物、状态、交接或既有证据，不置 DONE，不跨卡，不 commit/push。

FAIL 的直接原因是 R3-B-2 的错误执行入口仍在现行交接正文中，且本轮已只读复现。未以 Windows、未重跑完整 Linux 测试或缺硬件为阻塞理由。以下“静态核实”指本人读源码和正式断言，“日志复算”指本人重数保留原始日志，“本轮实跑”只指末节实际命令；三者不混同。

## 阻塞项

| 编号 | 严重度 | 文件:行 | 期望行为 | 实际行为 | 最小复现步骤 | 依据（契约/规格文件:行原文） |
|---|---|---|---|---|---|---|
| R4-B-1 | P2 | **docs/evidence/stage-01/S1-03/handoff.md:360**、**:479**；**docs/evidence/stage-01/S1-03/mutation-batch.py:9** | 正式再生成入口用 Python 执行 Python 脚本；整改表与实际正文一致。§8 应使用 `python -B docs/evidence/stage-01/S1-03/mutation-batch.py --src "<repo-root>"`。 | §12.5 声称 R3-B-2 已修，§8 仍逐字是 `bash .../mutation-batch.py --src "<repo-root>"`。Bash 在第9行 `def _arg_value(flag):` 发生语法错误，无法再生成证据。§7.4 的正确 Python 命令不能消除 §8 的错误入口。 | ①读 handoff:360 与 :479；②在源码根仅执行 `bash -n docs/evidence/stage-01/S1-03/mutation-batch.py`（不执行主体、不落盘）；③观察第9行 `(` 附近语法错误、exit 2；④执行 `python -B docs/evidence/stage-01/S1-03/mutation-batch.py --check --src .`，得48点/bad0。**本轮将当前 Windows 脚本经 SSH stdin 交给 guest 的 bash -n，实际 rc=2；Python --check 实际 rc=0。** | **AGENTS.md:35**：“同步相关当前文档；正文写当前真实规则”；**docs/architecture/module-ownership.md:69**：“只把编译改通过而留下旧文档不算完成”；**:71**：“每张任务卡交付：改动范围、入口/构建命令……正式测试及结果”。 |

本项是交接入口缺陷，不是 mutation 算法或 UART 功能失败。关闭时须改正文并核对正文和对照表一致；仅再次写“已修”或只做 Python 语法检查不能关闭它。本轮未修改该命令。

## 非阻塞项

| 编号 | 严重度 | 文件:行 | 期望行为 | 实际行为 | 最小复现步骤 | 依据（契约/规格文件:行原文） |
|---|---|---|---|---|---|---|
| R4-N-1 | P3 | **host/device/src/port.c:429**；**host/device/README.md:61**；**contracts/include/locker_contracts.h:181** | 契约负责人统一原生 reply.message_type 的方向位语义。 | 当前写 request_type OR 0x80，线上和正式测试自洽；README/summary/handoff 已明确“待契约负责人裁定”。生成头只有 uint8_t 声明，没有冻结原生字段的解释。继续保留待裁定，不把本卡解释当共享合同已批准。 | ①读生成头181；②对照 uart-v1:13；③读 README61；④由负责人统一 Core/未来 MCU sink 的解释，本卡不手改生成物。 | **docs/architecture/uart-v1.md:13**：“响应为请求 type OR 0x80”；**module-ownership.md:11** 的真源统一修改职责。**原生字段语义规格未覆盖，需契约负责人裁定。** |
| R4-N-2 | P2 | **docs/evidence/stage-01/S1-03/summary.md:75**、**:139**、**:169**、**:293**、**:329**；**handoff.md:5**；**host/simulator/README.md:65**；对照 **vm-s103-daemon-smoke.log:11**、**:13** | 当前报告中的检查数与所指 canonical 原始日志一致。 | 上述正文均写249；同一 run_id 的原始日志实际是 `DAEMON_CHECKS=250`、`PASS ... (250 checks)`，failures0/exit0。日志与 guest 原件 SHA 全等，新增旧session断言也真实存在（daemon_smoke.py:569）。用户输入的249同样不是当前日志事实。本项不否定进程测试通过或新增断言，且契约没有最低断言数量门槛，故不升级为产品阻塞。 | ①运行 `python -X utf8 -B -c "from pathlib import Path; print(Path('docs/evidence/stage-01/S1-03/vm-s103-daemon-smoke.log').read_text())"`；②读11/13行得到250；③运行 `rg -n '249' host/simulator/README.md docs/evidence/stage-01/S1-03/summary.md docs/evidence/stage-01/S1-03/handoff.md`；④同步当前数字并保留原始日志。 | **AGENTS.md:35**：“正文写当前真实规则”；**module-ownership.md:71**：“正式测试及结果”“阶段报告保留测试日志索引”。数字漂移需更正，但没有据此发明249/250产品验收标准。 |
| R4-N-3 | P3 | **docs/evidence/stage-01/S1-03/mutation-batch.py:361**、**:364**、**:369**；**summary.md:203**、**:249**；**mutation.log:649** | 说明失败行数与分母的实际统计范围；缺完整测试汇总时明确标注分母不完整。 | m43 的646条 C 失败行可复算，三条 red_test 汇总为210/461、354/624、2/168，即566条失败、分母1253。另80条失败来自 test_device_port，日志未保留该二进制完整汇总。因此“across 3 test binaries”仅代表有完整汇总的3个，不能解释为所有失败只来自3个二进制或1253覆盖全部646条。45/48捕获结论仍成立：boot直接断言已变红；没有构建失败误算。没有原始完整CTest输出，不能断言缺汇总一定由超时造成。 | ①读 mutation.log 的 `anchor=host/simulator/src/sim.c:2179` 段；②数三条 red_test 的失败数/分母，得566/1253；③数该段 `  FAIL ...test_device_port.c:` 行，得80；④将口径写成“全部失败行646；具有完整汇总的3个测试分母1253；port完整分母未取得”。 | **module-ownership.md:71**：“正式测试及结果”“实际运行或明确的静态检查”。**规格未规定 mutation 分母格式，不据统计表述缺口升级为产品失败。** |
| R4-N-4 | P3 | **host/device/README.md:80**；**summary.md:288**；**handoff.md:298**、**:305**、**:429**；对照 **port.c:115**、**:127**、**:171**、**evidence-run-vm.sh:15**、**:17**、**vm-s103-gates.log:1**、**vm-s103-run-summary.log:28** | 当前说明准确区分注入时钟、扫描行为、目录守卫与证据所在位置。 | README仍称 pending“按实际单调时间”，实际使用config.clock；summary称127-142“用continue”，该函数没有字面continue，且“陈旧条目复用号”的理由不能豁免未完成号唯一性。handoff称工作区必须预先存在，但脚本先mkdir其下级，再检查WS，不能证明预先存在；305的live_tree_files单次计数不能独立证明前后树完全未变；429仍称verdict写进两个日志，实际上gates仅16条GATE_OK、verdict只在run-summary。历史SSH_EXIT只在文档自述/终端声明，无保留transcript可复算。主路径说明及当前运行可核，列文字精度项。 | ①比对 README80 与 port115/171；②读 pending_take，确认自然扫描且pending_add拒重复在途号；③按脚本15→17控制流核目录守卫；④比较两个日志及搜索 `SSH_EXIT`，区分单次计数、代码未删除与同树前后散列证据。无需执行mkdir或复验脚本。 | **AGENTS.md:35**：“正文写当前真实规则”；**uart-v1.md:23**：“不得复用仍未完成的请求号”；**module-ownership.md:71** 要求实际结果与环境。这里未另加目录安全模型或要求保存所有终端输出作为产品门槛。 |

## 已核实通过项

以下逐项说明已验证的范围和证据；不以 summary/handoff 的“已整改”作为通过依据。

### 输入、范围、指纹与阶段公共约束

- 已读取根 AGENTS、README、stages/README、stage-plan、S1-03原文及公共约束，UART-v1全文，contract-source/uart-layouts/生成头/黄金帧，相关 internal-interfaces/module-ownership/traceability，25个受管文件及33个既有证据文件。机器索引仍为 active_task/recommended_next=S1-03，S1-03 IN_PROGRESS/report=null，前两卡DONE；本轮未改变这些状态。UART功能不等于阶段1门禁或硬件完成。
- 当前25项的完整 SHA/字节数均与清单相等，SHA前12位、字节数、物理行数均匹配用户给定表；总584011B。daemon_smoke.py为659物理行、658个LF，末行没有终止LF，不能把此差异当内容变更。
- source-manifest：25条数据+3条注释，2793B，SHA-256=`3ef6dbb4c8962e8ddfbf9649db10ebe2c31f9eb67021f342cb82f44e2338846d`；vm-s103-manifest.sha256逐字节相同。mutation.log：183170B、1417行，SHA-256=`e3f477eac9b1d997d81a2e6428b1153da97bb9e65f9270f255b0416547ba98ad`。本次输入是完整64位，R3-B-3已关闭。summary:16-23的八个契约/规格/索引摘要及字节逐项复算相等。
- S1-01清单中common源码/测试21项全匹配；S1-02清单32项全匹配，其中根CMake去掉88/89/95/96四条接入后为3943B、SHA=`f9e1964ee54465f5e29299721a3f342f929cc81e3ff6a6da8d00b77fc34258b4`，其余31项直接匹配。这支持根仅四行接入及复用前序实现。非Git工作副本没有开工前全项目快照，**不能证明所有scripts/contracts等历史上从未被改动**；本轮checker、八项摘要及既有清单核对不是完整历史diff。
- 根CMake:14/20-22、四个模块/测试CMake核为C11、POSIX、CMake/CTest；simulator CMake:27-32复用locker_common/device/storage_thirdparty。依赖锁沿用SQLite3.54.1 Public Domain和cJSON1.7.19 MIT的固定版本/摘要（deployment/host/dependencies.lock.json:28、:70、:133、:172）；未下载新库、未自研HTTP/密码/数据库。CivetWeb/libsodium/TLS仍由既定后续卡消费。既有实现的只读审计适用本项目小改/离线调研豁免，本轮没有启动新方案。
- 超时注入：port:115/171调用device_now_ms/config.clock；device_deadline_now已不存在。test_device_port:1638推进5001ms，:1642注入迟到响应、:1644核未投递，:1646新号QUERY仍可用；:1589另保留真实时钟烟测，:1969-1970注册两者。此处为源码和保留测试记录核实，本轮未运行落盘的port测试。正式故障入口见sim.h:40、sim.c:2448；不是临时生产开关或公开HTTP。
- 明确模拟标识存在于sim_main:736的实际ready输出、两个README、handoff配置样例的simulated=true与NOT_RUN说明，以及vm-s103-daemon-smoke.log:1。未为模拟标识扩展公共schema。扫描四个授权目录无临时.db/.sqlite/.sock/.pyc/.o/.a/.tmp/CMakeCache.txt；正式测试、fixture、日志均保留。所列WSL脚本、wsl-*和joint-fix-round1.md均不存在；当前summary/handoff明确“已删除/不用WSL”。历史audit.md/audit-round2.md的历史记录未被当作现存WSL证据，未修改历史记录。

### 13项审计重点的独立核对

- **1 黄金帧与长度：**本人用Python/json/re拼接C字符串并assert与JSON frame_hex全等，4/4通过。HELLO61B/payload49；SNAPSHOT28B/payload16。binascii.crc_hqx(frame[2:-2],0xffff)为0xbdd8/0xea6e，线路d8bd/6eea；crc_error算0xea6e而线路6eeb；oversized_header为10B、声明513。123456789算0x29b1，LE字节b129。C对应wire:116/217，覆盖offset2到payload末、不含magic/CRC。黄金常量见test_golden_frames:17/21/24/27。本轮未运行会开SQLite fixture的golden二进制。
- **11对payload逐字段求和：**从uart-layouts各字段宽度独立得到HELLO49/71、SNAPSHOT16/189、FETCH49/(85+96N,N=0..4,最大469)、ACK48/34、RESUME64/27、DISARM18/27、EXECUTE99/93、QUERY48/93、HEALTH73/27、ANCHOR36/46、RELEASE80/27；event96=96、DEVICE_EVENT=128。与codec:215/245及test_codec:14-44相同；非零result严格18B见codec:1189-1199、test_codec的test_error_response_shape。本轮guest test_codec实际179 checks/exit0。
- **2 流解析边界：**wire.h:73固定524B；wire:169-178满缓冲明确丢1字节；:196-220对magic/超长/CRC错均丢1字节；先满足frame_size再读CRC和拷payload，不按对端长度malloc。sim_main:232-245复用同parser，:198/215只重建已校验的有界帧。test_wire的逐字节、粘帧、噪声、CRC、超长、最大帧和handler拒绝用例均注册；本轮既有二进制130 checks/exit0。daemon:206-219另外覆盖假长度包住完整HELLO及多帧，不冒称本轮重启daemon实测。
- **3 会话/重放/跳号：**sim:1106-1153同session+host的HELLO不清水位，换会话撤资格/健康/屏障，保留活动意图；:1931-1947先decode再session拒绝。EXECUTE:1640-1671按本session/action查行并比较全部意图，command_id另做跨session查重；:1686-1688水位不连续拒绝；:1690-1705检查日志、活动、门/物品、READY、连续性、健康。测试分别见protocol:353、:654、:1180、:1244；旧session负例:1306、同水位换command:782、跳号:718、完整参数变更:1280/1283/1286及脉冲计数:1293/1316。结构性slot/kind负例填了operation_id后由codec:904/907先拒，**不能称为sim handler重复校验各自被单独覆盖**；整体字节入口拒绝仍符合规格。
- **4 重开持久化：**sim:266-289独立sim_meta/sim_events/sim_commands/sim_commands_v2，v2主键(session_id,action_no)；:2132只打开config.journal_path，WAL/FULL在:2142。:2179每次open增boot，sim_boot_body:2033设seq1，:2186重算bounds；session_watermark:1082按会话取已保留行。QUERY:1786用当前session查历史command，保留原session/boot。journal:266实际restart后取两boot键；:318 close/open同库；:557在:585 destroy、:590 create、:591 open同一路径，验证活动/释放检查点；:836核未驱动意图重启仍ACCEPTED、脉冲计数0。不是只改内存变量模拟重开，也不是MCU掉电验收。
- **5 ACK/脉冲独立：**drop_ack、drop_execute_ack、suppress_pulse是三个独立字段和入口（sim:102-104、:2430、:2439、:2457）；:2289只按相应type隐藏回复，既不反置pulse，也不重发。protocol:1320循环suppressed=0/1，核reply_length0、水位1、pulse0/1、QUERY分别ACCEPTED/PULSE_STARTED/FINISHED；:1368显式重复只回原状态、计数不增。port:1655的一次发送后QUERY计帧、daemon:139及其EXECUTE/QUERY/SQLite段为进程用例；ACK_PERSISTED丢回复另有protocol:1114和port:1307。daemon实际启动由subprocess.Popen(binary,...)（:152/:339），不是import模拟引擎。
- **6 提交失败：**sim_journal_commit:415-417在fail_journal/一次COMMIT故障时ROLLBACK并false；execute:1690守卫失败不推进水位/has_active，:1730事务成功才赋值。journal:427确实只证明前置守卫，不能拿它单独证明COMMIT失败；另有:488五阶段故障循环，在READY之后:508注入BEGIN/INTENT/EVENT/COMMIT/RESULT，:512-520同时检查内存、sim_commands_v2、sim_events与meta；COMMIT点真正进入sim_run_txn并回滚。RESULT点保留已接纳水位1而无脉冲，与“意图提交失败水位0”区分。:806用SQLite触发器产生真实写失败，off不能清FAULT；:624物理提交失败回滚输入/revision。
- **7/8 私有入口及独立库：**sim_main:71用AF_UNIX SOCK_STREAM，:79 umask0077→bind→:86 chmod0600→listen；daemon:355实际stat/S_IMODE并断言0600，保留日志:2-3为两个0600，m44改变chmod会令两项变红。本轮没新建socket重做stat。源码无AF_INET/HTTP server/公共模拟路由/真实tty/HAL/GPIO实现，sim只使用自己journal_path，无Repository调用。调用者仍必须按配置给独立路径，未宣称任意指定业务库路径也会自动安全隔离。
- **9 越界/残留/空实现：**四行根接入、common21项/storage31项以及清单均复算匹配；25文件逐项职责见下表。端口实际收发、解析、持久事务及失败分支存在，未发现空实现/固定成功替代卡片。没有Git或开工前全量测试副本，不能绝对证明历史从未删/放宽测试；当前正式测试保留历史反例、对应错误预期已按规格修正，未见用删除失败用例掩盖本轮缺陷。临时残留扫描结果见上文。
- **10 原始证据：**本人重数普通/ASan/TSan各19条Passed，三份exit0，总耗时9.62/10.98/11.77s；build/configure原文未发现warning:/error:，记录Werror配置，ASAN_REPORTS0/TSAN_HITS0。17个本地vm日志的SHA与guest原件逐个相等。17个C测试数为[97,5,53,164,133,53,80,46,865,131,90,179,130,461,507,168,369]，和3531，本卡六项和1814=3531−1717；4个check_JSON与design-checks.log解析对象逐个相等。daemon为**250/0**（N-2），不计入C总数。16条GATE_OK与run-summary末行VM_RUN_VERDICT=PASS可核，不能把文档中的历史SSH_EXIT当已留原始输出。真实UART/GPIO硬截止/MCU-littlefs掉电/CRC认证仍NOT_RUN，没有改写成已验证。
- **11 mutation非空洞性：**本人用AST/literal_eval读全部48个old/new，逐点old在当前源恰好一次、日志anchor与old→new吻合；实际--check得到points48/bad0。逐段重数45个ctest8、3个ctest0，48个configure0/build0；BUILD_FAILED0成立。失败位置470个不同文件:行全部回到正式TEST_CHECK宏，并核代表失败与其变异行为关联；不是编译失败或泛FAIL行计捕获。48行复算表及3个等价边界见后。分母说明缺口独列N-3。没有执行full，未写mutation.log。
- **12 测试质量及边界：**旧session/错误body/错误type后正确回复仍可接收等真实socket测试见port测试:1398/:1916；daemon真正驱动独立可执行文件、stat、控制和SQLite，不拿进程内socket测试代替sim_main。三处C分页循环在protocol:271、journal:179、port测试:939均有LOCKER_TEST_PAGE_LIMIT=64及超界失败；daemon prepare:230仍用while True，靠读超时/CTest120s收敛，不能声称所有语言的分页循环均有页数界。m10/11/12/13主要证明入口错误码，codec/sim重复守卫不等于每一层独立覆盖。
- **13 A-F语义：**A线上OR0x80符合UART13；原生DTO解释仍N-1。B pending_take不匹配自然继续扫描且不消费，pending_add:156同锁拒重复在途号，符合UART23；没有字面continue（N-4）。C重开在sim:2168-2170报UNKNOWN，只有constructor:2006-2007定义CLOSED/PRESENT，符合未知不冒充观测；journal:594-595核重建对象。D silent_gap机制已整体删除，sim:1318-1323只要continuity=false即gap1，与UART370已知缺口需报1及UART50锁存规则相容；规格没有要求锁存后某页必须报0，不创造带内缺口豁免。E sim:843-861满通知队列不改已提交日志/continuity，protocol:990核34条日志、恰32次pop后NOT_READY再补取，符合UART356。F sim_main:256/449按语法和引擎token区分，README:43有说明，daemon含对应负例；是私有控制协议，不加公共wire_result或HTTP。

### 前三轮逐项回查

表内“解除”只指原具体反例；不是本轮完整动态重验。路径缩写：port/codec/wire=host/device/src/*.c，sim=host/simulator/src/sim.c；protocol/journal=tests/unit/device/test_sim_protocol.c、test_sim_journal.c；port测试=tests/contract/device/test_device_port.c；daemon=tests/contract/device/daemon_smoke.py。

| 历史项 | 本人核对的当前落点与证据 | 判定 |
|---|---|---|
| B-1 响应session | port:127-142/:408-409；port测试:1398-1432，外来帧后真帧仍投递 | 解除 |
| B-2 错误body镜像 | port:417-419仅result0应用；port测试:1434-1451 | 解除 |
| B-3 旧意图/guard/游标迁移 | port:716/:761/:846/:881；port测试:1489、:1849-1908含真实close/open后保持boot/revision的旧意图 | 解除 |
| B-4 设备身份/能力 | port:185-198/:410-416/:605；port测试:1455四种失败 | 解除；不宣称认证 |
| B-5 在途号/超时/回绕 | port:112-182/:503；port测试:1416-1418/:1618/:1916 | 解除 |
| B-6 sink背压 | port:202-214锁存/shutdown、:493阻发送；port测试:1544-1578 | 解除 |
| B-7 屏障/健康/驱动前截止 | sim:1371/:1399/:1703/:1741/:1756/:1823；protocol:1180；journal:744 | 解除；detail遗漏另按R3复核 |
| B-8 正常脉冲结束 | sim:1575-1598/:2310；protocol:932-984，结束/释放/下一动作 | 解除；非硬件截止 |
| B-9 会话水位/全参去重 | sim:282/:1082/:1643/:1670；protocol:1244-1316；journal:349 | 解除；未来回收策略另需验证 |
| B-10 重启/释放检查点 | sim:2057/:2168/:1897；journal:557-620/:836 | 解除 |
| B-11 EXECUTE丢回复 | sim:2289/:2439；protocol:1320；port测试:1655；daemon:139 | 解除 |
| B-12 通知满与日志丢失 | sim:843-861；protocol:990-1036 | 解除 |
| B-13 缺口/真实事务失败 | sim:1318/:1356/:2482/:2504；journal:488/:624/:647/:806 | 解除 |
| B-14 daemon重同步/零号/未知type | sim_main:195-245；sim:2270-2280；daemon:193-219 | 解除 |
| B-15 mutation虚假等价/漏测 | 48点锚点/日志独立复算，3点当前调用图等价见后 | 原反例解除；统计N-3 |
| B-16 复验删活跃源/吞门禁/工作区 | evidence-run-vm:9-10/:22/:107/:220-256无rm -rf；mutation:17-20工作区/日志分离 | 删除及传播反例解除；目录守卫文字N-4 |
| N-1 异步推送 | sim_main:776/:805排空；daemon:449-455不追加UART请求即接两事件 | 解除 |
| N-2 控制解析 | sim_main:314/:335/:392/:594；daemon非法hex、尾垃圾、NUL、跨read长行、多行负例 | 解除 |
| N-3 有界IO及关闭 | transport:140-174、atomic connected；port:965；port测试:1761/:1822 | 有界写/关闭互斥可核；不宣称覆盖所有交错 |
| N-4 SQL读取/u64 | sim:187/:574/:651/:1238；journal损坏行测试:687；两个COUNT辅助失败返回0 | 原核心反例解除；不是所有sqlite_step都走sim_sql_ok |
| N-5 生命周期/签名 | 生成头:289-311、port:949-992、handoff:198/:204/:243、sim_destroy:2014 | 主要错误解除 |
| N-6 状态/数字 | stage-plan依赖只有S1-01，前两卡DONE；3531/1814实算；daemon见N-2 | 主要关闭，保留当前数字漂移 |
| N-7 缺Python少测 | tests/contract/device/CMakeLists.txt:43 FATAL_ERROR | 解除 |
| N-8 固定无关临时unlink | device_test_support:192-199 pid+counter，无旧wal_cleanup固定路径 | 解除 |
| N-9 后端标记 | sim_main:736、daemon日志:1 | 解除 |
| R2-B-1 | run-vm独立batches/RUN/src，无rm -rf；extract:2-4非活跃目标 | 解除；不以标签充当前后散列 |
| R2-B-2 | sim_echo_session:1016、:2286-2287在dispatch前取请求首16B；daemon:569及protocol:1402新增比较 | 解除；坏FETCH早退由该前置代码保护 |
| R2-B-3 | 无SIM_META_SILENT_GAP/silent_gap；sim:1318；protocol:512-520 | 解除 |
| R2-B-4 | sim:1897-1898 detail0；journal:605/:613-617核TEXT零与CAST | 解除 |
| R2-B-5 | port:115/:171注入；port测试:1638推进，另:1589真实时钟 | 解除 |
| R2-B-6 | handoff:125合法UID、:202发号边界、:204/:243销毁失效、:223通知满 | 四处主要错误解除 |
| R2-N-1 | 两README run_id指向run-summary首行 | 解除 |
| R2-N-2 | gates16条GATE_OK，run-summary:28真实verdict；SSH历史无transcript | verdict落盘解除；文字精度N-4 |
| R2-N-3 | mutation:352源位置正则、:364分母相加、daemon另计；48点实算匹配表 | 主问题解除；m43汇总不完整N-3 |
| R2-N-4 | message_type持续登记待负责人裁定 | 保留N-1 |
| R3-B-1 | sim:1520/:1528/:1538用active action十进制文本；:1769内存也用request action；journal:790-797三条type12/detail断言，jump3/5均执行 | **源码确已修，解除** |
| R3-B-2 | handoff:360仍Bash，与:479冲突；本轮bash -n rc2 | **未解除，R4-B-1** |
| R3-B-3 | mutation183170B/1417行/完整64位SHA与本次输入、summary一致 | 解除 |
| R3-N-1 | 原生message_type未定，继续明确登记 | 保留N-1 |
| R3-N-2 | sink按值复制、耗尽NOT_READY、destroy失效、live/batch区别、写路径SQL与TEXT比较均已在相应正文改正 | 主要改正；少量旧表述N-4，不能说全部文字已清 |
| R3-N-3 | daemon:569旧session逐字比较、protocol:1402坏HELLO前缀比较；:697填operation、:763-765读真实snapshot flag | 新断言确在；slot/kind首先是decoder校验，非重复handler守卫独立覆盖 |
| R3-N-4 | wire:90删除0xc0例外；test_wire新增request_no0/1的INVALID，当前实跑130checks | 解除 |

### mutation 的等价性边界与48点复算

- **m24_hello_version：当前调用图等价。** codec:836先拒requested_version≠1，sim_dispatch:1934解码失败即返回；到不了sim:1114第二检查。protocol:378版本负例经过真实decoder，但不能证明第二层单独执行。无需为不可达第二层伪造动态覆盖。
- **m28_ack_upper：正常可达日志状态等价。** 初始ACK=(0,0)；HELLO:1137 fetched=ACK；FETCH:1324-1326只从合法提交页next_after推进，故fetched≤last。删through>last检查仍会在:1356因through>fetched返回同CURSOR_GAP；其余行为不变。论证限于引擎正常创建/维护的日志，不包括外部篡改或损坏数据库；不能扩大成任意DB内容也等价。
- **m31_execute_replay：当前保留命令行的实现等价。** action0被codec:910拒。W>0来自HELLO按本session最大行（sim:1082）或成功插入后的:1733，必存在session/action=W的行；相等请求在:1643命中，完全相同返回已有状态、不同参数REPLAY_BLOCKED，到不了:1687的相等分支。W=UINT64_MAX同理。代码没有命令明细回收，因此不能拿这份证明批准未来回收后删除水位防线；uart-v1:388的长期防重放要求仍有效。
- m44-48的old确在sim_main生产行为上；由daemon捕获，C测试不链接main。m47名字含resync，但实际变异是request_no大端重建，直接失败为关联号；CRC重同步由daemon:206-215另测，不能把m47当单独CRC恢复变异。m10-13捕获的是入口可观测返回码，不夸大为完整状态机证明。
- 表中 n/d 为原始C源位置失败行数/具有完整红测试汇总的分母之和，daemon另计；m43按N-3标注分母不完整。每点old唯一、日志new一致、configure/build为0均已逐项assert；未重新运行full。

| 点位 | 当前锚点 | 结果 | n/d；daemon红 | 相关失败观测（原日志） | mutation.log段首行 |
|---|---|---|---|---|---|
| m1_crc_poly | host/device/src/wire.c:26 | CAUGHT | 34/298；12 | tests/unit/device/test_wire.c:16 expected 10673, got 0 | 60 |
| m2_magic | host/device/src/wire.c:196 | CAUGHT | 5/298；3 | tests/unit/device/test_wire.c:213 expected 1, got 0 | 114 |
| m3_oversize_len | host/device/src/wire.c:205 | CAUGHT | 2/298；0 | tests/unit/device/test_wire.c:261 stream.stats.length_errors > 0u | 130 |
| m4_crc_mismatch | host/device/src/wire.c:219 | CAUGHT | 8/805；3 | tests/unit/device/test_wire.c:234 expected 0, got 1 | 139 |
| m5_reqno_zero | host/device/src/wire.c:98 | CAUGHT | 1/130；0 | tests/unit/device/test_wire.c:77 expected status 3, got 0 | 159 |
| m6_event_reqno | host/device/src/wire.c:95 | CAUGHT | 1/130；0 | tests/unit/device/test_wire.c:80 expected status 3, got 0 | 166 |
| m7_pending_type | host/device/src/port.c:134 | CAUGHT | 2/369；0 | tests/contract/device/test_device_port.c:1945 expected 2, got 3 | 173 |
| m8_event_session | host/device/src/port.c:362 | CAUGHT | 1/369；0 | tests/contract/device/test_device_port.c:1297 expected 0, got 1 | 181 |
| m9_reply_flag | host/device/src/port.c:429 | CAUGHT | 93/369；0 | tests/contract/device/test_device_port.c:1951 expected 130, got 2 | 188 |
| m10_simulated_gate | host/device/src/port.c:612 | CAUGHT | 1/369；0 | tests/contract/device/test_device_port.c:865 expected status 5, got 8 | 287 |
| m11_slot_id | host/device/src/port.c:703 | CAUGHT | 1/369；0 | tests/contract/device/test_device_port.c:910 expected status 3, got 6 | 294 |
| m12_fetch_limit | host/device/src/port.c:784 | CAUGHT | 2/369；0 | tests/contract/device/test_device_port.c:923 expected status 3, got 6 | 301 |
| m13_anchor_reqno | host/device/src/port.c:924 | CAUGHT | 3/369；0 | tests/contract/device/test_device_port.c:840 expected status 3, got 5 | 309 |
| m14_cursor_halfzero | host/device/src/codec.c:209 | CAUGHT | 3/309；1 | tests/unit/device/test_codec.c:268 expected 10, got 0 | 318 |
| m15_event_type_range | host/device/src/codec.c:294 | CAUGHT | 2/179；0 | tests/unit/device/test_codec.c:592 expected 10, got 0 | 330 |
| m16_card_type | host/device/src/codec.c:312 | CAUGHT | 1/179；0 | tests/unit/device/test_codec.c:619 expected 10, got 0 | 338 |
| m17_reserved | host/device/src/codec.c:324 | CAUGHT | 2/179；0 | tests/unit/device/test_codec.c:630 expected 10, got 0 | 345 |
| m18_supply_unknown | host/device/src/codec.c:327 | CAUGHT | 1/179；0 | tests/unit/device/test_codec.c:638 expected 10, got 0 | 353 |
| m19_decode_version | host/device/src/codec.c:1169 | CAUGHT | 1/179；0 | tests/unit/device/test_codec.c:378 expected 1, got 0 | 360 |
| m20_failed_len | host/device/src/codec.c:1191 | CAUGHT | 1/179；0 | tests/unit/device/test_codec.c:556 expected 10, got 0 | 367 |
| m21_fetch_len | host/device/src/codec.c:1221 | CAUGHT | 1/179；0 | tests/unit/device/test_codec.c:470 expected 10, got 0 | 374 |
| m22_queue_full | host/simulator/src/sim.c:851 | CAUGHT | 1/507；0 | tests/unit/device/test_sim_protocol.c:1027 expected status 5, got 0 | 381 |
| m23_queue_no_session | host/simulator/src/sim.c:847 | CAUGHT | 1/461；0 | tests/unit/device/test_sim_journal.c:332 expected 5, got 0 | 388 |
| m24_hello_version | host/simulator/src/sim.c:1114 | NOT_CAUGHT | 0/0；0 | 无失败；等价边界见上 | 395 |
| m25_has_more | host/simulator/src/sim.c:1276 | CAUGHT | 11/498；0 | tests/unit/device/test_sim_protocol.c:281 expected 0, got 11 | 400 |
| m26_fetch_after_bound | host/simulator/src/sim.c:1297 | CAUGHT | 1/507；0 | tests/unit/device/test_sim_protocol.c:473 expected 10, got 0 | 417 |
| m27_ack_regression | host/simulator/src/sim.c:1347 | CAUGHT | 3/507；0 | tests/unit/device/test_sim_protocol.c:559 expected 10, got 0 | 424 |
| m28_ack_upper | host/simulator/src/sim.c:1350 | NOT_CAUGHT | 0/0；0 | 无失败；等价边界见上 | 433 |
| m29_resume_mode | host/simulator/src/sim.c:1412 | CAUGHT | 2/507；0 | tests/unit/device/test_sim_protocol.c:650 expected 2, got 1 | 438 |
| m30_execute_horizon | host/simulator/src/sim.c:1679 | CAUGHT | 18/507；0 | tests/unit/device/test_sim_protocol.c:708 expected 4, got 0 | 446 |
| m31_execute_replay | host/simulator/src/sim.c:1687 | NOT_CAUGHT | 0/0；0 | 无失败；等价边界见上 | 470 |
| m32_execute_journal | host/simulator/src/sim.c:1690 | CAUGHT | 7/461；0 | tests/unit/device/test_sim_journal.c:534 expected 8, got 3 | 475 |
| m33_execute_door | host/simulator/src/sim.c:1696 | CAUGHT | 14/507；0 | tests/unit/device/test_sim_protocol.c:736 expected 4, got 0 | 488 |
| m34_borrow_item | host/simulator/src/sim.c:1698 | CAUGHT | 16/876；0 | tests/unit/device/test_sim_protocol.c:726 expected 4, got 0 | 508 |
| m35_health_timeout | host/simulator/src/sim.c:1374 | CAUGHT | 4/507；0 | tests/unit/device/test_sim_protocol.c:1229 expected 3, got 0 | 531 |
| m36_pulse_flag | host/simulator/src/sim.c:1771 | CAUGHT | 17/967；2 | tests/unit/device/test_sim_journal.c:576 expected 3, got 2 | 541 |
| m37_release_pulse | host/simulator/src/sim.c:1878 | CAUGHT | 10/507；0 | tests/unit/device/test_sim_protocol.c:961 expected 4, got 0 | 568 |
| m38_release_idempotent | host/simulator/src/sim.c:1885 | CAUGHT | 5/876；0 | tests/unit/device/test_sim_protocol.c:891 expected 0, got 4 | 584 |
| m39_dispatch_decode | host/simulator/src/sim.c:1934 | CAUGHT | 4/507；2 | tests/unit/device/test_sim_protocol.c:1398 expected 10, got 0 | 596 |
| m40_dispatch_session | host/simulator/src/sim.c:1940 | CAUGHT | 3/968；3 | tests/unit/device/test_sim_journal.c:293 expected 2, got 0 | 609 |
| m41_continuity_flag | host/simulator/src/sim.c:945 | CAUGHT | 1/461；0 | tests/unit/device/test_sim_journal.c:416 expected 4, got 0 | 623 |
| m42_inject_latch | host/simulator/src/sim.c:2488 | CAUGHT | 6/968；1 | tests/unit/device/test_sim_journal.c:414 !locker_sim_continuity_ok(fixture.sim) | 630 |
| m43_boot_increment | host/simulator/src/sim.c:2179 | CAUGHT | 646/1253；5（d不完整） | tests/unit/device/test_sim_journal.c:239 expected 1, got 0 | 645 |
| m44_socket_mode | host/simulator/src/sim_main.c:86 | CAUGHT | 0/0；2 | uart socket is mode 0600; control socket is mode 0600 | 1305 |
| m45_status_token | host/simulator/src/sim_main.c:259 | CAUGHT | 0/0；2 | card uid length must be 0/4/7/10; zero gap rejected | 1313 |
| m46_drop_link | host/simulator/src/sim_main.c:488 | CAUGHT | 0/0；1 | drop_link closes the accepted uart socket | 1321 |
| m47_frame_resync | host/simulator/src/sim_main.c:213 | CAUGHT | 0/0；24 | reply echoes request_no 1; snapshot reply echoes request_no 2 | 1328 |
| m48_event_flush | host/simulator/src/sim_main.c:184 | CAUGHT | 0/0；1 | control produces two immediate asynchronous events | 1358 |

### 25个受管文件指纹

以下由本轮hashlib/read_bytes/splitlines独立计算；完整摘要逐项与source-manifest比较全等。

| 文件 | SHA前12位 | 字节 | 物理行 |
|---|---|---:|---:|
| CMakeLists.txt | d8f1850b3979 | 4090 | 97 |
| host/device/CMakeLists.txt | 7bb766cd517a | 797 | 31 |
| host/device/README.md | d2cc1f19ce28 | 9540 | 98 |
| host/device/include/locker/device/codec.h | 390af779943e | 7048 | 152 |
| host/device/include/locker/device/transport.h | 4a51972fed87 | 2553 | 60 |
| host/device/include/locker/device/wire.h | 6bb4ea57b45b | 4303 | 108 |
| host/device/src/codec.c | b8bb1650cb3e | 48259 | 1266 |
| host/device/src/port.c | 8110920a24eb | 39388 | 992 |
| host/device/src/transport.c | 9849c989e46b | 6398 | 203 |
| host/device/src/wire.c | 8c532948cca1 | 7980 | 243 |
| host/simulator/CMakeLists.txt | 38e323558eda | 1520 | 45 |
| host/simulator/README.md | d2f47f226a82 | 6620 | 67 |
| host/simulator/include/locker/simulator/sim.h | 212608674017 | 5604 | 119 |
| host/simulator/src/sim.c | 3d81fd2b53f4 | 99545 | 2642 |
| host/simulator/src/sim_main.c | 2c205495648d | 27930 | 827 |
| tests/unit/device/CMakeLists.txt | cb8a5a701287 | 984 | 26 |
| tests/unit/device/device_test_support.h | 7243fbb2e46c | 10664 | 311 |
| tests/unit/device/test_codec.c | d8a26e31ba2b | 39632 | 777 |
| tests/unit/device/test_sim_journal.c | cba82f070087 | 42809 | 883 |
| tests/unit/device/test_sim_protocol.c | a8b2227ecfbb | 69822 | 1436 |
| tests/unit/device/test_wire.c | 709d109fada7 | 14631 | 353 |
| tests/contract/device/CMakeLists.txt | 977630d510db | 2180 | 44 |
| tests/contract/device/daemon_smoke.py | 013431f1a4af | 32731 | 659 |
| tests/contract/device/test_device_port.c | b0d8066c95e4 | 84134 | 1982 |
| tests/contract/device/test_golden_frames.c | 421fe6fddf5d | 14849 | 355 |

## 环境与 NOT_RUN

### 本轮实际运行

| 命令或操作 | 本轮输出/范围 |
|---|---|
| `python -X utf8 -B scripts/check_contracts.py` | exit0/PASS；0.2.1、24 routes、82 ports、28 RPC、11 UART、4向量、max469；pycparser为parse-only PASS；hardware_profile UNVERIFIED、business_or_hardware_tested=false |
| `python -X utf8 -B scripts/check_stage_plan.py` | exit0/PASS；45卡、active S1-03、done2、runtime_or_hardware_tested=false |
| `python -X utf8 -B scripts/check_vision_contracts.py` | exit0/PASS；camera_owned/rknn_available/model_inference_tested/hardware_tested均false |
| `python -X utf8 -B scripts/check_feasibility.py` | exit0/PASS；async3、exposure negative3、runtime_or_hardware_tested=false |
| `python -X utf8 -B docs/evidence/stage-01/S1-03/mutation-batch.py --check --src .` | exit0，48 CHECK_OK，CHECK_SUMMARY points=48 bad=0；在prepare/full/日志写入前退出 |
| Windows stdin Python（`python -X utf8 -B -`） | hashlib/json/binascii/AST/re统计及断言；不保存复算脚本，不覆盖checker JSON/清单/日志 |
| `ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=yes locker-vm "uname -a"` | exit0，Linux ubuntu 7.3.0-rc5 x86_64；本轮连接实际可用 |
| SSH `python3 -B -` 只读源/Cache/tar/log | exit0；guest批次25/25源相等，批次1151文件；/tmp/s103_snapshot.tar.gz实物81706992B、SHA `6f4479f1f1a5d3beffef4739eb2a7a1b198709819b7c81b002e5b5af0f049b9e`，tar普通文件1151、25个受管成员全匹配；仅extractfile读字节，未解包；17个vm日志与guest原件SHA全等 |
| guest三套现有CMakeCache只读 | source均指batches/20261025-185812/src、Werror=ON；ASan flags为address,undefined，TSan为thread |
| `/home/%USER%/locker-workspace/build/S1-03/20261025-185812/tests/unit/device/test_wire` | 本轮实跑既有二进制，PASS (130 checks)，exit0；无重编译 |
| 同构建目录 `tests/unit/device/test_codec` | 本轮实跑既有二进制，PASS (179 checks)，exit0；先核调用图无SQLite fixture/落盘 |
| 当前Windows mutation脚本文本经SSH stdin交给 `bash -n` | SSH原生returncode=2，第9行def的“(”语法错误；未执行脚本主体，直接复现R4-B-1 |

本机Windows Python的可选pycparser存在；保留VM检查JSON标NOT_RUN(optional pycparser unavailable)。这是两个环境的真实差别，不当成C编译或契约漂移。复算脚本中曾有Windows默认编码/正则及清单格式解析错误，均已改用UTF-8/正确格式后重新assert成功；失败尝试没有写文件，也未计为核验通过。

### 本轮没有运行或无法独立证实的内容

- **Linux CMake configure/build、完整CTest：NOT_RUN。** SSH虽可用，只读边界不允许创建构建目录/DB/socket/测试日志，故不重建或调用会落盘的完整套件。19/19是已核对原始日志及guest实物的保留结果，不是本轮19/19。
- **test_sim_journal、test_sim_protocol、test_device_port、test_golden_frames及其新增反例动态运行：NOT_RUN。** 正式测试会打开SQLite或创建socket；包括R3-B-1的jump3/5回归，本轮只核实现、断言、注册和461checks保留记录，没有冒称重新执行该分支。真实时钟烟测本轮亦NOT_RUN。
- **ASan/UBSan、TSan运行：NOT_RUN。** 仅核现有Cache、配置/构建/CTest日志、零报告记录；未重跑，零报告不证明所有未测试交错安全。
- **mutation full：NOT_RUN。** 会复制/修改沙箱并写日志，不在只读授权内；45/48来自逐段独立复算，只有--check为本轮实跑。
- **daemon进程启动、现场stat、控制故障注入：NOT_RUN。** 未创建新journal/socket或启动模拟器；250检查及两个0600来自已核原始日志、正式脚本和mutation失败观测。
- **WSL构建/CTest：NOT_RUN且未采用任何WSL证据。** 本轮没有尝试WSL，不把此前E_ACCESSDENIED冒充本轮输出。
- **真实UART/DMA/115200电气时序、ARM/32位交叉编译/Keil、STM32 HAL/GPIO硬截止、MCU/littlefs/RK介质掉电、CRC认证/抗篡改：NOT_RUN。** hardware profile仍UNVERIFIED；软件150ms转换、SQLite重开和环境目标历史编译不能替代它们。
- **历史SSH_EXIT=0终端记录、开工时四目录不存在、当时list_agents无并发、被删除的1113万行/1.7GB历史日志：未取得原始可复算记录，不据文档自述认证。** 它们未用于本轮PASS依据；当前源/快照/保留日志可核的数字已单独列出。
- **完整历史修改全集：不可证明。** 本项目按用户说明不是Git仓库，未用git status推定范围；仅证明本轮保护范围散列、前序清单覆盖项和明确列出的源/证据一致性。

本轮仅新增audit-round4.md，既有33个证据文件保留，新增后34个。未为同步文档擅改summary/handoff/README/stage-plan；需开发方处理的正文问题已在B/N表完整列明。

