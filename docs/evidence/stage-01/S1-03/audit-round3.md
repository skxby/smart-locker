# S1-03 round-3 独立审计
结论：FAIL
阻塞项：3 项；非阻塞项：4 项

日期：2026-10-25；项目：%REPO%；契约：0.2.1 / REVIEW_DRAFT。
R3-B-1、B-2是当前源码/交接的反例；B-3是用户要求严格核对的输入指纹问题，不是“日志遭篡改”的判断。即使确认B-3，前两项仍使本轮FAIL。

以下区分“本轮实跑”“静态核实”“独立复算保留日志”。未采信开发方自述代替验证，未以Windows、未重跑全套Linux或缺硬件作为阻塞。仅写本报告，不修实现/测试/契约/证据脚本，不置DONE，不跨卡，不commit/push。审计期间观察到外部输入变化，最终版本见末节。

## 阻塞项

| 编号 | 严重度 | 文件:行 | 期望行为 | 实际行为 | 最小复现步骤 | 依据（契约/规格文件:行原文） |
|---|---|---|---|---|---|---|
| R3-B-1 | P2 | **host/simulator/src/sim.c:1534**、**:1765**；**tests/unit/device/test_sim_journal.c:744**、**:785** | 驱动前第二次检查过期而不启动脉冲时，若记录类型仍是COMMAND_ACCEPTED，detail必须保留action_no；持久记录和实时通知一致。 | 第二次检查失败后，sim_cancel_unstarted_body把启动检查点改为COMMAND_ACCEPTED，却写detail的20位零文本；pulse_checkpoint.detail也置0。action_no=1时会有type12/detail1及type12/detail0。现有jump=5回归只核REJECTED、无type2和查询状态，没核新type12的detail，已有全绿不能排除此反例。 | **供开发复现，本轮未运行会落库的测试：**①用现有Debug构建，gdb启动tests/unit/device/test_sim_journal；②设断点“break test_sim_journal.c:781 if jump == 5”，run，停在第二次提交后超时场景；③“print fixture.journal_path”取尚未清理的独立日志路径；④另一终端运行“sqlite3 '<该路径>' 'SELECT event_seq,event_type,detail FROM sim_events WHERE event_type=12 ORDER BY boot_id,event_seq;'”；⑤末条detail按源码必为00000000000000000000，应为00000000000000000001。FETCH/DEVICE_EVENT也应核同字段，不得只改测试预期。 | **docs/architecture/uart-v1.md:345**：“COMMAND_ACCEPTED … detail 为 action_no；只证明意图提交”；**:386**：“已接纳 action_no 仍消耗，不能回退以便重试”。 |
| R3-B-2 | P2 | **docs/evidence/stage-01/S1-03/handoff.md:360**；**docs/evidence/stage-01/S1-03/mutation-batch.py:9** | 正式再生成入口使用Python解释器执行Python脚本，命令可复现。 | §8写成“bash …/mutation-batch.py --src '<repo-root>'”。脚本第9行Python的def被Bash判为语法错误。本轮只读bash -n已复现SSH原生rc2。这与POSIX构建是否可用无关。两个helper已被外部改为接收参数，不继续把其早先硬编码算在当前版本上；本条错误命令仍存在。 | ①在VMware源码根读handoff §8；②替换<repo-root>为实际批次src；③仅做无副作用语法检查“bash -n docs/evidence/stage-01/S1-03/mutation-batch.py”；④输出第9行“def _arg_value(flag):”附近语法错误、exit2。无需启动full mutation即可否定Bash执行入口。 | **AGENTS.md:35**：“同步相关当前文档”；**docs/architecture/module-ownership.md:69**：“只把编译改通过而留下旧文档不算完成”；**:71**：“每张任务卡交付：…入口/构建命令”。 |
| R3-B-3 | P3 | **docs/evidence/stage-01/S1-03/mutation.log:1**（核验对象）；**docs/evidence/stage-01/S1-03/summary.md:201**（完整值） | 用户指定的完整SHA-256基线应有64个十六进制字符，并与复算完全相等。 | 用户给的是63位“5276bc605e46c489d3b79f81cebe7b414f3e8158b48735f57e1ad0a2e1a4eb8”；实算64位为“5276bc605e46c489d3b79f81cebe7b414f3e8158b48735f57e1ad0a2e1a4eb8d”。后者与summary:201一致，180950B也一致，前63位完全相同，最可能是输入漏写末尾d。不能据此指控源码/日志变化；也不能自行确认缺位基线。本轮已请求补齐确认，写报告时未收到答复。 | ①Windows用Python/hashlib读取mutation.log并打印len(bytes)、sha256.hexdigest()；②得180950和上述64位值；③对用户给的摘要作len得63；④由提供基线者确认/补齐即可关闭本项，不要求开发方修改日志。可运行命令见下文。 | **docs/architecture/module-ownership.md:71**：“阶段报告保留…文件指纹和运行环境”；直接阻塞依据是**用户本轮指纹硬性要求**：“若与你算出的不符即为阻塞项”（用户消息无仓库行号，故不伪造其文件行号，也不冒充UART功能缺陷）。 |

B-3复算命令：python -B -c "from pathlib import Path; import hashlib; b=Path('docs/evidence/stage-01/S1-03/mutation.log').read_bytes(); print(len(b), hashlib.sha256(b).hexdigest())"。

