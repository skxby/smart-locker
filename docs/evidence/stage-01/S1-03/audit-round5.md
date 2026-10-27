# S1-03 round-5 独立审计
结论：FAIL
阻塞项：1 项；非阻塞项：3 项

日期：2026-10-25（Asia/Shanghai）。工作副本：`%REPO%`。契约：`0.2.1 / REVIEW_DRAFT`。

FAIL 来自本轮实际复现的 R5-B-1：连接截止不能打断阻塞的 `connect()`。这不否定已有的正常场景测试结果，也不是因为 Windows、未重跑整套 Linux 测试或缺少硬件而判 FAIL。R4-B-1 的 Python 执行入口已经改正，不能继续把它列为未修。

本轮仅新增本报告；不修源码、测试、契约、生成物、README、stage-plan、summary/handoff 或既有证据，不置 DONE，不跨卡，不 commit/push。审计过程中建立的 guest 临时验证目录全部在 `~/locker-workspace/build/S1-03/`，验证后已删除；没有修改活跃源、批次源或 canonical 日志。动态编译的 transport 探针使用匿名 memfd，不留探针源码/库文件。

以下严格区分：**本轮实跑**、**静态核实**、**既有日志独立复算**。历史报告中的“本轮”不移植成本次执行记录。用户消息部分乱码，审计以可识别的路径/数字、当前机器真源和仓库原文为准。

## 阻塞项

### R5-B-1 / P2：连接队列满时，5000 ms 连接截止失效

**位置：** `host/device/src/transport.c:76`、`:83`、`:108-120`；调用入口 `host/device/src/port.c:617-620`；现有有界传输测试 `tests/contract/device/test_device_port.c:1761-1792`。

**期望：** 模块声明的连接时限覆盖连接系统调用本身。对仍监听但不接受连接、队列已满的模拟 socket，`locker_device_transport_connect(..., 5000)` 应在截止附近失败并返回，不能无限阻塞 `locker_device_open()`。

**实际：** socket 以阻塞模式创建，`:83` 直接 `connect()`；只有调用返回后才检查截止。guest 上用指纹匹配的当前 `transport.c` 编译并调用真实 C 函数：监听 `listen(0)`，先用一个连接填满队列，再在子进程调用 5000 ms 连接。5400 ms 后仍没有返回；父进程 `accept()` 释放队列后，函数才返回 `status=0`，实测耗时 **5406 ms**。即使已经越过截止，成功分支 `:115-116` 仍直接返回 OK。未接受连接时没有代码路径能执行截止检查。

**影响：** 服务启动/重连可被一个不再接收连接的模拟端点卡住；模块现有的“最多5000 ms”约定不成立。现有有界写测试验证的是 `send/poll`，没有覆盖连接队列满的路径。本项不声称发生了重复开锁。

**最小复现（本轮已执行；可执行版本见末节）：**

1. 在 guest 的独立临时工作目录创建 AF_UNIX/SOCK_STREAM 监听器，调用 `listen(0)`，保持不 accept。
2. 建立一个填充连接，使队列没有空位。
3. 用当前 `transport.c` 的真实 `locker_device_transport_connect`，对子进程传入该路径及 `timeout_ms=5000`。
4. 父进程等待5400 ms，观察调用尚未返回；随后 accept 填充连接。
5. 观察子进程返回 OK、耗时5406 ms；关闭所有 fd 并清理该临时目录。

**依据：** `host/device/include/locker/device/transport.h:36-38` 原文：“Retries for at most timeout_ms of real time”；`host/device/README.md:75`：“connect（10 ms 重试，总超时 5000 ms）”；`docs/evidence/stage-01/S1-03/handoff.md:216`：“连接超时 … 5000，每 10 ms 重试”。这是已交付模块自己的时限承诺，不是本次新设 UART 时限。`docs/architecture/internal-interfaces.md:28` 将 open 定义为设备生命周期入口。建议以非阻塞连接及统一实际单调截止处理队列满/重试，补实际连接截止回归；本轮未修改实现。

## 非阻塞项

### R5-N-1 / P3：原生 reply.message_type 方向位仍待契约负责人裁定

**位置：** `contracts/include/locker_contracts.h:181`、`host/device/src/port.c:429`、`host/device/README.md:61`、`docs/evidence/stage-01/S1-03/summary.md:289`。

**期望：** Core 与未来 MCU sink 对原生 DTO 字段的解释由契约负责人统一。

**实际：** 实现写 `request_type | 0x80`，线上行为及当前正式断言自洽；生成头只有 `uint8_t message_type`，没有解释原生字段是否带方向位。README/summary 已明确登记待裁定，不能将这个登记当作裁定已完成。本项延续 R2-N-4 / R3-N-1 / R4-N-1，不升级为已证实的线上协议缺陷。

**最小复核步骤：**

1. 读生成头181，核对字段声明没有方向位语义。
2. 对照 `port.c:429` 与 `test_device_port.c:1951` 的 `0x82` 预期。
3. 读 README61 的“待契约负责人裁定”。
4. 由契约负责人统一真源、生成物及调用方解释；功能卡不手改 contracts。

**依据：** `docs/architecture/uart-v1.md:13`：“响应为请求 type OR 0x80”，规定的是线路；`docs/architecture/module-ownership.md:11`：“由架构负责人统一修改真源、重新生成并验证”。**原生字段语义规格未覆盖，需契约负责人裁定。**

### R5-N-2 / P2：活跃树指纹只覆盖文件名和大小，不能证明内容未变

