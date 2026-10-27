# S1-03 round-9 独立审计
结论：PASS
阻塞项：0 项；非阻塞项：4 项

审计日期：2026-10-26。工作副本：`%REPO%`；契约 `0.2.1 / REVIEW_DRAFT`。本结论仅针对 S1-03 的软件契约验收及本次所列复核范围，不是阶段 1 门禁或硬件验收。S1-03 仍为 `IN_PROGRESS`，阶段门禁仍为 `NOT_RUN`。仅写本报告，不修改实现、正式测试、契约、生成物、状态、README、summary、handoff 或证据工具，不 commit/push，不跨卡。

本轮实际使用 Windows Python 和 SSH 到既有 VMware guest。独立重算文件、黄金帧及留存日志；在既有构建目录运行六个 C 测试和两组路径专项回归；另做当前源码的严格语法编译与独立 daemon 实验。没有重跑全量 configure/build/CTest、sanitizer 或 mutation。下文区分本轮运行与历史原件复核，不用开发方的 PASS 自述代替证据。

## 阻塞项

| 编号 | 严重度 | **文件:行** | 期望行为 | 实际行为 | 最小复现步骤 | 依据（规格的**文件:行**原文） |
|---|---|---|---|---|---|---|
| 无 | — | — | — | 在已核对范围内未发现违反本卡验收条款的阻塞反例。 | — | — |

## 非阻塞项

