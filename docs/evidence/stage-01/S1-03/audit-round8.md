# S1-03 round-8 独立审计
结论：FAIL
阻塞项：1 项；非阻塞项：2 项

审计日期：2026-10-26。工作副本：`%REPO%`，契约 `0.2.1 / REVIEW_DRAFT`。S1-03 保持 `IN_PROGRESS`，阶段 1 门禁保持 `NOT_RUN`。本轮只新增本报告，不修实现、正式测试、契约、生成物、README、summary、handoff、状态索引或证据工具；不 commit/push、不跨卡。

本轮 SSH 实际可用。除 Windows 静态复算外，已在现有 VMware guest 对当前源码做全新 Debug、RelWithDebInfo 严格构建与完整 CTest，复跑既有 ASan/UBSan、TSan 构建的 CTest，并实际运行路径反例。FAIL 来自当前源码新编译目标的确定反例，不来自 Windows 环境限制。全部现有正式测试通过，不能覆盖本报告发现的悬空符号链接配置。

## 阻塞项

| 编号 | 严重度 | **文件:行** | 期望行为 | 实际行为 | 最小复现步骤 | 依据（规格的**文件:行**原文） |
|---|---|---|---|---|---|---|
| R8-B-1 | P2 | **host/simulator/src/sim_main.c:86**、**:90**、**:120**、**:139**、**:142**、**:146**、**:815**；实际删除 **:181**，先开库 **:832**，后创建 socket **:844**、**:850**；**host/simulator/src/sim.c:2132**、**:2142** | 独立日志及两个协议入口在成功启动后保持可用；实际会命中同一主库或 WAL/SHM 的配置，在创建/删除资源前拒绝。 | `realpath(journal)` 对末级悬空符号链接返回 ENOENT，回退却只解析它的父目录并拼回链接名称，没有解析链接的目标。SQLite 随后沿链接创建实际目标及其 sidecar，socket 创建流程再 unlink 这些文件。`j.db -> c` 或 `j.db -> u` 时打印 ready，但主库 fd 已显示 `(deleted)`，restart 返回 `error io_error`；`j.db -> real.db`、任一 socket=`real.db-wal`/`real.db-shm` 时也打印 ready，restart 返回 ok 却把对应入口变成普通文件，control 新连接 errno=111。 | ①全新目录 D，确认 c 不存在，创建相对链接 `D/j.db -> c`；②启动当前编译的 daemon，`--uart D/u --control D/c --journal D/j.db`；③实际收到 ready；④经 D/c 发送 `restart\n`，实际 `error io_error`；⑤quit 后 c 主库不存在，仅 c-wal/c-shm 及悬空 j.db 留下。下方脚本同时复现主库、WAL、SHM 对两个 socket 的六种组合，含正常及拒绝对照。 | **docs/stages/stage-01-task-cards.md:62**：“模拟设备使用独立 SQLite 文件保存设备日志/动作水位”；**:63**：“模拟器重启后动作查询及事件键仍可核对”；**docs/architecture/module-ownership.md:55**：“测试控制走另一条权限 `0600` 的本地控制入口”；当前模块说明 **host/simulator/README.md:22**：“拒绝四种冲突”及“不创建也不删除任何资源”。这里要求的是已承诺的日志/入口隔离，没有增加新业务能力或任意并发文件系统防护要求。 |

R7-B-1 的普通 `/./` 别名及字面 WAL/SHM 撞名反例已被修复；其“所有实际路径身份冲突均已隔离”的结论仍不能成立。R7-B-2 的相同/别名双 socket 反例已修复。R8-B-1 的六种破坏组合共用一个根因，不拆成多个阻塞项。

修复应识别或明确拒绝末级悬空链接，校验 SQLite 实际将使用的主库及 sidecar 身份；不能只给 `j.db -> c` 加字符串特判。回归需覆盖主库、WAL、SHM 对两个入口的组合，以及相对链接、多跳链接、父目录链接、正常分离路径；拒绝时比较原有目录项/链接是否保持原样。允许明确拒绝不支持的链接配置，本报告不要求实现所有链接形式，更不声称验证了并发改名/恶意目录替换。

