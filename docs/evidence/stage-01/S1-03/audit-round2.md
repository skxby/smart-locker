# S1-03 round-2 独立审计
结论：FAIL
阻塞项：6 项；非阻塞项：4 项

日期：2026-10-25。工作副本：%REPO%；基线：0.2.1 / REVIEW_DRAFT。FAIL 来自下列规格偏差和未完成整改，不以 Windows 或未重跑全套 Linux 测试为理由。未修代码、测试、契约或证据脚本，未置 DONE、跨卡、commit/push。

“静态核实”指独立阅读代码/断言；“保留日志”指独立复算既有记录；“本轮实跑”仅指确实执行的命令。未运行的动态反例提供给开发方复现，不冒称已执行。R2 编号与历史 audit.md 的编号独立。

## 阻塞项

| 编号 | 严重度 | 文件:行 | 期望行为 | 实际行为 | 最小复现步骤 | 依据（规格文件:行原文） |
|---|---|---|---|---|---|---|
| R2-B-1 | P1 | `docs/evidence/stage-01/S1-03/evidence-run-vm.sh:6`、`:17`、`:27`；`docs/evidence/stage-01/S1-03/handoff.md:408` | 使用独立批次快照，保留既有源码；整改说明与脚本一致。 | SRC 和 EXPECTED_SRC 都固定为同一路径，常规情况下字符串检查必过；第27行仍递归删除该目录，再解包。该目录本轮 SSH 核实确实存在且25/25源匹配。handoff“不再有 rm -rf”不成立。新增校验失败退出/末尾门禁不能修复删除问题；仅检查末级非symlink也不能确认父目录及活跃工作区。 | **只读复现，禁止真的执行删除**：①读脚本第6、16、17行，两个变量均为 `$HOME/locker-workspace/S1-03/src`；②SSH只读查看该现有目录的manifest，确有25项；③沿正常通过guard的控制流到第27行，仍是 `rm -rf "$SRC"`；④下一步第29行在同一路径解包，会先删除既有源。这是原B-16未修部分。 | `AGENTS.md:22`：“不移动/删除旧源码”；`AGENTS.md:24`：“整改版需新的VM日志”；`docs/architecture/module-ownership.md:71`：“实际变更/命令/结果/限制”。 |
| R2-B-2 | P2 | `host/simulator/src/sim.c:1924`、`:1926`、`:1934`、`:2273`；`host/device/src/codec.c:853` | 错误响应也回送可读取的原请求session，不能给旧请求贴当前session。 | handle_frame先以当前session初始化reply；decode失败直接返回，跳过request-session回显。旧session的FETCH(max_count=0)得BAD_PAYLOAD，但响应session变成当前session。帧版本等早退也保留当前session。若请求号/类型撞上当前pending，会消费当前请求；不应用错误body不能弥补关联错误。 | ①HELLO建立S2；②构造type3、request_no99，payload=`S1(16)+当前epoch(16)+cursor(0,0)(16)+00`，S1≠S2，CRC正确；③经locker_sim_handle_frame并解码；④实际result10/sessionS2，应保留S1。复用test_sim_protocol.c的sim_exchange即可，必须断言response.fetch_events.session_id，不只断言result。本轮动态反例NOT_RUN。 | `docs/architecture/uart-v1.md:29`：“其他响应回送该请求 session。拒绝旧会话时回送旧请求 session，仅用于关联错误”；`:23`：“当前 session+request_no+response_type 三者都匹配才应用响应”；`:21`规定字段非法的错误响应。 |
| R2-B-3 | P2 | `host/simulator/src/sim.c:1308`、`:1313`、`:2474`；`tests/unit/device/test_sim_protocol.c:508`、`:516`；`docs/evidence/stage-01/S1-03/handoff.md:430` | 对存在已知缺失的请求范围，FETCH可成功返回可用记录，但必须gap_before_page=1。带内标记不等于缺失事实补齐。 | inject_gap已持久确认丢失、continuity=false，却不设silent_gap；FETCH仅看silent_gap或after早于first_available。正式测试在注入丢失2条后，取包含缺口标记的页仍断言gap=0。“有带内标记所以可为0”是规格没有的豁免。ACK拒跨缺口/锁存HISTORY_INCOMPLETE已实现，但不能代替FETCH字段要求。 | ①用test_fetch_pagination场景生成BOOT、门开、物品移除，尾(1,3)；②control_inject_gap(sim,2)，产生(1,4) CONTINUITY_LOST、dropped2；③FETCH after(1,1)、max_count4；④result0、3条可用记录，但gap0（现第516行断言此值）；应为1。需同时修实现和错误测试预期。本轮该动态反例NOT_RUN。 | `docs/architecture/uart-v1.md:370`：“after 已被回收或范围存在已知缺口，仍以 OK 返回 gap_before_page=1、first_available 和可用记录”；`:50`：“即便 event_seq 连续也不能认为无丢失”；`AGENTS.md:28`：“不自行放宽验收”。 |
| R2-B-4 | P2 | `host/simulator/src/sim.c:1889`、`:1890`、`:1449`；`tests/unit/device/test_sim_journal.c:605` | CONTEXT_RELEASED保留operation/command，detail必须0。 | release把active_intent.action_no作为detail传给sim_transition，直接写进事件。首、次释放落1、2；现有测试只数type13行，不验detail。本轮只读guest保留daemon SQLite，实际读到这两个值。 | ①恢复READY，执行action_no1；②等模拟脉冲结束或用suppress_pulse，发送匹配RELEASE_CONTEXT；③FETCH/DEVICE_EVENT取type13，实际detail1，应0。亦可直接对既有daemon日志库只读执行 `SELECT event_seq,event_type,detail FROM sim_events WHERE event_type=13`，本轮得到seq3/detail1和seq7/detail2，末节附可运行命令。 | `docs/architecture/uart-v1.md:352`：“operation/command 标明被释放上下文；detail=0”；`:46`：“不适用为0”。 |
| R2-B-5 | P2 | `host/device/src/port.c:105`、`:119`、`:175`；`tests/contract/device/test_device_port.c:1550`、`:1564` | pending协议超时回归由注入单调时钟确定性推进，另保留真实时钟烟测。 | pending_add/expire绕过config.clock读CLOCK_MONOTONIC。正式超时测试实际等5300ms，注入clock没有推进。已有模拟器健康/准备截止的注入时间测试和daemon真实烟测，不能覆盖这条pending语义超时路径。 | ①读port.c:105/119/175，确认均取真实时间；②读test_device_port.c:1550～1569，只有port_sink_wait(...,5300)；③在相同hold-response夹具只推进注入clock5001ms，立即注入保留响应，pending不会按注入clock过期；④只有真实等待5秒才过期。第三步为供开发补正式回归的静态反例，本轮socket测试NOT_RUN。 | `docs/stages/stage-01-task-cards.md:123`：“超时测试使用注入的单调时钟……集成测试至少另保留一项真实时钟烟测”。 |
| R2-B-6 | P2 | `docs/evidence/stage-01/S1-03/handoff.md:202`、`:200`、`:221`、`:125`；`host/device/src/port.c:995`、`:685` | 当前交接准确描述生命周期；destroy成功后指针失效，不可再用。正文参数/队列规则也应同步。 | handoff:202仍保证destroy后调用任意端口返回INVALID，并称created已清零；实际destroy直接free，后续API读取device->created构成UAF，不能保证INVALID。原N-5这一内容没改，末尾却称已修。正文还保留“最多17次发号尝试”、通知队满锁存continuity、无效expected_device_id=locker-device-01样例，分别不符port.c:180、sim.c:863、port.c:609。 | ①读handoff:202的保证；②对照destroy末尾free和get_snapshot:685读取；③按正文 `create(&d); destroy(d); get_snapshot(d,1);` 是未定义行为，非安全INVALID（本轮不执行错误调用）；④正文应直接说明旧指针失效/调用者置NULL，并同步其他列出的真实规则，不能靠末尾对照表覆盖正文。 | `contracts/include/locker_contracts.h:292`：“Free only after close and thread join.”；`docs/architecture/internal-interfaces.md:28`：“线程退出后销毁”；`AGENTS.md:35`：“同步相关当前文档”；`docs/architecture/module-ownership.md:69`：“只把编译改通过而留下旧文档不算完成”。 |