**位置：** `docs/evidence/stage-01/S1-03/evidence-run-vm.sh:21-28`、`:200-206`、`:221-222`、`:257-261`；`summary.md:118`、`:133`；`handoff.md:307`、`:498`。

**期望：** 用于支持“活跃源码树未改变”的前后指纹应覆盖文件内容；计数的 before 值应在运行前采样。若只检查目录清单/大小，报告应准确限定其含义。

**实际：** `find -printf '%P %s\n' | sort | sha256sum` 哈希的是路径与长度，不读源码内容。本轮提取当前脚本中的原函数，在独立临时目录将 `x.c` 从 `alpha` 等长改成 `bravo`，函数前后都得 `c99415fc679682e5e2919f0a54c8d6891531fcced8265d2ed5416f4b339bde31`，内容 SHA 则不同。门禁会把该等长改写判为 unchanged。另 `run-summary` 的两个计数均在测试后执行 `find`；`extract.log` 的 `live_tree_files_before` 也在解包后采样，不能称为解包前保存的计数。

**最小复现（本轮已执行）：**

1. 从当前 run-vm 脚本只提取 `live_tree_fp()` 函数，不执行验收脚本主体。
2. 在工作区 build 下新建临时目录和5字节文件 `x.c=alpha`，以该目录为 LIVE 计算指纹。
3. 将这个临时文件等长写为 `bravo`，再次调用同一原函数。
4. 比较两次函数结果与两次内容 SHA；前者相等、后者不同，清理临时目录。

**依据：** `docs/development/vm-ubuntu-workflow.md:16`：“不能覆盖正在使用的源码”；`docs/architecture/module-ownership.md:71` 要求实际文件指纹和运行证据。`handoff.md:498` 自己明确披露采用路径/大小算法，因此不是隐瞒算法。脚本确实已不删除活跃源、解包目标也确实是独立 batch，**没有证据表明历史活跃源真的被改过**，故本项列为证据精度非阻塞项；不能用它推翻既有字节相同的测试日志。R4-N-4 的这一子项只能部分关闭。

### R5-N-3 / P3：交接中的私有控制入口说明仍有旧表述

**位置：** `docs/evidence/stage-01/S1-03/handoff.md:178`、`:183-185`；对照 `host/simulator/src/sim_main.c:397`、`:516-525`、`:736-737`，`host/simulator/src/sim.c:2352-2356`、`:2389-2393`；正确说明在 `host/simulator/README.md:18-20`、`:30`。

**期望：** 当前交接示例与真正控制入口一致，区分私有 C 引擎 API 和 socket 文本协议。

**实际：** handoff178 仍写启动输出 `ready uart=...`，实际及原始日志均为 `ready device backend: simulator uart=...`；handoff185 把 `card HEX` 的长度写为0/4/7/10，但文本入口拒绝空串，0长度只由 C 引擎 API 支持。本轮启动匹配源码的既有 daemon：`card \n` → `error bad_argument`，`card 01020304\n` → `ok`，`quit\n` → `ok`，退出0、stderr空。183/184 的 unknown 输入还会产生 HEALTH_CHANGED，不能全部概括成相应门/物品确定事件。

**最小复现（启动标识和 card 已实跑；unknown 事件按源码静态核实）：**

1. 在全新临时目录启动现有 `locker-device-sim --uart U --control C --journal J`，读取 stdout 首行。
2. 连接私有控制 socket，依次发送 `card \n`、`card 01020304\n`，比较返回值。
3. 对照 handoff178/185 与 simulator README；区分 C API 的空读卡和文本入口。
4. 发送 quit、等待退出并清理该临时目录；unknown 的事件类型对照 sim.c 对应三元分支。

**依据：** `docs/stages/stage-01-task-cards.md:19`：“启动日志、配置样例和测试报告必须明确 device backend: simulator”；`:65` 的 DONE 证据包括“控制入口说明”；`AGENTS.md:35` 要求同步相关当前文档。生产标识及实际解析行为正确，故为交接文档问题，不否定0600或字节协议验收。R1-N-9 的生产标识反例已解除，旧交接样例残余在此明确保留。

## 已核实通过项

**审计边界：** 已读取执行入口、阶段0架构/状态机、S1-03公共规则及任务卡、UART全文、机器枚举/布局/生成DTO、内部接口/所有权/追踪归属、25个受管文件及其正式测试相关断言；对照前四轮审计和本次 summary/handoff/证据脚本。本轮是既有单卡实现审计，没有提出大方案或下载依赖，不触发新项目的 GitHub 方案调研。文件/日志哈希检查覆盖清单全部条目；代码判断不等于穷尽所有线程交错或所有数据库损坏形式。

### 状态、指纹、复用与范围