下面是在 guest 实际执行过的独立复现脚本。经 `ssh locker-vm` 进入 Bash 后运行。它从已核对的 canonical 源码全新构建，所有新文件均在 `build/S1-03` 的独立临时目录，退出自动清理，不改源码或 canonical 证据日志：

```bash
python3 -B - <<'PY'
from pathlib import Path
import os, socket, subprocess, tempfile, select, stat
ws = Path.home() / 'locker-workspace'
src = ws / 'S1-03/batches/20261026-032231/src'
with tempfile.TemporaryDirectory(prefix='r8-repro-', dir=ws/'build/S1-03') as bd:
    commands = [
        ['cmake', '-S', str(src), '-B', bd, '-DCMAKE_BUILD_TYPE=Debug',
         '-DLOCKER_BUILD_HOST=ON', '-DLOCKER_BUILD_TESTS=OFF',
         '-DLOCKER_WARNINGS_AS_ERRORS=ON'],
        ['cmake', '--build', bd, '--target', 'locker-device-sim', '-j4']]
    for command in commands:
        r = subprocess.run(command, capture_output=True, text=True, timeout=180)
        print('FRESH_REPRO_BUILD_EXIT', r.returncode, flush=True)
        if r.returncode:
            raise RuntimeError(r.stdout + r.stderr)
    binary = str(Path(bd)/'host/simulator/locker-device-sim')
    cases = ['main_control', 'main_uart', 'wal_control', 'shm_control',
             'wal_uart', 'shm_uart', 'chain_control', 'existing_control',
             'valid_dangling']
    for case in cases:
        with tempfile.TemporaryDirectory(prefix='r8-case-', dir=ws/'build/S1-03') as d:
            u, c, j = d+'/u', d+'/c', d+'/j.db'
            target = d+'/real.db'
            if case in ['main_control', 'chain_control', 'existing_control']:
                target = c
            if case == 'main_uart':
                target = u
            if case in ['wal_control', 'shm_control']:
                c = target + ('-wal' if case.startswith('wal') else '-shm')
            if case in ['wal_uart', 'shm_uart']:
                u = target + ('-wal' if case.startswith('wal') else '-shm')
            if case == 'existing_control':
                Path(c).touch()
            if case == 'chain_control':
                os.symlink('c', d+'/link')
                os.symlink('link', j)
            else:
                os.symlink(os.path.basename(target), j)
            p = subprocess.Popen([binary, '--uart', u, '--control', c, '--journal', j],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            clients = []
            try:
                assert select.select([p.stdout], [], [], 5)[0]
                line = p.stdout.readline().strip()
                print(case, 'READY', line.startswith('ready'), flush=True)
                if line.startswith('ready'):
                    deleted = [os.readlink(f) for f in Path('/proc/%d/fd' % p.pid).iterdir()
                               if '(deleted)' in os.readlink(f)]
                    print(' DELETED_FDS', deleted, flush=True)
                    x = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    clients.append(x); x.settimeout(2); x.connect(c)
                    x.sendall(b'restart\n')
                    print(' RESTART', repr(x.recv(1024)), flush=True)
                    print(' SOCKETS_AFTER_RESTART', stat.S_ISSOCK(os.stat(u).st_mode),
                          stat.S_ISSOCK(os.stat(c).st_mode), flush=True)
                    y = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    clients.append(y); y.settimeout(2)
                    try:
                        y.connect(c); print(' RECONNECT OK', flush=True); active = y
                    except OSError as e:
                        print(' RECONNECT_ERRNO', e.errno, flush=True); active = x
                    active.sendall(b'quit\n')
                    print(' QUIT', repr(active.recv(1024)), flush=True)
                print(' EXIT', p.wait(timeout=5), 'STDERR', p.stderr.read().strip(),
                      'AFTER', sorted(os.listdir(d)), flush=True)
            finally:
                for client in clients:
                    client.close()
                if p.poll() is None:
                    p.kill(); p.wait()
        print(' CASE_CLEANED', not Path(d).exists(), flush=True)
print('BUILD_CLEANED', not Path(bd).exists(), flush=True)
PY
```

实跑输出摘要如下；所有 ready 案例 quit 回复均为 `ok`、进程均 exit 0、stderr 为空；所有 CASE_CLEANED 及 BUILD_CLEANED 均为 True。两个构建命令均 exit 0。