B-1的gdb/SQLite步骤是复现说明，**不是本轮已执行结果**。已有正式测试能够驱动该分支；源码的两处零值足以定位偏差。

## 非阻塞项

| 编号 | 严重度 | 文件:行 | 期望行为 | 实际行为 | 最小复现步骤 | 依据（契约/规格文件:行原文） |
|---|---|---|---|---|---|---|
| R3-N-1 | P3 | **host/device/src/port.c:429**；**host/device/README.md:61**；**contracts/include/locker_contracts.h:181** | 契约负责人统一原生reply.message_type解释。 | 实现为request_type OR 0x80，测试自洽，README/summary/handoff已登记待裁定。线上规则不等于原生DTO语义已经冻结。 | ①读生成头181，仅uint8_t声明；②对照UART13线上规则；③读README61；④交负责人统一Core/未来MCU sink解释，本卡不改生成物。 | **docs/architecture/uart-v1.md:13**：“响应为请求 type OR 0x80”；**module-ownership.md:11**：“由架构负责人统一修改真源”。**原生字段语义规格未覆盖，需契约负责人裁定。** |
| R3-N-2 | P2 | **docs/evidence/stage-01/S1-03/handoff.md:198**、**:202**、**:243**、**:382**、**:436**、**:461**、**:464**；**summary.md:144** | 正文、整改表和索引准确描述实现及原始记录。 | handoff204已明确destroy后指针失效，故不重复round-2整体UAF阻塞；但243仍把INVALID解释为“已销毁”，不能保证释放地址再调用返回INVALID。198称借用sink表，实际port582复制表、借用user；202写发号耗尽→BUSY，正常发送路径port503返回NOT_READY。382称gates.log也含VM_RUN_VERDICT，实际只run-summary28有；17日志均无SSH_EXIT。461把batch的SRC_FILES当活跃树前后计数，1150与live_tree_files1149实属不同树且无before值。436“所有step经sim_sql_ok”不成立；464仍引detail<>0旧SQL。旧3489已被外部改为3509，不列当前缺陷。 | ①对照204/243与port991的free；②读port582/503；③搜索17日志的SSH_EXIT、VM_RUN_VERDICT；④比较source-verify31与run-summary9-10，不把两树计数当同树前后比较；⑤核journal测试613-617实际用零文本及CAST。 | **AGENTS.md:35**：“正文写当前真实规则”；**locker_contracts.h:292**：“Free only after close and thread join.”；**module-ownership.md:71**要求实际结果。主路径已澄清且主要数字可复算，列文档非阻塞。 |
| R3-N-3 | P2 | **docs/evidence/stage-01/S1-03/handoff.md:462**；**tests/contract/device/daemon_smoke.py:558**；**tests/unit/device/test_sim_protocol.c:1388**、**:692**、**:755** | 整改表只声称断言真正验证的行为；回显错误session有原反例回归；状态机负例不被更早结构守卫遮蔽。 | R2-B-2代码已修，但handoff所称两个“回显session”断言不存在：daemon旧快照只验type/length/result/CRC，没验payload[:16]；C坏HELLO前缀也只验结果/无副作用。755根据continuity构造flag常量，没读snapshot；692的slot/kind负例未设operation_id，先被codec UUID检查拒。真实PULSE_ACTIVE另有949/959，slot/kind编码层另有test_codec271，故不冒充当前状态机失效。 | ①读daemon558-568，无旧session字节比较；②读protocol1386-1389，同样无响应session比较；③跟踪684 memset至699，operation未设值，而codec899先拒；④读754-755，只依赖continuity；⑤补旧session+坏FETCH等早退正式回归并收窄整改表。本轮未执行新反例。 | **docs/architecture/uart-v1.md:29**：“其他响应回送该请求 session”；**:382**列动作条件；**module-ownership.md:31**禁止“用只反射实现的断言代替验收”。正确实现及其他独立断言仍在，不据覆盖建议增加功能阻塞。 |
| R3-N-4 | P3 | **host/device/src/wire.c:90**、**:91**；**tests/unit/device/test_wire.c:69** | encoder合法类型检查与定义集合一致。 | 通用encoder放行DEVICE_EVENT OR RESPONSE_FLAG，即0xc0，并要求非零request_no。生产调用使用0x40事件或0x81..0x8b响应，未找到0xc0生产调用，故不据未使用私有输入阻断当前链路。未知请求的错误响应已有独立error encoder。 | ①复用test_wire的frame数组/length；②调用locker_wire_encode_frame(0xc0u,1u,NULL,0u,frame,sizeof frame,&length)；③由90-99可知返回OK、length12；④该类型未定义，补负例或解释工具用途。该C调用本轮NOT_RUN。 | **docs/architecture/uart-v1.md:13**：“请求1..11；响应为请求type OR 0x80；异步DEVICE_EVENT=0x40”；**host/device/include/locker/device/wire.h:88**：“INVALID for a bad type/length argument”。 |

## 已核实通过项

“历史反例静态解除”仅评价原反例，不批准DONE，也不覆盖R3-B-1。