优先处理R2-B-1/B-2/B-3：分别影响复验源码保全、会话关联和缺口契约。B-4是实际事件数据不符；B-5是明确测试纪律未满足；B-6是可误导调用方产生内存错误的现行交接说明。

## 非阻塞项

| 编号 | 严重度 | 文件:行 | 期望行为 | 实际行为 | 最小复现步骤 | 依据（规格文件:行原文） |
|---|---|---|---|---|---|---|
| R2-N-1 | P3 | `host/device/README.md:90`；`host/simulator/README.md:65`；`docs/evidence/stage-01/S1-03/vm-s103-environment.log:12` | 当前入口run_id对应现存证据。 | README仍20261025-124245，当前日志/summary/handoff为20261025-151907。测试数字可独立对上，故标识漂移不否定保留运行记录。 | ①读两处README；②读environment.log:12；③对照summary:118，run_id不一致，更新当前入口。 | `AGENTS.md:35`：“同步相关当前文档”；`docs/architecture/module-ownership.md:71`：“阶段报告保留测试日志索引、提交/文件指纹和运行环境”。 |
| R2-N-2 | P2 | `docs/evidence/stage-01/S1-03/summary.md:144`；`docs/evidence/stage-01/S1-03/handoff.md:296`；`docs/evidence/stage-01/S1-03/evidence-run-vm.sh:207`、`:238` | 声称历史命令输出verdict/SSH_EXIT，应有原始transcript。 | 17个vm-s103-*文件均无VM_RUN_VERDICT=PASS。脚本在cat/打包后才向终端输出verdict，缺失原因可解释；各门禁码能复算为0，但不能证明历史终端确实输出该行/SSH_EXIT。本轮SSH成功也不能补作历史记录。 | ①Python遍历本目录glob('vm-s103-*')找VM_RUN_VERDICT，结果0文件命中；②读脚本207/238的输出顺序；③核各exit为0，区分“门禁可复算满足”和“原始输出已留存”；④下次保留transcript或收窄现有措辞。 | `docs/architecture/module-ownership.md:71`：“DONE 证据来自实际运行或明确的静态检查；‘代码已生成’‘AI 自查通过’不是证据”。 |
| R2-N-3 | P3 | `docs/evidence/stage-01/S1-03/mutation-batch.py:347`、`:348`、`:356`；`docs/evidence/stage-01/S1-03/summary.md:271` | 区分FAIL输出行、逐条断言和各测试分母。 | fails混入FAIL test.name汇总行，也可跨多个测试；denom只取首个。m7是2条源位置失败+1汇总，记3；m43的646是FAIL行数，含638条C源位置行及其余8条daemon/汇总，不能称646条失败断言/437。45/48不因此改变。 | ①取mutation.log的m7段，数2条源位置行及1汇总；②对照startswith('FAIL ')；③m43区分源位置/汇总/daemon，得638/8；④改称FAIL行，按测试分别给分母。 | `docs/architecture/module-ownership.md:71`：“实际变更/命令/结果/限制”。规格未限定mutation统计格式，不升级为产品失败。 |
| R2-N-4 | P3 | `host/device/src/port.c:433`；`host/device/README.md:61`；`contracts/include/locker_contracts.h:181` | 当前响应类型表示可继续使用，由契约负责人统一跨端原生DTO解释。 | 线上OR0x80有明文，原生message_type仅uint8_t声明。实现/测试自洽，但“本卡裁定”不是共享契约已定案。 | ①读生成头181；②对照UART13线上规则；③读README61；④交负责人确认Core/未来MCU sink均按含0x80解释，不由功能卡手改生成物。 | `docs/architecture/uart-v1.md:13`：“响应为请求 type OR 0x80”；`docs/architecture/module-ownership.md:11`：“由架构负责人统一修改真源”。**原生字段语义规格未覆盖，需契约负责人裁定**。 |