| 案例 | ready | 被删除的打开文件（省略 D） | restart | restart 后 UART/control 是 socket | control 重连 | quit 后目录内容 |
|---|---|---|---|---|---|---|
| main_control | True | c (deleted) | error io_error | True / True | OK | c-shm、c-wal、j.db（悬空链接） |
| main_uart | True | u (deleted) | error io_error | True / True | OK | j.db（悬空链接）、u-shm、u-wal |
| wal_control | True | real.db-wal (deleted) | ok | True / False | errno=111 | j.db、real.db |
| shm_control | True | real.db-shm (deleted) | ok | True / False | errno=111 | j.db、real.db |
| wal_uart | True | real.db-wal (deleted) | ok | False / True | OK | j.db、real.db |
| shm_uart | True | real.db-shm (deleted) | ok | False / True | OK | j.db、real.db |
| chain_control | True | c (deleted) | error io_error | True / True | OK | c-shm、c-wal、j.db、link |
| existing_control | False | — | — | — | — | c、j.db 原样保留；exit 2，stderr 为 distinct filesystem entries 错误 |
| valid_dangling | True | 无 | ok | True / True | OK | j.db、real.db |

`main_control`/`main_uart` 的主库在首次 ready 前就已经被 unlink；不是 restart 才触发删除。sidecar 四例则证明“restart 返回 ok”本身也不足以证明两个协议入口仍可达。链式链接同样复现；合法分离的悬空 journal 链接正对照可以正常 restart，排除了“所有符号链接一律不能启动”的错误归因。

## 非阻塞项

| 编号 | 严重度 | **文件:行** | 期望行为 | 实际行为 | 最小复现步骤 | 依据（**文件:行**及原文） |
|---|---|---|---|---|---|---|
| R8-N-1 | P3 | **contracts/include/locker_contracts.h:181**；**host/device/src/port.c:429**；**host/device/README.md:61** | 跨模块原生 reply DTO 的方向位含义由契约负责人统一确定。 | 实现/测试仍使用 `request_type OR 0x80`；生成头只声明 `uint8_t message_type`。README 已明确登记待裁定，不将此实现约定说成共享契约已冻结。延续 R7-N-1。 | ①读生成头 181；②对照 port429；③读 README61；④由契约负责人确认调用方解释。审计未改契约或生成物。 | **docs/architecture/uart-v1.md:13**：“响应为请求 type OR 0x80”是线上规则；**docs/architecture/module-ownership.md:69**：“接口变更必须一起评估调用方”。原生字段此含义**规格未覆盖，需契约负责人裁定**，故不升级阻塞。 |
| R8-N-2 | P3 | **docs/evidence/stage-01/S1-03/handoff.md:5**、**:304**、**:534**；**summary.md:282**；**mutation-batch.py:148** | 当前说明使用当前数字及真实语义；历史数据须明确隔离，不能同时称旧日志为“当前值”。 | handoff5 仍写 daemon 286，当前原始日志是 318；304 仍称“三套消毒器”，303 已正确写为普通构建加两套消毒器；534 写 README 69/7999，实际 71/7999。summary282 在同一当前 §5.4 仍把 51 点位、197711 B/1527 行称“当前值”，与201/203的53点位、203255 B/1593行冲突。mutation 脚本148的 m22 文字仍称队满“latches continuity”，实际 old/new 改的是队满 return，当前正式规则是不锁存日志缺口。主表和原始结果可核对，不据这些残余否定整批证据。 | ①读取上述行；②对照 daemon 日志21的318及实际文件 splitlines=71；③对照 mutation.log 的203255 B/1593 LF与53行点位汇总；④读 sim.c843～861，队满仅丢实时通知。应直接同步当前正文，历史审计原件保持不变。 | **docs/architecture/module-ownership.md:69**：“只把编译改通过而留下旧文档不算完成”；**:71**：“阶段报告保留测试日志索引、提交/文件指纹和运行环境”；**docs/architecture/uart-v1.md:356**：“发送队列满不丢日志”。此项为文档/标签精度，不据此新定产品规则。 |

R7-N-2 中 summary 的范围表、完整活跃树指纹、raw 归档索引等主体已同步；R8-N-2 只登记仍可逐行定位的残余，不把明确标注历史/已作废的旧数字本身判为错误。WSL 历史审计中保留的旧输入叙述也不等于当前仍宣称使用 WSL 验收。

