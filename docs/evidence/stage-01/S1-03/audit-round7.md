# S1-03 round-7 独立审计
结论：FAIL
阻塞项：2 项；非阻塞项：2 项

审计日期：2026-10-26。基线为契约 0.2.1 / REVIEW_DRAFT；S1-03 仍为 IN_PROGRESS。只读审查当前代码、正式测试、round-1～round-6 报告和留存 VMware 证据，唯一交付文件为本报告；未修实现、修改状态、commit 或 push。

本轮 SSH 实际可用，已运行 canonical 批次已有的六个设备测试二进制、独立 daemon 冒烟和隔离路径探针。FAIL 来自探针复现的启动路径资源冲突，不来自 Windows 的环境限制。round-6 的默认路径截断反例已经修复，但新增的字符串撞名检查没有完整隔离数据库及两个 socket 的文件系统对象。

## 阻塞项

| 编号 | 严重度 | **文件:行** | 期望行为 | 实际行为 | 最小复现步骤 | 依据（规格的**文件:行**原文） |
|---|---|---|---|---|---|---|
| R7-B-1 | P2 | **host/simulator/src/sim_main.c:713**、**:731**、**:743**、**:749**；实际删除在 **:87**；WAL 配置 **host/simulator/src/sim.c:2142** | 启动成功后，独立 SQLite 日志及其 WAL 文件不能被 socket 创建路径覆盖；模拟重启仍能重开该日志，并继续提供控制入口。冲突配置应在资源创建前拒绝。 | 守卫仅比较 journal 主文件与 socket 路径的原始字符串。journal=`D/./c`、control=`D/c` 时比较不等，但指向同一目录项；先开库，再 unlink/control-bind，删除了自己的主库，仍打印 ready，restart 返回 `error io_error`。journal=`D/./u` 同样破坏主库。另一个可复现分支是 control=`D/j.db-wal`、journal=`D/j.db`：WAL 被控制 socket 替换，restart 虽返回 ok，却将控制路径重新变成普通 WAL 文件，新控制连接得到 ECONNREFUSED。 | ①在 guest 全新临时目录 D 启动 canonical binary，uart=D/u、control=D/c、journal=D/./c；②确认 ready 后发 restart；③得到 error io_error，quit 后仅剩 c-wal/c-shm，主库不在；④改为 journal=D/j.db、control=D/j.db-wal，restart 后检查控制路径已非 socket，重新 connect 得 errno=111。下方探针可直接执行，含独立正常对照。 | **docs/stages/stage-01-task-cards.md:62**：“模拟设备使用独立 SQLite 文件保存设备日志/动作水位”；**:63**：“模拟器重启后动作查询及事件键仍可核对”；**docs/architecture/module-ownership.md:54**：“模拟器独占自己的设备模型和模拟持久化文件”；当前模块说明 **host/simulator/README.md:20**：“三个路径在创建任何数据库或 socket 之前校验”。这些反例直接破坏持久化/重启或控制入口，不是要求新增一种未规定的业务能力。 |
| R7-B-2 | P2 | **host/simulator/src/sim_main.c:690**、**:713**、**:743**、**:749**、**:764**；**:87** | UART 字节入口和“另用”的控制入口应分别可达；冲突参数不能被报告为两个入口 ready。 | 未检查 uart 与 control 是否指向同一个 socket 目录项。传两个完全相同的短路径且 journal 独立时，第二次 listen_unix 会 unlink 第一个 UART socket，再在同一路径 bind 控制 socket。两个 listen 均成功并打印 ready，但任何按配置连接 UART 的客户端实际进入文本控制解析器；合法黄金 HELLO 收到 ASCII error bad_argument，没有 0x81 响应。 | ①全新目录 D；②启动 `locker-device-sim --uart D/c --control D/c --journal D/j.db`，实际打印 ready；③连接 D/c，发送 uart-golden.json 的 hello_request.frame_hex；④收到 `error bad_argument\n` 文本而非 UART-v1 帧。下方 same_sockets 案例即为本轮运行的复现。 | **docs/stages/stage-01-task-cards.md:62**：“经 AF_UNIX SOCK_STREAM 提供 UART-v1 字节，另用权限 0600 的私有控制入口”；**docs/architecture/module-ownership.md:55**：“测试控制走另一条权限 0600 的本地控制入口”；**docs/architecture/uart-v1.md:11**：“固定十六进制 4c 4b”。本项依据是两个实际可用的独立入口，而非自行规定 CLI 错误码。 |