- **入口与状态已核。** 阅读根AGENTS、README、stages/README、stage-plan、S1-03原文、UART全文、相关内部接口/归属和机器契约。索引仍S1-03 IN_PROGRESS、report=null、active_task/recommended_next=S1-03；前两卡DONE，阶段1 gate NOT_RUN。契约源0.2.1/REVIEW_DRAFT，生成头版本一致。
- **四项Windows设计检查实跑均exit0/PASS。** python -X utf8 -B scripts/check_contracts.py、check_stage_plan.py、check_vision_contracts.py、check_feasibility.py。输出24 routes、82 ports、28 RPC、11 UART、4 vectors、max payload469、45 cards、done2；hardware_profile=UNVERIFIED，business/runtime/hardware_tested=false。Windows pycparser为parse-only PASS；保留VM JSON为可选依赖未装的NOT_RUN，不混同。
- **25项源完整SHA/字节匹配。** 独立hashlib核清单和用户前12位，共580983B；清单25条数据+3注释、2793B、SHA580bb8bd73a9b62b10834285d43602a7526fb59e42d2d8d322ff4d3a61c82e28；与vm-s103-manifest.sha256逐字相同。八个契约/规格/索引SHA及字节逐项匹配summary16-23。未发现受管源码漂移；mutation完整基线单列B-3。
- **前序及根接入有清单支持。** S1-01的common源码/测试21项全匹配；S1-02的32项全匹配，其中根CMake仅去掉88/89/95/96后为3943B、SHA f9e1964ee54465f5e29299721a3f342f929cc81e3ff6a6da8d00b77fc34258b4，其余31项直接匹配。没有Git/开工前全项目清单，不能证明所有scripts历史字节从未变化。
- **复用及模块边界静态符合。** 根/局部CMake核为C11/POSIX、CMake/CTest；simulator CMake27-32复用locker_common/device/storage_thirdparty、SQLite3.54.1 Public Domain及cJSON1.7.19 MIT，无本卡新增下载/浮动master/自研DB/HTTP。无业务Repository调用，sim2128只开config.journal_path；调用者仍须给独立路径，不能扩大为任意传入业务库路径也安全。
- **黄金帧4/4逐字独立assert相等。** JSON frame_hex与test_golden_frames.c17/21/24/27的拼接C常量直接比较。HELLO61B/payload49、SNAPSHOT28B/payload16。binascii.crc_hqx(frame[2:-2],0xffff)得0xbdd8/0xea6e，线路d8bd/6eea；crc_error计算0xea6e而线路6eeb；oversized_header10B、声明513。123456789→0x29b1，LE=b129。wire115/216覆盖offset2至payload末，不含magic/CRC。
- **11对长度逐字段求和。** 读uart-layouts字段宽度，得HELLO49/71、SNAPSHOT16/189、FETCH49/(85+96N，N0..4，最大469)、ACK48/34、RESUME64/27、DISARM18/27、EXECUTE99/93、QUERY48/93、HEALTH73/27、ANCHOR36/46、RELEASE80/27；event96=96、DEVICE_EVENT=128。与codec215/245和test_codec14一致；错误响应18B由codec1189、test_codec502实际检查。
- **解析边界有实现及本轮wire动态支持。** wire.h73缓冲524；wire168/171处理满缓冲，195/205/218对magic、超长、CRC错均丢1字节；先满足长度才读CRC/拷payload，无对端长度malloc。test_wire124/149/188/211/232/258/287覆盖逐字节、粘帧、噪声、CRC、超长、最大帧、真实handler BUSY。sim_main244复用同parser，195有界重建；daemon206有损坏长度包住合法HELLO的进程用例。未声称每个理论兜底分支均动态覆盖。
- **SSH本轮实际可用，guest快照实物独立复算。** ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=yes locker-vm "uname -a" exit0，Linux ubuntu 7.3.0-rc5 x86_64。只读批次src25/25匹配、1150文件、host/tests stray0；/tmp/s103_snapshot.tar.gz实物81686184B、SHA802a5ae0700c8dc23dfce68b365b8d445750470674d73c5f891daaa4e639edd9，普通文件1150、25受管成员全匹配。tar用extractfile读字节，没有解包。
- **两个guest既有C二进制本轮实跑。** /home/%USER%/locker-workspace/build/S1-03/20261025-180516/tests/unit/device/test_wire→PASS device.test_wire (128 checks)；同目录test_codec→PASS device.test_codec (179 checks)，均exit0。已查调用图不调用sim_fixture_open；未重建，不冒充本轮19/19。
- **保留日志主要数字独立复算。** unit-tests1-35的17个C数为[97,5,53,164,133,53,80,46,865,131,90,179,128,443,505,168,369]，和3509，本卡六C为1792=3509−1717。普通/ASan/TSan各19条Passed、10.15/11.20/11.56s。daemon日志11-14记249/0/PASS/exit0，不计入C总数。source-verify实际25条OK，footer25/25/0/0、STRAY0、SRC_FILES1150；run_id均20261025-180516。四个保留check JSON与design-checks.log对应JSON逐对象相等。
- **sanitizer配置和历史记录相符。** guest三Cache同批次src、Werror=ON；ASan address,undefined，TSan thread；对应配置/构建exit0、CTest各19/19、ASAN_REPORTS0/TSAN_HITS0。本轮未重跑，不从零报告推断未测试交错安全。
- **R2-B-1删除反例解除。** evidence-run-vm9-10为独立batches/RUN/src，22拒已有批次，无rm -rf；32-38仅解包批次，107校验失败exit4。extract2-4证明本次目标不同于活跃树；标签不是活跃树前后内容证明（N-2）。脚本15先mkdir再查WS，不能据workspace_missing守卫证明原目录已存在；本次已有guest批次不受该诊断问题影响。
- **R2-B-2实现确已改。** sim1016-1022读取payload首16B，2282-2283在dispatch前设reply；2274未知type同样回显，1927早退不再附当前session。旧session+坏FETCH反例静态会保留旧session；针对性新增断言并不存在，见N-3。
- **R2-B-3缺口豁免删除。** 搜索实现无SIM_META_SILENT_GAP/silent_gap；sim1318-1323在continuity=false时gap1；protocol512-520正式核inject_gap2后gap1、3条可用记录、first(1,1)。journal647-684另核失败结束后的gap/拒ACK/重启锁存。
- **R2-B-4释放detail已修。** sim1893-1894传reason0/detail0；journal613-617用规范零文本并CAST核0，618重启后active仍清。R3-B-1是另一事件类型的遗漏。
- **R2-B-5时钟已修。** port115/171都调用注入device_now_ms，旧device_deadline_now已无；port测试1638推进5001ms、1642迟到响应不投递、1646新号QUERY可用、1648-1651核回绕；1589另有真实时钟HELLO/SNAPSHOT/close烟测，不再靠真等5.3秒验证协议pending超时。
- **R2-B-6主要四处已改。** handoff125为24位UID；202不再17次尝试；204声明destroy后失效；223队满只丢通知。port981-992 close/join后free，sim2010-2016 destroy关库；残余措辞归N-2。
- **R2-N-1..4回查。** 两README90/65指向run-summary首行；gates实际16个GATE_OK，run-summary28保存VM_RUN_VERDICT=PASS；mutation按断言行和红测试分母、daemon另计，独立复算见表；message_type已登记待裁定。历史SSH_EXIT和“两个日志都含verdict”的证据不足归N-2。