## 已核实通过项

以下每项仅表示注明范围的静态核对、留存原文复算或本轮实际运行通过；不能由此推出整卡、阶段门禁或硬件通过。

1. **入口、状态与依赖。** 读取 AGENTS、根 README、stages/README、stage-plan、本卡58～65行、UART全文及相关 internal-interfaces/module-ownership/traceability。JSON 直接解析得契约0.2.1/REVIEW_DRAFT，active_task/recommended_next=S1-03，S1-03=IN_PROGRESS，前两卡DONE，阶段1 gate=NOT_RUN。机器依赖仅S1-01；S1-02是实际复用，不擅改依赖图。traceability57～65把设备端口初实现分配给S1-03，真实串口后续为S2-06。本轮是既有实现审计，无新架构、依赖下载或选型，适用小改/离线审查的调研豁免。

2. **25文件指纹独立复算。** Python对 source-manifest 的每条完整SHA、字节数核对，25/25匹配，总608862 B；对 round-8 输入的SHA前12位/字节/splitlines逐项比对，零差异。清单2794 B、25条数据加3条注释，与 vm-s103-manifest.sha256 逐字节相同；完整SHA=`6e0e9d04a2b6bd51d764e587e7769dddc71b7be84462a8be0919303fd5668f7e`。未把证据脚本混入25文件分母。

3. **变更范围及前卡复用。** 对 round-7-prompt 的25条旧指纹重算，仅 sim_main.c、daemon_smoke.py、simulator/README.md 三项变化，其他22项相同。S1-01清单中的21个common源码/测试文件全匹配；S1-02的31个非根文件全匹配。根CMake只去掉88/89/95/96四条接入后3943 B，SHA=`f9e1964ee54465f5e29299721a3f342f929cc81e3ff6a6da8d00b77fc34258b4`，精确匹配S1-02清单。SQLite3.54.1/Public Domain、cJSON1.7.19/MIT复用已有锁和 locker_storage_thirdparty，未增加HTTP、密码库或第二份数据库实现。无Git历史，不能据此认证所有过去未留基线的文件从未变化。

4. **VM源码与本地对应。** 经SSH核对 canonical `batches/20261026-032231/src` 的25文件及 contracts/scripts/common/storage 共98个文件完整SHA，98/98相同。guest现存 `/tmp/s103_snapshot.tar.gz` 为83375877 B，SHA=`bbf1f72b352471cc04f2f5de77ac2c88ba5b7bc29174899c21810c433bf5145f`，与run-summary11～12一致；没有重打包或替换旧快照。

5. **归档原件来源。** 本地17个vm-s103日志、manifest和mutation.log共19份，与guest对应canonical/mutation日志逐份完整SHA匹配。snapshot、canonical run、mutation run保持各自来源，没有用本轮测试时长覆盖历史原文。

6. **黄金帧独立复算。** 解析 test_golden_frames.c17～27的四组C字符串并与 uart-golden.json逐字节比较：HELLO61 B/payload49/CRC0xbdd8/线路d8bd；SNAPSHOT28 B/payload16/CRC0xea6e/线路6eea；crc_error重算0xea6e但存6eeb；oversized_header10 B声明513。`binascii.crc_hqx(b"123456789",65535)=0x29b1`，线路b129。wire.c116/217覆盖offset2至payload末，不含magic/CRC。正式黄金帧测试在本轮两套新构建中均PASS。

7. **11组布局和事件大小。** 按 uart-layouts 字段宽度独立求和：HELLO49/71、SNAPSHOT16/189、FETCH49/(85+96N)、ACK48/34、RESUME64/27、DISARM18/27、EXECUTE99/93、QUERY48/93、HEALTH73/27、ANCHOR36/46、RELEASE80/27；N≤4得最大469；event96=96、DEVICE_EVENT=128。对照codec.c14～55/215～274及生成DTO157～182，无struct内存直接上线；错误响应在codec1189～1199严格18 B。

8. **流解析和边界。** wire.c157～243使用固定524 B累积区；magic、>512长度、CRC错误在196/205/219各丢1字节重同步，不按外部长度动态分配；完整帧才复制payload，满缓冲有丢字节出路。sim_main.c307～355复用同一parser，并显式重建LE请求号。正式 test_wire、golden和daemon在本轮CTest均PASS，m2/m3/m4/m47的原始红断言也对应这些行为；不把它当真实UART线路噪声实测。