| 编号 | 严重度 | **文件:行** | 期望行为 | 实际行为 | 最小复现步骤 | 依据（**文件:行**及原文） |
|---|---|---|---|---|---|---|
| R9-N-1 | P3 | **contracts/include/locker_contracts.h:181**；**host/device/src/port.c:429**；**host/device/README.md:61** | 跨模块原生 reply DTO 的方向位含义由共享契约明确。 | 实现与测试使用 `request_type OR 0x80`，生成头仅声明 `uint8_t message_type`；README 仍明确登记待裁定。线上响应含方向位有依据，但不能据此声称原生 DTO 字段已经获得共享契约确认。延续 R8-N-1。 | ①读生成头181；②对照 port429；③读 README61；④由契约负责人确认 Core/后续调用方的解释。 | **docs/architecture/uart-v1.md:13**：“响应为请求 type OR 0x80”；**docs/architecture/module-ownership.md:11**：“由架构负责人统一修改真源”。原生 DTO 此字段**规格未覆盖，需契约负责人裁定**；本卡不应手改生成头。 |
| R9-N-2 | P2 | **host/simulator/src/sim_main.c:221**、**:248**、**:257**；**host/simulator/README.md:22**；**tests/contract/device/daemon_smoke.py:562**；**docs/evidence/stage-01/S1-03/handoff.md:554** | 路径守卫的说明和测试理由应基于实际 vendored SQLite 行为；额外拒绝策略应明确为本模块的保守限制。 | “SQLite 将 sidecar 追加在收到的链接字面名字上”不符合本轮所用实现。实跑 `j -> real` 的 daemon，实际出现 `real-wal/real-shm`，没有 `j-wal/j-shm`；重启仍如此。SQLite 的 Unix VFS 会解析主库符号链接。因而检查 `journal_spelling` 的第二组 sidecar 名会额外拒绝原本没有 SQLite 文件碰撞的配置；m55 的8条红检查证明这项私有拒绝策略可观测，不能证明删去它会损坏日志。目标名的 sidecar 守卫仍必要且已验证。 | ①在 guest 临时目录 D 建 `j -> real`；②以 uart=D/u、control=D/c、journal=D/j 启动现有 daemon；③ready 后列目录，得到 j、real、real-wal、real-shm、u、c；④经 control 发 restart，返回 ok，sidecar 仍在 real 名下；⑤另用 control=D/j-wal 启动，当前守卫 exit2，但这是额外策略拒绝。下附可运行脚本。 | **docs/architecture/module-ownership.md:69**：“只把编译改通过而留下旧文档不算完成”；**docs/stages/stage-01-task-cards.md:62**：“模拟设备使用独立 SQLite 文件保存设备日志/动作水位”。本地依赖源码 **host/storage/third_party/sqlite/sqlite3.c:47610**、**:47621**、**:47632**、**:47685** 直接解析链接。本卡规格没有要求接受所有合法链接配置；该限制的取舍**规格未覆盖，需契约负责人裁定**，因此不升级阻塞。建议修正理由并明确策略，而非仅为消除 mutation 红灯修改预期。 |
| R9-N-3 | P3 | **docs/evidence/stage-01/S1-03/mutation-batch.py:258**、**:263**、**:268**、**:273**、**:278**、**:296**、**:308**、**:320**、**:375**、**:427**、**:459**；**handoff.md:549**、**:555**；**summary.md:185**、**:205**、**:270** | 当前引用、逐字输出和整改说明应与原件一致；检查器不能被描述成已经校验未实际比较的字段。 | 56个 old 均唯一，但8个 `ref` 标签仍是旧行号，raw 的 `anchor=` 实际也写入 `ref`；`--check` 动态计算位置，却不比较 `ref`，所以56/bad0不能支持“全部 ref 已重算”。例如 m44 的 ref=193，实际302；m51=815，实际924。summary205逐字引用 m49 的120.10秒，原件是120.08秒；270开头称12点全部 daemon红非0，但本轮 m51 是0红的超时，后文才正确说明。handoff549写“round-8 审计 PASS”，原审计第二行是 FAIL。summary185的 listen_unix62/chmod86也是旧位置，现为278/302。主结果、动态 anchor 及完整原件仍可核对。 | ①用 AST 读取 POINTS；②逐点算 `old` 的首行，与 ref 中行号比较，得到下表8处；③只读打开 tgz 中 m49 第43行及 m51 第45行；④对照上述 summary/handoff。下附无写入检查命令。 | **AGENTS.md:36**：“正文写当前真实规则”；**docs/architecture/module-ownership.md:71**：“阶段报告保留测试日志索引、提交/文件指纹和运行环境”。这些是引用与表述精度问题，不否定56份原件或主计数；历史审计原件应保留，不按新行号改写。 |
| R9-N-4 | P3 | **tests/contract/device/__pycache__/daemon_smoke.cpython-314.pyc**（二进制，无文本行）；**docs/evidence/stage-01/S1-03/evidence-make-manifest.sh:21**；**evidence-make-snapshot.sh:20** | 源码工作区清理时识别生成缓存，避免它被全目录清单收进去；区分本地树与历史快照。 | Windows 当前测试目录存在一份未列入25文件清单的 Python 缓存，61986 B，mtime/ctime 均为本地时间2026-10-26 02:12:50，早于本轮审计；SHA=`8d732ef3905eda66b80f3a9115180ce144e572f48197ae5d0c8afda477788f84`。make-manifest 会枚举它，而 snapshot 显式排除 pyc。它不在已验证 VM 快照内，不据此否定历史 `SRC_STRAY_ARTIFACTS=0`。本轮所有 Python 均用 `-B`，未产生此缓存。 | ①`Get-Item -LiteralPath 'tests/contract/device/__pycache__/daemon_smoke.cpython-314.pyc'`；②`Get-FileHash -Algorithm SHA256 -LiteralPath 'tests/contract/device/__pycache__/daemon_smoke.cpython-314.pyc'`；③对照清单生成器21及快照排除规则20。 | **AGENTS.md:37**：“完成前自动检查本轮临时残留”；**docs/stages/stage-01-task-cards.md:135**：“不得把临时数据库、运行 socket、测试凭据和调试打印提交为正式代码”。普通缓存不是业务代码或正式证据；本轮仅允许写 audit-round9.md，故只登记并交开发方清理，没有越权删除。无 Git/提交事实，不声称该缓存已经被提交。 |

R9-N-3 的8处 ref 差异如下。位置列为当前源码中 old 的真实首行；mutation.log 的动态 `anchor=` 是正确的，错误保留在 `audit_ref=` 和 raw 第二行的描述标签中。

| 点位 | ref 所写 | 实际首行 |
|---|---|---|
| m44_socket_mode | sim_main.c:193 | 302 |
| m45_status_token | sim_main.c:366 | 475 |
| m46_drop_link | sim_main.c:595 | 704 |
| m47_frame_resync | sim_main.c:320 | 429 |
| m48_event_flush | sim_main.c:291 | 400 |
| m50_journal_default_len | sim_main.c:801 | 910 |
| m51_journal_collision | sim_main.c:815 | 924 |
| m53_wal_shm_off | sim_main.c:263 | 267 |

以下两个脚本可经 `ssh locker-vm` 进入 Bash 后直接运行；均无需新建 build 目录或修改源码。第一个复现 R9-N-2，也复核权限、黄金 HELLO 的分段发送和重启。临时目录在既有 runtime/simulator 下，退出自动清理。