### round-1逐项回查

下表只关闭历史具体反例，不据“已整改”自述判通过。

| 历史项 | 当前源码及正式测试证据 | 独立判断 |
|---|---|---|
| B-1 响应session | port127-140/409；test_device_port1398-1452 | 三元匹配；外来响应后真响应仍可交付，原反例解除。 |
| B-2 错误body镜像 | port417；port测试1435-1451 | 仅result0应用，错误仍投递。 |
| B-3 旧意图/游标迁移 | port716/761/846/881；port测试1489、1849-1908 | 实现有绑定；保持boot/revision的真实close/open旧意图负例存在。 |
| B-4 身份/能力 | port185/410/605；port测试1455 | 实际UID派生24位hex并核0x1f能力，不提供认证。 |
| B-5 在途号/回收/回绕 | port112-182；port测试1416/1638/1945 | 同锁拒重复、注入时间回收、回绕拒绝，不重发。 |
| B-6 sink背压 | port202-214/493；port测试1544-1578 | BUSY/NO_SPACE后锁存、shutdown、全部请求NOT_READY。 |
| B-7 恢复/健康/截止 | sim1371/1699/1737/1752/1819；protocol1166；journal744 | 非空守卫与专门负例存在；第二次截止事件detail另见B-1。 |
| B-8 脉冲生命周期 | sim1571/2306；protocol922-975；journal647 | 150ms确定性结束/释放/下一动作存在，不是硬截止。 |
| B-9 会话水位/参数 | sim282/1082/1639；protocol1234-1306 | 复合主键、全意图比较、command跨会话查重、新session action1场景。 |
| B-10 重启/释放 | sim2053/2164/1893；journal557-620/824 | 真destroy/create/open同库；释放再重启；未知不伪装物理确定。 |
| B-11 EXECUTE丢回复 | sim2285/2435；protocol1310；port1655；daemon256-302 | 独立开关；一次EXECUTE后QUERY与持久计数。 |
| B-12 通知与日志 | sim843-860；protocol980-1026 | 33通知时34日志保留、pop恰好32次，FETCH/ACK补齐。 |
| B-13 缺口/故障 | sim1318/1356/2478/2500；journal488/624/647/794 | 五阶段真实事务注入及SQLite触发器负例；off不清真实FAULT。 |
| B-14 daemon重同步/零号 | sim_main195-247；sim2266；daemon193-219 | 同parser逐字节恢复、零号副作用前拒、未知非零type回UNSUPPORTED。 |
| B-15 mutation | mutation逐段及下文等价证明 | 45捕获有相关观测，3生存只在当前调用图/可达状态等价。 |
| B-16 源码/工作区 | evidence-run-vm9/107/220；mutation17-20/382 | 不删活跃src、门禁非零传播、工作区入build/logs；入口错误另见B-2。 |
| N-1 异步推送 | sim_main776/805；daemon449-455 | 不再靠下一UART请求排空。 |
| N-2 控制解析 | sim_main314/335/392/594；daemon164/425-443 | 整token、溢出、hex、NUL、超长跨read、多行负例。 |
| N-3 有界写/关闭 | transport140-174；port965；port测试1761/1822 | 1000ms IO截止、原子connected及写/关闭串行，不证明所有race均测。 |
| N-4 SQL读取 | sim187/574/651/1238；journal687 | step/宽度/u64溢出核心检查存在，不接受“所有step同helper”夸大。 |
| N-5 交接生命周期 | 生成头289-311、port981、handoff37/204 | 核心签名与销毁顺序已对；正文残余N-2。 |
| N-6 状态/数字 | stage-plan、summary40、清单/日志复算 | 索引依赖仅S1-01，当前数字核实，不重判前卡。 |
| N-7 Python缺失 | tests/contract/device/CMakeLists.txt43 | FATAL_ERROR明确daemon NOT_RUN，不静默少测。 |
| N-8 固定临时路径 | device_test_support192-199 | pid+counter，无无关wal_cleanup；未清理正式fixture。 |
| N-9 后端标记 | sim_main736、daemon日志1 | 实际启动含device backend: simulator；handoff178样例仍旧。 |