9. **响应关联与镜像。** port127～142只在session/request_no/type全匹配时消费pending；417仅result=0应用body，185～198核对实际UID与必要capabilities；历史QUERY318～325不覆盖当前会话/boot镜像。正式端口负例1399、1456、1705、2003实际经socket驱动，m7变异使2032/2038变红。`pending_take` 不匹配时自然继续for扫描，没有字面continue也不构成遗漏。reply.message_type方向位只保留R8-N-1。

10. **输入归属、背压、生命周期。** port716/761/846/881核验原意图、守卫和游标归属，1936测试实际close/open后拒旧意图且不写字节；202～213锁存sink拒绝、shutdown，493拒新写。close/destroy在949～992串行停止、join再释放；handoff198/204/243已经写明sink结构按值复制、借用回调参数和destroy后指针失效。正式端口回归本轮PASS，不推断未运行的任意并发交错均无race。

11. **时间与连接截止。** pending115/171使用注入clock，端口测试1619推进时间回收；1590另保留真实时钟烟测。transport109先O_NONBLOCK，119对满队列失败重试，133以统一deadline poll，137取SO_ERROR，143恢复阻塞；1802测试填满accept队列并断言700 ms预算下失败且<3000 ms、不发布connected，本轮PASS。保留OS调度余量，不把注入协议时间与socket连接实时时间混为一谈；m49原始超时另见下文。

12. **会话与动作幂等。** sim1016/2286在decode早退前回显请求session；1114/1122及1940落实HELLO版本、同session+host幂等、其他旧session拒绝。sim1643比较全部不可变意图，1667核验跨session command，1686严格水位+1。正式protocol657/1244、journal跨open/close、port/daemon均实际参与本轮CTest，未把codec字段合法性测试冒充状态机覆盖；slot/kind首先由decoder拒绝。

13. **恢复屏障、健康与驱动前检查。** sim1396～1415检查持久屏障、fetched/ACK覆盖、活动上下文、日志、输入和健康；1703要求READY，1741及1756两次提交后重新采时与检查。journal744～803的jump3/5夹具确实跨两个COMMIT返回点推进，断言消耗水位但无脉冲；790～797按20位TEXT核对COMMAND_ACCEPTED detail=action_no。protocol1176单独检查连续补取及新鲜Core。上述正式测试本轮PASS。

14. **独立持久化和重开。** sim266～289仅管理sim_*表，2132只开配置的journal，WAL+FULL、BEGIN IMMEDIATE；没有调用Repository业务端口或默认打开业务库。sim2057恢复活动意图，1897先持久释放再清内存且detail=0；2168重开door/item=UNKNOWN，2179递增boot，BOOT seq=1，2186重算边界，历史command可查询。journal557/836含真正destroy/create/open，非只看内存计数。本项仅限分离路径正常场景；R8-B-1已证明配置身份检查仍有破口。

15. **提交失败和不确定结果。** sim415对提交故障ROLLBACK，966包装事务失败后标FAULT；1730提交成功前不设水位/has_active。journal488～539对BEGIN/INTENT/EVENT/COMMIT/RESULT逐类注入：COMMIT从READY路径进入真实事务而非被fail_journal前置挡掉，核对SQL行/水位/活动上下文；RESULT失败保留已接纳意图且不造脉冲，随后再动作LOG_UNAVAILABLE。正式461-check journal测试本轮两套新构建PASS。ACK不代表开门，持久意图不代表已驱动。

16. **丢ACK与脉冲独立。** sim2289分别控制type4响应及成功type7响应，suppress_pulse独立；1575推进模拟脉冲结束，驱动状态先关闭再写结束记录。protocol1320、port1656和daemon中EXECUTE/QUERY场景分别覆盖丢回复后只查询及脉冲是否发生。未将仅ACK_PERSISTED丢回复当作EXECUTE丢ACK；也未以suppress_pulse替代正常结束/释放路径。