两个阻塞项的修复范围均是 daemon 的启动路径隔离。R7-B-1 关注持久化文件族与 socket 冲突；R7-B-2 关注两个协议入口互相覆盖。无需扩展公共契约，也不应只增加针对字符串 `/./` 的单个特判。

建议一次完成的回归边界：保留已通过的 99/100/107 字节边界；对 journal 主文件与任一 socket 的同址别名、journal 的 -wal/-shm 与任一 socket 的冲突、uart/control 的同址及别名配置分别覆盖；拒绝时不创建或删除资源。合法分离路径仍须完成 HELLO、restart、断开后重连与 quit。别名处理的具体实现可以由开发方选择，但必须阻止上述已经实测的对象覆盖；本报告没有证明所有符号链接/并发改名情形均已覆盖。

以下是本轮实际执行过的合并探针。在 VMware guest 的 Bash 执行；仅使用指定 canonical binary，为每个案例在 build/S1-03 下创建独立 TemporaryDirectory，finally 关闭客户端/子进程，退出后清理临时目录，不覆盖 canonical 日志：

~~~bash
python3 -B - <<'PY'
from pathlib import Path
import os, socket, stat, subprocess, tempfile, select
ws = Path.home() / "locker-workspace"
binary = ws / "build/S1-03/20261026-010653/host/simulator/locker-device-sim"
hello = bytes.fromhex(
    "4c4b0101310001000000000000000000000000000000000000000000000000004000800000000000000a"
    "0000000000004000800000000000000b01d8bd")