表中省略目录的port/codec/wire分别指host/device/src/下同名.c，sim指host/simulator/src/sim.c；protocol/journal指tests/unit/device/test_sim_protocol.c、test_sim_journal.c，port测试指tests/contract/device/test_device_port.c，daemon指tests/contract/device/daemon_smoke.py。

### A～F语义独立判断

- **A：message_type含0x80。** 线上符合UART13；原生DTO待裁定N-1，未改契约。
- **B：不匹配继续扫描。** port131-140的for自然继续、不消费错项；156拒在途同号。符合UART23。源码无字面continue，“陈旧条目复用号”不能当允许重复在途号的理由。
- **C：重开UNKNOWN。** sim2002-2003只定义全新模拟场景，2164-2166已有日志重开置UNKNOWN；journal338-342/585-595有跨open及重建对象断言，符合未知不得伪装确定事实。
- **D：锁存后所有页gap1。** UART370要求已知缺口不能报完整；当前没有“带内标记豁免”。epoch级锁存保守报1，不违反明文禁止项；规格没要求锁存后某页必须报0，不能自行添加豁免。该结论不表示每页物理上都缺记录，也不定位具体缺失。
- **E：通知满不锁存continuity。** sim851时日志已提交，符合UART356“发送队列满不丢日志”；正式测试同时核数量、32次pop、补取。
- **F：语法/引擎token。** sim_main256-299/449-576与sim README43一致，是0600私有协议；没有加公共wire_result或HTTP路由。daemon真实断言区分bad_argument/missing_argument/unknown_command/invalid等。

### mutation独立复算

- 本轮实跑“python -X utf8 -B docs/evidence/stage-01/S1-03/mutation-batch.py --check --src .”：exit0、48 CHECK_OK、points48/bad0。另用AST/literal_eval逐点读old/new，old各恰好1次，日志anchor及old→new与当前源相符，未执行full。
- 48段configure0/build0；45段ctest8、3段ctest0；无构建失败误算捕获。逐段重数C断言位置行、红测试分母之和、daemon标签，全与日志和summary207-254一致；每条C失败位置都回到正式TEST_CHECK行。
- m44-48由真实daemon捕获；daemon152/339确为Popen独立可执行文件。m47实际改request_no字节序，名字含resync不等于单独证明CRC恢复；后者另有正式用例。m10/11/12/13部分主要捕获入口错误码，不夸大成整个状态机安全证明。
- **m24等价（当前调用图）：** codec836先拒requested_version≠1，sim1927-1931早退，到不了sim1114第二检查；protocol373-380有版本负例，不等于第二层独立被测。
- **m28等价（当前正常日志可达状态）：** 初始ACK不越尾；HELLO1137 fetched=ACK，FETCH1324仅从已提交page.next_after推进，故fetched≤last；删through>last仍被1356的through>fetched返回同CURSOR_GAP。不覆盖外部篡改/损坏数据库。
- **m31等价（当前不回收命令行）：** action0先被codec910拒；W>0来自sim1082的本session最大行或1729成功接纳。等于W先命中1639，参数同返回原状态、不同REPLAY_BLOCKED，达不到1683相等分支；UINT64_MAX同理。未来回收须重做证明，不能删水位防线。
- 三处C分页helper在protocol271、journal179、port测试939均有LOCKER_TEST_PAGE_LIMIT=64和越界失败；daemon prepare230的Python循环仅受请求/CTest总超时，未冒称所有循环有页数上界。
- mutation.log180950B，1405个LF/实际记录行；末尾换行后split('\n')得到1406个元素，末个为空。用户/summary“1406行”可由此解释，不当成内容变化。完整摘要问题单列B-3。

下表“证据行”为mutation.log段首；n/d是C失败断言数/红测试分母之和，daemon另计。每个捕获列至少一个相关断言或daemon观测；全量C失败行已逐条检查。