- **静态核实：** `stage-plan.json` 仍 active_task/recommended_next=S1-03，S1-03=IN_PROGRESS/report=null，depends_on仅S1-01；S1-01/S1-02=DONE；阶段1 gate=NOT_RUN。README、stages/README 与此一致。未把审计 FAIL 或本轮探针结果写成卡状态变化。
- **本轮实跑：** `git status --short` 输出 `fatal: not a git repository`，原生 `$LASTEXITCODE=128`（工具启动的 PowerShell 进程显示exit1）。没有 Git 基线，不能声称知道历史全部改动。
- **独立复算：** summary16-23的8项完整SHA/字节数全部匹配；25项受管文件的完整SHA/字节数逐一匹配 source-manifest 及用户给定指纹，总计 **584113 B**。清单为25条数据+3条注释、2793 B，SHA=`77fc9b5abbc9dba4b74cef5080d4f743608999739c4fc672242468fe57c2cecc`，与 `vm-s103-manifest.sha256` 逐字节相同。
- **本轮 guest 只读核验：** `ssh -o BatchMode=yes -o ConnectTimeout=5 locker-vm` 实际可用，guest `uname` 为 Ubuntu/x86_64、kernel7.3.0-rc5。canonical batch `~/locker-workspace/S1-03/batches/20261025-201026/src` 的25项SHA/大小全部匹配；当前 `evidence-run-vm.sh`、`mutation-batch.py` 与该批次副本也匹配，分别为 `983a8ad33c0c9f440fc8e1d1a11699bfe1580e597f14e4066ce801cfd95d415a`、`55479415504c491225b4a0cb877ab17c14cdcb9a1e3209c09143240e3e3ec659`。没有沿用历史SSH失败当成本轮事实。
- **独立复算前序清单：** S1-01的host/common和tests/unit/common共21项匹配；S1-02清单31项直接匹配，根CMake移除本卡88/89/95/96四条add_subdirectory后也匹配其旧SHA（共32项）。这支持当前复用边界，不能由此证明所有未列入历史清单的文件从未被改动。
- **静态核实：** simulator通过局部CMake复用 `locker_common`、`locker_device`、`locker_storage_thirdparty`；实际vendored SQLite头标3.54.1、cJSON头标1.7.19，依赖锁/许可为Public Domain、MIT。没有新增第三方、HTTP服务、密码学实现、HAL、GPIO或真实tty代码；独立日志用sim_*表，未调用Repository或共享业务连接。配置者仍须提供独立路径，不能把“只写sim_*”说成允许传入营业DB路径。
- **范围/残留：** 本轮开始S1-03证据目录34个文件，报告加入后35个；本目录无旧WSL runner/wsl日志/wsl-run-id/joint-fix-round1。没有把历史WSL结果纳入本卡当前证据。四个模块/测试目录文件数为9/5/6/4，加根CMake共25，未发现 `.o/.a/.so/.pyc/.db/.sock/.tmp/.bak/CMakeCache.txt` 残留。正式证据脚本、mutation日志、fixture和历史审计全部保留。

| 受管文件 | SHA前12位 | 字节 | 物理行 |
|---|---|---:|---:|
| CMakeLists.txt | d8f1850b3979 | 4090 | 97 |
| host/device/CMakeLists.txt | 7bb766cd517a | 797 | 31 |
| host/device/README.md | f7b198c02a2b | 9642 | 98 |
| host/device/include/locker/device/codec.h | 390af779943e | 7048 | 152 |
| host/device/include/locker/device/transport.h | 4a51972fed87 | 2553 | 60 |
| host/device/include/locker/device/wire.h | 6bb4ea57b45b | 4303 | 108 |
| host/device/src/codec.c | b8bb1650cb3e | 48259 | 1266 |
| host/device/src/port.c | 8110920a24eb | 39388 | 992 |
| host/device/src/transport.c | 9849c989e46b | 6398 | 203 |
| host/device/src/wire.c | 8c532948cca1 | 7980 | 243 |
| host/simulator/CMakeLists.txt | 38e323558eda | 1520 | 45 |
| host/simulator/README.md | 3810b32f1d6e | 6620 | 67 |
| host/simulator/include/locker/simulator/sim.h | 212608674017 | 5604 | 119 |
| host/simulator/src/sim.c | 3d81fd2b53f4 | 99545 | 2642 |
| host/simulator/src/sim_main.c | 2c205495648d | 27930 | 827 |
| tests/contract/device/CMakeLists.txt | 977630d510db | 2180 | 44 |
| tests/contract/device/daemon_smoke.py | 013431f1a4af | 32731 | 659 |
| tests/contract/device/test_device_port.c | b0d8066c95e4 | 84134 | 1982 |
| tests/contract/device/test_golden_frames.c | 421fe6fddf5d | 14849 | 355 |
| tests/unit/device/CMakeLists.txt | cb8a5a701287 | 984 | 26 |
| tests/unit/device/device_test_support.h | 7243fbb2e46c | 10664 | 311 |
| tests/unit/device/test_codec.c | d8a26e31ba2b | 39632 | 777 |
| tests/unit/device/test_sim_journal.c | cba82f070087 | 42809 | 883 |
| tests/unit/device/test_sim_protocol.c | a8b2227ecfbb | 69822 | 1436 |
| tests/unit/device/test_wire.c | 709d109fada7 | 14631 | 353 |

### 本次要求的协议/持久化/测试核对