17. **缺口与实时队列。** sim843～861对无会话或32通知满仅丢实时通知，已提交日志保留；protocol990核对34行日志、32次pop、第33次NOT_READY、dropped=0。真正缺口在2488持久锁存continuity=false；1318～1323此时所有页gap=1，1356拒跨缺口ACK，不再有silent_gap豁免，RESUME不清锁存。符合UART50/356/370；m22/m23/m41/m42均有对应红断言。m22的旧文字标签归R8-N-2，不据标签否认实际实现。

18. **私有控制与测试职责。** sim_main181～198先收窄umask再bind、chmod0600；865实际含 `device backend: simulator`。daemon_smoke通过subprocess.Popen启动独立程序，实际stat两个socket；控制输入有整token、双hex、溢出/NUL/超长丢到换行处理，错误类来自私有status_token，不擅加wire_result。没有公共HTTP模拟路由、HAL/GPIO或直接账本写者。contract CMake43在缺Python时FATAL_ERROR；三处FETCH夹具均有LOCKER_TEST_PAGE_LIMIT=64及越界失败，正式fixture和故障注入不是临时残留。

19. **round-6长度与round-7普通别名回归。** sim_main788/801/805/815都在create828/open832/listen844/850前；默认99 B control合法、100/107 B默认journal拒绝，显式短journal可支持107 B control。daemon316及468的正式用例本轮随两套新CTest运行通过，七个别名拒绝案例不仅检查exit2，还检查 `os.listdir(case_dir)` 为空（500～501）。另用canonical binary独立实跑：alias_control、alias_uart、wal_collision、shm_collision、same_sockets、same_alias_sockets均exit2且目录空；父目录symlink别名exit2且原有link不变；正常分离路径ready/restart/quit成功。这些通过不覆盖末级悬空链接，见R8-B-1。

20. **realpath特性宏实编译。** sim_main22为 `_XOPEN_SOURCE 700`。本轮在临时目录分别用Debug和RelWithDebInfo、`LOCKER_WARNINGS_AS_ERRORS=ON`全新configure/build，均exit0；保留根CMake的C11及C_EXTENSIONS OFF，且两套完整19/19 CTest均exit0。可据此确认本guest上realpath/sigaction/umask及socket接口声明可见，无隐式声明问题；不扩展为所有libc/平台保证。

21. **canonical历史数字逐原文核对。** `vm-s103-ctest.log:41/43/44`=19/19、10.08 s、exit0；unit-tests35的3551可由前17条C checks求和（公共505、仓储1212、设备四项1277、契约两项557）；另一个CTest是schema drift脚本，另一个是daemon，不能都计作C断言。daemon21～24=318/0/exit0；ASan日志41～45=19/19、11.79 s、exit0、reports0；TSan同位置=19/19、12.67 s、exit0、hits0。source-verify26～31=25/25、failed0、stray0、SRC_FILES1166。文档残余旧值见R8-N-2。

22. **活跃树与门禁历史证据。** extract5～10与run-summary7～14的前后文件数均1158，完整内容指纹均 `c1fb320ab195b80fc40ec05fdc18772c9562767f27a6816e2241e4733da13a6a`。脚本live_tree_fp确实读取每个文件内容SHA，解包前和测试后各取一次；15次need_zero加live-tree/design共17门禁。gates日志17行GATE_OK，与run-summary16～32逐字一致；33确实是VM_RUN_VERDICT=PASS。解包只进入独立batches，不含rm -rf活跃源。此处复核历史采样记录，不声称本轮重新执行了整套证据脚本或重现历史时刻的活跃树。

23. **mutation日志与raw全部复算。** mutation.log=203255 B、1593个LF、以LF结束，SHA=`27958b1186cb5ac96a90e8b9403dbf1a88ccd0120a12fad394e0e95cc92fa51a`；raw归档21271 B、SHA=`7da6e2735223956c59fd841e46422a3a90b2dde2cbd79cbe3089334ae6db2121`，54个tar条目=目录+53文件。内存读取每份未过滤stdout，独立重算C失败行、完整summary分母、daemon红数、timeout和exit，与53条汇总全部一致：CAUGHT49、CAUGHT_TIMEOUT1、NOT_CAUGHT3、构建失败0。49个CAUGHT均有红断言或daemon红检查，没有把编译失败冒充捕获。逐点查看失败位置与目标行为可对应，例如m7=端口2032/2038，m39=协议1398/1402，m44=两socket0600，m47=请求号回显，m51/52/53=对应路径拒绝检查。