| 点位 | 当前锚点 | 结果 | n/d；daemon红 | 相关失败观测 | 证据行 |
|---|---|---|---|---|---|
| m1_crc_poly | host/device/src/wire.c:26 | CAUGHT | 34/296；12 | tests/unit/device/test_wire.c:16 expected 10673, got 0 | mutation.log:60 |
| m2_magic | host/device/src/wire.c:195 | CAUGHT | 5/296；3 | tests/unit/device/test_wire.c:205 expected 1, got 0 | mutation.log:114 |
| m3_oversize_len | host/device/src/wire.c:204 | CAUGHT | 2/296；0 | tests/unit/device/test_wire.c:253 stream.stats.length_errors > 0u | mutation.log:130 |
| m4_crc_mismatch | host/device/src/wire.c:218 | CAUGHT | 8/801；3 | tests/unit/device/test_wire.c:226 expected 0, got 1 | mutation.log:139 |
| m5_reqno_zero | host/device/src/wire.c:97 | CAUGHT | 1/128；0 | tests/unit/device/test_wire.c:77 expected status 3, got 0 | mutation.log:159 |
| m6_event_reqno | host/device/src/wire.c:94 | CAUGHT | 1/128；0 | tests/unit/device/test_wire.c:80 expected status 3, got 0 | mutation.log:166 |
| m7_pending_type | host/device/src/port.c:134 | CAUGHT | 2/369；0 | tests/contract/device/test_device_port.c:1945 expected 2, got 3 | mutation.log:173 |
| m8_event_session | host/device/src/port.c:362 | CAUGHT | 1/369；0 | tests/contract/device/test_device_port.c:1297 expected 0, got 1 | mutation.log:181 |
| m9_reply_flag | host/device/src/port.c:429 | CAUGHT | 93/369；0 | tests/contract/device/test_device_port.c:1951 expected 130, got 2 | mutation.log:188 |
| m10_simulated_gate | host/device/src/port.c:612 | CAUGHT | 1/369；0 | tests/contract/device/test_device_port.c:865 expected status 5, got 8 | mutation.log:287 |
| m11_slot_id | host/device/src/port.c:703 | CAUGHT | 1/369；0 | tests/contract/device/test_device_port.c:910 expected status 3, got 6 | mutation.log:294 |
| m12_fetch_limit | host/device/src/port.c:784 | CAUGHT | 2/369；0 | tests/contract/device/test_device_port.c:923 expected status 3, got 6 | mutation.log:301 |
| m13_anchor_reqno | host/device/src/port.c:924 | CAUGHT | 3/369；0 | tests/contract/device/test_device_port.c:840 expected status 3, got 5 | mutation.log:309 |
| m14_cursor_halfzero | host/device/src/codec.c:209 | CAUGHT | 3/307；1 | tests/unit/device/test_codec.c:268 expected 10, got 0 | mutation.log:318 |
| m15_event_type_range | host/device/src/codec.c:294 | CAUGHT | 2/179；0 | tests/unit/device/test_codec.c:592 expected 10, got 0 | mutation.log:330 |
| m16_card_type | host/device/src/codec.c:312 | CAUGHT | 1/179；0 | tests/unit/device/test_codec.c:619 expected 10, got 0 | mutation.log:338 |
| m17_reserved | host/device/src/codec.c:324 | CAUGHT | 2/179；0 | tests/unit/device/test_codec.c:630 expected 10, got 0 | mutation.log:345 |
| m18_supply_unknown | host/device/src/codec.c:327 | CAUGHT | 1/179；0 | tests/unit/device/test_codec.c:638 expected 10, got 0 | mutation.log:353 |
| m19_decode_version | host/device/src/codec.c:1169 | CAUGHT | 1/179；0 | tests/unit/device/test_codec.c:378 expected 1, got 0 | mutation.log:360 |
| m20_failed_len | host/device/src/codec.c:1191 | CAUGHT | 1/179；0 | tests/unit/device/test_codec.c:556 expected 10, got 0 | mutation.log:367 |
| m21_fetch_len | host/device/src/codec.c:1221 | CAUGHT | 1/179；0 | tests/unit/device/test_codec.c:470 expected 10, got 0 | mutation.log:374 |
| m22_queue_full | host/simulator/src/sim.c:851 | CAUGHT | 1/505；0 | tests/unit/device/test_sim_protocol.c:1017 expected status 5, got 0 | mutation.log:381 |
| m23_queue_no_session | host/simulator/src/sim.c:847 | CAUGHT | 1/443；0 | tests/unit/device/test_sim_journal.c:332 expected 5, got 0 | mutation.log:388 |
| m24_hello_version | host/simulator/src/sim.c:1114 | NOT_CAUGHT | 0/0；0 | 无失败；等价性限界见上 | mutation.log:395 |
| m25_has_more | host/simulator/src/sim.c:1276 | CAUGHT | 11/496；0 | tests/unit/device/test_sim_protocol.c:281 expected 0, got 11 | mutation.log:400 |
| m26_fetch_after_bound | host/simulator/src/sim.c:1297 | CAUGHT | 1/505；0 | tests/unit/device/test_sim_protocol.c:473 expected 10, got 0 | mutation.log:417 |
| m27_ack_regression | host/simulator/src/sim.c:1347 | CAUGHT | 3/505；0 | tests/unit/device/test_sim_protocol.c:559 expected 10, got 0 | mutation.log:424 |
| m28_ack_upper | host/simulator/src/sim.c:1350 | NOT_CAUGHT | 0/0；0 | 无失败；等价性限界见上 | mutation.log:433 |
| m29_resume_mode | host/simulator/src/sim.c:1412 | CAUGHT | 2/505；0 | tests/unit/device/test_sim_protocol.c:650 expected 2, got 1 | mutation.log:438 |
| m30_execute_horizon | host/simulator/src/sim.c:1675 | CAUGHT | 17/505；0 | tests/unit/device/test_sim_protocol.c:702 expected 4, got 0 | mutation.log:446 |
| m31_execute_replay | host/simulator/src/sim.c:1683 | NOT_CAUGHT | 0/0；0 | 无失败；等价性限界见上 | mutation.log:469 |
| m32_execute_journal | host/simulator/src/sim.c:1686 | CAUGHT | 7/443；0 | tests/unit/device/test_sim_journal.c:534 expected 8, got 3 | mutation.log:474 |
| m33_execute_door | host/simulator/src/sim.c:1692 | CAUGHT | 13/505；0 | tests/unit/device/test_sim_protocol.c:730 expected 4, got 0 | mutation.log:487 |
| m34_borrow_item | host/simulator/src/sim.c:1694 | CAUGHT | 15/874；0 | tests/unit/device/test_sim_protocol.c:720 expected 4, got 0 | mutation.log:506 |
| m35_health_timeout | host/simulator/src/sim.c:1374 | CAUGHT | 4/505；0 | tests/unit/device/test_sim_protocol.c:1219 expected 3, got 0 | mutation.log:528 |
| m36_pulse_flag | host/simulator/src/sim.c:1767 | CAUGHT | 16/947；2 | tests/unit/device/test_sim_journal.c:576 expected 3, got 2 | mutation.log:538 |
| m37_release_pulse | host/simulator/src/sim.c:1874 | CAUGHT | 10/505；0 | tests/unit/device/test_sim_protocol.c:951 expected 4, got 0 | mutation.log:564 |
| m38_release_idempotent | host/simulator/src/sim.c:1881 | CAUGHT | 5/874；0 | tests/unit/device/test_sim_protocol.c:881 expected 0, got 4 | mutation.log:580 |
| m39_dispatch_decode | host/simulator/src/sim.c:1930 | CAUGHT | 3/505；2 | tests/unit/device/test_sim_protocol.c:1388 expected 10, got 0 | mutation.log:592 |
| m40_dispatch_session | host/simulator/src/sim.c:1936 | CAUGHT | 3/948；2 | tests/unit/device/test_sim_journal.c:293 expected 2, got 0 | mutation.log:604 |
| m41_continuity_flag | host/simulator/src/sim.c:945 | CAUGHT | 1/443；0 | tests/unit/device/test_sim_journal.c:416 expected 4, got 0 | mutation.log:617 |
| m42_inject_latch | host/simulator/src/sim.c:2484 | CAUGHT | 6/948；1 | tests/unit/device/test_sim_journal.c:414 !locker_sim_continuity_ok(fixture.sim) | mutation.log:624 |
| m43_boot_increment | host/simulator/src/sim.c:2175 | CAUGHT | 640/1233；5 | tests/unit/device/test_sim_journal.c:239 expected 1, got 0 | mutation.log:639 |
| m44_socket_mode | host/simulator/src/sim_main.c:86 | CAUGHT | 0/0；2 | uart socket is mode 0600; control socket is mode 0600 | mutation.log:1293 |
| m45_status_token | host/simulator/src/sim_main.c:259 | CAUGHT | 0/0；2 | card uid length must be 0/4/7/10; zero gap rejected | mutation.log:1301 |
| m46_drop_link | host/simulator/src/sim_main.c:488 | CAUGHT | 0/0；1 | drop_link closes the accepted uart socket | mutation.log:1309 |
| m47_frame_resync | host/simulator/src/sim_main.c:213 | CAUGHT | 0/0；24 | reply echoes request_no 1; snapshot reply echoes request_no 2 | mutation.log:1316 |
| m48_event_flush | host/simulator/src/sim_main.c:184 | CAUGHT | 0/0；1 | control produces two immediate asynchronous events | mutation.log:1346 |