## 已核实通过项

- **四项Windows设计检查本轮实跑均exit0/PASS**：`python -B scripts/check_contracts.py`、`check_stage_plan.py`、`check_vision_contracts.py`、`check_feasibility.py`。实际输出0.2.1、24 routes、82 ports、11 UART requests、4 vectors、45 cards、active_task=S1-03、done2；hardware_profile=UNVERIFIED，runtime/business/hardware_tested=false。本机pycparser PASS(parse only)，VM保留JSON为可选依赖未装的NOT_RUN；不把parse当编译。
- **25项指纹独立全匹配**：逐文件SHA前12位/字节/物理行与用户列表一致；完整SHA/字节亦匹配source-manifest。合计575528B。清单25条数据+3条注释、2789B，SHA=`c84e1772bda04ee4a1dac2feb5e45d80ba7c8b6f34c6770ffbce326bf383d7bf`，与vm-s103-manifest.sha256逐字节相同。8个契约/规格/索引指纹亦逐一匹配summary:16～23。
- **前序及根接入可复算**：S1-01清单中common源码/测试21项全部匹配；S1-02清单32项，根去除88/89/95/96四行后也匹配，其他31项直接匹配。去除四行的根为3943B、SHA=`f9e1964ee54465f5e29299721a3f342f929cc81e3ff6a6da8d00b77fc34258b4`。simulator CMake:27复用现有locker_common/device/storage_thirdparty；SQLite3.54.1 Public Domain、cJSON1.7.19 MIT沿用既定vendor/依赖锁，无新增下载。无Git/开工前全项目快照，不能由此证明所有scripts历史字节从未改过。
- **黄金帧独立逐字一致**：读取JSON frame_hex，与test_golden_frames.c:17/21/24/27的C拼接常量比较，4/4相同。HELLO61B/payload49，SNAPSHOT28B/payload16。binascii.crc_hqx(frame[2:-2],0xffff)得0xbdd8/0xea6e，线路d8bd/6eea吻合；crc_error算0xea6e、线路6eeb，应拒；oversized_header10B、payload513。123456789→0x29b1，LE为b129。wire.c:115/:216覆盖version到payload末，不含magic/CRC。
- **长度逐字段独立求和**：HELLO49/71、SNAPSHOT16/189、FETCH49/(85+96N，N0..4，最大469)、ACK48/34、RESUME64/27、DISARM18/27、EXECUTE99/93、QUERY48/93、HEALTH73/27、ANCHOR36/46、RELEASE80/27；event96=96、DEVICE_EVENT=128。与codec.c:215/:245及test_codec.c:14长度断言一致；错误响应18B，codec:1189、test_codec:502真实检查。
- **本轮guest既有二进制实跑**：`.../20261025-151907/tests/unit/device/test_wire`→128 checks；`.../tests/unit/device/test_codec`→179；`.../tests/contract/device/test_golden_frames`→168，均PASS/exit0。不是本轮重编译，也不是本轮19/19 CTest。golden测试实际临时落盘的审计失误在末节披露，不能称其无落盘。
- **解析器边界有实现及实测支持**：wire.h:73缓冲524B；wire.c:168/:171有满缓冲处理，:195/:205/:218对magic/>512/CRC错误均丢1字节；先查长度再读CRC/拷payload，不按对端长度分配。test_wire.c:124/149/188/211/232/258/287覆盖逐字节、粘帧、重同步、CRC、超长、最大帧及实际handler BUSY。sim_main.c:244复用同一parser，:195受限重建；daemon_smoke.py:206有损坏长度内嵌HELLO进程用例，本轮daemon未重跑。
- **持久化/重启真实有代码和跨open/close断言**：sim.c:271四表，:2124仅开config.journal_path，:2134 WAL+FULL，:2171 boot+1，BOOT seq1，:2178重算first/last；无业务Repository调用。test_sim_journal.c:338同对象close/open，:585真实destroy/create/open同一日志，:603释放成功后:606再次重启active=false；:349旧command可跨重启查询，活动PULSE_STARTED转UNKNOWN_RESULT。新session水位0，不等于旧动作检查点消失。
- **ACK/PULSE独立控制成立**：sim.c:2275区分type4 drop_ack、type7成功回复drop_execute_ack，:1746 suppress_pulse仅影响后续脉冲。test_sim_protocol.c:1307覆盖suppress on/off，丢EXECUTE回复后QUERY/计数；test_device_port.c:1580走真实AF_UNIX transport；daemon_smoke.py:152/:272/:291确实Popen独立进程、一次EXECUTE、QUERY、SQL计starts/finishes，并非import替代进程。本轮未重跑这些会建DB/socket的用例。
- **提交失败不是仅前置guard**：sim.c:420在fail_journal或正式COMMIT注入时ROLLBACK；execute:1685遇注入/FAULT返回LOG_UNAVAILABLE。test_sim_journal.c:488分BEGIN/INTENT/EVENT/COMMIT/RESULT五阶段注入，COMMIT确实经过sim_run_txn提交，查SQL行数/meta水位/active；:782用SQLite trigger拒绝结果UPDATE，off不能清FAULT。该静态核实由历史437断言日志支持，本轮journal二进制NOT_RUN。
- **私有0600及禁止越界可核**：sim_main.c:79 umask0077→bind→:86 chmod0600；daemon_smoke.py:355真os.stat/stat.S_IMODE，vm-s103-daemon-smoke.log:2/:3两个0600。代码仅AF_UNIX/SOCK_STREAM；搜索host中AF_INET/HTTPServer/mg_start等，无本卡公共HTTP/模拟路由、HAL/GPIO/tty实现。启动:736含device backend: simulator；未向公共schema加字段。journal路径由配置独立给出，不把SQLite文件头当成“任意配置都绝不可能指向业务库”的证明。
- **保留日志数字逐项复算**：17个C PASS行断言为[97,5,53,164,133,53,80,46,865,131,90,179,128,437,503,168,357]，总3489，S1-03六个C合计1772=3489−1717；daemon249不计入C总数。三套CTest各19条Passed，17C+2Python，总时14.84/16.07/16.69s，与summary/handoff吻合。daemon249/0/exit0；source-verify实际25条OK，footer为MANIFEST_LINES25、SHACHECK_OK25、FAILED0、EXIT0、STRAY0、SRC_FILES1178。verdict原始终端未保留，见N-2。
- **guest快照实物独立复算**：SSH只读/tmp/s103_snapshot.tar.gz，实测81671731B、SHA=`52b0d3ea17d3b4524c30d5a78387f8a4190584c56d2e0e49539a10570417afbe`，tar普通文件1178，内含清单SHA匹配。guest当前源25/25匹配、文件1178、host/tests stray0。run_id日志为20261025-151907；三套CMakeCache分别确认Werror=ON、ASan address+undefined、TSan thread参数；本轮未重跑sanitizer。
- **mutation独立锚点/日志核查**：本轮 `python -B -X utf8 docs/evidence/stage-01/S1-03/mutation-batch.py --check --src .` exit0、points48/bad0；AST/literal_eval再次确认48个old均恰好1次，逐个看new对应分支。48段configure/build均0；ctest8的45段、ctest0的3段，无build失败混算CAUGHT。日志181722B、1453行、SHA=`e49a4b779be22e4ec550bfe0449d1eca8b7657a11e4119a9e43a9a895189a362`；7个SOURCE_FINGERPRINT与当前源一致。逐点对应见下表，不能从捕获率推导整个状态机无遗漏。
- **测试质量/残留**：三处C分页helper分别test_sim_protocol.c:271、test_sim_journal.c:179、test_device_port.c:932使用LOCKER_TEST_PAGE_LIMIT=64且有越界失败；codec校验没有被冒充READY状态机测试，后者test_sim_protocol.c:1163单独覆盖。daemon prepare:230的Python while True仅受单次响应/CTest总超时限制，不在“三处C循环”结论范围。四个授权目录扫描无.db/.sock/.pyc/.o/.a/.tmp/CMakeCache.txt；未清理正式测试/证据/诊断日志。真实UART/GPIO/MCU掉电、CRC认证在当前报告仍NOT_RUN。