```bash
python3 -B - <<'PY'
from pathlib import Path
import os, tempfile, subprocess, socket, select, stat, struct, binascii
ws = Path.home() / 'locker-workspace'
binary = ws / 'build/S1-03/20261026-054252/host/simulator/locker-device-sim'
for spelling_collision in (False, True):
    with tempfile.TemporaryDirectory(prefix='r9-repro-', dir=ws/'runtime/simulator') as d:
        u, c, j = d+'/u', d+'/c', d+'/j'
        os.symlink('real', j)
        if spelling_collision:
            c = j + '-wal'
        p = subprocess.Popen([str(binary), '--uart', u, '--control', c, '--journal', j],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            assert select.select([p.stdout], [], [], 5)[0]
            ready = p.stdout.readline().strip()
            print('SPELLING_COLLISION', spelling_collision, 'READY', ready)
            if ready.startswith('ready'):
                print('MODES', oct(stat.S_IMODE(os.stat(u).st_mode)),
                      oct(stat.S_IMODE(os.stat(c).st_mode)))
                print('ENTRIES', sorted(os.listdir(d)))
                h = bytes.fromhex(
                    '4c4b0101310001000000000000000000000000000000000000000000000000004000'
                    '800000000000000a0000000000004000800000000000000b01d8bd')
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
                    s.settimeout(2); s.connect(u); s.sendall(h[:7]); s.sendall(h[7:])
                    b = b''
                    while len(b) < 83:
                        part = s.recv(512)
                        assert part
                        b += part
                    print('HELLO', len(b), hex(b[3]), struct.unpack_from('<H', b, 4)[0],
                          struct.unpack_from('<H', b, 26)[0],
                          binascii.crc_hqx(b[2:-2], 65535) == int.from_bytes(b[-2:], 'little'))
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
                    s.settimeout(2); s.connect(c)
                    for command in (b'restart\n', b'quit\n'):
                        s.sendall(command); print(command, s.recv(1024))
            print('EXIT', p.wait(timeout=5), 'STDERR', p.stderr.read().strip())
        finally:
            if p.poll() is None:
                p.kill(); p.wait()
    print('TEMP_CLEANED', not Path(d).exists())
PY
```

本轮独立实验的对应输出：普通分离路径与 `j -> real` 均 ready，restart/quit=`ok`，exit0；链接场景目录为 `c,j,real,real-shm,real-wal,u`，两个 mode=`0o600`；HELLO=`83 / 0x81 / payload71 / result0 / CRC_OK=True`。control 改为 `j-wal` 时 exit2，stderr=`paths must name distinct filesystem entries`，原链接保留。另测主库撞名、双 socket 链接别名均 exit2；既有 WAL 符号链接导致开库失败的场景 exit1，未被误写成“全部配置拒绝均exit2”或“无任何副作用”。所有本轮独立临时目录均已清理。

以下在项目根用 Windows Python 或 guest 的同源树执行，复核 R9-N-3。它只解析 AST，不导入或执行 mutation 脚本主体。

```python
from pathlib import Path
import ast, re
p = Path('docs/evidence/stage-01/S1-03/mutation-batch.py')
t = ast.parse(p.read_text(encoding='utf-8'))
points = next(ast.literal_eval(n.value) for n in t.body
              if isinstance(n, ast.Assign)
              and any(isinstance(x, ast.Name) and x.id == 'POINTS' for x in n.targets))
for name, ref, rel, old, new, needle in points:
    text = Path(rel).read_text(encoding='utf-8')
    assert text.count(old) == 1, name
    line = text.count('\n', 0, text.index(old)) + 1
    if int(re.search(r':(\d+)', ref)[1]) != line:
        print(name, ref.split()[0], 'actual', line)
```

## 已核实通过项

以下“运行”均为本轮实际执行；“原件复核”是读取保留原文并独立复算，不表示本轮重跑了其命令。

1. **执行入口与状态。** 核读根 AGENTS、README、stages/README、stage-plan、本卡58～65、UART-v1全文及相关 internal-interfaces/module-ownership/traceability。JSON 解析为契约0.2.1/REVIEW_DRAFT，active_task/recommended_next=S1-03、S1-03=IN_PROGRESS，S1-01/S1-02=DONE。设备端口实施归属见 traceability57～65、93～96、104及137～147；真实串口在后续卡，不把本卡扩为硬件实现。本轮是既有源码审计，无新架构、选型或依赖下载，不需要为形成新方案另做 GitHub 调研。

