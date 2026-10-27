# S1-03 round-6 独立审计
结论：FAIL
阻塞项：1 项；非阻塞项：3 项

审计日期：2026-10-26。对象：契约 0.2.1 / REVIEW_DRAFT 下的 S1-03；只读检查当前实现、正式测试、round-1～round-5 原报告及现存 VMware 证据。唯一新增文件是本报告，没有修代码、改变状态、commit 或 push。判断依据为正式任务卡、机器契约、UART-v1 和内部接口；未把开发方整改声明当作验证结果。

R5-B-1 的阻塞 connect 反例已解除。本轮新发现的是默认 journal 路径截断后与控制 socket 重名，实际启动会 unlink 自己刚打开的数据库；它独立于前五轮问题，因此本轮仍 FAIL。普通用例通过不覆盖这个路径边界。

## 阻塞项

| 编号 | 严重度 | **文件:行** | 期望行为 | 实际行为 | 最小复现步骤 | 依据（**文件:行**及原文） |
|---|---|---|---|---|---|---|
| R6-B-1 | P2 | **host/simulator/src/sim_main.c:681**、**:682**、**:699**、**:703**、**:721**；删除发生在 **:78**；容量定义 **host/simulator/include/locker/simulator/sim.h:33** | 默认路径应为完整的 control 路径加 .journal；如果超过 108 字节数组容量，应在创建数据库/监听 socket 前明确拒绝配置。启动成功的模拟器必须保留可重新打开的独立日志。 | 默认分支忽略 snprintf 返回值，静默截断。control 恰好为 107 个 ASCII 字节时，截断后的 journal_path 完全等于 control_path。先打开 SQLite，随后 listen_unix(control_path) 的 unlink 删除该数据库目录项并用 socket 替换；进程仍打印 ready。实测 restart 返回 error io_error，退出后只剩 -wal/-shm，没有主数据库。100 字节路径也静默变成 .journa，违反默认路径说明；显式 --journal 分支已有长度检查，默认分支没有。 | ①在 guest 独立临时目录生成总长 107 字节的 control 路径；②只传 --uart 与 --control，不传 --journal；③读取 ready 行并检查 journal= 与 control= 完全相同；④通过控制 socket 发送 restart，实际 error io_error，再 quit；完整可运行命令及对照在下方。 | **docs/stages/stage-01-task-cards.md:62**：“模拟设备使用独立 SQLite 文件保存设备日志/动作水位”；**:63**：“模拟器重启后动作查询及事件键仍可核对”；**host/simulator/README.md:18**：“--journal 默认 <control>.journal”；**docs/architecture/module-ownership.md:54**：“模拟器独占自己的设备模型和模拟持久化文件”。 |

### R6-B-1 实际复现与一次修对的验收边界

在 VMware guest 的 Bash 执行下方命令。只启动全新实例，临时目录位于已有 build/S1-03 下；不访问活跃模拟器、不覆盖 canonical 日志。此代码就是本轮实测探针，退出时删除自己的临时目录。