### 25项指纹复算

| 文件 | SHA前12位 | 字节 | 物理行 |
|---|---|---:|---:|
| `CMakeLists.txt` | `d8f1850b3979` | 4090 | 97 |
| `host/device/CMakeLists.txt` | `7bb766cd517a` | 797 | 31 |
| `host/device/README.md` | `04acbe1fd83f` | 9288 | 98 |
| `host/device/include/locker/device/codec.h` | `390af779943e` | 7048 | 152 |
| `host/device/include/locker/device/transport.h` | `4a51972fed87` | 2553 | 60 |
| `host/device/include/locker/device/wire.h` | `6bb4ea57b45b` | 4303 | 108 |
| `host/device/src/codec.c` | `b8bb1650cb3e` | 48259 | 1266 |
| `host/device/src/port.c` | `764c080a0f29` | 39303 | 996 |
| `host/device/src/transport.c` | `9849c989e46b` | 6398 | 203 |
| `host/device/src/wire.c` | `b951abb050f6` | 7914 | 242 |
| `host/simulator/CMakeLists.txt` | `38e323558eda` | 1520 | 45 |
| `host/simulator/README.md` | `a254096e8e89` | 6568 | 67 |
| `host/simulator/include/locker/simulator/sim.h` | `212608674017` | 5604 | 119 |
| `host/simulator/src/sim.c` | `4180e2674e95` | 98720 | 2628 |
| `host/simulator/src/sim_main.c` | `2c205495648d` | 27930 | 827 |
| `tests/contract/device/CMakeLists.txt` | `977630d510db` | 2180 | 44 |
| `tests/contract/device/daemon_smoke.py` | `4848411a6407` | 32364 | 654 |
| `tests/contract/device/test_device_port.c` | `e3c7be1398b2` | 80749 | 1906 |
| `tests/contract/device/test_golden_frames.c` | `421fe6fddf5d` | 14849 | 355 |
| `tests/unit/device/CMakeLists.txt` | `cb8a5a701287` | 984 | 26 |
| `tests/unit/device/device_test_support.h` | `7243fbb2e46c` | 10664 | 311 |
| `tests/unit/device/test_codec.c` | `d8a26e31ba2b` | 39632 | 777 |
| `tests/unit/device/test_sim_journal.c` | `f3ec4a1abf4a` | 41133 | 859 |
| `tests/unit/device/test_sim_protocol.c` | `cedafda772bc` | 68706 | 1418 |
| `tests/unit/device/test_wire.c` | `897352a133aa` | 13972 | 345 |