### 25项受管文件指纹

每个受管文件均已核对其职责/实现/测试或文档。行数字段采用LF数量，与用户受管表口径一致；daemon末行无终止LF，末行定位654而LF数653，不影响字节/SHA核验。

| 文件 | SHA前12位 | 字节 | LF数 |
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
| host/device/src/wire.c | b951abb050f6 | 7914 | 242 |
| host/simulator/CMakeLists.txt | 38e323558eda | 1520 | 45 |
| host/simulator/README.md | d2f47f226a82 | 6620 | 67 |
| host/simulator/include/locker/simulator/sim.h | 212608674017 | 5604 | 119 |
| host/simulator/src/sim.c | 3b76890341a6 | 99309 | 2638 |
| host/simulator/src/sim_main.c | 2c205495648d | 27930 | 827 |
| tests/contract/device/CMakeLists.txt | 977630d510db | 2180 | 44 |
| tests/contract/device/daemon_smoke.py | 4848411a6407 | 32364 | 653 |
| tests/contract/device/test_device_port.c | b0d8066c95e4 | 84134 | 1982 |
| tests/contract/device/test_golden_frames.c | 421fe6fddf5d | 14849 | 355 |
| tests/unit/device/CMakeLists.txt | cb8a5a701287 | 984 | 26 |
| tests/unit/device/device_test_support.h | 7243fbb2e46c | 10664 | 311 |
| tests/unit/device/test_codec.c | d8a26e31ba2b | 39632 | 777 |
| tests/unit/device/test_sim_journal.c | dc24469de2af | 42014 | 871 |
| tests/unit/device/test_sim_protocol.c | 12d8602495ab | 68917 | 1421 |
| tests/unit/device/test_wire.c | 897352a133aa | 13972 | 345 |

- **私有入口/模拟边界静态核对。** sim_main79先umask0077→bind→86 chmod0600→listen；daemon355真stat/S_IMODE，保留日志2-3为两个0600，本轮未重做stat。模块只开AF_UNIX SOCK_STREAM，无公共HTTP模拟路由、真实HAL/GPIO/tty实现；未给公共schema加模拟字段。硬件脉冲只模拟计数。不存在空实现/固定成功占位完成本卡的情况。
- **残留检查已执行，未清理正式内容。** 四个授权目录扫描无.db/.sock/.sqlite/.pyc/.o/.a/.tmp/CMakeCache.txt；正式fixture/证据/诊断日志保留。evidence-run-wsl.sh、wsl-*、joint-fix-round1.md不存在。当前summary/handoff只说WSL证据已删除；历史audit.md/audit-round2.md的历史叙述未作为现存WSL验收证据。