~~~bash
python3 -B - <<'PY'
from pathlib import Path
import tempfile, subprocess, socket, stat, os
base = Path.home() / "locker-workspace/build/S1-03"
binary = base / "20261026-000712/host/simulator/locker-device-sim"
with tempfile.TemporaryDirectory(prefix="r6-path-", dir=base) as tmp:
    parent = Path(tmp)
    control = str(parent / ("c" * (107 - len(str(parent)) - 1)))
    uart = str(parent / "u")
    assert len(os.fsencode(control)) == 107
    proc = subprocess.Popen(
        [str(binary), "--uart", uart, "--control", control],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        ready = proc.stdout.readline().strip()
        print("CONTROL_BYTES", len(os.fsencode(control)))
        print("READY", ready)
        print("CONTROL_IS_SOCKET", stat.S_ISSOCK(os.stat(control).st_mode))
        print("DEFAULT_JOURNAL_EXISTS", Path(control + ".journal").exists())
        cli = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        cli.settimeout(3)
        cli.connect(control)
        cli.sendall(b"restart\n")
        print("RESTART_REPLY", repr(cli.recv(4096).decode()))
        cli.sendall(b"quit\n")
        print("QUIT_REPLY", repr(cli.recv(4096).decode()))
        cli.close()
        print("EXIT", proc.wait(timeout=3), "STDERR", repr(proc.stderr.read()))
        print("LEFT_FILES", [p.name for p in parent.iterdir()])
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
print("TEMP_CLEANED", not parent.exists())
PY
~~~

本轮输出关键值：

~~~text
CONTROL_BYTES 107
READY ready device backend: simulator ... control=<107字节路径> journal=<同一107字节路径>
CONTROL_IS_SOCKET True
DEFAULT_JOURNAL_EXISTS False
RESTART_REPLY 'error io_error\n'
QUIT_REPLY 'ok\n'
EXIT 0 STDERR ''
LEFT_FILES ['<同一路径basename>-shm', '<同一路径basename>-wal']
TEMP_CLEANED True
~~~

另做了三个全新实例对照，排除了“操作系统本来不支持 107 字节 socket 路径”的解释：

| control 路径字节数 | journal 参数 | 本轮实际结果 |
|---|---|---|
| 99 | 省略 | 完整 .journal 文件存在，restart=ok |
| 100 | 省略 | ready 的路径以 .journa 结束；预期完整 .journal 不存在，restart=ok，但已静默改写持久化路径 |
| 107 | 显式短路径 --journal | 独立文件存在，restart=ok |
| 107 | 省略 | 数据库路径与控制 socket 重名，restart=error io_error |

建议修复仅需在默认路径生成处检查 snprintf 的负值及返回长度，超界则在任何资源创建前返回配置错误；不要求扩展公共契约或增大路径预算。正式进程回归至少覆盖 99 字节默认路径成功、100/107 字节默认路径拒绝且不创建 DB/socket，以及 107 字节 control 配显式短 journal 成功重启。重新生成与源码匹配的 manifest/VM 证据，更新 summary/handoff 的实际结果；本审计未代为实施这些变更。

## 非阻塞项

| 编号 | 严重度 | **文件:行** | 期望行为 | 实际行为 | 最小复现步骤 | 依据（**文件:行**及原文） |
|---|---|---|---|---|---|---|
| R6-N-1 | P3 | **contracts/include/locker_contracts.h:181**；**host/device/src/port.c:429**；**host/device/README.md:61** | 跨模块调用方能从共享原生 DTO 合同确定 message_type 是否包含响应方向位。 | 实现填 request_type OR 0x80，线上语义与 UART-v1 一致，正式测试也以此消费；生成头仅声明 uint8_t。当前 README 已明确登记待负责人裁定，故不重复升级阻塞。 | ①读生成头 181 行；②读 port.c 429 行；③对照 README 61 行及 uart-v1 13 行。 | **uart-v1.md:13**：“响应为请求 type OR 0x80”；**module-ownership.md:11**：“发现缺项或矛盾时，提交……变更说明，由架构负责人统一修改真源”。原生 DTO 的方向位解释属于**规格未覆盖，需契约负责人裁定**；本卡不得手改生成头。 |
| R6-N-2 | P2 | **docs/evidence/stage-01/S1-03/mutation-batch.py:358**、**:370**、**:397**、**:406**、**:447**；**mutation.log:1366**；**summary.md:263** | 以后归档能区分 CTest 自身超时、外层 timeout、红断言及其他失败，并保留支撑结论的原始输出。 | 脚本正确识别两种超时，但只把筛选后的断言/摘要写入 mutation.log；m49 段及 guest 对应日志目录均没有 ***Timeout 原行、120.10 秒或被超时的具体测试名。现存记录能复算 ctest=8、hung=1、红断言=0；不能独立从原件复核“原批次确为某测试在 120.10 秒被 CTest 杀掉”。本轮自己的隔离探针已另证实 m49 真实挂起，因此不否定该点的非空洞性，不凭缺 transcript 创设产品阻塞。 | ①搜索 mutation.log 中 ***Timeout/120.10；无命中；②读脚本输出筛选与写入位置；③查看 guest logs/S1-03/mutation/20261025-234500，只有同 SHA 的 mutation.log；④对照 summary 的具体超时说法。 | **module-ownership.md:71**：“阶段报告保留测试日志索引、提交/文件指纹和运行环境”。完整 mutation 原文保存格式**规格未覆盖**；此项是可追溯性建议。建议下一次正式跑保留每点完整 CTest stdout、退出码和超时来源；旧批次无法恢复的原文应明确标未留存。 |
| R6-N-3 | P3 | **docs/evidence/stage-01/S1-03/summary.md:6**、**:144**、**:201**、**:278**；**handoff.md:304**、**:497** | 当前执行说明直接使用当前数字、批次和门禁规则；需要保留旧结果时显式标历史。 | summary 278 仍以“另修……加上界后”叙述 647 条、185389 B/1418 行，与当前 257 行的 646 条、186270 B/1426 行冲突；201 将 mutation 运行号 234500 称为源码批次，实际源码路径是 223627。6 行称全部同一最终快照/run_id 跑完，实际 canonical=000712、mutation 另跑且源码内容匹配。144 称 16 项经 need_zero，实为 15 次调用，加 live-tree 与 design 两项共17。handoff304称“三套消毒器”，实际普通构建加两套消毒器；497 一行同时含未明确隔离的旧647/81与当前646/80。主结果表数字正确，这些残余不使代码失效。 | ①对照 summary257/278 与 mutation.log646起的实际 FAIL 行；②读 mutation.log2 和 canonical run-summary1；③数 evidence-run-vm.sh258～272 的15次 need_zero；④对照 handoff304/497。 | **AGENTS.md:35**：“正文写当前真实规则，不在顶部累积补丁说明”；**module-ownership.md:71**：“实际运行或明确的静态检查”。建议把旧647/81及旧文件大小移到标明作废的历史说明；分别写 canonical run、mutation run 与匹配的源码树，不写成同一次运行。 |

## 已核实通过项

以下“通过”分别注明静态核对、本轮实际执行或历史原始日志复算，不能据此将本卡/阶段/硬件状态改为 DONE/PASS。

### 1. 基线、25文件与前序复用

- **状态核实**：contract-source 的 contract_version=0.2.1、status=REVIEW_DRAFT；stage-plan 的 active_task/recommended_next=S1-03、S1-03=IN_PROGRESS/report=null、前两卡DONE、阶段1 gate=NOT_RUN。对应当前 README 与 stages/README。机器依赖边只有 S1-01，S1-02 是实际复用的既有仓储依赖，不擅改依赖图。
- **25文件内容核实**：逐文件 SHA前12位、字节数及 LF 数与本轮输入一致；合计590103 B。daemon_smoke.py 为658个LF、659个逻辑文本行且末行无LF；完整SHA和32731 B均匹配，这是计行口径差异，不能当源码漂移。
- **清单核实**：source-manifest.txt 25条数据、3条注释，共2794 B；完整SHA=31e100721b80a063bcaea3ea583c7c3783fb30ea2ed7678749eb519654364263。25条均独立复算匹配；vm-s103-manifest.sha256 与其逐字节相同。
- **Windows/VM来源核实**：通过 SSH 对 canonical 的 batches/20261026-000712/src 与 mutation 来源 batches/20261025-223627/src 各核25文件，加 contracts/、scripts/、host/common/、host/storage/，各98个“清单/目录文件核对项”均与本地匹配（其中有重复包含的文件，98不是唯一文件总数）。17份本地 vm-s103-*.log 与 guest canonical logs 下原件逐份完整SHA一致。
- **快照核实**：本轮读取 guest /tmp/s103_snapshot.tar.gz，实际81762182 B、完整SHA=07b759c28e446dda7cbe59c051e90f1e9d2d4799e97aff96b0bbd9c03c873249，与 run-summary:11～12及 summary:118一致，没有重新打包。
- **前卡复用核实**：S1-02 manifest 的31个非根文件全部匹配；去掉当前根 CMake 的88/89/95/96四行后，3943 B、SHA=f9e1964ee54465f5e29299721a3f342f929cc81e3ff6a6da8d00b77fc34258b4，恰好回到S1-02根文件。S1-01旧manifest里的21个common源码/测试文件全部匹配。依赖锁及vendor中的SQLite3.54.1/cJSON1.7.19保持复用；无新增HTTP、密码或数据库实现。这里证明的是留存基线匹配，不声称没有Git也能恢复全部历史修改过程。
- **边界核实**：sim.c:2132唯一SQLite打开使用config.journal_path；DDL为sim_*表，未调用Repository业务写入；CMake复用locker_storage_thirdparty，未重下载依赖。两个socket为AF_UNIX；此卡实现中未发现公共HTTP模拟路由、真实UART/HAL/GPIO调用或固定成功占位。
- **证据目录核实**：本卡WSL专用脚本、wsl-*.log、wsl-run-id.txt、joint-fix-round1.md确已不存在；当前summary/handoff明确不使用WSL证据。旧审计描述旧输入属于历史记录，不重新当本轮证据。输入的37文件列表之外，启动器还已生成audit-round6-codex-output.txt；此会话转录的存在不算受管源码指纹不匹配，不改写开发方文件数。

### 2. 十三项审计重点

| 重点 | 本轮自行核对的结论与证据 |
|---|---|
| 1 黄金帧/长度/CRC | Python从JSON读取四个frame_hex，独立拼接test_golden_frames.c:17～28的字符串，四个逐字节相等。HELLO61 B/CRC0xbdd8→d8bd，SNAPSHOT28 B/CRC0xea6e→6eea；crc_error计算仍0xea6e而存6eeb；oversized_header10 B声明513。binascii.crc_hqx(ASCII123456789,65535)=0x29b1，线路b1 29。wire.c:116与:217覆盖offset2到payload末，不含magic/CRC。六个正式二进制本轮实际通过，含golden168条。 |
| 2 流式边界 | wire.c:169固定524字节缓冲；magic错误196、超长205、CRC错误219均丢1字节重同步；足够帧长后才取CRC/copy，满缓冲也有丢1字节出路，没有按对端长度分配。daemon使用同一stream_feed（sim_main:244），重建原请求号LE。test_wire:132/157/196/219/240/266覆盖逐字节、粘帧、垃圾、CRC、超长、最大帧；daemon:206～219覆盖损坏长度内嵌完整帧。 |
| 3 会话/动作去重 | sim.c:1122同session+host重试不重置；换会话撤销资格/清实时队列；1932先解码、1940验证非HELLO session。1640按session/action查原行，1646～1652比较完整不可变意图，1667全局command去重，1686严格水位+1。protocol:353/657/1244及port旧意图、外来响应回归本轮运行通过。 |
| 4 重启/持久化 | sim_meta/sim_events/sim_commands/sim_commands_v2独立；sim.c:2179每次open递增boot，2023写本boot首条seq1，2186重算边界；2057恢复活动检查点，1082按会话恢复水位，1786历史QUERY保留原关联。journal:266/318/349/557/836实际跨close/open或destroy/create验证，本轮461检查通过。此结论只覆盖已测短路径；R6-B-1是新增反例。 |
| 5 ACK/脉冲独立 | sim.c:2289仅type4受drop_ack，type7成功响应单独受drop_execute_ack；suppress_pulse只影响1751驱动段。protocol:1114/1320、port:1306/1656、daemon:256～301分别验证，daemon从独立进程查询SQLite中的start/finish计数；本轮均执行通过，不以ACK当开门。 |
| 6 提交失败 | sim.c:415实际ROLLBACK；966事务失败锁存FAULT；1730成功后才推进watermark/has_active。journal:488循环BEGIN/INTENT/EVENT/COMMIT/RESULT，COMMIT注入确到提交路径，随后读取SQL行数、水位与pulse=0，不只靠fail_journal前置拒绝。RESULT失败保留已接纳意图/水位并报不确定，不能错误要求其回滚已提交接纳；806还用真实SQLite触发器拒绝更新。本轮执行通过。 |
| 7 私有0600/无HTTP | sim_main:80先umask0077再bind，恢复umask后chmod0600；独立daemon_smoke.py本轮真实Popen启动可执行文件，实际stat两个socket均0600、250检查/0失败。没有import模拟引擎冒充daemon。路径重名问题单独列B1。 |
| 8 独立数据库 | sim.c:266独立DDL、2132独立路径；不访问业务Repository。journal正式测试读取SQLite头与自有表；daemon分别传临时独立journal。业务库未被本轮探针打开。 |
| 9 权限/残留 | 根CMake四行差异与前卡SHA核实；生成头/JSON、scripts与前卡组件未因本轮审计变化。检查host/device、host/simulator和本卡tests未发现.o/.a/.so/.pyc、DB、socket或一次性调试代码；测试诊断与正式故障注入保留。最终再做全树SHA收尾核对。 |
| 10 数字/证据 | 17条PASS汇总独立加和3551，S1-03六项=1834，原前卡部分=1717；三份CTest日志各19/19，普通10.16s、ASan11.33s、TSan11.83s，退出码0/报告0。daemon250/0；manifest25/25、SRC_FILES1155、SRC_STRAY_ARTIFACTS0；extract前后各1155、内容指纹相等；17个GATE_OK与run-summary内联完全对应，末行为VM_RUN_VERDICT=PASS。当前文档残余另列N3，没有用它们替代日志。 |
| 11 mutation非空洞 | AST读取49个old/new，每个old在当前对应文件恰好一次；--check实际49/bad0。逐段核anchor、替换文本、configure/build=0、失败行、完整汇总分母和daemon标签；45 CAUGHT均有相关C断言或daemon红检查，未把编译失败计为捕获。guest保留49个变异源码树，25×49=1225个文件逐一核对，仅指定一处替换且测试与来源一致。m49隔离动态证据及三个等价边界见下。 |
| 12 测试质量 | codec与handler职责分开，不把codec负例说成handler分支独立覆盖。三处C分页循环均以LOCKER_TEST_PAGE_LIMIT=64为界（protocol271、journal179、port940），另有越界断言。Python daemon的分页while仍由正式CTest的120s外限控制，不声称它也具有64页内部上界。缺Python时CMake:43 FATAL_ERROR，不能通过少测变绿。 |
| 13 A～F裁定 | A线上响应位符合uart:13，原生DTO未定项留N1；B pending_take只在三条件匹配时消费，否则自然继续扫描，pending_add同锁拒在途重复号，符合uart:23；C重开UNKNOWN与内部接口“未验证不READY”一致，journal594/595真读状态；D连续性失效任意页gap1，不再有silent_gap，与uart:50/370一致；E通知满不丢已提交日志、不锁存连续性，符合uart:356；F控制面语法/引擎错误分开已有README:43说明，协议未限定这些私有文本token，不自创新阻塞。 |

按机器布局逐字段宽度独立求和，11条请求/成功响应payload如下；与codec.c:27～55/215～274和正式payload_sizes测试相符：

| 消息 | 请求 | 成功响应 |
|---|---:|---:|
| HELLO | 49 | 71 |
| GET_SNAPSHOT | 16 | 189 |
| FETCH_EVENTS | 49 | 85+96N，N=0..4，最大469 |
| ACK_PERSISTED | 48 | 34 |
| RESUME_READY | 64 | 27 |
| DISARM | 18 | 27 |
| EXECUTE_UNLOCK | 99 | 93 |
| QUERY_COMMAND | 48 | 93 |
| HOST_HEALTH | 73 | 27 |
| TIME_ANCHOR | 36 | 46 |
| RELEASE_CONTEXT | 80 | 27 |

event96=96 B；DEVICE_EVENT payload=16+16+96=128 B；错误响应严格18 B（codec.c:1189～1199）。

### 3. round-1～round-5 逐项回查

“解除”表示旧报告给出的具体反例在当前代码/文档已被修正；不等于批准所有未来扩展或所有硬件行为。

| 前轮编号 | 当前独立核对的落点 | 结论 |
|---|---|---|
| B-1 | port127～142/408，port测试1399验证session/type/request关联 | 解除 |
| B-2 | port417仅result0应用镜像；错误响应仍交sink，测试1399 | 解除 |
| B-3 | port716/761/846/881校验原DTO绑定；测试1936真实close/open旧意图拒绝 | 解除 |
| B-4 | port185/410及605校验实际UID和必要cap；测试1456 | 解除，不称身份认证 |
| B-5 | port145同锁唯一、112注入时钟过期、176回绕；测试1619/2003 | 解除 |
| B-6 | port202背压锁存/停止/shutdown，493拒新写；测试1545 | 解除 |
| B-7 | sim1371/1399/1703/1741/1756/1823；protocol1176和journal744 | 解除 |
| B-8 | sim1575及2310推进模拟结束；protocol932、journal557/647 | 解除，只模拟计数 |
| B-9 | sim282复合主键、1646全参数去重、1667跨会话command检查；protocol1244 | 解除；不批准未来删除回收后重放防线 |
| B-10 | sim2057活动恢复、1897持久释放后清内存；journal557/836 | 解除 |
| B-11 | sim2289/2439独立EXECUTE丢回复；protocol1320、port1656、daemon139 | 解除 |
| B-12 | sim843队满只丢实时通知；protocol990 | 解除 |
| B-13 | sim1318页gap、1356拒ACK跨缺口、2504保留真实FAULT；journal488/806 | 解除 |
| B-14 | sim_main195/244共用流解析；sim2270零号先拒；daemon193～219 | 解除 |
| B-15 | 49点锚点/变异树/失败来源实算；3个生存点限定论证 | 原虚假等价反例解除，原始超时留存限N2 |
| B-16 | run-vm9～10独立批次，无rm -rf；129校验失败退出、245起归档门禁；mutation17～20工作区分离 | 原删除/吞门禁反例解除；内容指纹见R5-N-2 |
| N-1 | sim_main776/805轮询及控制后推送，daemon449起不补UART请求 | 解除 |
| N-2 | sim_main314/335/392/594严格整token/超长丢至换行；daemon正式负例 | 解除 |
| N-3 | transport192有界非阻塞send、sim_main100有界write；atomic connected、port965关闭互斥 | 原写阻塞反例解除；连接另见R5-B-1 |
| N-4 | sim187溢出、574/651短BLOB、1238读取错误检查；journal687 | 核心反例解除；两个诊断COUNT失败仍返回0，未宣称全部SQL走同一helper |
| N-5 | close返回status，destroy后失效；port949/981、sim2014；handoff204/243 | 解除 |
| N-6 | 机器依赖只有S1-01、前两卡DONE；现存数字实算 | 核心解除，残余数字见N3 |
| N-7 | contract/device CMake43缺Python直接失败 | 解除 |
| N-8 | device_test_support192～199使用pid/counter，无旧固定wal_cleanup路径 | 解除 |
| N-9 | sim_main736实际ready标记；本轮daemon实测；handoff178一致 | 解除 |
| R2-B-1 | run-vm批次解包、无递归删除；extract2～4实际非活跃路径 | 解除 |
| R2-B-2 | sim1016/2286在dispatch前读请求首16B；daemon569与protocol1402真memcmp | 解除 |
| R2-B-3 | 无silent_gap；sim1318；protocol512～520期望gap1 | 解除 |
| R2-B-4 | sim1898 detail0；journal613～617按20位TEXT零及CAST比较 | 解除 |
| R2-B-5 | port115/171用device_now_ms；测试1638推进注入clock，1590另保留真实时钟 | 解除 |
| R2-B-6 | handoff125合法UID、202发号边界、204/243销毁失效、223队满语义 | 旧四处错误解除 |
| R2-N-1 | 两README引用run-summary首行而非写死旧run | 解除 |
| R2-N-2 | gates写17项，run-summary内联并追加PASS；SSH_EXIT无归档的边界已写明 | 解除 |
| R2-N-3 | assertion行正则、complete_summaries分母与daemon分列，逐点复算一致 | 原混算问题解除 |
| R2-N-4 | 原生message_type待定仍明确登记 | 保留为R6-N-1 |
| R3-B-1 | sim1528/1538和1769 detail=action；journal790～797三条真实SQL断言，jump3/5均执行 | 解除 |
| R3-B-2 | handoff336/337为python3 -B，362为python -B | 当前执行入口解除；历史表引用旧命令不视为现行入口 |
| R3-B-3 | mutation完整64位SHA重新计算一致 | 解除 |
| R3-N-1 | 同R2-N-4 | 保留为R6-N-1 |
| R3-N-2 | sink按值复制、耗尽NOT_READY、destroy失效、live/batch、日志分工、TEXT/写路径说明 | 主要解除，零星精度见N3 |
| R3-N-3 | daemon569/协议1402回显session断言存在；protocol697有operation、763～765真读flags | 解除；slot/kind首先由decoder拒绝，不冒称handler独立覆盖 |
| R3-N-4 | wire90～93拒0xc0；test_wire89～94覆盖request_no0/1 | 解除，本轮wire130checks通过 |
| R4-B-1 | handoff362当前可执行入口是Python；check-only实际49/bad0 | 解除 |
| R4-N-1 | 同message_type | 保留为R6-N-1 |
| R4-N-2 | 当前daemon250/0与日志11/13及本轮实测相符 | 解除 |
| R4-N-3 | 当前m43=646，完整汇总分母仅1253，另80行无port完整汇总 | 核心口径解除，summary278旧数字见N3 |
| R4-N-4①②③⑤ | README80注入clock；pending_take自然扫描；run-vm15守卫在mkdir前；gates/verdict职责分开 | 解除 |
| R4-N-4④ / R5-N-2 | run-vm31～34实际sha256sum文件内容；39～40在解包53前采样；214～218前后计数+内容双比较；273独立门禁 | 解除；原日志记录相等内容指纹，非只有路径/大小 |
| R5-B-1 | transport109置O_NONBLOCK，119处理EAGAIN重试，133到同一deadline poll、137 SO_ERROR、143恢复阻塞；测试1802填满队列并1855断言full | 解除；本轮正式port389及隔离20检查实测通过 |
| R5-N-1 | 同message_type | 保留为R6-N-1 |
| R5-N-3 | handoff178标记、183/184 unknown事件、185文本card4/7/10与C API0长度区分，符合sim_main397/520、sim2352/2389/2402 | 解除 |

### 4. mutation 独立复算与证明边界

mutation.log 实际186270 B、1426个LF，SHA=8a7083bdd5648554043b0f9b5f8f221440eabffd17ca6fdc95208b7bcab8a146。guest的234500日志原件同SHA；脚本完整SHA=4f2f36edf08cc506bd13448623b584c1999a46825acedaeb4b9dc12f2ee1850d，与本地一致。全部49段configure/build=0；分类为45个CAUGHT、1个CAUGHT_TIMEOUT、3个NOT_CAUGHT、0个BUILD_FAILED。没有把生存变异算通过。

a=打印的C断言失败行数；k=打印完整红汇总的二进制数；d=仅这k份完整汇总的检查分母；p=daemon红检查。a/d不能当作总体失败比例。每段失败源行号均回查当前对应测试文件；以下列首个相关失败位置，m9等还包括同一缺陷导致的后续链式失败。

| 点位 | a/k/d/p | 首个相关失败或证据 |
|---|---|---|
| m1_crc_poly | 34/2/298/12 | test_wire.c:16，CRC黄金值 |
| m2_magic | 5/2/298/3 | test_wire.c:213，重同步 |
| m3_oversize_len | 2/2/298/0 | test_wire.c:261，length_errors |
| m4_crc_mismatch | 8/3/805/3 | test_wire.c:234，坏CRC未拒 |
| m5_reqno_zero | 1/1/130/0 | test_wire.c:77 |
| m6_event_reqno | 1/1/130/0 | test_wire.c:80 |
| m7_pending_type | 2/1/389/0 | test_device_port.c:2032，外来类型错误消费pending |
| m8_event_session | 1/1/389/0 | test_device_port.c:1298，旧session事件 |
| m9_reply_flag | 93/1/389/0 | test_device_port.c:1946等，响应方向不符 |
| m10_simulated_gate | 1/1/389/0 | test_device_port.c:866 |
| m11_slot_id | 1/1/389/0 | test_device_port.c:911 |
| m12_fetch_limit | 2/1/389/0 | test_device_port.c:924 |
| m13_anchor_reqno | 3/1/389/0 | test_device_port.c:841 |
| m14_cursor_halfzero | 3/2/309/1 | test_codec.c:268 |
| m15_event_type_range | 2/1/179/0 | test_codec.c:592 |
| m16_card_type | 1/1/179/0 | test_codec.c:619 |
| m17_reserved | 2/1/179/0 | test_codec.c:630 |
| m18_supply_unknown | 1/1/179/0 | test_codec.c:638 |
| m19_decode_version | 1/1/179/0 | test_codec.c:378 |
| m20_failed_len | 1/1/179/0 | test_codec.c:556 |
| m21_fetch_len | 1/1/179/0 | test_codec.c:470 |
| m22_queue_full | 1/1/507/0 | test_sim_protocol.c:1027，第33次pop应NOT_READY |
| m23_queue_no_session | 1/1/461/0 | test_sim_journal.c:332 |
| m24_hello_version | 0/0/0/0 | NOT_CAUGHT，调用图等价边界见下 |
| m25_has_more | 11/1/498/0 | test_sim_protocol.c:281，分页总数 |
| m26_fetch_after_bound | 1/1/507/0 | test_sim_protocol.c:473 |
| m27_ack_regression | 3/1/507/0 | test_sim_protocol.c:559 |
| m28_ack_upper | 0/0/0/0 | NOT_CAUGHT，健康可达日志状态等价 |
| m29_resume_mode | 2/1/507/0 | test_sim_protocol.c:650 |
| m30_execute_horizon | 18/1/507/0 | test_sim_protocol.c:708 |
| m31_execute_replay | 0/0/0/0 | NOT_CAUGHT，当前保留全部命令行的实现等价 |
| m32_execute_journal | 7/1/461/0 | test_sim_journal.c:534，真实FAULT后再动作 |
| m33_execute_door | 14/1/507/0 | test_sim_protocol.c:736 |
| m34_borrow_item | 16/2/896/0 | test_sim_protocol.c:726 |
| m35_health_timeout | 4/1/507/0 | test_sim_protocol.c:1229 |
| m36_pulse_flag | 17/2/967/2 | test_sim_journal.c:576 |
| m37_release_pulse | 10/1/507/0 | test_sim_protocol.c:961 |
| m38_release_idempotent | 5/2/896/0 | test_sim_protocol.c:891 |
| m39_dispatch_decode | 4/1/507/2 | test_sim_protocol.c:1398 |
| m40_dispatch_session | 3/2/968/3 | test_sim_journal.c:293 |
| m41_continuity_flag | 1/1/461/0 | test_sim_journal.c:416 |
| m42_inject_latch | 6/2/968/1 | test_sim_journal.c:414 |
| m43_boot_increment | 646/3/1253/5 | test_sim_journal.c:239起；另hung=1 |
| m44_socket_mode | 0/0/0/2 | daemon实际socket mode检查 |
| m45_status_token | 0/0/0/2 | daemon非法UID长度错误token |
| m46_drop_link | 0/0/0/1 | daemon accepted UART关闭检查 |
| m47_frame_resync | 0/0/0/24 | daemon请求号LE回显；点名resync不代表独立CRC变异 |
| m48_event_flush | 0/0/0/1 | daemon仅控制注入后立即收异步事件 |
| m49_connect_nonblock | 0/0/0/0 | CAUGHT_TIMEOUT，ctest=8/hung=1；本轮另做隔离动态验证 |

- **m43**：独立按失败文件计数210(journal)+354(protocol)+2(golden)+80(port)=646；三份完整汇总210/461、354/624、2/168合计566/1253。port的80条没有完整汇总，不推算它的分母。协议变异下动态检查数624并不应硬套正常507。
- **m24**：codec.c:836先拒payload requested_version≠1，sim.c:1934解码失败返回原码；经现有入口到不了sim1114的第二层检查。只论删除这一重复守卫的当前可观测等价，不声称handler第二层被独立测试。
- **m28**：初始ACK0，HELLO置fetched=ACK，1324只沿合法页推进且不超过last；删除1350后，through>last仍必然through>fetched，在1356回同CURSOR_GAP。该不变量可归纳，不依赖被删除守卫自身。论证限引擎维护的健康日志；不包括外部改库/损坏元数据。
- **m31**：action0由codec910拒；W>0来自会话最大行或本次成功插入，W行尚未回收。相等请求先命中1643：全参相同回原结果，不同回REPLAY_BLOCKED；不会走到1687的等号差异。不能据此批准未来回收明细后移除水位防线，uart388仍要求回收后防重放。
- **m49实际隔离执行**：没有修改源码文件。将当前transport.c内容与正式test_device_port.c的include包装从stdin交给gcc，输出到匿名memfd；链接canonical已有静态库。自定义main仅调用正式test_connect_deadline_when_accept_queue_full及TEST_REPORT。当前版本编译exit0、20checks PASS、约707ms；应用脚本第49点old→new的内存变异后仍编译exit0，子进程3005ms未返回，由探针kill，stdout/stderr均空。被kill实例的pid专属/tmp/locker_s103_backlog_<pid>.sock由父进程明确清理，检查已不存在。此独立探针证明超时缺陷可观察，**没有声称本轮重跑原49点或原CTest120秒路径**。
- **分类代码核实**：mutation-batch.py:370同时覆盖rc124与输出含***Timeout；416先排CONFIGURE_FAILED/BUILD_FAILED，420有红且失败优先CAUGHT，422无红且hung为CAUGHT_TIMEOUT。现m43红+hang保留CAUGHT，m49无红hang单列CAUGHT_TIMEOUT，符合输入要求。原始超时输出的留存不足见N2。

## 环境与 NOT_RUN

### 本轮确实运行的命令及结果

| 环境/动作 | 本轮结果及证明范围 |
|---|---|
| Windows：git status --short | Python subprocess独立读取原生退出码128，fatal: not a git repository；直接PowerShell工具调用包装返回1，不能混同为Git原生退出码 |
| Windows：python -B scripts/check_contracts.py | exit0/PASS；0.2.1、24routes/82ports/11UART/4vectors；pycparser PASS(parse only)，不是编译/运行；hardware_profile仍UNVERIFIED |
| Windows：python -B scripts/check_stage_plan.py | exit0/PASS；45卡、active S1-03、done2，runtime_or_hardware_tested=false |
| Windows：python -B scripts/check_vision_contracts.py | exit0/PASS；未验证相机/RKNN/模型推理/硬件 |
| Windows：python -B scripts/check_feasibility.py | exit0/PASS；设计一致性，runtime_or_hardware_tested=false |
| Windows：python -B docs/evidence/stage-01/S1-03/mutation-batch.py --check --src . | exit0，49个CHECK_OK、CHECK_SUMMARY points=49 bad=0；未运行full主体、未覆盖mutation.log |
| Windows：Python hashlib/json/re/AST/binascii stdin复算 | 25清单、前卡清单、四黄金帧、11布局、17日志数字、49段统计及失败源行号均按上述复算 |
| guest：ssh -o BatchMode=yes -o ConnectTimeout=8 locker-vm | 实际成功；Linux7.3.0-rc5 x86_64、/home/%USER%；未使用WSL |
| guest：只读SHA核对快照/两个来源树/17日志/49变异树 | 25文件与共享依赖匹配，17日志匹配，快照SHA/字节匹配，1225个变异文件匹配 |
| guest：bash -n 三个正式evidence shell脚本 | 各exit0；未执行主体/写新清单/重打包 |
| guest：canonical已有六个设备测试二进制，分别以timeout45调用 | wire130、codec179、journal461、protocol507、golden168、port389；六个exit0、共1834checks；是运行已有二进制，不是本轮完整重编译/CTest |
| guest：python3 -B <canonical-src>/tests/contract/device/daemon_smoke.py <canonical-build>/host/simulator/locker-device-sim <本轮独立临时目录>/case | exit0、250checks/0failures、两个socket均0600，daemon_exit0；目录位于build/S1-03/r6-daemon-*，TemporaryDirectory收尾TEMP_CLEANED=True；未覆盖原daemon日志 |
| guest：GCC/C11/-Wall/-Wextra/-Wpedantic/-Werror/pthread，匿名memfd隔离连接探针 | 当前与m49内存变异均编译成功；20checks/707ms与3005ms挂起对照，详上；不是原49点full |
| guest：R6-B-1及三个边界对照 | 实际启动四个独立daemon；107默认重名/restart失败，99默认和107显式短journal重启成功，100默认截断；全部临时目录清理 |

六个已有测试的可重放入口（guest）：

~~~bash
B="$HOME/locker-workspace/build/S1-03/20261026-000712"
for t in tests/unit/device/test_wire tests/unit/device/test_codec \
         tests/unit/device/test_sim_journal tests/unit/device/test_sim_protocol \
         tests/contract/device/test_golden_frames tests/contract/device/test_device_port; do
    timeout 45 "$B/$t"
done
~~~

### 没有运行或没有独立证实的内容

| 项目 | 本轮状态 | 具体原因与边界 |
|---|---|---|
| 全量Linux configure/build/19项CTest | NOT_RUN | 本轮是只读审计，未重建canonical目录或刷新Testing日志；源与保留批次逐字节匹配，核对历史19/19，并实际运行本卡六二进制和独立daemon。不是因为Windows不能审C而省略。 |
| 全套ASan/UBSan、TSan重编译与执行 | NOT_RUN | 本轮未运行消毒器二进制；19/19、0报告来自内容匹配的历史原始日志。隔离连接探针未加sanitizer，不宣称全部故障交错无race。 |
| full49点mutation、CTest120s及外层timeout300路径重跑 | NOT_RUN | full会复制/改写沙箱、重建并覆盖正式mutation.log；本轮只跑check-only、核对保留49变异树及原日志、另以匿名memfd隔离重现m49。没有伪造新的45/1/3计数。 |
| 原批次m49的***Timeout原文/精确120.10s | 未能独立证实 | 原始输出没有归档，本地/guest同SHA的mutation.log只有过滤后统计；具体限制见N2。本轮3秒隔离kill不是原CTest超时。 |
| evidence-run-vm.sh主体/新canonical打包 | NOT_RUN | 会生成构建/快照/日志；未运行。现有快照与日志均只读核对。 |
| WSL | NOT_RUN | 用户指定本卡不使用WSL证据，没有尝试运行或解除旧E_ACCESSDENIED。 |
| 32位/ARM交叉编译 | NOT_RUN | 当前运行证据为VMware x86_64；未调用交叉工具链。 |
| 真实UART/115200/DMA/线路噪声、电气与真实断线 | NOT_RUN | 未接串口实物，AF_UNIX只验证字节及模拟语义。 |
| HAL/GPIO脉冲硬截止、MCU/littlefs掉电、RK介质掉电 | NOT_RUN | 没有本卡实物运行；SQLite事务、进程重开与模拟150ms转换不能替代硬件证据。 |
| CRC抗篡改/身份认证 | NOT_RUN/不承担该能力 | uart-v1:21明确排除认证；CRC黄金值通过不证明安全认证。 |

### 文档与残留收尾

本次没有变更行为/接口/契约，没有修改当前README、summary、handoff或stage-plan；发现的需同步事项按只写报告的边界登记在B1/N1～N3。没有新项目或较大实施方案，因此本轮没有新增GitHub调研或引入依赖。

本轮helper均通过stdin执行，连接探针二进制仅存匿名memfd；正式测试自有临时资源和本轮daemon/path探针目录已收尾，没有保留一次性脚本、DB、socket或编译文件。本报告所附代码为可复现审计证据，不是待接入产品的实现。收尾已以开工时1666个现存文件的完整SHA集合复查：新增仅本报告，无删除；既有文件中仅启动器持续写入的audit-round6-codex-output.txt发生变化，其余全部完整SHA不变。该转录由原有runner的 *> $out 重定向持续写入，本审计没有编辑它。未清理或删除任何前序正式测试/证据/诊断日志，不存在需用户决定删除的本轮不确定残留。

S1-03继续保持IN_PROGRESS；本报告不改变阶段1 gate=NOT_RUN。R6-B-1修复并提供匹配的新回归/证据前，不应将本卡收尾为DONE。