### round-1逐项整改核查（静态结论，不等于本轮全套动态通过）

| 原项 | 独立核查证据 | 判断 |
|---|---|---|
| B-1 | port.c:131三元匹配；test_device_port.c:1390外来session后正确响应仍可交付。 | 原端口反例已改；模拟器错误回显另见R2-B-2。 |
| B-2 | port.c:421仅result0更新；test_device_port.c:1405注入18B BUSY后QUERY/快照。 | 错误body污染原路径已改。 |
| B-3 | port.c:720/:850/:885绑定；:765游标归属；test_device_port.c:1774保持epoch/boot/revision后真正close/open提交旧意图，无写。 | 原迁移反例已改。 |
| B-4 | port.c:189派生UID及0x1f能力；test_device_port.c:1423四类HELLO负例。 | 原身份反例已改。 |
| B-5 | port.c:149同锁拒重复、:116回收、:180禁回绕；test_device_port.c:1384/:1550。 | 功能分支补齐；注入时间纪律R2-B-5未满足。 |
| B-6 | port.c:206锁存+shutdown、:497复核；test_device_port.c:1512 BUSY/NO_SPACE后三API拒绝、帧不增。 | 原背压反例已改。 |
| B-7 | sim.c:1373恢复、:1366年龄、:1818实例/严格进展、:1736/:1751提交后重采；test_sim_protocol.c:1163、test_sim_journal.c:732。 | 原无READY/健康及提交跨期反例确有实现/负例，非只改测试。 |
| B-8 | sim.c:1570脉冲结束、:1542失败关闭后不确定性；test_sim_protocol.c:919、test_sim_journal.c:635。 | 原永不结束已改；不证明硬截止。 |
| B-9 | sim.c:287复合主键、:1638全意图比较、:1662全局command；test_sim_protocol.c:1231。 | 原水位/参数反例已改。 |
| B-10 | sim.c:2049恢复active、:2160 UNKNOWN、:1889持久release；test_sim_journal.c:557 destroy/create/open及释放失败/成功后重启。 | 原检查点/伪物理事实已改；detail新问题R2-B-4。 |
| B-11 | sim.c:2275/:2425独立EXECUTE丢回复；C协议:1307、port:1580、daemon:256。 | 原关键故障注入缺口已补。 |
| B-12 | sim.c:863队满仅丢通知；test_sim_protocol.c:1000/:1014校验dropped0、日志34、32次pop后NOT_READY。 | 原通知/日志混同已改。 |
| B-13 | sim.c:1542持久silent_gap、:1351阻ACK、:2489不清真实FAULT；test_sim_journal.c:488/:635/:782。 | 部分通过；inject_gap豁免仍有R2-B-3。 |
| B-14 | sim_main.c:244复用parser，sim.c:2261先拒0号；daemon_smoke.py:193/:206。 | 切帧/0号副作用已改；错误关联早退R2-B-2。 |
| B-15 | m13/m32/m39有相关失败；m24/m28/m31当前可达状态等价，48项构建成功。 | 原空泛等价论证已改；统计名称R2-N-3。 |
| B-16 | manifest/stray失败exit4，末尾exit5，mutation入build/logs。 | 删除固定源码仍在，R2-B-1，不能称全部整改。 |
| N-1 | sim_main.c:776/:805轮询/控制后flush；daemon测试仅控制注入即收事件。 | 实现已改，非只调等待。 |
| N-2 | sim_main.c:314溢出/整token、:392双hex、:594丢至换行、:610拒NUL。 | 原宽松路径已改。 |
| N-3 | transport.c:140 poll+非阻塞send，sim_main.c:100有界写，connected原子，close串行；test_device_port.c:1686/:1747。 | 原无限写等待已改；未覆盖交错不推定无race。 |
| N-4 | sim.c:187溢出、:579/:656/:1237 BLOB宽度及SQL读错；test_sim_journal.c:675损坏负例。 | 核心读取已补；诊断计数:739/:2623仍以0表示读错，不能称所有step经sim_sql_ok。 |
| N-5 | close/destroy/sim_destroy签名部分已改；handoff:202仍保证free后可调用，:196称借用sink表而实现复制。 | 未整体闭环，R2-B-6。 |
| N-6 | summary:40依赖仅S1-01；25项行/字节和合计实算吻合。 | 本卡数字同步；模块run_id另见R2-N-1，不重新裁定前卡。 |
| N-7 | tests/contract/device/CMakeLists.txt:43缺Python FATAL_ERROR。 | 不再静默少注册daemon。 |
| N-8 | device_test_support.h:192 pid+counter；固定wal_cleanup unlink已无。 | 原无关残留已移除；保留正式fixture。 |
| N-9 | sim_main.c:736、daemon log:1含backend标记。 | 启动实现已改；handoff:176文字仍旧。 |

### A～F语义裁定