2. **源码指纹。** 对输入列出的25个文件逐个复算完整 SHA、字节和 splitlines，25/25匹配，总620932 B；daemon_smoke.py 为52617 B、1076行、1075个LF且末行无换行。source-manifest 为2794 B，25条数据加3条注释；SHA=`3218c11e7a53d5320e6f6588aa702f00a7f4f93a8924247b04e6789fbd7cdfb6`，与 vm-s103-manifest.sha256 内容一致。未把注释当数据或把证据脚本混进25文件分母。

3. **变更边界。** 对 round-8-prompt 的25条旧指纹逐项比对，只变化 simulator/README、sim_main.c、daemon_smoke.py，其他22个受管文件相同。S1-01清单中的21个common源码/测试文件、S1-02清单中31个非根文件均逐字节匹配。根CMake去掉88/89/95/96四条接入后3943 B、SHA=`f9e1964ee54465f5e29299721a3f342f929cc81e3ff6a6da8d00b77fc34258b4`，匹配S1-02基线。contract-source、生成头、uart-layouts、黄金帧、UART规格、模块所有权、任务卡、stage-plan的完整指纹均与 summary16～23所列一致。`git status --short` 实际exit128；没有Git历史，不能认证未留基线文件的完整历史修改集合。

4. **VM同源及原件来源。** 本地25受管文件加 contracts/scripts/common/storage 共98个文件与 guest `batches/20261026-054252/src` 完整SHA逐项一致，98/98。17个 vm-s103 日志加manifest共18份与 guest canonical日志逐字节相同；mutation.log 和全部56份 raw 与 guest `logs/S1-03/mutation/20261026-050500` 逐份SHA一致。guest现存 `/tmp/s103_snapshot.tar.gz` 实测84060028 B、SHA=`7893c95c0b1257d69006b6b2da45a9a80fffda8f38309f151a6275591e1901aa`，匹配 run-summary11～12。没有重打包或替换历史原件。

5. **黄金帧。** 用 Python 正则拼接 test_golden_frames.c17～27的四组常量，与 JSON 的四个 frame_hex 比较，全部逐字节相同：HELLO61 B/payload49/CRC0xbdd8/线路d8bd；SNAPSHOT28 B/payload16/CRC0xea6e/线路6eea；坏CRC向量重算0xea6e但存6eeb；超长header10 B声明513。`binascii.crc_hqx(b'123456789',65535)=0x29b1`，线路为b129。wire.c116、217覆盖offset2至payload末，不含magic/CRC。本轮黄金帧二进制实际168checks通过。

6. **布局与有界读写。** 依 uart-layouts 的类型宽度独立求和得 HELLO49/71、SNAPSHOT16/189、FETCH49/(85+96N)、ACK48/34、RESUME64/27、DISARM18/27、EXECUTE99/93、QUERY48/93、HEALTH73/27、ANCHOR36/46、RELEASE80/27，N≤4得最大469；event96=96，DEVICE_EVENT=128。codec逐字段LE编解码，错误响应严格18 B；未把C结构体内存直接上线。

7. **流解析。** wire.c158～243固定524 B缓冲，magic、>512长度、CRC错误在196/205～209/219～222走逐字节重同步；完整帧才复制payload，缓冲满有明确丢字节出路，不按输入长度malloc。daemon的 service_on_frame/service_read_uart 共用该解析器。test_wire 的单字节、半帧、连续帧、CRC/超长、最大帧与handler中止用例实际130checks通过；独立daemon还实跑了黄金HELLO的7字节拆分。此处不是UART电气测试。

8. **响应关联与镜像。** port127～142只在session/request_no/type均匹配时消费pending；417仅result0应用成功body；185～197核对实际UID及必要capabilities；历史QUERY不替换当前会话/boot镜像。测试1399、1456、1705、2003实际经socket覆盖这些行为。pending_take不匹配时自然进入下一轮扫描；在途号唯一性由pending_add145～172同锁保证。方向位待裁定单列R9-N-1。

9. **输入绑定、背压与销毁。** port716/761/846/881核对意图、游标及guard归属；1936正式测试close/open后提交旧意图，断言拒绝且无新写。sink拒绝在202～213锁存并shutdown，493拒新请求。close/destroy在949～992停止、join后释放；README74～81和handoff的正文不再保证free后指针可调用。正式端口测试本轮389checks通过；不据此声称任意未测线程交错均无race。