24. **超时及新路径变异原件。** m49 raw确含 `18/19 Test #18: contract.device.test_device_port .....***Timeout 120.10 sec`，ctest=8、C失败0、daemon红0、hung1，归CAUGHT_TIMEOUT；不是外层exit124，也不是红断言。m43原文ctest=8、`***Timeout 120.01 sec`，C失败646=210+354+2+80，完整汇总3份/分母1253，daemon红5、hung1，按有红优先归CAUGHT。m50/m51/m52/m53的daemon红数分别7/32/12/8，与raw一致。这些数来自留存033000批次，本轮未重新跑53个mutation。

25. **53个锚点与变异源。** Windows实际执行 `python -B docs/evidence/stage-01/S1-03/mutation-batch.py --check --src .`，exit0、points53/bad0。另通过AST只读POINTS，全部old唯一，全部ref行号等于old首行；包括m43=2179、m44=193、m51=815、m52=120、m53=157。guest留存 `build/S1-03/mutation/20261026-033000/src/<point>` 的53棵树，每棵25文件共1325次比较全部符合“只替换目标old一次”。目标文本按Python读写的CRLF→LF规则比较，其余原始字节比较；没有把换行转换误报额外变异。mutation来源批次023708与canonical032231受管源码一致。

26. **三个NOT_CAUGHT逐项裁定。** m24删除handler版本检查，但codec836已拒requested_version≠1，dispatch1934早退，线上可达行为相同。m28删除through>last检查，在健康可达日志中fetched≤last：open清fetched、HELLO用合法ACK初始化，FETCH1324只推进至page.next_after≤last；1356仍对越界through返回同一个CURSOR_GAP。m31把水位比较≤改为<，相等W>0时该session的W行仍保留，1643先返回原状态或REPLAY_BLOCKED，不能到1687；W=0/action0则先被decoder拒绝。三项只是在当前公开入口、正常API可达状态及不回收command明细条件下等价，不代表任意数据库篡改/未来回收后的行为仍等价；不把3个生存点记为捕获。

27. **四项Windows设计检查实跑。** `python -B scripts/check_contracts.py`、`check_stage_plan.py`、`check_vision_contracts.py`、`check_feasibility.py`均exit0/PASS。contract输出24路由/82端口/11 UART请求/4向量/max469，stage输出45卡/active=S1-03/done2；hardware仍false或UNVERIFIED。Windows的可选pycparser实际PASS(parse only)，归档VM输出NOT_RUN(optional pycparser unavailable)，环境差异有正文说明，不混称C编译结果。

28. **历史整改逐组复核。** round-1 B1～6对应上述关联/镜像/绑定/身份/号/背压，B7～10对应恢复/截止/脉冲/水位/重启，B11～14对应丢ACK/通知/缺口/daemon解析，B15～16对应mutation及独立批次；这些原反例有实际实现分支和正式回归，均参与本轮新CTest。round-2 B2～5的请求session回显、gap、release detail和注入clock均核对；B6生命周期旧保证已删除。round-3 B1的detail SQL断言成立；round-3 B2/round-4 B1的当前再生成入口确为Python，未再把历史表中的旧Bash命令当现行指令；B3完整散列已复算。round-5连接截止、round-6长度和raw留存、round-7字面/父目录别名均按上文核实。原N类的异步推送、严格控制解析、有界写、SQL宽度/错误、缺Python失败及无关固定unlink已具备相应实现/测试；两个诊断COUNT失败仍可返回0，未声称所有SQL诊断都有独立错误通道。未关闭的原生DTO约定保留N1，文档残余保留N2；路径隔离新反例为B1。

29. **残留与WSL边界。** 当前本卡目录不存在evidence-run-wsl.sh、wsl-*.log、wsl-run-id.txt、joint-fix-round1.md，summary/handoff明确排除WSL证据；历史审计原件保持原文。未在源码目录写db/socket/构建/pyc或临时脚本，未删正式测试、fixture、日志或审计输入；本轮guest的r8路径探针和全新构建TemporaryDirectory均已自动清理。启动/收尾对1671个非audit-round8前缀文件做完整SHA比较，零变化、零新增；本轮审计写入仅本报告。当前文档需改处只登记N2，遵守只写审计报告边界，不代开发方同步其他文件。没有为凑预测的51个文件伪造runner日志。