| 项 | 独立判断 | 依据及文档 |
|---|---|---|
| A message_type含0x80 | 本地表示自洽；原生DTO仍需共享裁定，R2-N-4。 | UART:13、生成头:181；device README:61/summary:287已说明。 |
| B 类型/session不符继续扫描 | 接受匹配行为。代码是for自然继续，无字面continue；不消费不匹配条目。 | port.c:135/:139，UART:23；pending_add:160禁同号在途复用。summary:288的“陈旧条目复用了同一号”不能作为正常设计前提。 |
| C 重开UNKNOWN | 接受。constructor全新场景确定；已有日志boot非0时UNKNOWN。 | sim.c:1998/:2160；stage-00-architecture.md:90“UNKNOWN不能替换成false”；sim README:56、summary:289已说明。 |
| D 带内/静默gap拆分 | silent_gap持久化有价值；带内标记豁免已知缺口页gap没有规格支持，R2-B-3。 | UART:370/:50；handoff:430虽有解释，功能卡文档不能产生契约豁免。 |
| E 通知队满不锁存连续性 | 接受；日志已提交，满的是发送通知队列。 | UART:356、sim.c:863、sim README:57；handoff:221仍相反，R2-B-6。 |
| F 语法/引擎token | 接受私有协议分类，非新增公共wire_result。 | sim_main.c:256/:295/:449、sim README:43；m45被真实daemon invalid类断言捕获。无共享token硬编码规格，不发明额外门禁。 |

### mutation逐点非空洞性核查

每个old均在被测源恰好1次，new确实改动所述分支；以下回到原始失败行并核对正式断言。捕获仅证明该分支/错误码可观测，不等于证明整个状态机。m9列直接message_type断言；m10/11/12/13部分主要区分入口错误码，不能夸大成MCU安全验证。

| 点位 | 当前锚点 | 结果 | 相关失败断言/观测 | mutation.log:段首行 |
|---|---|---|---|---|
| `m1_crc_poly` | `host/device/src/wire.c:26` | CAUGHT | `tests/unit/device/test_wire.c:16: expected 10673, got 0` | 60 |
| `m2_magic` | `host/device/src/wire.c:195` | CAUGHT | `tests/unit/device/test_wire.c:205: expected 1, got 0` | 114 |
| `m3_oversize_len` | `host/device/src/wire.c:204` | CAUGHT | `tests/unit/device/test_wire.c:253: stream.stats.length_errors > 0u` | 130 |
| `m4_crc_mismatch` | `host/device/src/wire.c:218` | CAUGHT | `tests/unit/device/test_wire.c:226: expected 0, got 1` | 140 |
| `m5_reqno_zero` | `host/device/src/wire.c:97` | CAUGHT | `tests/unit/device/test_wire.c:77: expected status 3, got 0` | 160 |
| `m6_event_reqno` | `host/device/src/wire.c:94` | CAUGHT | `tests/unit/device/test_wire.c:80: expected status 3, got 0` | 168 |
| `m7_pending_type` | `host/device/src/port.c:138` | CAUGHT | `tests/contract/device/test_device_port.c:1870: expected 2, got 3` | 176 |
| `m8_event_session` | `host/device/src/port.c:366` | CAUGHT | `tests/contract/device/test_device_port.c:1290: expected 0, got 1` | 185 |
| `m9_reply_flag` | `host/device/src/port.c:433` | CAUGHT | `tests/contract/device/test_device_port.c:1876 expected 130, got 2` | 193 |
| `m10_simulated_gate` | `host/device/src/port.c:616` | CAUGHT | `tests/contract/device/test_device_port.c:858: expected status 5, got 8` | 310 |
| `m11_slot_id` | `host/device/src/port.c:707` | CAUGHT | `tests/contract/device/test_device_port.c:903: expected status 3, got 6` | 318 |
| `m12_fetch_limit` | `host/device/src/port.c:788` | CAUGHT | `tests/contract/device/test_device_port.c:916: expected status 3, got 6` | 326 |
| `m13_anchor_reqno` | `host/device/src/port.c:928` | CAUGHT | `tests/contract/device/test_device_port.c:833: expected status 3, got 5` | 335 |
| `m14_cursor_halfzero` | `host/device/src/codec.c:209` | CAUGHT | `tests/unit/device/test_codec.c:268: expected 10, got 0` | 345 |
| `m15_event_type_range` | `host/device/src/codec.c:294` | CAUGHT | `tests/unit/device/test_codec.c:592: expected 10, got 0` | 357 |
| `m16_card_type` | `host/device/src/codec.c:312` | CAUGHT | `tests/unit/device/test_codec.c:619: expected 10, got 0` | 366 |
| `m17_reserved` | `host/device/src/codec.c:324` | CAUGHT | `tests/unit/device/test_codec.c:630: expected 10, got 0` | 374 |
| `m18_supply_unknown` | `host/device/src/codec.c:327` | CAUGHT | `tests/unit/device/test_codec.c:638: expected 10, got 0` | 383 |
| `m19_decode_version` | `host/device/src/codec.c:1169` | CAUGHT | `tests/unit/device/test_codec.c:378: expected 1, got 0` | 391 |
| `m20_failed_len` | `host/device/src/codec.c:1191` | CAUGHT | `tests/unit/device/test_codec.c:556: expected 10, got 0` | 399 |
| `m21_fetch_len` | `host/device/src/codec.c:1221` | CAUGHT | `tests/unit/device/test_codec.c:470: expected 10, got 0` | 407 |
| `m22_queue_full` | `host/simulator/src/sim.c:863` | CAUGHT | `tests/unit/device/test_sim_protocol.c:1014: expected status 5, got 0` | 415 |
| `m23_queue_no_session` | `host/simulator/src/sim.c:859` | CAUGHT | `tests/unit/device/test_sim_journal.c:332: expected 5, got 0` | 423 |
| `m24_hello_version` | `host/simulator/src/sim.c:1113` | NOT_CAUGHT | `无失败；等价分析见下` | 431 |
| `m25_has_more` | `host/simulator/src/sim.c:1275` | CAUGHT | `tests/unit/device/test_sim_protocol.c:281: expected 0, got 11` | 437 |
| `m26_fetch_after_bound` | `host/simulator/src/sim.c:1296` | CAUGHT | `tests/unit/device/test_sim_protocol.c:473: expected 10, got 0` | 455 |
| `m27_ack_regression` | `host/simulator/src/sim.c:1342` | CAUGHT | `tests/unit/device/test_sim_protocol.c:556: expected 10, got 0` | 463 |
| `m28_ack_upper` | `host/simulator/src/sim.c:1345` | NOT_CAUGHT | `无失败；等价分析见下` | 473 |
| `m29_resume_mode` | `host/simulator/src/sim.c:1407` | CAUGHT | `tests/unit/device/test_sim_protocol.c:647: expected 2, got 1` | 479 |
| `m30_execute_horizon` | `host/simulator/src/sim.c:1674` | CAUGHT | `tests/unit/device/test_sim_protocol.c:699: expected 4, got 0` | 488 |
| `m31_execute_replay` | `host/simulator/src/sim.c:1682` | NOT_CAUGHT | `无失败；等价分析见下` | 512 |
| `m32_execute_journal` | `host/simulator/src/sim.c:1685` | CAUGHT | `tests/unit/device/test_sim_journal.c:534: expected 8, got 3` | 518 |
| `m33_execute_door` | `host/simulator/src/sim.c:1691` | CAUGHT | `tests/unit/device/test_sim_protocol.c:727: expected 4, got 0` | 532 |
| `m34_borrow_item` | `host/simulator/src/sim.c:1693` | CAUGHT | `tests/unit/device/test_sim_protocol.c:717: expected 4, got 0` | 552 |
| `m35_health_timeout` | `host/simulator/src/sim.c:1369` | CAUGHT | `tests/unit/device/test_sim_protocol.c:1216: expected 3, got 0` | 575 |
| `m36_pulse_flag` | `host/simulator/src/sim.c:1766` | CAUGHT | `tests/unit/device/test_sim_journal.c:576: expected 3, got 2` | 586 |
| `m37_release_pulse` | `host/simulator/src/sim.c:1873` | CAUGHT | `tests/unit/device/test_sim_protocol.c:948: expected 4, got 0` | 612 |
| `m38_release_idempotent` | `host/simulator/src/sim.c:1880` | CAUGHT | `tests/unit/device/test_sim_protocol.c:878: expected 0, got 4` | 629 |
| `m39_dispatch_decode` | `host/simulator/src/sim.c:1926` | CAUGHT | `tests/unit/device/test_sim_protocol.c:1385: expected 10, got 0` | 642 |
| `m40_dispatch_session` | `host/simulator/src/sim.c:1932` | CAUGHT | `tests/unit/device/test_sim_journal.c:293: expected 2, got 0` | 654 |
| `m41_continuity_flag` | `host/simulator/src/sim.c:957` | CAUGHT | `tests/unit/device/test_sim_journal.c:416: expected 4, got 0` | 667 |
| `m42_inject_latch` | `host/simulator/src/sim.c:2474` | CAUGHT | `tests/unit/device/test_sim_journal.c:414: !locker_sim_continuity_ok(fixture.sim)` | 675 |
| `m43_boot_increment` | `host/simulator/src/sim.c:2171` | CAUGHT | `tests/unit/device/test_sim_journal.c:239: expected 1, got 0` | 689 |
| `m44_socket_mode` | `host/simulator/src/sim_main.c:86` | CAUGHT | `uart socket is mode 0600` | 1341 |
| `m45_status_token` | `host/simulator/src/sim_main.c:259` | CAUGHT | `card uid length must be 0/4/7/10` | 1349 |
| `m46_drop_link` | `host/simulator/src/sim_main.c:488` | CAUGHT | `drop_link closes the accepted uart socket` | 1357 |
| `m47_frame_resync` | `host/simulator/src/sim_main.c:213` | CAUGHT | `reply echoes request_no 1` | 1364 |
| `m48_event_flush` | `host/simulator/src/sim_main.c:184` | CAUGHT | `control produces two immediate asynchronous events` | 1394 |