10. **注入时间及真实连接截止。** pending115/171使用config.clock，1619测试推进注入时间回收，1590另有真实时钟烟测。transport109设O_NONBLOCK、119处理满队列、133以同一真实deadline poll、137检查SO_ERROR、143恢复阻塞；1802正式回归填满accept队列，700ms预算失败且耗时<3000ms、未发布connected，accept腾位后能成功。本轮该二进制实际通过。没有把OS连接实时时间替换成协议注入时间。

11. **会话与动作幂等。** sim1016/2286在解码早退前读取请求session回显；1114/1122及1940落实HELLO版本、同session+host幂等、其他旧session拒绝。1643比较全部不可变参数，1667检查历史command，1686严格watermark+1。protocol353、657、1244以及port旧响应/旧意图用例实际运行通过；slot/kind首先由decoder拒绝，没有冒称handler单层独立覆盖。

12. **恢复屏障和驱动前守卫。** sim1156快照取已提交边界，1324推进连续补取，1399核对snapshot/fetch/ACK屏障，1405拒绝不安全状态，1703检查READY及健康；1730事务成功后才推进水位和active，1741/1756重采时并复查期限/guard。protocol1176和journal744实际执行。R3-B-1的修复确在实现中：sim1528/1538持久写action_no，1769回写同值；journal790～797以20位TEXT比较COMMAND_ACCEPTED detail，未只改测试绕过实现。

13. **独立持久化与重启。** sim使用独立sim_meta/sim_events/sim_commands/sim_commands_v2，按(session,action_no)保留历史，WAL+FULL见2132～2144；不调用业务Repository或打开其路径。2179每次open递增boot，2023写新boot首条seq1，2186重算边界；2057恢复active，1897提交释放后才清内存。journal266/318/349/557/836实际跨close/open或destroy/create验证查询、epoch/事件键和未决上下文。重开时2168～2170输入为UNKNOWN，constructor初始场景为CLOSED/PRESENT；不推定无观测时物理正常。

14. **ACK与脉冲独立。** sim2289～2292及2430/2439/2457分开控制ACK_PERSISTED响应、EXECUTE响应和脉冲；protocol1320分别跑suppress开/关并验证零响应、watermark1、pulse_count0/1、QUERY状态与重复请求不再驱动。1114另测ACK_PERSISTED丢响应。port1656覆盖只QUERY不自动重发。本轮相关二进制全部通过；daemon全场景覆盖只作原件复核。

15. **真正事务失败。** sim410在注入COMMIT失败时ROLLBACK，sim_run_txn失败锁存故障；execute1690在日志不可用时拒新动作。journal488逐一注入BEGIN/INTENT/EVENT/COMMIT/RESULT，真实检查SQLite命令行数、事件行数、水位和活动意图，RESULT失败保留已接纳意图，之前失败不推进；806另用真实SQLite故障证明off不能清FAULT。该461checks二进制本轮实跑通过，不把仅前置fail_journal拒绝冒充提交失败覆盖。

16. **事件缺口及队满语义。** 未发现SIM_META_SILENT_GAP残留；sim1318～1323在连续性锁存false时每页gap1，1356拒跨缺口ACK；2498～2509不以off抹去真实FAULT。与uart-v1:50、370及372一致。sim843～861通知队满只丢live副本，日志仍保留，符合356；protocol990实际验证32次pop成功、第33次NOT_READY、dropped0、日志34。不能据带内CONTINUITY_LOST标记把gap改0。

17. **脉冲与释放。** sim1575模拟到期结束、先清pulse_active再写结果，失败走1551不确定性；1878活动脉冲拒释放，1897提交成功后才清关联。CONTEXT_RELEASED detail为0（1898），COMMAND_ACCEPTED detail为action_no（1726）；分别符合uart-v1:352/345。它们只是软件状态及计数，未验证GPIO、硬定时器或真实脉宽。

18. **私有控制面。** sim_main278～312为AF_UNIX、umask0077后bind、chmod0600；独立daemon实验实际stat两个入口均0600、收到明确simulator启动标记，并收到CRC正确的0x81响应。控制文本的整token、十六进制、溢出、超长丢至换行、内嵌NUL及状态token路径有正式负例。语法类与引擎状态类token是私有约定（README45～47），无新增公共HTTP/任意进程命令；对当前host/device、host/simulator源码及CMake核查，没有HTTP服务器、HAL或GPIO实现。