for case in ("separate", "alias_control", "alias_uart", "wal_collision", "same_sockets"):
    with tempfile.TemporaryDirectory(prefix="r7-proof-", dir=ws / "build/S1-03") as tmp:
        uart, control, journal = tmp + "/u", tmp + "/c", tmp + "/j.db"
        if case == "alias_control": journal = tmp + "/./c"
        if case == "alias_uart": journal = tmp + "/./u"
        if case == "wal_collision": control = journal + "-wal"
        if case == "same_sockets": uart = control
        proc = subprocess.Popen(
            [str(binary), "--uart", uart, "--control", control, "--journal", journal],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            assert select.select([proc.stdout], [], [], 4)[0], "no startup output"
            ready = proc.stdout.readline().strip()
            print(case, "READY", ready.startswith("ready device backend: simulator"), flush=True)
            u = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            clients.append(u); u.settimeout(2); u.connect(uart); u.sendall(hello)
            reply = u.recv(4096)
            print("  UART", reply[:4].hex(), repr(reply[:36]), flush=True)
            c = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            clients.append(c); c.settimeout(2); c.connect(control)
            c.sendall(b"restart\n")
            print("  RESTART", repr(c.recv(4096)), flush=True)
            is_socket = os.path.exists(control) and stat.S_ISSOCK(os.stat(control).st_mode)
            print("  CONTROL_SOCKET_AFTER_RESTART", is_socket, flush=True)
            r = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            clients.append(r); r.settimeout(2)
            try:
                r.connect(control)
                print("  CONTROL_RECONNECT OK", flush=True)
                active = r
            except OSError as exc:
                print("  CONTROL_RECONNECT errno=" + str(exc.errno), flush=True)
                active = c
            active.sendall(b"quit\n")
            print("  QUIT", repr(active.recv(4096)), flush=True)
            print("  EXIT", proc.wait(timeout=4), "STDERR", repr(proc.stderr.read()), flush=True)
            print("  FILES", sorted(p.name for p in Path(tmp).iterdir()), flush=True)
        finally:
            for client in clients: client.close()
            if proc.poll() is None: proc.kill(); proc.wait()
    print("  TEMP_CLEANED", not Path(tmp).exists(), flush=True)
PY
~~~

本轮输出要点（路径中的随机临时目录名省略）：

| 案例 | 启动 | 黄金 HELLO | restart | restart 后控制路径/重连 | quit 后剩余文件 |
|---|---|---|---|---|---|
| separate | ready | 首4字节 4c4b0181 | ok | socket=True；重连 OK | j.db |
| alias_control | ready | 4c4b0181 | error io_error | 原控制 socket 仍可达 | c-shm、c-wal；无主库 |
| alias_uart | ready | 4c4b0181 | error io_error | 控制 socket 仍可达 | u-shm、u-wal；无主库 |
| wal_collision | ready | 4c4b0181 | ok | socket=False；重连 errno=111 | j.db |
| same_sockets | ready | 首4字节 6572726f，即 ASCII erro；文本 error bad_argument | ok | 控制 socket 可达，UART 不可达 | j.db |

五例均 EXIT=0、STDERR=''、TEMP_CLEANED=True。正常退出码不表示初始化后的两种协议和日志资源正确。

## 非阻塞项

| 编号 | 严重度 | **文件:行** | 期望行为 | 实际行为 | 最小复现步骤 | 依据（**文件:行**及原文） |
|---|---|---|---|---|---|---|
| R7-N-1 | P3 | **contracts/include/locker_contracts.h:181**；**host/device/src/port.c:429**；**host/device/README.md:61** | 跨模块原生 reply DTO 的 message_type 方向位语义由共享契约统一。 | 实现和正式测试使用 request_type OR 0x80；生成头仅声明 uint8_t，README 明确登记待契约负责人裁定。保留前几轮非阻塞项，不将线上规则自动扩展为原生 DTO 的明文要求。 | ①读生成头181；②对照port429；③读README61，确认仍待裁定。 | **uart-v1.md:13**：“响应为请求 type OR 0x80”；**module-ownership.md:11**：“由架构负责人统一修改真源”。原生 DTO 此字段**规格未覆盖，需契约负责人裁定**。本卡不得手改生成头。 |
| R7-N-2 | P3 | **docs/evidence/stage-01/S1-03/summary.md:62**、**:65**、**:133**、**:203**；**handoff.md:513** | 当前执行报告使用当前文件大小、散列和原始输出留存事实，历史数字明确隔离。 | summary范围表仍写 simulator README=67行/6620B、sim_main=827行/27930B，实际69行/7232B、855行/29174B；133行散列缩写以 e067 结尾，原日志实际以10a5结尾；203行仍称“该段没有保留完整 CTest 输出”，但当前归档51份完整stdout已可核对。handoff513所写活跃树 e0a1d88b…e067 也不对应当前日志。主结果计数、完整源码清单和完整散列均可核对，因此不据此否定整个运行证据。 | ①Python read_bytes 计字节/换行；②对比 vm-s103-extract.log:6/:8 的完整散列；③只读打开 mutation-raw-ctest.tgz 中 m43 原文；④对照上述报告行。 | **AGENTS.md:36**：“正文写当前真实规则，不在顶部累积补丁说明”；**module-ownership.md:71**：“阶段报告保留测试日志索引、提交/文件指纹和运行环境”。建议改正文中的现值，历史审计原件保持不变。 |

## 已核实通过项

以下结论限于逐项注明的静态检查、原始日志复算或本轮运行，不表示整卡/阶段/硬件通过。

1. **状态与范围核对**：直接解析 contract-source 和 stage-plan，契约为0.2.1/REVIEW_DRAFT；active_task/recommended_next=S1-03，S1-03=IN_PROGRESS/report=null，S1-01/S1-02=DONE，阶段1 gate=NOT_RUN。README与阶段入口保持这一状态。机器依赖边只有S1-01，S1-02为实际复用，不擅改依赖图。

2. **25文件完整指纹核对**：逐条解析 source-manifest.txt 的“sha256、字节数、路径”三字段，与本地原始字节重新计算比较，25/25匹配，合计599690B；与本轮输入的25个SHA前12位/字节/LF数一致。清单2794B，完整SHA256=`f5c1c1388eff5157f7f4c08c81a8019a8bddfad7455ade983691ff2e1e6d1d27`，与 vm-s103-manifest.sha256 逐字节相同。对 round-6-prompt 的25项旧指纹重新比对，只有 sim_main.c、daemon_smoke.py、simulator/README.md 三项改变，其余22项相同；未把证据脚本混进25项分母。

3. **前卡复用核对**：S1-01清单中的21个common源码/测试文件匹配；S1-02清单31个非根文件匹配。根CMake去掉88/89/95/96四行后3943B，SHA256=`f9e1964ee54465f5e29299721a3f342f929cc81e3ff6a6da8d00b77fc34258b4`，与S1-02原清单相同。当前 simulator CMake:27～32复用locker_storage_thirdparty；依赖锁记录SQLite3.54.1/Public Domain及cJSON1.7.19/MIT，无本卡新下载、自研HTTP/密码/数据库或浮动master。这里只证明留存基线匹配，不以无Git工作区认证全部历史修改过程。

4. **Windows与VM来源核对**：SSH成功返回 Linux7.3.0-rc5 x86_64。对 `batches/20261026-010653/src` 核对25文件及contracts/scripts/common/storage，共98个文件核对项，全部完整SHA匹配。17个vm-s103-*.log、manifest与guest canonical logs原件逐份完整SHA一致；guest mutation.log及mutation-batch.py也与本地一致。读取guest现存 /tmp/s103_snapshot.tar.gz，82740974B，完整SHA256=`2c29b564eb6e43bfac84034a067d3bc3c5017cb92bb77bc27cf060cb8386af48`；与run-summary:11～12一致。未重打包快照。

5. **四黄金帧和CRC独立复算**：Python拼接 test_golden_frames.c:17～27 的C字符串，与JSON四个frame_hex逐字节一致。HELLO61B、payload49、CRC0xbdd8/线路d8bd；SNAPSHOT28B、payload16、CRC0xea6e/线路6eea；crc_error重算0xea6e但存6eeb；oversized_header10B声明513。binascii.crc_hqx(b"123456789",65535)=0x29b1。wire.c:116/:217覆盖offset2至payload末，不含magic和CRC。黄金帧C测试本轮实际168checks PASS。

6. **布局独立复算**：从uart-layouts.json按字段宽度求和，11对payload依次为HELLO49/71、SNAPSHOT16/189、FETCH49/(85+96N)、ACK48/34、RESUME64/27、DISARM18/27、EXECUTE99/93、QUERY48/93、HEALTH73/27、ANCHOR36/46、RELEASE80/27。N≤4故最大469；event96=96、DEVICE_EVENT=128。与codec.c:14～55的常量/表达式、生成DTO及UART-v1一致；非零result响应严格18B（codec.c:1189～1199）。未用C struct内存布局直接上线。

7. **流式边界与daemon同源解析**：wire.c:169缓冲524B；magic不符、长度>512、CRC错误分别在196/205/219分支丢1字节重同步；够完整帧才复制payload，满缓冲也有丢字节出路。sim_main.c:241～254直接调用同一stream_feed，service_on_frame:215～228重建头部及LE请求号。test_wire实际130checks PASS，daemon覆盖损坏长度嵌套合法帧及实时控制事件接收；不能把这一结果等同真实串口噪声验证。

8. **响应关联、输入归属及背压**：port.c:127～142仅在session/request_no/type三者匹配时消费pending；:417只在设备result=0应用body；:185～197验证实际UID及必要capabilities；:716、:846、:883核验原始意图/guard绑定；:761～773校验游标设备与epoch。历史QUERY仅在原command会话/boot匹配时更新镜像（:318～325）。:202～213锁存sink拒绝并shutdown，发送入口:493拒绝新动作/健康。相关socket回归包含外来响应后再收真响应、错误镜像、旧意图重连、sink拒绝、历史查询、请求号超时/回绕；本轮test_device_port实际389checks PASS。pending超时使用注入clock（:115/:171），正式真实时钟烟测为test_device_port.c:1590。

9. **会话/恢复/动作防线**：sim.c:1122同session+host HELLO幂等；:1940拒绝旧session；:1396～1415检查已落库边界、活动上下文、日志、物理输入和Core健康；:1643比较全部不可变意图；:1686严格水位+1；:1703要求READY；:1741/:1756持久提交后重新采时并检查驱动条件。COMMAND_ACCEPTED detail在:1528/:1538/:1769保留action_no，journal测试:790～797用20位TEXT SQL核验。protocol实际507checks PASS；不是仅把旧的错误测试预期改成绿灯。

10. **持久化、失败和ACK独立性**：sim.c:266～289的sim_*表只写config.journal_path，WAL+FULL、BEGIN IMMEDIATE、commit失败ROLLBACK（:402～425）；接纳/水位/事件同事务，:1690日志故障拒绝新动作。:2100恢复活动意图，:2168重开后door/item=UNKNOWN，:2179递增boot；release:1897提交清空检查点，detail=0。:2289分别控制ACK_PERSISTED与EXECUTE响应丢失；suppress_pulse与它们独立，:1575提供PULSE_FINISHED模拟转换。正式journal461checks、protocol507checks、port389checks和daemon286checks本轮均实际通过，覆盖提交故障、结果故障、释放重开及丢ACK后只QUERY；这些正常场景不消除本轮启动路径反例。

11. **缺口与通知队列**：sim.c:843～860在无session或32通知满时不入实时队列，已提交日志保留，不伪造历史丢失；:2482～2490持久锁存真正丢失；:1318～1323对continuity_ok=false报告gap=1，没有silent_gap豁免；:1356拒绝跨缺口ACK。两种场景正式测试及对应m22/m23/m41/m42变异均有可辨别断言。原生message_type保留R7-N-1；pending_take没有字面continue；初始化明确场景与重开UNKNOWN分开；status_token语法/引擎错误分类是私有控制协议，不擅加公共wire_result。

12. **0600及模块边界**：sim_main.c:88/:95先umask0077再chmod0600，本轮daemon实测两个socket均0600，启动行含device backend: simulator。相关CMake及实现没有HTTP路由、HAL/GPIO驱动或业务Repository写入。sim_main未链接进C测试，正式daemon_smoke确实启动独立进程；tests/contract/device/CMakeLists.txt:42～43在缺Python时FATAL_ERROR，不会靠少注册测试变绿。LOCKER_TEST_PAGE_LIMIT限制分页夹具；旧固定wal_cleanup无关unlink已不存在。

13. **原始VM运行数字独立复核**：canonical run_id=20261026-010653；三份CTest原文各19/19，普通10.00s、ASan+UBSan12.20s/ASAN_REPORTS=0、TSan12.61s/TSAN_HITS=0。逐条C测试checks相加3551，daemon原文286/0failures。source-verify为25/25、SRC_STRAY_ARTIFACTS=0、SRC_FILES=1158。extract原文live_tree_files_before/after均1155；两份完整指纹均为`0e8a60f2c0e7e4f3b15d010cdf58983291d3eb78cb423f7d5cffc0e9686b10a5`。脚本15次need_zero，加live-tree和design共17门禁；gates.log实际17个GATE_OK，run-summary内联同17行并以VM_RUN_VERDICT=PASS结尾。指纹函数在解包前后读取文件内容，含文件数双条件；不是仅凭一次计数或路径+大小证明未改活跃树。上述19/19等是留存批次证据，本轮未重跑全量CTest。

14. **mutation的51份原文逐点复算**：mutation.log=197711B、1527个LF且以LF结尾，完整SHA256=`1cc351b7dd6ef7cab1b821e13a63f1d1ddafe9ad3cbac716619220b20d1eff6f`。从mutation-raw-ctest.tgz只读解压到内存，51个文件，归档19322B，完整SHA256=`e7714025030c518fd817c136402706884df7ad84d56d3973bff4dc73c44d77ae`。逐份重数C失败行、完整summary个数/分母、daemon红检查、超时标记及退出码，与mutation.log的51行汇总全部一致：CAUGHT47、CAUGHT_TIMEOUT1、NOT_CAUGHT3、CONFIGURE/BUILD_FAILED0。不是重跑full后得到的新计数。

15. **变异源确实与脚本对应**：本轮Windows `mutation-batch.py --check --src .` 实际exit0、51个CHECK_OK、bad=0。通过AST只读取POINTS，对guest留存51棵变异源的每棵25文件比较，1275/1275符合“原文件复制，仅目标按old→new替换一次”。目标文件按脚本Python文本读写的通用换行规则比较，不能将CRLF转LF误判为额外变异。原文的CTest路径为mutation/20261026-011500；来源为canonical batches/20261026-010653/src。没有把构建失败当非空洞捕获。

mutation重点的独立复算结果：

| 点位 | C失败行 / 完整汇总数 / 汇总分母 / daemon红 | 原文与结论 |
|---|---|---|
| m7_pending_type | 2 / 1 / 389 / 0 | 正式同号异类型响应不能消费pending；有真实反例断言 |
| m22_queue_full | 1 / 1 / 507 / 0 | 第33次pop必须NOT_READY，非仅检查恒真状态 |
| m24_hello_version | 0 / 0 / 0 / 0 | NOT_CAUGHT；codec.c:836先拒版本，sim_dispatch:1934早退，不能到handler:1114的重复检查。仅当前调用图等价 |
| m28_ack_upper | 0 / 0 / 0 / 0 | NOT_CAUGHT；初始ACK=0，HELLO令fetched=ACK，合法页只推进至last；ACK即使删上界也仍受through≤fetched约束，可归纳ACK≤fetched≤last，through>last必被:1356同码拒绝。论证不以被删守卫本身为前提；限健康、引擎维护的日志 |
| m31_execute_replay | 0 / 0 / 0 / 0 | NOT_CAUGHT；action0先被codec拒，当前W>0有保留的该session/action行，相等请求先进入existing完整比较分支，不到:1687等号差异。仅当前不回收命令明细实现等价，不批准未来删水位防线 |
| m43_boot_increment | 646 / 3 / 1253 / 5 | CAUGHT且hung=1。逐文件210(journal)+354(protocol)+2(golden)+80(port)=646；完整汇总只有前三个，566/1253，不能将1253称646的完整分母。raw中port确有***Timeout 120.01sec |
| m49_connect_nonblock | 0 / 0 / 0 / 0 | CAUGHT_TIMEOUT，ctest=8/hung=1；raw明确18/19的contract.device.test_device_port为***Timeout 120.10sec，整次ctest_seconds=124.27；不是外层timeout300的exit124 |
| m50_journal_default_len | 0 / 0 / 0 / 7 | CAUGHT；实际7个daemon红检查，configure/build=0；证明长度守卫回归可区分，但未覆盖路径别名 |
| m51_journal_collision | 0 / 0 / 0 / 4 | CAUGHT；实际4个daemon红检查，configure/build=0；覆盖字面同名，不证明文件系统同址已隔离 |

其余42个点位的C/daemon计数、完整summary分母与hung字段也已逐一从raw复算匹配。脚本:404覆盖ctest ***Timeout与外层124；:453～464先排构建失败、红失败优先，再判无红超时。m43保留CAUGHT、m49单列CAUGHT_TIMEOUT，三个生存变异如实保留NOT_CAUGHT；不把48/51称51/51。

前六轮整改核对结果（只解除原反例，不代替本轮总体结论）：

| 前轮项 | 本轮依据与结果 |
|---|---|
| round-1 B-1～B-6 | port的三元关联、result分支、DTO绑定、UID/cap、pending唯一/超时/回绕及sink锁存均见上8；对应正式socket测试本轮实际通过 |
| round-1 B-7～B-10 | READY/恢复/健康及驱动前期限复查、模拟脉冲结束、session动作主键/完整参数、活动/释放持久检查点均见上9～10；原反例未继续复现 |
| round-1 B-11～B-14 | EXECUTE丢响应独立控制、通知背压与真缺口分离、真实事务失败、daemon同源解析/零请求号副作用前拒绝，分别由源码和正式运行验证 |
| round-1 B-15～B-16 | 生存变异逐项论证且未冒称捕获；独立batches目录无rm -rf活跃源，失败门禁落盘；原反例解除，未执行脚本主体 |
| round-1 N-1～N-9 | 实时flush、严格文本解析、有界发送/原子connected、SQL/BLOB/u64检查、生命周期文档、Python必需、无关固定unlink与模拟标识已核对；范围表仍有本轮N2，不称所有文字无误 |
| round-2 B-1～B-6 | 独立批次、sim_echo_session先于早退、无silent_gap、CONTEXT_RELEASED detail0、注入pending时钟及真实烟测、销毁失效语义均核对；原反例解除 |
| round-2 N-1～N-4；round-3 B/N项 | README指向实际run；verdict落盘；统计分母分离；COMMAND_ACCEPTED detail/action真实SQL断言；Python入口与完整SHA；session/flags/slot-kind测试不再声称不存在的断言；0xc0编码被拒。DTO待裁定延续N1 |
| round-4 B-1；N项 | handoff:362是真python -B，历史表中引用bash错误不作为当前执行命令；daemon现为286；m43统计范围正确；clock/扫描/守卫/日志分工已修。旧值明确作废者不重列阻塞；当前正文残差见N2 |
| round-5 B-1；N项 | transport.c:109非阻塞connect、:133同一真实单调截止poll、SO_ERROR后恢复阻塞；正式accept队列满700ms截止用例实际随389checks通过。活跃树改为内容哈希且解包前采样；控制面文档与源码相符。message_type仍N1 |
| round-6 B-1 | 默认snprintf检查在任何资源创建前执行。正式daemon本轮确认99字节默认成功、100/107默认exit2/no resources、107显式短journal成功、字面journal/control撞名exit2；36条新增检查确实执行。原截断反例解除；对象隔离仍有本轮B1/B2 |
| round-6 N-2 / N-3 | 51份raw现在确实归档，m49精确超时可独立核对；canonical/mutation跑次及17门禁/646与80的主表已修；剩余现值/说明漂移登记本轮N2 |

本卡证据目录没有evidence-run-wsl.sh、wsl-*.log、wsl-run-id.txt或joint-fix-round1.md；当前summary/handoff明确不使用WSL证据，历史审计对旧输入的叙述不视为当前WSL验收声明。目录启动时44个文件（含已经存在的audit-round7-codex-output.txt，尚无audit-round7-run.log），不能为凑输入预估的46个而创建runner文件。

## 环境与 NOT_RUN

| 动作 | 本轮实际结果与边界 |
|---|---|
| Windows git status --short | subprocess读到Git原生exit128，fatal: not a git repository；PowerShell包装退出1不能当Git原生码 |
| Windows四项 `python -B scripts/check_{contracts,stage_plan,vision_contracts,feasibility}.py` | 四个均exit0/PASS；check_contracts的pycparser为PASS(parse only)，hardware_profile=UNVERIFIED；设计检查不证明业务或硬件 |
| Windows `python -B docs/evidence/stage-01/S1-03/mutation-batch.py --check --src .` | exit0；points=51/bad=0；只匹配锚点，没有运行full/覆盖mutation.log |
| Windows Python标准库只读计算 | hashlib/json/re/AST/binascii/tarfile；25清单、22项未变、前卡复用、四黄金帧、11布局、3551总和、51份raw与汇总逐一复算 |
| guest `ssh -o BatchMode=yes -o ConnectTimeout=8 locker-vm "uname -srmo"` | exit0；Linux7.3.0-rc5 x86_64，未用WSL |
| guest来源/证据/快照/变异源检查 | 98项源匹配；17日志+manifest匹配；快照完整SHA/字节匹配；1275变异源项匹配；读取guest m49/m50/m51 raw得到与归档相同完整SHA |
| guest三个evidence脚本的 `bash -n` | 各exit0；仅语法检查，未执行打清单/打包/复验主体 |
| guest已有六个C设备测试，分别 `timeout 45 "$B/<test>"` | wire130、codec179、journal461、protocol507、golden168、port389；各exit0，合计1834checks；运行已有binary，不是本轮重编译 |
| guest正式daemon_smoke.py，canonical src/binary，独立build/S1-03/r7-*临时目录 | exit0；DAEMON_CHECKS=286、DAEMON_FAILURES=0、两socket0600、daemon_exit0；TemporaryDirectory已清理，没有覆盖原日志 |
| guest上方五例隔离探针 | 实际输出如阻塞项表；正常对照成功，两个别名删库、WAL控制路径失效、同址socket误路由均复现；所有临时目录清理 |

六个二进制与正式daemon的本轮执行路径：

~~~bash
S="$HOME/locker-workspace/S1-03/batches/20261026-010653/src"
B="$HOME/locker-workspace/build/S1-03/20261026-010653"
for t in tests/unit/device/test_wire tests/unit/device/test_codec \
         tests/unit/device/test_sim_journal tests/unit/device/test_sim_protocol \
         tests/contract/device/test_golden_frames tests/contract/device/test_device_port; do
    timeout 45 "$B/$t"
done
# daemon由Python TemporaryDirectory包装调用；其argv为：
# python3 -B "$S/tests/contract/device/daemon_smoke.py" \
#   "$B/host/simulator/locker-device-sim" "<本轮独立临时目录>/smoke"
~~~

| 没有运行的项目 | 状态 | 原因与可用证据边界 |
|---|---|---|
| 全量Linux configure/build/19项CTest重跑 | NOT_RUN | 审计未重建canonical或刷新Testing日志；实际跑了六个既有二进制与独立daemon，19/19和3551来自逐份匹配的留存日志 |
| ASan/UBSan、TSan重编译及运行 | NOT_RUN | 本轮没有调用sanitizer binary；19/19及零报告为已核对的原批次记录，不能据此断言未执行的交错均无race |
| full51点mutation、CTest120秒及外层timeout300重跑 | NOT_RUN | full会复制/变异/重建并写正式日志；本轮只跑check-only、核留存变异树及51份raw，不把旧批次47/1/3冒充本轮新跑 |
| evidence脚本主体与重新打快照 | NOT_RUN | 只读审计，不重写既有清单、快照和canonical证据 |
| WSL | NOT_RUN | 用户明确排除该证据来源，无需尝试解除历史E_ACCESSDENIED |
| ARM/32位、真实UART电气/115200/DMA/线路噪声 | NOT_RUN | 本轮为VMware x86_64/AF_UNIX，无串口实物或交叉工具链验证 |
| HAL/GPIO限时脉冲硬截止、MCU/littlefs/RK介质掉电 | NOT_RUN | SQLite事务、模拟150ms转换及软件重开不能代替硬件测量/掉电证据 |
| CRC认证/抗篡改 | NOT_RUN/不承担 | UART-v1:21已明确排除该能力，CRC黄金值不证明认证 |

文档与残留收尾：本轮未变更产品行为/接口，没有更新README、summary、handoff、stage-plan或契约，需改正文档事项已在N2登记。审计不是新项目或较大实施方案，未新增依赖或外部GitHub调研。所有计算/探针通过stdin或-c运行，Python使用-B；本轮独立daemon/path探针临时目录均已确认清理，没有新留一次性脚本、DB、socket或构建产物，不清理前序正式证据。正式daemon路径夹具为60字节碰撞案例额外创建了空目录 /tmp/locker-sim-lim-3fo0zind（本轮01:54:52创建）；收尾确认其为空、非符号链接、父目录确为/tmp后用rmdir移除，输出OWN_DAEMON_EMPTY_FALLBACK_CLEANED=True。早于本轮的同名前缀目录没有触碰。

最终文件差分：对开工时记录的456个受检文件（host/tests/contracts/scripts/docs/config及根入口）的完整SHA集合再次核对，新增仅本报告；既有文件无变化、无删除。既有runner持续写入的audit-round7-codex-output.txt及尚未生成的audit-round7-run.log不在此内容比较中，没有由审计手工编辑或创建。报告新增后证据目录45项；runner最终是否另写run.log以其实际输出为准。

S1-03保持IN_PROGRESS，阶段1 gate保持NOT_RUN。解除R7-B-1/R7-B-2、补对应正式进程回归并提供匹配源码的新证据后再复审；本报告不执行修复、不置DONE、不跨卡。