三个NOT_CAUGHT独立等价分析，只限当前实现及正常已建立的独立日志，不包括外部任意改库或未来回收策略：

- **m24_hello_version：当前调用图等价。** codec.c:836先拒requested_version!=1，sim_dispatch:1926即返回，无法到sim_handle_hello:1113；删第二处不改变线上结果。test_sim_protocol.c:373有正式版本负例，不是完全无版本测试。
- **m28_ack_upper：当前可达状态等价。** 正常ack≤last；HELLO置fetched=ack（sim.c:1136），FETCH只从page.next_after推进（:1319），next_after来自已提交记录（:1269），所以fetched≤last。删through>last早退，这种输入仍through>fetched，在:1351返回同一个CURSOR_GAP，不会持久写。不能扩大到外部删改sim_events/sim_meta的损坏库。
- **m31_execute_replay：当前可达状态等价。** action0在codec.c:910先拒；W>0来自本session最大已存命令（sim.c:1089）或成功接纳事务后推进（:1728）。当前不回收命令行，等于W的请求先走:1638 existing分支，回原状态或REPLAY_BLOCKED，到不了:1682；UINT64_MAX同理仍有行。未来加入回收后必须重做证明，不能删除水位防线。

## 环境与 NOT_RUN

### 实际执行

| 命令/操作 | 本轮输出/结果 |
|---|---|
| `git status --short` | exit128，fatal: not a git repository。 |
| 上文四个 `python -B scripts/check_*.py` | 各exit0/PASS；读取stdout，未覆盖保留JSON。 |
| `python -B -X utf8 docs/evidence/stage-01/S1-03/mutation-batch.py --check --src .` | exit0，48 CHECK_OK，CHECK_SUMMARY points=48 bad=0；CHECK_ONLY在prepare/full/写日志之前退出。首次包装子进程输出因Windows编码解码异常丢失，已用子进程-X utf8重跑取得完整输出，不采信丢失stdout。 |
| Windows stdin Python/hashlib/json/binascii/AST | 25项指纹、清单、黄金帧、CRC、长度、8项规格指纹、日志数字及mutation全部完成复算；未保存临时脚本。 |
| `wsl.exe --exec uname -a` | exit0，Linux yuanangyu 6.18.33.2-microsoft-standard-WSL2 x86_64；stderr为localhost代理提示。仅探测环境，未用WSL作构建/验收证据。 |
| `ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=yes locker-vm 'uname -a'` | exit0，Linux ubuntu 7.3.0-rc5 x86_64；本次SSH实际可用。 |
| SSH stdin `python3 -B -` | 只读manifest、源树、tar、CMakeCache；25/25匹配，1178文件，stray0，快照SHA/字节均吻合。tar用extractfile读字节，没有解包到磁盘。 |
| guest既有 `test_wire`、`test_codec`、`test_golden_frames` | 各exit0/PASS，128/179/168 checks；无本轮重建。golden临时落盘的越界另述。 |
| guest既有daemon日志SQLite只读查询 | URI mode=ro&immutable=1，不创建WAL/SHM。`.../daemon-smoke/xpz6e_oq7/j`的type13为seq3/detail1、seq7/detail2；`.../daemon-smoke/device.db`的type14为seq4/detail3。 |