1. **黄金帧——独立复算。** JSON的4个frame_hex与 `test_golden_frames.c:17/:21/:24/:27` 的C拼接常量逐字一致。HELLO61B/payload49，SNAPSHOT28B/payload16；`binascii.crc_hqx(frame[2:-2],0xffff)` 分别为0xbdd8/0xea6e，线路d8bd/6eea。crc_error仍算0xea6e而线路6eeb，应拒；oversized_header10B，声明513。`123456789`独立算0x29b1。依据UART19，wire116/217覆盖offset2到payload末，不含magic/CRC。
2. **11类长度——从JSON字段类型独立求和，并对照codec/test_codec。** HELLO49/71、SNAPSHOT16/189、FETCH49/(85+96N，N=0..4，最大469)、ACK48/34、RESUME64/27、DISARM18/27、EXECUTE99/93、QUERY48/93、HEALTH73/27、ANCHOR36/46、RELEASE80/27；event96=96，DEVICE_EVENT=128；错误响应严格18。依据UART各消息布局和`:28`，codec1189-1199及test_codec502-565有真实错误shape检查。
3. **流式解析——静态核实及既有断言。** wire158-239固定524B，半帧等待、有多帧循环，长度>512或CRC错误仅丢1字节找magic；不按对端长度分配。sim_main232-245复用同一parser，195-219逐字段重构原头。daemon206-219覆盖错误长度吞入嵌套完整HELLO及粘帧。只凭格式合法不宣称业务字段合法，字段拒绝在codec/sim层。
4. **会话/水位/不重放——静态核实。** sim1106同session+host重试保持水位，新会话撤销资格；1914先decode再验当前session；执行1640-1688完整意图/command查重和严格水位，旧session/跳号不驱动。port716/846/881不迁移旧DTO，写路径没有EXECUTE自动重发循环。依据UART360、382-390；对应protocol1244、port测试1849的保持boot/revision后close/open旧意图反例。
5. **独立SQLite与重启——静态核实。** sim266-289为sim_meta/events/commands/v2，u64固定20位十进制文本，boot递增、新boot事件seq=1，bounds按boot/seq排序，当前session水位与历史command可区分。2057恢复active，2168重开物理UNKNOWN；release1897提交后才清内存。journal557实际destroy/create/open同库，836覆盖suppressed意图不重放。依据UART35-46、388-402；不是MCU/littlefs掉电验收。
6. **ACK与脉冲独立——静态核实。** ACK_PERSISTED丢响应开关与EXECUTE丢响应、suppress_pulse分离，sim2289-2292/2439；protocol1320-1370两种suppress场景均检查一次动作后QUERY和持久计数；daemon139-313驱动真实独立进程。REQUEST/ACCEPTED/PULSE_STARTED/门状态未合并。依据任务卡63与UART345-346、386-390。
7. **提交失败——静态核实。** sim410/966失败rollback并FAULT；execute1690拒新动作；意图提交失败不抬水位/不建active，RESULT失败则保留已接纳水位和意图，不能混说成“均未接纳”。journal488五阶段分别查commands/meta/events；806以SQLite触发器制造真实写失败，off不能清FAULT；624物理输入与revision回滚。依据任务卡63及UART386。PULSE_FINISHED写失败先关模拟位再锁存UNKNOWN/缺口，仍不是硬截止证据。
8. **0600与控制边界——静态核实+原日志。** sim_main79先umask0077，86再chmod0600；daemon355-357真正stat/S_IMODE，原日志两个0600；m44使这两项变红。只有AF_UNIX两个私有入口，未创建HTTP模拟路由。当前路径配置安全隔离仍由装配负责。本轮另启动过临时daemon做N3定点复核，没有重跑完整250项验收。
9. **接口/范围/测试非空——静态核实。** 生成头217-233/289-311的14个DevicePort符号对应真实收发、回调、线程退出；不是空成功占位。四行根接入和前序清单可复算；没有Git/开工前完整测试快照，不能绝对证明历史从未删/放宽测试。当前正式测试仍有拒绝/状态/日志反例，未发现用固定假数据替代本卡端口。
10. **canonical结果——独立复算保留日志。** 17个本地vm日志与guest原件的完整SHA逐个相等。普通/ASan+UBSan/TSan日志各19条Passed、各exit0，耗时分别9.64/11.32/11.67s；报告数0，configure/build输出未检出warning:/error:。17个C测试计数97、5、53、164、133、53、80、46、865、131、90、179、130、461、507、168、369，和3531；本卡六项和1814，daemon另250/0，不能并入C总数。四个check_JSON与design-checks.log对象相等。
11. **运行身份——独立复算保留日志。** run_id=20261025-201026，snapshot_sha256=`7de889879548c816d7cdac5af94e80ca8ee4a2e7cb0f22c6f1f77bd1ea2c1d1b`、81726656B；MANIFEST_LINES/SHACHECK_OK=25、SHACHECK_FAILED=0、SRC_STRAY_ARTIFACTS=0、SRC_FILES=1152。gates有17条GATE_OK，run-summary内联这些行并有VM_RUN_VERDICT=PASS。快照压缩包本轮没有重新读取/解包，压缩包SHA是日志记录，不能称本轮重算压缩包。活跃树清单指纹前后相等，但证明范围按N2限定；历史SSH_EXIT未归档，不能以本轮SSH成功补成历史transcript。
12. **mutation——独立复算+本轮check-only。** 48个old在当前原文件各出现恰好1次，AST/literal_eval读点位，逐段核anchor和old→new；48个configure/build为0，45个ctest8、3个ctest0。所有打印的C失败位置均映射到当前正式TEST_CHECK行；daemon另计，不能把BUILD_FAILED当捕获。完整full本轮NOT_RUN。具体数字及等价边界见下节。
13. **重点A-F——静态核实。** A线上OR0x80正确，原生语义仍N1。B pending_take127-142只在三条件同时命中才消费，不匹配自然继续扫描；pending_add145-173同锁拒在途重复号，原函数无字面continue。C重开UNKNOWN不冒充CLOSED/PRESENT。D无SIM_META_SILENT_GAP/silent_gap；sim1318-1323只要continuity=false每页报gap1，符合UART370已知缺口及50锁存，不另造“带内标记豁免”。E843-861队满仅丢通知、不丢日志/不改连续性，protocol990核34日志、32次pop后NOT_READY。F status_token256与语法token区分，README有正式说明，不加公共wire result。

### round-1～round-4 逐项回查

“原反例解除”只针对该项具体反例，来自当前源码/正式断言及匹配日志，不表示本轮动态重验全部场景。路径缩写：port/wire/codec为host/device/src下文件；sim/main为host/simulator/src/sim.c、sim_main.c；protocol/journal为tests/unit/device的对应测试；port测试为tests/contract/device/test_device_port.c；evidence下路径均指S1-03。