## 环境与 NOT_RUN

### 本轮实际运行

| 操作 | 本轮结果 |
|---|---|
| Windows四项checker | 上述命令均exit0/PASS，未覆盖保留JSON。 |
| Windows stdin Python | hashlib/json/binascii/AST及清单/黄金帧/长度/日志复算；审计代码只在内存，无另存脚本。 |
| mutation --check --src . | exit0、48/48、bad0；在prepare/full/日志写入之前退出。 |
| SSH uname | exit0，guest确实可连接。 |
| SSH stdin python3 -B - | 只读批次src、tar成员和Cache；实物SHA/字节/1150文件/25项源确认，无解包。 |
| guest既有test_wire/test_codec | 分别128/179 checks，PASS/exit0，无重建。 |
| guest bash -n mutation-batch.py | SSH原生rc2，第9行def语法错误，确认B-2；未执行脚本主体。 |
| guest旧快照脚本不传--src的--check | 原生rc2、SOURCE_ROOT_MISSING /mnt/e/stm32项目。这是打包时旧脚本，不能否定审计中外部改为os.getcwd()的当前Windows脚本，未执行full。 |

非零SSH结果另用Windows Python subprocess参数数组直接采returncode2，避免PowerShell包装进程exit1与远端码混淆。没有Git历史核验；用户已声明不是Git仓库，未再用git status推定范围。

### 明确未运行

- **Linux CMake configure/build、完整CTest：NOT_RUN。** SSH可用，但只允许报告落盘，未建构建目录/重编译，未调用会写DB/socket的整套CTest。保留19/19不冒充本轮。
- **test_sim_journal/test_sim_protocol/test_device_port/test_golden_frames：本轮NOT_RUN。** 这些二进制会落盘；golden319也开fixture SQLite，故未重复round-2误跑golden的越界。真实时钟烟测仅静态和保留日志支持，未单独实跑。
- **ASan/UBSan、TSan运行：本轮NOT_RUN。** 仅读Cache与原始日志，未重跑。
- **mutation full：本轮NOT_RUN。** 会复制/修改沙箱并写mutation.log；只运行不落盘--check，45/48是保留记录独立复算。
- **daemon启动/控制注入、B-1的gdb+SQLite反例、N-4新增C调用：本轮NOT_RUN。** 未建DB/socket/新故障证据；249/0来自保留记录，非本轮驱动249项。
- **WSL构建/CTest/证据：本轮NOT_RUN且未采用。** 没有用WSL替代VM验收。
- **真实UART/DMA/电气时序、32位/ARM交叉编译/Keil、STM32 HAL、GPIO硬截止、MCU/littlefs/RK介质掉电：NOT_RUN。** 模拟150ms转换、SQLite重开、旧环境目标构建不能替代；profile仍UNVERIFIED。CRC认证/抗篡改NOT_RUN，UART21明确CRC不承担认证。

### 输入变化和审计边界

最初读取handoff45698B，期间外部改为46394B，并将首段3489改为3509；两个打包helper从硬编码挂载改为参数；mutation脚本从20767B变20744B、默认源根改为os.getcwd()。这些变化不是本审计命令写入，已重新读取后按最新版本判断，未继续把外部已修问题列为当前阻塞。25个受管源、清单、mutation日志仍匹配。最终输入摘要：

| 文件 | SHA-256 |
|---|---|
| handoff.md | 0dfd7b04e0979842de2b8ebebe4a443e8114fa8a128d80b966d7e5283587e33e |
| summary.md | d0aadfc4b8c62f47016d48b370e3fed2b32a243b9de2df25d12213242fd0ad8f |
| evidence-make-manifest.sh | 2fdf16261b2e5952801ccbbe51272192990149b742e611d6fddd0cc034c882a3 |
| evidence-make-snapshot.sh | 0043f4d67c8506e4d129360d9c1c5f9e688fe0c09e37e677e07c3c25c64d18ad |
| evidence-run-vm.sh | 704c0824a5979aded36548ff17eb687964ad6130c886674bcd02d98bae5ecfd8 |
| mutation-batch.py | 2f9ed6476eaca4e207eed7c857b5cd44da7018b488160686ca94ab4eaae79718 |
| mutation.log | 5276bc605e46c489d3b79f81cebe7b414f3e8158b48735f57e1ad0a2e1a4eb8d |
| source-manifest.txt | 580bb8bd73a9b62b10834285d43602a7526fb59e42d2d8d322ff4d3a61c82e28 |
| audit.md | cc823445cde67b5f80f125cedcf8c96f3ecb0aad27ca78a69ea44c2f1189f35d |
| audit-round2.md | f189c7bf35ab581b849cda20305f8180fe646f090f58802084b1f7b39ac5600d |

这些是最终读取版本，不冒称审计期间变化后的证据脚本已被旧canonical日志动态复验。本报告是本轮唯一写入；没有生成临时DB/socket、删改源码/日志/正式测试/状态。写入前本卡证据目录32文件，新增报告后33。用户明确禁止同步README/summary/stage-plan，本轮未改它们。