可复核已存在的R2-B-4数据（Linux Bash，仅读取历史库，不重新运行daemon）：

```bash
ssh locker-vm 'python3 -B -' <<'PY'
from pathlib import Path
import sqlite3
p = Path.home() / 'locker-workspace/logs/S1-03/20261025-151907/daemon-smoke/xpz6e_oq7/j'
with sqlite3.connect(p.as_uri() + '?mode=ro&immutable=1', uri=True) as db:
    print(db.execute('SELECT event_seq,event_type,detail FROM sim_events WHERE event_type=13').fetchall())
PY
```

实际输出两条：`('00000000000000000003',13,'00000000000000000001')`、`('00000000000000000007',13,'00000000000000000002')`。它们不是MCU/littlefs证据。

### 未执行

- **Linux CMake configure/build：NOT_RUN。** guest可连，但未创建构建目录/重编译；不能借审计运行会删除固定src的复验脚本。
- **完整CTest、test_sim_journal/test_sim_protocol/test_device_port：NOT_RUN。** 会创建DB/socket/测试产物，故仅静态核查及复算保留日志；既有19/19不冒充本轮全跑。
- **ASan/UBSan、TSan：本轮NOT_RUN。** 阅读了三套配置/构建/CTest记录和当前Cache参数；历史各19/19、ASAN_REPORTS0、TSAN_HITS0不能覆盖未执行反例/交错。
- **mutation full：本轮NOT_RUN。** 仅--check和内存中的AST/日志复算；full会复制/修改沙箱及重写mutation.log，越过本轮写入边界。45/48是保留日志独立复算结果。
- **daemon启动、控制注入、新增协议动态反例：本轮NOT_RUN。** 未重新起daemon或建socket/DB；249/0来自保留日志及正式测试源码核对，不是本轮实跑249项。
- **真实UART/DMA/电气/时序、STM32 HAL/ARM交叉编译/Keil、GPIO脉冲硬截止、MCU/littlefs/RK介质掉电：NOT_RUN。** 模拟150ms转换/计数无硬件保证。CRC认证/抗篡改NOT_RUN，且规格明确CRC不承担认证；profile保持UNVERIFIED。

### 审计自身边界与稳定性

未修改实现、正式测试、契约、生成器、stage-plan、README、summary/handoff、已有证据脚本/日志，未清理项目内容。最终持久交付仅本audit-round2.md。

**一次审计操作越界必须披露：**执行guest已有test_golden_frames前，我误判它为无落盘。该测试:319经sim_fixture_open创建临时SQLite，:343经正式fixture清理DB/WAL/SHM。因此不能声称“本轮从未写过报告外任何文件”，尽管没有改正式源码/文档/证据。发现后停止其他可能落盘测试；只读核查`/tmp/locker_s103_golden_frames_*`为空，没有再执行清理命令。wire/codec不调用fixture打开。这是审计自身失误，不转嫁成产品阻塞项。

审计期间还观察到handoff.md SHA从`dfa296f53c8c40d714c04f6a6b6d1000184d5833a84c7cc73a22892f9444b179`变为`d6e8ce8543744ffc9488a5bbff22112da02b8335fe6fc57f153d0a89accaf78e`（443行、38624B），不是本审计命令写入。已重新读取最新文件，handoff行号按后者；25项源及其余已采样正式文件未同时变化，不能把外部文档变更推定为实现修复。

本卡旧WSL专用脚本、16个wsl日志、run-id和joint-fix记录在当前目录不存在；当前summary/handoff不使用WSL2证据。audit.md关于WSL的内容是用户要求原样保留的round-1历史审计，未作为现存round-2证据。打包helper的WSL路径是工具依赖，不是WSL验收PASS声明。

机器索引仍S1-03 IN_PROGRESS、report=null、active_task/recommended_next=S1-03；S1-01/S1-02 DONE，阶段1 gate NOT_RUN。本报告不改这些状态。