| 历史项 | 当前核对落点 | 本轮判定 |
|---|---|---|
| B-1 响应session | port127-142/408；port测试1398-1432，外来帧后真帧仍交付 | 原反例解除 |
| B-2 错误body污染镜像 | port417-419；port测试1434-1451 | 原反例解除 |
| B-3 旧意图/guard/游标迁移 | port716/761/846/881；port测试1489、1849-1908 | 原反例解除 |
| B-4 设备身份/能力 | port185-198/410-416/605；port测试1455 | 原反例解除；不是认证 |
| B-5 在途号/回收/回绕 | port112-182/503；port测试1416/1618/1916 | 原反例解除 |
| B-6 sink背压继续写 | port202-214/493；port测试1544-1578 | 原反例解除 |
| B-7 READY/健康/提交后截止 | sim1371/1399/1703/1741/1756/1823；protocol1176；journal744 | 原反例解除，detail另核R3-B-1 |
| B-8 正常脉冲不结束 | sim1575/2310；protocol932-984；journal647 | 原反例解除；只模拟计数 |
| B-9 水位/不完整去重 | sim282/1082/1643/1670；protocol1244；journal349 | 原反例解除；不批准未来回收后删防线 |
| B-10 重启/释放检查点 | sim2057/2168/1897；journal557-620/836 | 原反例解除 |
| B-11 EXECUTE丢ACK缺口 | sim2289/2439；protocol1320；port测试1655；daemon139 | 原反例解除 |
| B-12 实时通知满误作历史丢失 | sim843-861；protocol990-1036 | 原反例解除 |
| B-13 缺口/真实FAULT被清除 | sim1318/1356/2482/2504；journal488/624/647/806 | 原反例解除 |
| B-14 daemon切帧/0号副作用 | main195-245；sim2270-2280；daemon193-219 | 原反例解除 |
| B-15 泛称等价/空洞捕获 | 48点old/new/anchor与失败位置逐段复算，见下表 | 原反例解除；不把3个生存算捕获 |
| B-16 删除旧源/吞门禁/散落HOME | run-vm9-10/30-41/114-120/242-284；mutation17-20 | 原删除/传播反例解除；指纹证明精度N2 |
| N-1 异步事件需下个UART请求 | main776/805排空；daemon449-455 | 原反例解除 |
| N-2 控制解析宽松 | main314/335/392/594；daemon164/425-443 | 原反例解除 |
| N-3 无限写/关闭互斥 | transport140-174；port965；port测试1761/1822 | 原写路径已改；连接路径另立B1，不混同 |
| N-4 SQL读错/短BLOB/u64 | sim187/574/651/1238；journal687 | 核心原反例解除；诊断COUNT非ROW仍返回0 |
| N-5 签名/销毁/UAF说明 | 生成头289-311；port949-992；handoff198/204/243；sim2014 | 原生命周期错误解除 |
| N-6 状态/计数 | stage-plan与当前入口一致；3531/1814/250实算 | 当前数字可核，不重判前卡 |
| N-7 缺Python仍少测变绿 | tests/contract/device/CMakeLists.txt43 FATAL_ERROR | 原反例解除 |
| N-8 固定无关unlink | device_test_support192-199为pid+counter，无wal_cleanup旧常量 | 原残留解除 |
| N-9 后端标记 | main736、原daemon日志1正确；handoff178仍旧 | 生产反例解除，文档残余N3 |
| R2-B-1 删除活跃源 | run-vm独立batches/RUN/src，无rm -rf；原extract目标可核 | 原删除反例解除；未夸大元数据指纹 |
| R2-B-2 早退回显错误session | sim1016/2286-2287在dispatch前读请求首16B；daemon569、protocol1402 | 原反例解除；坏FETCH早退也经过该前置路径 |
| R2-B-3 silent_gap豁免 | silent_gap全部删除；sim1318；protocol512-520预期gap1 | 原反例解除 |
| R2-B-4 release detail非0 | sim1897-1898传0；journal613-617比较20位零TEXT并CAST | 原反例解除 |
| R2-B-5 pending绕过注入时钟 | port115/171；port测试1638推进；1589保留真实时钟烟测 | 原反例解除；不表示transport OS时限也正确 |
| R2-B-6 交接销毁/号/队列/UID | handoff125、202、204/243、223与当前实现对应 | 原四处错误解除 |
| R2-N-1 README run_id陈旧 | 两README引用run-summary首行 | 原漂移解除 |
| R2-N-2 verdict只在终端 | gates17项，run-summary33真verdict；handoff385说明SSH_EXIT未归档 | 原verdict落盘问题解除 |
| R2-N-3 mutation混算断言/分母 | mutation352-375、日志48段；m43不同来源独立重数 | 当前统计明确，不能称完整分母 |
| R2-N-4 原生message_type | 生成头181未新增解释；README61仍待裁定 | 保留N1 |
| R3-B-1 COMMAND_ACCEPTED detail丢action | sim1528/1538、1769都保留action；journal790-797三条真实SQL断言 | 原反例解除；jump3/5两提交后分支均在正式回归 |
| R3-B-2 Bash执行Python | handoff362正文已为python -B；336-337为python3 -B | 原反例解除，不能继续判旧入口FAIL |
| R3-B-3 不完整输入SHA | 本次mutation完整64位SHA与用户/summary201相等 | 旧输入缺位问题不在当前基线重列 |
| R3-N-1 message_type | 同R2-N-4 | 保留N1 |
| R3-N-2 交接七处事实 | sink复制、耗尽NOT_READY、destroy失效、日志职责、live/batch、SQL检查范围/TEXT已分述 | 原主要错误已改，活跃树证明限N2 |
| R3-N-3 声称不存在的断言 | daemon569逐字回显；protocol1402；697补operation、763-765读真实flag | 原指认缺口已补；slot/kind仍先由decoder拒绝，不称handler独立覆盖 |
| R3-N-4 0xc0 encoder例外 | wire90-93拒绝；test_wire89-94核0/1请求号 | 原反例解除；本轮test_wire130checks PASS |
| R4-B-1 再生成入口未改 | handoff362真实正文python -B；本轮--check48/bad0 | 原阻塞解除；历史表引用旧错误不等于当前可执行入口 |
| R4-N-1 message_type | 同上 | 保留N1 |
| R4-N-2 daemon249/250漂移 | summary/handoff/README已为250，与原日志11/13相符 | 原计数漂移解除 |
| R4-N-3 m43分母范围误述 | mutation373-375和summary203/256明确完整汇总子集；647=210+354+2+81 | 原统计措辞已改，当前日志亦一致 |
| R4-N-4 注入时钟/continue/守卫/指纹/日志 | README80、summary290、run-vm15-19、gates/run-summary分工已改；指纹实际仅路径+大小 | 前三项及日志分工解除，指纹子项部分关闭并列N2 |