19. **R6/R7/R8路径整改。** 长度检查897～918、身份检查924均在create937、open941和listen953/959之前。canonical_missing解析父目录，canonical_entry对末级悬空链接循环readlink、按链接目录解释相对目标，8跳超限或解析失败拒绝；socket互撞、journal目标及其sidecar撞名均有守卫。正式path_aliasing32checks与dangling_links73checks本轮实跑通过，包含相对/绝对/多跳/父目录链接、环、超限链、普通别名和正对照。R7-B-1/B-2、R8-B-1给出的原破坏配置未再出现ready后日志被删或入口被替换。额外字面sidecar策略的理由不成立，保留R9-N-2。

20. **拒绝测试的强度和限度。** daemon_smoke482～507比较退出、不超时、stderr、最终目录项集合和每条readlink值；643～662的空目录案例还检查无遗留项。这比只断言exit2强，且m52/m53/m54原件的红检查确落在对应拒绝场景。最终目录集合不能证明进程期间从未“创建再清掉”瞬态文件；本轮同时核对守卫先于资源创建的源码顺序。未新增跟踪所有瞬态系统调用的契约要求，也未声称已验证并发目录替换。

21. **本轮实际C运行。** 复用`~/locker-workspace/build/S1-03/20261026-054252`，依次直接执行test_wire130、test_codec179、test_sim_protocol507、test_sim_journal461、test_golden_frames168、test_device_port389，六个exit0，共1834checks。另以runpy加载正式daemon测试辅助函数，定点调用test_daemon_path_aliasing与test_daemon_dangling_links；它们通过Popen驱动独立daemon，分别32/73checks、FAILURES=[]。没有把这105条写成全套391条重跑。

22. **严格语法编译。** 从既有目标flags.make提取当前包含目录、`-std=c11 -Wall -Wextra -Wpedantic -Werror`，对wire/codec/transport/port/sim/sim_main六个生产C文件逐个执行`cc ... -fsyntax-only <file>`，全部exit0、stdout/stderr均0 B，不生成object。sim_main的`_XOPEN_SOURCE 700`足以使本机glibc相关接口在严格C11下可见；没有重新configure或链接新目标，不将语法检查称为全新构建通过。

23. **canonical数字原件复核。** run-summary1为20261026-054252，extract5～9及run-summary7～14记录live文件数1158/1158，前后指纹均`e8b1d7b7fa0d6d9504bdb259fbed0e27a1ee868b7154cfee4bf8c6c9e99497d7`。source-verify26～31为25/25、失败0、stray0、SRC_FILES1170。unit-tests全部17条PASS分母独立相加3551，设备部分1834，前卡1717；daemon日志32～35为391/0/exit0。普通CTest41～44为19/19、10.04秒、exit0；ASan对应41～45为19/19、12.47秒、0报告；TSan为19/19、15.98秒、0命中。configure/build原文无警告/错误，退出均0。四个check JSON与design-checks的四个EXIT0对应。以上均是历史原件，不是本轮测试耗时。

24. **归档门禁与活跃树保护。** evidence-run-vm9～10解包独立batches目录，无rm -rf活跃源；15～19先验证工作区再mkdir，31～40与214～218按内容SHA及文件数前后比较。258～272共15个need_zero，加live-tree与design两项共17，gates恰17条GATE_OK；run-summary16～32内联同样17条，33为VM_RUN_VERDICT=PASS。失败路径exit4/5存在；verdict在打包294之前已入归档。不运行其主体，不补造未归档SSH_EXIT。快照时文档早于数字回填的边界见summary144，不能据源码匹配声称快照内summary/handoff也是最终版本。

25. **mutation原件逐点复算。** mutation.log为208118 B、1648 LF、SHA=`02dde78cf442c60016bed5cb75b1753a9030d4c392321d0859ef410a01616e8c`；raw归档23622 B、SHA=`5338baac0ef63adeb091d3eefdc0a5f2b5a8a51f4fe2924c91a0b58c7908796b`，57个tar条目=目录+56文件。AST提取56个old，每个在当前源码恰好一次；实际`--check --src .`为56/bad0。逐份raw重新数C断言、完整汇总分母、daemon红及Timeout，得到CAUGHT51、CAUGHT_TIMEOUT2、NOT_CAUGHT3，与主表一致；所有configure/build记录均0，无BUILD_FAILED伪装CAUGHT。每个CAUGHT都有真实相关断言/进程检查；m55只证明额外策略，限R9-N-2。m43为646条C失败，210+354+2+80；完整汇总只含前三项、分母1253，另port超时。m49 raw43为port Timeout120.08、ctest exit8；m51 raw45为daemon Timeout120.10、exit8，均不是外层timeout124。m50/m52/m53/m54/m55/m56的daemon红分别7/16/24/28/8/4。没有把超时混入红断言，也没有按旧轮次的m51红32改写本轮0红。