## 环境与 NOT_RUN

本轮实际主机为Windows PowerShell，Python来自既有环境；`git status --short`实际报告“not a git repository”（exit128），没有可用Git历史可供审计。SSH实际返回 `Linux ubuntu 7.3.0-rc5 x86_64 GNU/Linux`，沿用用户现有guest，不用WSL、不安装依赖、不改网络配置。

| 操作 | 本轮实际结果与范围 |
|---|---|
| 全新Debug configure/build/CTest | 在build/S1-03/r8-build-*临时目录；C11/EXTENSIONS OFF、HOST/TESTS/WARNINGS_AS_ERRORS=ON；configure/build/ctest全exit0，19/19，10.07 s |
| 全新RelWithDebInfo configure/build/CTest | 同上独立子目录，全exit0，19/19，9.74 s |
| 既有ASan+UBSan构建CTest复跑 | `ctest --test-dir ~/locker-workspace/build/S1-03/20261026-032231-asan --output-on-failure`，exit0、19/19、17.64 s；读取本次Testing/Temporary/LastTest.log，无ASan/LeakSanitizer/runtime error命中，含daemon318 checks |
| 既有TSan构建CTest复跑 | 对应-tsan目录，exit0、19/19、14.07 s；本次LastTest.log无ThreadSanitizer warning，含daemon318 checks。未另改sanitizer环境变量，未将其称为一轮新消毒器构建 |
| daemon正式用例 | 上述四次CTest都实际启动独立daemon，非import；318检查来自原始canonical日志和本次两套消毒器LastTest原文；全新普通构建CTest也实际执行该用例 |
| daemon路径定点实验 | canonical目标及独立新编译目标均实际执行；新编译目标的完整复现/输出见B1。新构建、案例目录均清理 |
| Windows四设计检查、mutation --check、SHA/CRC/布局/原始日志复算 | 均已实际执行；不是Linux运行或产品门禁 |
| 本轮完整53点mutation configure/build/CTest | **NOT_RUN**。本轮执行的是check-only、53份既有raw独立复算及1325次留存变异源码比较，没有把历史50/53改称本轮重跑 |
| 新ASan/UBSan、TSan configure/build | **NOT_RUN**。复用了既有消毒器构建；本轮新configure/build仅普通Debug、RelWithDebInfo及B1专用Debug目标 |
| evidence-run-vm.sh主体、重打快照/清单、重生成canonical证据 | **NOT_RUN**。只读核验脚本和原件，临时复验输出记录在本报告及会话工具输出，不覆盖17份canonical日志 |
| 真UART、115200/8N1电气/线路噪声/DMA/物理断线 | **NOT_RUN**。AF_UNIX字节正确不证明实物串口 |
| ARM/32位交叉编译、STM32 HAL/Keil、GPIO脉冲硬截止 | **NOT_RUN**。本轮仅x86_64主机模拟，硬件脉冲只模拟计数；不引用环境目标冒充本卡实物结果 |
| MCU/littlefs/RK介质掉电、锁柜与业务/客户端闭环 | **NOT_RUN**。进程SQLite重开不等同介质掉电；后续卡未开发 |
| CRC抗篡改/身份认证 | **NOT_RUN**且不作能力承诺；UART21明确CRC不承担身份认证 |
| hardware profile / WSL | profile仍**UNVERIFIED**；WSL **NOT_RUN**，用户明确排除该证据来源 |

完整复验命令为 `cmake -S <canonical-src> -B <temporary-build> -DCMAKE_BUILD_TYPE=<Debug或RelWithDebInfo> -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON`，随后 `cmake --build <temporary-build> -j4`、`ctest --test-dir <temporary-build> --output-on-failure`。两套新构建依次执行；消毒器复跑使用既有构建，不与同一构建目录中的其他CTest并发。临时构建清理输出为 `TEMP_CLEANED True`。

本报告支持继续整改R8-B-1、同步R8-N-2并由契约负责人处理R8-N-1；不支持把S1-03置DONE。正式测试及历史门禁记录的PASS保持其各自测试范围，不能覆盖本轮已经实测的主库/协议入口破坏。