### mutation 48点独立复算

`mutation.log` 实际 **185389 B / 1418行**，完整SHA=`ecba26eb1788c9a1d6e932e38ce2cab828baa7ee7ff3e752f21c4202f50bd6e5`。逐点configure/build均0。下表a为实际打印的C断言行数，k为有完整red_test汇总的二进制数，d只合计那k份汇总的检查数，p为daemon红检查；**a/d不是统一覆盖率或完整失败比例**。

| 点位 | 当前锚点 | 结果 | a / k / d / p |
|---|---|---|---|
| m1_crc_poly | wire.c:26 | CAUGHT | 34 / 2 / 298 / 12 |
| m2_magic | wire.c:196 | CAUGHT | 5 / 2 / 298 / 3 |
| m3_oversize_len | wire.c:205 | CAUGHT | 2 / 2 / 298 / 0 |
| m4_crc_mismatch | wire.c:219 | CAUGHT | 8 / 3 / 805 / 3 |
| m5_reqno_zero | wire.c:98 | CAUGHT | 1 / 1 / 130 / 0 |
| m6_event_reqno | wire.c:95 | CAUGHT | 1 / 1 / 130 / 0 |
| m7_pending_type | port.c:134 | CAUGHT | 2 / 1 / 369 / 0 |
| m8_event_session | port.c:362 | CAUGHT | 1 / 1 / 369 / 0 |
| m9_reply_flag | port.c:429 | CAUGHT | 93 / 1 / 369 / 0 |
| m10_simulated_gate | port.c:612 | CAUGHT | 1 / 1 / 369 / 0 |
| m11_slot_id | port.c:703 | CAUGHT | 1 / 1 / 369 / 0 |
| m12_fetch_limit | port.c:784 | CAUGHT | 2 / 1 / 369 / 0 |
| m13_anchor_reqno | port.c:924 | CAUGHT | 3 / 1 / 369 / 0 |
| m14_cursor_halfzero | codec.c:209 | CAUGHT | 3 / 2 / 309 / 1 |
| m15_event_type_range | codec.c:294 | CAUGHT | 2 / 1 / 179 / 0 |
| m16_card_type | codec.c:312 | CAUGHT | 1 / 1 / 179 / 0 |
| m17_reserved | codec.c:324 | CAUGHT | 2 / 1 / 179 / 0 |
| m18_supply_unknown | codec.c:327 | CAUGHT | 1 / 1 / 179 / 0 |
| m19_decode_version | codec.c:1169 | CAUGHT | 1 / 1 / 179 / 0 |
| m20_failed_len | codec.c:1191 | CAUGHT | 1 / 1 / 179 / 0 |
| m21_fetch_len | codec.c:1221 | CAUGHT | 1 / 1 / 179 / 0 |
| m22_queue_full | sim.c:851 | CAUGHT | 1 / 1 / 507 / 0 |
| m23_queue_no_session | sim.c:847 | CAUGHT | 1 / 1 / 461 / 0 |
| m24_hello_version | sim.c:1114 | NOT_CAUGHT | 0 / 0 / 0 / 0 |
| m25_has_more | sim.c:1276 | CAUGHT | 11 / 1 / 498 / 0 |
| m26_fetch_after_bound | sim.c:1297 | CAUGHT | 1 / 1 / 507 / 0 |
| m27_ack_regression | sim.c:1347 | CAUGHT | 3 / 1 / 507 / 0 |
| m28_ack_upper | sim.c:1350 | NOT_CAUGHT | 0 / 0 / 0 / 0 |
| m29_resume_mode | sim.c:1412 | CAUGHT | 2 / 1 / 507 / 0 |
| m30_execute_horizon | sim.c:1679 | CAUGHT | 18 / 1 / 507 / 0 |
| m31_execute_replay | sim.c:1687 | NOT_CAUGHT | 0 / 0 / 0 / 0 |
| m32_execute_journal | sim.c:1690 | CAUGHT | 7 / 1 / 461 / 0 |
| m33_execute_door | sim.c:1696 | CAUGHT | 14 / 1 / 507 / 0 |
| m34_borrow_item | sim.c:1698 | CAUGHT | 16 / 2 / 876 / 0 |
| m35_health_timeout | sim.c:1374 | CAUGHT | 4 / 1 / 507 / 0 |
| m36_pulse_flag | sim.c:1771 | CAUGHT | 17 / 2 / 967 / 2 |
| m37_release_pulse | sim.c:1878 | CAUGHT | 10 / 1 / 507 / 0 |
| m38_release_idempotent | sim.c:1885 | CAUGHT | 5 / 2 / 876 / 0 |
| m39_dispatch_decode | sim.c:1934 | CAUGHT | 4 / 1 / 507 / 2 |
| m40_dispatch_session | sim.c:1940 | CAUGHT | 3 / 2 / 968 / 3 |
| m41_continuity_flag | sim.c:945 | CAUGHT | 1 / 1 / 461 / 0 |
| m42_inject_latch | sim.c:2488 | CAUGHT | 6 / 2 / 968 / 1 |
| m43_boot_increment | sim.c:2179 | CAUGHT | 647 / 3 / 1253 / 5 |
| m44_socket_mode | sim_main.c:86 | CAUGHT | 0 / 0 / 0 / 2 |
| m45_status_token | sim_main.c:259 | CAUGHT | 0 / 0 / 0 / 2 |
| m46_drop_link | sim_main.c:488 | CAUGHT | 0 / 0 / 0 / 1 |
| m47_frame_resync | sim_main.c:213 | CAUGHT | 0 / 0 / 0 / 24 |
| m48_event_flush | sim_main.c:184 | CAUGHT | 0 / 0 / 0 / 1 |