26. **三个生存变异的独立可达性分析。** m24：codec836先拒HELLO requested_version，sim1932～1937解码失败早退，删handler1114的重复检查不改变线上行为。m28：正常可达状态中`ack_persisted <= fetched_through <= last_committed`；fetched只从HELLO1137的ACK或1324～1326的已提交页推进；through超过last必超过fetched，1356仍回同一CURSOR_GAP且无提交。m31：当前引擎无删除command明细的回收路径，session水位来自该session的MAX(action_no)或成功插入；action等于watermark必先在1640～1663命中已有行，全参数相同回旧结果、不同回REPLAY_BLOCKED，不能到1687的等号分支。接受这三项在当前正常可达调用图中的等价性；不延伸到任意篡改SQLite、未来删除明细或绕过decoder直接调用static handler。三个raw均19/19、无红；本轮没有重跑变异构建。

27. **测试边界与环境区别。** 三个分页循环均有LOCKER_TEST_PAGE_LIMIT64及越界断言（protocol271/280、journal179/188、port940/953），不是删除失败用例。CMake缺Python时43行FATAL_ERROR，不以减少测试数变绿。Windows本轮四项check脚本均PASS；Windows有pycparser，语法声明parse通过，VM历史JSON明确optional pycparser NOT_RUN，环境不同没有改写历史结论。源码目录扫描未见DB/socket/object/archive/shared-library临时产物；发现的既有pyc如R9-N-4，未隐藏或清除。

28. **文档现值与硬件边界。** handoff5现为391，304为普通构建与两套消毒器，summary范围表/主计数/完整指纹已对应当前原件；旧51点位/197711 B/1527行在287明确标历史作废，不据旧数字本身重开阻塞。当前summary6/341、handoff398/415明确WSL已删除且不再作证据；目录中相应WSL脚本、日志、run-id和joint-fix均不存在。历史audit文本中的旧输入不等于当前仍声称有WSL证据。真实UART、GPIO硬截止、MCU/littlefs掉电、CRC鉴权及硬件profile没有被写为本卡已验证；剩余精度问题已列N项。

历轮问题的本轮复核归纳如下，表示原阻塞反例在当前实现中的整改成立；不表示认可整改表中的每个标签或“全部文字已无误”声明。

| 历史问题 | 当前复核落点与结论 |
|---|---|
| round-1 B-1～B-6 | 三元响应匹配、错误body不应用、DTO归属绑定、实际UID/capabilities、pending唯一/超时/回绕、sink背压锁存均有实现及本轮端口回归；原路径解除。 |
| round-1 B-7～B-10 | READY/健康/屏障、提交后期限复核、模拟PULSE_FINISHED、完整意图去重/会话水位、活动与释放恢复均核对；journal/protocol本轮通过。 |
| round-1 B-11～B-16 | 独立EXECUTE丢响应、通知与日志缺口分离、真实事务故障、同源流解析与零号副作用前拒绝、变异等价性边界、独立batches及失败门禁均复核；原阻塞解除。 |
| round-1 N-1～N-9 | 控制后/轮询flush、严格文本、有界发送/原子connected、关键SQL/BLOB/u64读取、销毁语义、依赖状态、Python必需、固定无关unlink已去掉、simulator标记均核对；诊断COUNT读失败仍可返回0，不冒称全部SQL均走同一helper。现存缓存另列N4。 |
| round-2 B-1～B-6、N项 | 活跃树不删除、请求session先回显、无silent_gap、释放detail0、注入时钟及真实烟测、destroy后失效语义均成立；README跑次、verdict入档、断言/daemon分列已复核；DTO待定延续N1。 |
| round-3 B-1～B-3、N项 | COMMAND_ACCEPTED detail真实修复及TEXT SQL断言成立；当前mutation入口使用Python；完整SHA复算一致；session回显、PULSE_ACTIVE真快照、slot/kind有效身份及0xc0负例存在。当前精度残余不由历史表覆盖，列N3。 |
| round-4 B-1、N项 | handoff当前执行入口不再bash调用py；当前daemon主计数391对应原件；m43完整汇总子集口径正确；时钟、自然继续扫描、mkdir守卫、内容指纹、gates/verdict分工均成立。 |
| round-5 B-1、N项 | 非阻塞connect+同一截止poll+SO_ERROR及accept队列回归成立；内容哈希前后采样成立；控制命令与模拟启动说明一致。 |
| round-6 B-1、N项 | 默认journal长度守卫先于资源创建，历史99/100/107边界记录与源码相符；完整56份raw现可独立核对，超时来源真实保留；微小逐字引用漂移列N3。 |
| round-7 B-1/B-2、N项 | 普通别名、主库/目标sidecar冲突、同名/别名双socket的专项回归本轮32checks通过；特性宏严格语法编译通过。引用标签未全同步，列N3。 |
| round-8 B-1、N项；handoff12.11 | 悬空相对/绝对/多跳/父目录链接、环和超限链专项73checks通过；合法分离链接可重启。删除恒假journal_spelling主库比较不削弱原反例防护；新增字面sidecar规则可观测，但SQLite理由错误，列N2；ref全重算声明不成立，列N3。 |