- m43失败来源独立重数：journal210、protocol354、golden2、port81，共647。完整汇总只含210/461、354/624、2/168，即566/1253；port的81条没有完整汇总，不能推定其分母。变异下protocol路径可执行624次检查，不拿正常运行507硬套；断言宏在循环里会改变动态检查数。没有完整CTest原输出，不能从缺汇总反推具体截断/异常原因。
- m44-48全部由真实daemon检查捕获；m47实际改的是请求号字节重建，不应只因名称含resync就称其独立验证CRC重同步。CRC重同步另有daemon206-215负例。
- **m24当前调用图等价：** codec836先拒requested_version≠1，sim1934失败早退，到不了sim1114第二层；删除该第二层对当前入口不可观测，不伪造handler单独覆盖。
- **m28健康可达日志状态等价：** 初始ACK0；HELLO的fetched=ACK；1324-1326只从合法页推进，保持fetched≤last。through>last仍由1356的through>fetched回同CURSOR_GAP。论证限正常引擎维护的日志，不包括外部篡改/损坏DB。
- **m31当前未回收命令明细的实现等价：** action0先由codec910拒；W>0来自会话最大行或成功插入，W行仍存在。相等请求先命中1643，精确重复回原结果，不同参数REPLAY_BLOCKED，不能到1687相等分支。W为UINT64_MAX亦不改变可观测拒绝。UART388要求回收后仍防重放；不能把当前等价性用于批准未来回收明细后删水位防线。

## 未运行与 NOT_RUN

**本轮实际命令/结果：**

| 环境与命令/动作 | 结果及证明范围 |
|---|---|
| Windows：`python -B scripts/check_contracts.py` | exit0/PASS；0.2.1、24 routes、82 ports、11请求、4向量，硬件UNVERIFIED；pycparser为PASS(parse only)，不是编译链接 |
| Windows：`python -B scripts/check_stage_plan.py` | exit0/PASS；45卡、active S1-03、done2，runtime/hardware=false |
| Windows：`python -B scripts/check_vision_contracts.py` | exit0/PASS；未验证模型推理/相机/RKNN/硬件 |
| Windows：`python -B scripts/check_feasibility.py` | exit0/PASS；设计一致性，runtime/hardware=false |
| Windows：`python -B docs/evidence/stage-01/S1-03/mutation-batch.py --check --src .` | exit0，48个CHECK_OK、points48/bad0；没有构建、没有写mutation.log |
| Windows：Python hashlib/json/AST/re/binascii只读复算 | 上述清单、8项规格、4黄金帧/CRC、全部48段统计和正式断言位置、JSON/日志、前序清单匹配 |
| guest：SSH只读查询、25项文件SHA/大小及17日志SHA | 实际成功；guest日志与本地完整SHA逐项一致 |
| guest：`bash -n` 三个正式evidence shell脚本 | 各exit0；没有执行其主体，没有重打包/刷新清单/覆盖日志 |
| guest：canonical既有 `tests/unit/device/test_wire`、`test_codec` | 各exit0/PASS，130与179 checks；是本轮运行既有二进制，不是本轮全套重编译/CTest |
| guest：transport定点动态探针 | 当前transport.c以GCC/C11及-Werror成功编译；5000ms调用在5400ms未返回，accept后OK/5406ms；见B1 |
| guest：提取原live_tree_fp函数，在独立临时文件上调用 | 等长内容改写未改变函数指纹；内容SHA改变；见N2 |
| guest：临时独立daemon，仅startup/card/quit | 实际启动标记正确；空card拒绝、4B card成功、quit成功、exit0/stderr空；不是完整250检查重跑 |

**NOT_RUN 与具体原因：**

| 项 | 本轮状态 | 原因/边界 |
|---|---|---|
| 全量Linux配置/构建/19项CTest | NOT_RUN | 本轮实施代码没有变更，25项当前输入与已留VM批次完全匹配；依VM工作流26可复用匹配证据。本轮只跑上表两项纯编解码二进制及定点探针，未覆盖原构建/Testing日志。不是因Windows不能审C而跳过 |
| 全量ASan/UBSan、TSan | NOT_RUN | 没有重编整套；原三套19/19和0报告来自匹配canonical日志。定点transport探针没有开sanitizer，不说已验证全部竞态 |
| full 48点mutation | NOT_RUN | full会复制/修改变异沙箱并重建，还会覆盖源目录中的mutation.log；本轮只读审计，改用check-only与原始48段独立复算。没有生成新CAUGHT结论 |
| 完整daemon_smoke.py 250检查 | NOT_RUN | 会创建/清理正式测试工作目录及设备DB；本轮只做全新临时daemon的定点控制复核，并复算匹配历史250/0日志 |
| 新快照打包、完整evidence-run-vm主体 | NOT_RUN | 会写清单/解包/构建/证据；审计没有重发canonical验收，只做bash -n及源码/原日志核对 |
| WSL | NOT_RUN | 用户明确本卡当前证据只使用VMware，不调用WSL、不尝试解除旧E_ACCESSDENIED |
| 32位/ARM交叉编译 | NOT_RUN | 本卡现有主机证据为x86_64，未运行交叉工具链 |
| 真实UART、电气/噪声/DMA/115200时序 | NOT_RUN | 仅AF_UNIX字节模拟，未接串口实物 |
| HAL/GPIO脉冲硬截止、MCU/littlefs掉电、RK介质掉电 | NOT_RUN | 本卡没有实物；SQLite事务/close-open和模拟150ms状态转换不能替代硬件证据 |
| CRC认证/抗篡改 | NOT_RUN/不承担该能力 | UART21明文排除身份认证；CRC通过只表示误码检查，不证明安全认证 |

### R5-B-1 实际探针的可复现版本

在guest执行下面Python stdin代码即可。它只读canonical源码；GCC输出到匿名memfd，socket临时目录只放在既有工作区build下，由TemporaryDirectory收尾。`listen(0)` 是可复现的队列满最小条件；生产队列为4时填满相应队列同样会进入阻塞connect路径。预期修复后5400ms内已返回失败；当前版本的本轮结果见代码后的原始输出摘要。

```python
from pathlib import Path
import ctypes, os, subprocess, tempfile, socket, time, select, json

src = Path.home() / 'locker-workspace/S1-03/batches/20261025-201026/src'
base = Path.home() / 'locker-workspace/build/S1-03'
fd = os.memfd_create('r5_transport', 0)
cmd = ['gcc', '-pipe', '-shared', '-fPIC', '-std=c11', '-Wall', '-Wextra',
       '-Wpedantic', '-Werror', '-I' + str(src / 'contracts/include'),
       '-I' + str(src / 'host/device/include'),
       str(src / 'host/device/src/transport.c'), '-o', '/proc/self/fd/' + str(fd)]
subprocess.run(cmd, pass_fds=(fd,), check=True)
lib = ctypes.CDLL('/proc/self/fd/' + str(fd))

class Transport(ctypes.Structure):
    _fields_ = [('fd', ctypes.c_int), ('path', ctypes.c_char * 108),
                ('connected', ctypes.c_bool)]

lib.locker_device_transport_init.argtypes = [ctypes.POINTER(Transport)]
lib.locker_device_transport_connect.argtypes = [ctypes.POINTER(Transport),
                                               ctypes.c_char_p, ctypes.c_uint32]
lib.locker_device_transport_connect.restype = ctypes.c_int
lib.locker_device_transport_close.argtypes = [ctypes.POINTER(Transport)]

with tempfile.TemporaryDirectory(prefix='r5_', dir=base) as directory:
    path = os.fsencode(directory + '/u.sock')
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(path)
    server.listen(0)
    filler = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    filler.connect(path)
    rd, wr = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(rd)
        transport = Transport()
        lib.locker_device_transport_init(ctypes.byref(transport))
        started = time.monotonic()
        status = lib.locker_device_transport_connect(ctypes.byref(transport), path, 5000)
        os.write(wr, json.dumps({'status': status, 'elapsed_ms':
                 round((time.monotonic() - started) * 1000)}).encode())
        lib.locker_device_transport_close(ctypes.byref(transport))
        os._exit(0)
    os.close(wr)
    try:
        print('returned_within_5400ms=', bool(select.select([rd], [], [], 5.4)[0]))
        accepted, _ = server.accept()
        assert select.select([rd], [], [], 2)[0]
        print('result_after_accept=', os.read(rd, 4096).decode())
        accepted.close()
        os.waitpid(pid, 0)
    finally:
        os.close(rd)
        filler.close()
        server.close()
os.close(fd)
print('R5_TEMP_CLEANED', not Path(directory).exists())
```

本轮原始输出摘要：

```text
COMPILE_EXIT 0
returned_within_5400ms= False
result_after_accept= {"status": 0, "elapsed_ms": 5406}
R5_TEMP_CLEANED True
```

审计执行中另有未用于结论的读/探针编排失败：一次SSH远端命令引号错误exit1；第一次transport探针在断言不存在的runtime/S1-03目录时即退出、尚未创建临时目录；本地复算脚本曾因历史清单格式/绝对变异路径差异、PowerShell stdin非ASCII正则传输而报错，改为按各自格式和ASCII转义后复算成功。它们没有改源码/证据，没有被记作产品FAIL或PASS。

本轮收尾以开工时1662个现存文件的完整SHA集合核对：既有文件不得因本次审计改变，只允许新增本报告。报告中的已知文档问题按用户只写审计文件的边界登记，未越权修summary/handoff/README；没有需要用户裁定删除的本轮临时残留。