## 环境与 NOT_RUN

| 项 | 本轮实际状态 | 范围与证据边界 |
|---|---|---|
| Windows Python设计检查 | 已运行，四项PASS | `python -B scripts/check_{contracts,stage_plan,vision_contracts,feasibility}.py`各自执行；只证明设计一致性。 |
| Windows SHA/JSON/CRC/AST/tar复算 | 已运行 | 25受管文件、关键文档、四黄金向量、11布局、56锚点与56份raw均独立计算；辅助脚本用stdin，无落盘脚本。 |
| mutation check-only | 已运行，56/bad0 | `python -B docs/evidence/stage-01/S1-03/mutation-batch.py --check --src .`；未运行full，且该检查不验证ref标签。 |
| SSH及guest只读核验 | 已运行 | `ssh -o BatchMode=yes -o ConnectTimeout=8 locker-vm`成功；Linux7.3.0-rc5 x86_64；分区59G、已用44G、可用13G。核验98源文件、18份canonical原件、mutation及56份raw。 |
| Linux严格语法编译 | 已运行，六文件exit0 | 复用既有flags，`cc ... -fsyntax-only`；未写object，未新建build目录。 |
| 六个既有C测试二进制 | 已运行，六个exit0 | 1834checks；包含真实时钟烟测、连接截止、协议、持久化及端口回归，不等同19项CTest。 |
| daemon路径专项/独立实验 | 已运行 | 两组正式专项105checks，另做权限、分段黄金HELLO、sidecar、restart/quit及别名实验；全部独立临时目录清理。 |
| 全量Linux configure/build/link | **NOT_RUN** | 按本轮边界不创建新build、不重跑全构建；仅复核历史Debug/-Werror原件及本轮语法编译。 |
| 全套19项CTest | **NOT_RUN** | 本轮只运行上述六个二进制及路径专项；历史19/19与10.04秒已核原件。 |
| ASan/UBSan、TSan运行 | **NOT_RUN** | 本轮仅核历史同源日志、退出码与报告数；不将12.47/15.98秒写成本轮输出。 |
| 56点全量mutation | **NOT_RUN** | 按用户磁盘/耗时约束不重跑；仅check-only及全部原件复算，未改任何变异树。 |
| 全套391条daemon smoke | **NOT_RUN** | 本轮定点运行105条及独立场景；391/0属于已核验历史canonical原件。 |
| VM证据脚本主体/重打包快照 | **NOT_RUN** | 只读审查脚本顺序与门禁原件，不删除/移动活跃树，不覆盖历史日志。 |
| 真实UART、HAL/GPIO硬截止、MCU/littlefs/RK介质掉电、ARM/32位移植、CRC身份认证 | **NOT_RUN** | 无本卡实物或相应验收；CRC不是认证。硬件profile仍UNVERIFIED。 |
| Git差分/commit/push | Git状态检查exit128；其余未执行 | 当前不是Git仓库。本报告不置DONE、不更新stage-plan、不跨卡。 |

只读辅助复算曾因清单注释被当数据、Windows管道中的中文正则编码、首次实验选择了不存在的runtime/S1-03目录而提前退出；随后修正解析/改用既有runtime/simulator完成重算和实验。这些辅助错误未被算作产品FAIL或伪装成通过。全程未安装依赖、未修改SQLite/HAL/契约或实现；本轮正式产物仅本报告。已检查文档与临时残留，需开发方处理的内容均列为非阻塞项，未因审计写权限边界擅自修复。
