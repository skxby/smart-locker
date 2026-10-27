# S1-02 整改后独立复审（round 2）

日期：2026-10-24，Asia/Shanghai。主副本：%REPO%。契约：0.2.1 / REVIEW_DRAFT。

**结论：FAIL，不满足 S1-02 技术交接条件。** 首轮 B1–B3 均有未封住的边界路径；本轮记录 3 项阻塞发现 R2-B1–R2-B3。B4 的核心分页缺陷在静态审查与字节模型中已消除，B5 的入口防护已实现，但部分正式负例仍为空洞。不能据整改对照表将全部问题认定闭环。

本报告只审计；S1-02 保持 IN_PROGRESS，active_task=S1-02、report=null，阶段 1 gate_result=NOT_RUN。未开始 S1-03，未 commit/push。本文“已闭环”表示指定问题的源码/测试或文档落点经本轮核实；**不表示本轮运行过 C 测试**。

## 1. 本轮实际执行、环境和证据等级

先按用户指定顺序读取入口、卡文及公共约束、模块所有权/内部接口/UART、Repository 六端口与 schema、首轮审计和整改证据，再检查仓储实现、全部五个正式 C 测试、局部/根构建、依赖说明。补查公共 numeric、阶段 0、Journal 启动约定及 S1-01 清单。没有采信整改表替代源码核查。

普通 exec_command 在启动首个 PowerShell 读取命令时失败，错误为 Failed to create unified exec process / helper_unknown_error: setup refresh had errors；**未启动进程，没有退出码**。随后使用 Node 文件接口及 child_process.spawn（参数数组、windowsHide=true、cwd=主副本）完成只读计算和以下命令。未提权、未安装工具、未改系统或连接配置。

| 本轮实际命令/操作 | 退出码/结果 |
|---|---|
| python --version | 0；Python 3.14.6 |
| python -B scripts/check_contracts.py | 0；PASS，0.2.1，24 routes / 82 ports / 28 private RPC；c_declaration_syntax=PASS (parse only, not compile/link)，hardware_profile=UNVERIFIED，business_or_hardware_tested=false |
| python -B scripts/check_stage_plan.py | 0；PASS，45 cards，active_task=S1-02，implementation_cards_done=1，runtime_or_hardware_tested=false |
| python -B scripts/check_vision_contracts.py | 0；PASS，camera_owned=false，rknn_available=false，hardware_tested=false |
| python -B scripts/check_feasibility.py | 0；PASS，runtime_or_hardware_tested=false |
| python -B host/storage/tools/extract_schema.py --check | 0；OK，冻结子集 27525 B 匹配 |
| python -B -X utf8 -c <附录 A 的内存契约漂移脚本> | **1（预期红灯）**；DRIFT。调用原工具 main(--check)，未改磁盘契约或冻结文件 |
| python -B -X utf8 -c <附录 B 的 SQL 模型> | 0；documents 视图被保留，新增 meta/migrations，quick_check=ok；Python SQLite 3.53.2，**不是仓储 C 执行** |
| python -B -X utf8 -c <附录 C 的页预算模型> | 0；query/recovery 均为 62+38 条；所有模型文档/页通过项目 Python schema 校验；**不是仓储 C 执行** |
| wsl.exe -e uname -a | 4294967295 / 0xffffffff；UTF-16 输出含 Wsl/Service/CreateInstance/E_ACCESSDENIED（拒绝访问） |
| ssh -o BatchMode=yes -o ConnectTimeout=10 locker-vm "uname -a" | 255；stdout/stderr 均空；不能据此认定 guest 关机或具体网络故障 |
| where.exe cmake；where.exe gcc；where.exe clang（各自独立执行） | 各 1；当前 PATH 未找到 |
| Node fs + crypto：原始字节 SHA-256/大小、清单核对、日志数字相加、静态函数/断言站点统计 | 完成；不是 shell 命令，无进程退出码。详见下节及第 5 节 |

本轮没有执行 git status/diff；非 Git 事实来自任务背景及历史审计，不把其历史 exit=128 写成本轮实测。

**本轮 NOT_RUN**：Linux C 配置/构建、全部 CTest、C 端最小复现、ASan/UBSan、TSan、C mutation、真实 SQL I/O/磁盘满/COMMIT 故障注入，以及 C 示例编译。原因是 WSL 拒绝访问、SSH 未取得 Linux 执行环境，本机 PATH 也无上述工具。退出码均为“不适用（未启动）”，不能填写 0。storage.schema_source_drift 的 Python 程序直接执行及内存红灯已实跑，**CTest 调度本身仍 NOT_RUN**。

VM 的 WORKSPACE-INDEX、活跃源码快照、原始构建目录及 sanitizer 插桩选项本轮未访问。没有向 VM/WSL 同步或写文件。历史日志仅作内容与指纹核对，不作为本轮运行结果。

## 2. 输入身份和修改范围证据

SHA-256 取文件原始字节，不转换换行。下表均为本轮重算值。

| 文件 | 字节数 | SHA-256 |
|---|---:|---|
| contracts/contract-source.json | 66553 | 14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f |
| contracts/include/locker_contracts.h | 39925 | 34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a |
| contracts/internal-data.schema.json | 129569 | e7b8695c49115e2994cf126dc5ab629e5c82460e70d4c292b19576f55c60e386 |
| docs/stages/stage-01-task-cards.md | 16460 | 7f7561e83f9f614fd4ed226cefe3ce78ba367859a55ee92cd82212b25273fc93 |
| config/development/stage-plan.json | 24749 | 1ed89d9f5695c40f965f43d331907295eccca21eaf52d67bee92e5dc4fdf1b47 |
| docs/evidence/stage-01/S1-02/source-manifest.txt | 3692 | 78a33cd844d094fbecc201c2547772947add710697c9a619bec799f2716da99f |
| docs/evidence/stage-01/S1-02/audit.md | 23283 | 80ee21d98ed0877e2a3469a3ae9389f3ddc19663a4bf569bd39a25362ee3c8f8 |
| CMakeLists.txt | 3943 | f9e1964ee54465f5e29299721a3f342f929cc81e3ff6a6da8d00b77fc34258b4 |
| host/storage/src/repository.c | 68688 | 48c6eeddcd2ef5f6a94fa2a81d5cac5226200d53c068509dd90968400729e7b6 |
| host/storage/src/schema.c | 26069 | 19bd5b4f9e2497258adc5254c81eaba6deb583194468fb2e0c32f886a93f1723 |
| tests/unit/storage/test_repository.c | 88469 | f84e9e94960ddd934eac742a1fa24d814ecef8bffe7efe73de92e01e27d7e826 |
| host/storage/tools/extract_schema.py | 5355 | ab5990b55b661b8c7596002783a0f845266f0b5b7d91943d95578f1d117cb218 |
| deployment/host/dependencies.lock.json | 14054 | fbafeac52c9210d175f5f4dc5f542ad224b7b7d7e9608a5347f42daa01a8a1f5 |
| deployment/host/README.md | 6753 | 9887fe18df7f964a66725cd0ae5ee3a3bc7d37fcc58fcdbde229da3d90c7bc7a |

S1-02 source-manifest 全部 **32/32** SHA/大小匹配；根 CMake + host/storage + tests/unit/storage 内无遗漏文件。6 个 vendor 文件也全部匹配 dependencies.lock 的逐文件指纹；本轮未取得原归档，未独立证明“与官方归档逐字节一致”。

contract-source、生成头、内部 schema、卡文、stage-plan 五个关键输入均与首轮 audit.md:17–21 指纹相同。四项设计 checker 通过也支持当前生成产物一致，但不等价于证明所有历史目录均未改动。

S1-01 清单共 29 项：当前仅根 CMake、deployment/host/README.md、dependencies.lock.json 三项不同，其余 **26/26** 匹配。内存删除根 CMake:87、:92 两个 storage 接入整行后得到 3871 B、SHA-256=25d9ca8c06e859a8b8ab85146b15bde3c740aaef215abd71d24d23a4b2db55f7，与 S1-01 相同。

因此，能证实的共享文件变化为两行构建接入及两份依赖文档/元数据。卡文 :51 只授权局部 CMake；summary.md:340 声称这三项由集成负责人执行，但**没有独立授权记录可供本轮重建**，不能用“自称负责人”证明授权。未发现新增业务接口、HTTP/UI SQL 入口、设备动作或借还决策。对 scripts/config/firmware/docs/stages 全目录没有完整前轮字节基线，不能宣称历史“零改动”已全量证明；已核对的卡文和 stage-plan 确实未变。

## 3. 首轮 B1–B5 逐条复审

### B1：部分闭环，仍阻塞

已修复的路径：

- repository.c:1347–1358 同 boot 要求 seq 严格 +1，并防 UINT64_MAX 溢出；:1468–1475 在 BEGIN 前检查页内有序及相邻。
- test_import_page_boundaries，test_repository.c:1165–1180 用合法边界的 (1,1),(1,3) 断言 INVALID 和零落库；:1184–1191 连续对照页成功。因此不是“边界没填先被 B2 拒绝”。历史 mutation.log:109–117 也有这一断言变红记录，本轮 mutation 未运行。
- 有新记录的页：:1581–1600 校验推进关系并消费 gap 标志；test_import_continuity_latching 的 :1288、:1292、:1297、:1300 断言 gap 后 false、后续不能洗白。
- 同 boot 新历史回填：:1582 已按严格相邻拒绝，:1587 又有不倒退防线。test_import_backfill_rejected 的 :1340、:1353–1357、:1364 验证先存 5,6 后回填 3,4，CONFLICT、水位仍 6、事件仍两条。该用例实际先命中相邻检查，**不能单独证明 :1587 防线被执行**，但两道检查共同封住原首轮倒退反例。

**未闭环**：全部重复页在 :1569–1577 回滚返回，绕过 :1597 gap 锁存，见 R2-B1。现有去重用例 :942–943 的 gap=false；锁存用例 :1286–1288 全是新事件，二者没有交叉覆盖。

换 boot：实际规则是 boot 严格增加且 seq=1，允许跳号；不是 handoff.md:180、:522 写的 boot+1。:1361–1372 复用同一规则，test_repository.c:1302–1313 明确接受 boot 2→4，且此时 continuity 已为 false，未验证健康 true 下的跳 boot 结果。UART-v1.md:368 明确顺序递增，没有明确规定 boot 必须逐一加 1；故**不将跳 boot 本身直接判作新增确定性阻塞**。但“缺失 boot 一定没有落任何事件”不是仓储可由这两个键独立证明的事实，需契约负责人澄清与 BOOT/缺口标志的关系，并统一交接说明，不能声称已验证 boot 跳号被拒。

### B2：部分闭环，仍阻塞

repository.c:1483–1505 已先做 last_committed 哨兵、同 device/epoch、last_key<=last_committed、first_available（非零时）同日志且<=first_key、next_after==last_key，再进入事务。原“全零尾跳过检查”和“异设备/epoch 字节排序绕过”已封住。

test_import_page_boundaries 中 :1204、:1211、:1219、:1225、:1231、:1237 的六个 INVALID，以及 :1244、:1251–1253 的事件/水位/连续性不变，有 :1257–1258 同页恢复合法字段成功作对照。正常助手 :334–337、:1112–1115 已填真实边界，不再全部走零值豁免。

**未闭环**：只判断“是否 (0,0)”不是完整结构校验；(2,0) 等半零尾仍可通过，见 R2-B2。first_available 也有同类问题；其全零值还会跳过同日志检查。现有六项没有覆盖这些边界。

### B3：部分闭环，仍阻塞

repository.c:1776–1810 用 documents 表存在性与用户表数区分空库/有痕迹库；已初始化路径 :1682–1695 检查 meta/migrations/documents 三表存在，:1646–1678 校验版本及迁移行。真正空库 DDL+版本元数据位于 :1793–1803 的显式事务，SQL/元数据失败有 ROLLBACK，无删库路径；COMMIT 失败关闭连接也不会将未提交事务自动提交。

test_open_failure_paths 的 :1921–1924 建仅有 meta 的夹具，:1928、:1932 均要求 CONFLICT，:1944 要求仍仅一表；:1864–1879、:1895、:1910 分别覆盖垃圾文件、版本冲突、迁移行缺失。

但 db_user_table_count 只统计 table，忽略 view 等既有结构；初始化分支提交前没有调用 verify_schema_tables，导致 documents **视图**被 CREATE TABLE IF NOT EXISTS 跳过后仍可 open 成功，见 R2-B3。m8_init_tx 实际只关闭 :1784 的“有表痕迹”判定（summary.md:224、mutation.log:135–141），**不是删除 BEGIN/ROLLBACK 的变异**；不能据该名字声称已证明 DDL 中途失败回滚。当前正式回归也未注入“DDL 第一条已成功，后一条 SQL 失败”，未逐一删除三张必要表验证初始化/非初始化两种路径。

### B4：已闭环（核心缺陷的静态及回归核查；本轮 C 执行 NOT_RUN）

repository.c:918–955 从含最坏 revision、recovery collection 的实际序列化信封扣预算，留 NUL 1 B 与续页键最坏转义空间；:1038–1055 按整个 document 的 cJSON_PrintUnformatted 长度及分隔逗号计数，不能装下一条时保留非空页。:1062–1072 处理一条也装不下或返回续页键；query/recovery 共用 :1217–1219。

test_pagination_budget：100 条各含 256 个“验”的 audits，:1602 确认 768 UTF-8 字节；query 的 :1609–1618、:1646–1648 与 recovery 的 :1653–1664、:1690 断言 OK、<=65535、非空缩页、续页键及总计 100，覆盖两种信封。辅助模型独立得到：

| 模型 | documents 内容预算 | 单文档 | 两页条数 | 两页完整 UTF-8 长度 |
|---|---:|---:|---|---|
| query | 64277 B | 1027 B | 62、38 | 63852、39146 B |
| recovery | 64255 B | 1027 B | 62、38 | 63874、39168 B |

这些数字来自附录 C，不是本轮 C 输出。原“67 条导致整页 NO_SPACE”的累计公式已替换。引号/反斜杠由真实序列化自然计入；正式压力回归尚未专门逼近转义 key/页尾窗口，属覆盖补强，未发现预算溢出路径。

### B5：部分闭环；防护实现成立，回归范围声称过满

schema.c:35–62 在 cJSON 前扫描原始 0x00 和奇数反斜杠后的 Unicode NUL 转义；偶数反斜杠保留字面文本。repository.c:447–450、schema.c:730–733、:749–752 均在解析前调用。扫描不依赖 JSON 成员属于键还是值，因此两者都在保护范围；未修改第三方 cJSON。

有效回归：test_repository.c:1731–1739 的完整合法工具记录只破坏 U64；:1757–1773、:1775–1792 为合法 mutations 中的 raw NUL；:1798、:1802–1809 核查 revision/原工具，:1824、:1829、:1834 证明合法字面反斜杠能提交且保留。历史 m10/m11 有失败行，本轮未重跑。

**仍有空洞负例**：:1714–1717 的 revision 用例 mutations=[]，即使去掉 NUL 防护，也会被 schema minItems=1 拒绝；:1720–1728 的 key 用例 value={} 本来非法；:1742–1750 的属性名 ext+NUL+ra 截断后仍是未知 ext，也会被 additionalProperties=false 拒绝。未见 NUL 位于开头、raw NUL 位于属性名/key 的独立有效反例。因此不能认定已用正式回归证明“键和值、开头和中间全覆盖”。这是测试闭环缺口，**不据此虚构当前扫描器仍可绕过**。

## 4. 首轮 N1–N5 逐条复审

| 项 | 判定 | 本轮核实及证据 |
|---|---|---|
| N1 无投影示例/ACK | **部分闭环** | handoff.md:164、:170 已正确传 NULL；:527 声称明确 OK≠可 ACK，但实际 :165–166 的成功分支注释仍引导直接 ACK，:183 只有“只有 OK 才能 ACK”，没有说明 continuity_ok=false 的成功也存在。示例仍含未填事件/边界的占位注释，不是完整可独立编译运行的函数；本轮编译 NOT_RUN。NULL 修正成立，“完整可编译示例且明确非充分条件”未兑现。 |
| N2 正常重开描述 | **已闭环** | summary.md:236、handoff.md:484 均限定正常 close/reopen；test_persistence_across_reopen 在 test_repository.c:835 正常 close、:838 reopen，:853/:855/:867/:877/:888 核查借用/操作/outbox/幂等。fork 仅竞争；无崩溃或掉电通过声称。整改表中引用旧措辞是历史对照，不算继续冒称。 |
| N3 半写后 SQL 失败 | **已闭环（正式测试设计）** | test_repository.c:583–587 创建 outbox BEFORE INSERT RAISE(ABORT) trigger；:594 使用合法 PREPARED，:600–605 先有效 guard 后 outbox 写失败，apply_change 在 repository.c:696–735 顺序写入；:609–619 验证 guard/outbox 无半写、revision=0。:627 删除 trigger 后 :633–645 同样变更成功，排除了“输入原本非法”的解释。本轮 C 执行 NOT_RUN。FULL 同连接观测仍未覆盖，未被伪报通过。 |
| N4 依赖与模块说明 | **部分闭环** | dependencies.lock.json:129–203 vendor 状态与本地 6 文件一致，:71–72 列出 cJSON 消费卡，:222–226 unresolved 已更新；deployment/host/README.md:23–37 和新 host/storage/README.md 已存在并说明实际实现。但 lock:17 enforcement 仍写 cJSON 由 S1-05 消费；summary.md:98 仍称未改该 lock，与 :109、:273、:340 冲突。host/storage/README.md:68–70 让读者在源码根生成 build，也不符合 AGENTS.md:22 的 VM src 外构建纪律。应由对应负责人改说明，本审计不越权修文档。 |
| N5 numeric 复用/冻结门禁 | **已闭环（当前真源）** | u64.c:14–29/:49 复用 common parse/format/compare；schema.c:295–304 走 locker_u64_parse_decimal，common/numeric.c:23–32 保留规范拼写及溢出校验。定宽事件字段保留专用解析有理由。extract_schema.py:82–103 冻结闭包，:117–127 精确比对；tests/unit/storage/CMakeLists.txt:37–44 注册 storage.schema_source_drift。本轮原工具 check exit=0，内存改变 ToolRecord.name.maxLength 后原 main(--check) exit=1，真能红；47 defs/18 collections 匹配。Python 缺失时 :46–47 不注册此测试，仅配置消息 SKIPPED，故不是无条件强制门禁；本轮未执行 CTest。 |

## 5. 新发现/剩余阻塞与非阻塞问题

### R2-B1 / P1 / 阻塞：全重复页丢弃新 gap 标志，后续仍可保存健康连续性

定位：repository.c:1569–1577 早退，发生在 :1597–1600 之前。依据 UART-v1.md:370–372 及首轮 B1，“新发现的已知缺口”不能因事件去重被丢掉。

最小 C 复现（**本轮 NOT_RUN**，可使用现有 make_event/make_batch 与 read_import_cursor 助手，无投影）：

1. 新库导入同日志 (boot=1,seq=1..2)，边界均合法，gap=false，得到 through=(1,2)、continuity_ok=true。
2. 原样重复此页，仅 gap_before_page=true，first_available=(1,1)、last_committed/next_after=(1,2)。页内、范围、日志归属全部合法，没有别的拒绝原因。
3. 两条均 exists，new_count=0，函数返回 OK/原 through；事务被 rollback，continuity_ok 仍 true。
4. 再导入 seq=3、gap=false 的合法页，继承 stored_continuity=true 并提交 through=(1,3)。

应要求第 2 步将缺口事实可靠锁存且不应用重复投影、不推进 through，或明确拒绝并强制保留不可 ACK/不可营业状态；不能返回成功同时保留健康历史。当前 test_import_events_dedup 与 test_import_continuity_latching 没有上述交叉用例。

### R2-B2 / P1 / 阻塞：半零游标未经结构校验即可充当日志边界

定位：event_key.c:72–78 的 is_zero 仅识别“两者都零”；repository.c:1483–1505 直接拿其余结构做数值比较。UART-v1.md:38–39 明确禁止仅一个为零，健康日志从 1 开始。EventKey schema 的两个 U64 单字段范围不能替代这条组合语义。

最小 C 复现（**本轮 NOT_RUN**）：

- 用 make_event 构造合法 (1,1)，make_batch 填合法同日志边界；只把 last_committed 改为 (2,0)。
- is_zero=false，同日志=true，last_key=(1,1)<(2,0)，其余字段合法；新库首次连续条件也满足。静态路径会提交并返回 OK，而不是 INVALID。
- 另一个独立反例：事件与 last_committed/next_after 都是合法 (1,1)，只令 first_available=(0,1)，同日志且数值小于首条，也被接纳。事件本身的 (0,1)/(1,0) 同样没有统一合法游标判定；不要以 is_zero 的单元测试等同于合法性检查。
- 把非空页的 first_available 整体清零（含 device/epoch）也会被 :1493 跳过，无需满足同日志；UART 的空日志/开始哨兵不能凭文档“确实不知道就置零”扩充成非空页的未知首边界。

应先统一验证 event key/first_available/last_committed/next_after 的哨兵与非零组合，再在同日志内比较。原 B2 的全零/异设备绕过已修复，不代表这类结构已封住。

### R2-B3 / P2 / 阻塞：只有 documents 视图的有痕迹库被当成空目标，首次 open 假成功

定位：repository.c:224 只找 type=table；:255 只数用户 table；:1784 判断漏掉视图；:1795 执行 IF NOT EXISTS DDL；:1798 只校验元数据；新初始化分支没有 :1807 的三表核验。

最小夹具：

~~~sql
CREATE VIEW documents AS
SELECT 'loans' AS collection, 'x' AS key, '{}' AS value_json WHERE 0;
~~~

以 initialize_empty=true 打开：documents_exists=false、user_tables=0；DDL 新建 meta/migrations，却跳过同名 documents 视图；版本/迁移行校验通过，quick_check 也为 ok。**源码推导**首次 open 将返回有效 repo，随后查询可呈现空集合；下次再开却会因已有两张用户表且无 documents 表而 CONFLICT。第一次已经改变了这个不应被视为空库的目标。

本轮附录 B 在 Python SQLite 执行同一 DDL，实际得到：

~~~text
BEFORE [('view', 'documents')]
META [('schema_version', '1'), ('snapshot_revision', '0')]
MIGRATION [(1,)]
QUICK ('ok',)
AFTER [('view', 'documents'), ('table', 'meta'), ('table', 'migrations')]
~~~

此输出只佐证 SQLite 的 IF NOT EXISTS 行为，**没有运行 C 端 open**。整改须拒绝有既有对象的目标，并在初始化事务提交前确认必要结构；不能用 quick_check 替代应用 schema。现有仅 meta 的回归不覆盖此路径。

### 非阻塞发现与证据质量

- **R2-N1 / P2：回归覆盖仍不完整。** B5 三项空洞负例见第 3 节；B3 无 DDL 中途失败注入；B1 跳 boot 回归的起始 continuity 已为 false。应以其他字段合法的输入、前后完整状态及定向变异证明目标路径，而不是只看 INVALID/CONFLICT 数量。未发现删除正式测试的直接证据；没有前轮完整测试文件，不能证明逐断言无删除。
- **R2-N2 / P2：交接正文与整改表互相矛盾。** N1 的 OK/ACK 充分条件未写清；handoff.md:180/:522 写 boot+1，而 repository.c:1358 与 test_repository.c:1309 接受跳 boot，:177 又写 boot>。summary.md:252 还称“boot 跳号→CONFLICT”。正文必须按契约统一，不能让调用方任选解释。若据示例直接 ACK，将放大连续性缺陷。
- **R2-N3 / P3：mutation 证据不能全视为最终快照复验。** 两份 unit-tests.log 各 11 个二进制、断言完全相同：common=97/5/53/164/133/53，storage=80/28/549/131/81，总计 **1374**；5 个 storage 测试函数数量分别 7/5/18/11/5（按 event_key/regex/repository/schema/u64）与报告相符。CMake 为 6 common + 5 storage + 1 Python=12 CTest；WSL/VM 普通及 sanitizer 日志均写 12/12。数字相加自洽，但本轮没有独立运行产生 549/1374。静态 TEST_CHECK 宏站点数并非运行断言数（循环/分支会改变计数），不能混用。
- mutation.log 实有 12 项，10 项 TEST=8，m6/m9 两项 TEST=0，与总体声称一致；**m9b 的原始明细 :153 是 4/534，不是 summary.md:226 的 4/549**。失败后 root=NULL 会跳过断言，不能把成功总数填作该次实际分母。m1–m8 的 548、m10/m11 的 549 属不同批次，须保留批次身份。
- **m4 的快照关联存在实质疑点**：summary.md:220 称在当前 u64.c:51 把 left_length!=right_length 改成 false 会让“007/7”等断言失败；但当前 :40–47 先去前导零，:49 的 common 比较对这些合法输入返回 OK，根本不会进入 :50–55 的错误回退。仅做该替换不会破坏 :85–87 的前导零等价性。故 mutation.log:120–123 不能证明当前薄适配版本具有该次 mutation 覆盖，需重新运行并绑定指纹；本轮不认定造假，也不沿用“全部最终源码未再变”的概括。
- **R2-N4 / P3：预算余量和证据范围说明有误。** reserved_next_key 在 repository.c:920 为 192*6+8=**1160 B**，不是 summary.md:225、mutation.log:17 写的 36 B。m8 名为 init_tx，实际测的是痕迹判定，不是事务存在性。m6 的 FULL 漏检已如实登记，不升级成“实际同步为 OFF”的指控。
- WAL/FULL 生产设置确实存在于 repository.c:1764/:1768；WAL pragma 返回行未逐字校验，FULL 没有同连接运行断言，这两项延续已披露限制。普通文件 WAL 回归在 test_repository.c:1975、:1990；不能由它推定所有路径已核实 WAL，更不能推定真实介质掉电。

读取了该目录全部 25 个 .log（包括 mutation），并核对 vm-s102-manifest.sha256.txt 的 **32/32** 指纹与当前本地相同、source-verify 的 32 行 OK。VM run-summary:1–38 确实为 run_id=20261024-063018，configure/build/ctest 及 sanitizer 退出码为 0、ASAN_REPORTS/TSAN_HITS=0；三份 VM CTest 总时长分别 0.34/0.77/1.07s。WSL 普通为 0.24s。日志中未见编译 warning/error 行，也没有把其 pycparser NOT_RUN 改成语法 PASS 的痕迹；本轮 Windows 恰有可用 pycparser，结果不同是环境差异。仅凭已保存日志不能证明实际 sanitizer 插桩或 VM 当前状态，亦不能证明所有历史修改和失败测试均被保留。

## 6. 按 S1-02 卡验收条目评价

| 卡要求 | 本轮结论与证据 |
|---|---|
| 同一事件重复导入不重复推进 | 普通重复事件/投影路径静态成立，test_import_events_dedup :942–955 核查；但带新 gap 的重复页不能丢缺口，R2-B1 阻塞整体交接。C 执行 NOT_RUN。 |
| 失败事务不留半个占位/outbox | BEGIN IMMEDIATE→checks/revision→顺序 mutations→revision→COMMIT，错误 rollback；N3 trigger 回归有有效对照。静态及测试设计认可，本轮 C/SQL I/O 故障执行 NOT_RUN。 |
| 两个竞争申请只有一个成功 | 事务内 expected_revision 与 ABSENT，test_concurrent_slot_guard :740–769 两 fork 子进程各自开库，断言一 OK 一 CONFLICT，:779/:782 核查一条占位及 revision=1。源码成立，历史日志通过，本轮竞争 NOT_RUN。 |
| 重开保留有效借用、未决操作、幂等结果 | 正常 close/reopen 用例 :797–896 存六类、查询核心状态和幂等响应；只认可正常关闭重开范围。本轮执行 NOT_RUN，崩溃/掉电未证明。 |
| 游标不越缺口、不倒退 | 同 boot +1/历史回填已防护；R2-B1 缺口锁存漏路和 R2-B2 结构非法边界仍在，**FAIL**。换 boot 的正文/实现需统一。 |
| u64 数值比较，10 不排在 2 前 | common 规范解析/溢出检查，event_key.c:178–182 原生 uint64 比较、20 位补零 SQL 排序；test_u64 :77–88、test_repository :1432–1453 覆盖。静态认可，C 执行 NOT_RUN；NUL 回归不足另列。 |
| WAL + synchronous=FULL | 代码明确设置；普通文件 WAL 有正式断言，FULL 无运行观测/m6 漏检如实保留。静态配置认可，本轮运行 NOT_RUN；不代表 fsync/介质掉电验收。 |
| 首次初始化与损坏/迁移失败区分且不清库 | 原仅 meta 损坏路径及事务已整改，必要表检查存在；视图有痕迹库仍假成功且被写入元数据，R2-B3，**FAIL**。 |
| 六端口、受控集合、唯一 DDL、模块边界 | 六签名与生成头相符（头 :211/:213/:215/:273/:275/:277），18 集合、47 defs，独立 DDL、参数绑定，无设备副作用。未发现空端口/固定成功占位；初始化假成功单独列 R2-B3。共享文件授权证据限制见第 2 节。 |

## 7. 不覆盖的能力、文档和残留

本轮不覆盖 RK eMMC/SD 掉电/fsync 时延、ARM/32 位构建、CMake 3.16 下限、MCU/littlefs、真实 UART、锁脉冲硬截止、GPIO/电气、相机/RKNN/NPU、Android/LVGL、真实 HTTPS/IPC 全流程及完整借还业务。未把模型、模拟器、静态 checker 或 x86 历史日志当成这些能力的验证。

本次没有实现变更，当前文档过期/矛盾均已在本文件列出，由对应负责人整改；用户只授权新增本审计，故不修改 summary/handoff/README/机器索引。功能修复后应补独立有效回归和与最终源码绑定的 Linux 新证据，再提交复审；FAIL 不允许置 DONE 或自动跨卡。

本轮未创建临时脚本、DB/socket、构建目录或 mock，计算代码只在内存执行。卡目录未见 .o/.a/.so/.exe/.obj/.tmp/.bak/.orig/.pyc/DB/CMakeCache 残留。初始指纹基线中已有根目录 .tmp_codex_r2_in.txt（6177 B，审计任务输入）与 .tmp_codex_r2_out.log（10718 B，Codex 启动诊断），归属不是本轮所创建；它们可能是外层任务诊断，删去会丢排障信息。按“只新增一文件”边界保留，建议外层任务负责人确认归属后决定归档/清理。不能用这两个后来的条目反证开发方先前清理动作未发生。

**范围声明：只新增 docs/evidence/stage-01/S1-02/audit-round2.md（UTF-8），未修改其他源码、测试、契约、stage-plan、summary.md、handoff.md 或 audit.md；未置 DONE，未 commit/push，未开始 S1-03。** 写入后对初始 1598 个文件重算：无删除，新增项只有本报告；1597 个原文件保持原始字节指纹，唯一观测到变化的是原有 .tmp_codex_r2_out.log 诊断日志增长。本 Agent 没有写入该日志的工具调用，不能将外层会话日志的自动增长说成实现改动，也不能声称整个目录除报告外字节完全未变化。源码、正式测试、契约、状态、summary/handoff/audit 均保持原指纹。

## 附录 A：本轮实际内存漂移脚本（exit=1，预期）

命令：python -B -X utf8 -c <以下源码>。调用原抽取工具的同一 main 检查路径，只替换其 CONTRACT 对象的内存 read_bytes，不写真实输入。

~~~python
import runpy, json, sys
m = runpy.run_path('host/storage/tools/extract_schema.py')
g = m['main'].__globals__
c = json.loads(g['CONTRACT'].read_bytes())
c['$defs']['ToolRecord']['properties']['name']['maxLength'] -= 1
class Source:
    def read_bytes(self):
        return json.dumps(c, ensure_ascii=False).encode('utf-8')
g['CONTRACT'] = Source()
fn = m["main"]
sys.exit(fn(["--check"]))
~~~

## 附录 B：本轮实际 SQL 辅助模型（exit=0）

命令：python -B -X utf8 -c <以下源码>。内存 SQLite，不写数据库文件；该模型不是 C 仓储最小复现的执行记录。

~~~python
import sqlite3, pathlib, json
ddl = pathlib.Path('host/storage/schema-v1.sql').read_text(encoding='utf-8')
c = sqlite3.connect(':memory:')
c.execute("CREATE VIEW documents AS SELECT 'loans' AS collection,'x' AS key,'{}' AS value_json WHERE 0")
print('BEFORE', c.execute('SELECT type,name FROM sqlite_master').fetchall())
try:
    c.executescript('BEGIN IMMEDIATE;\n' + ddl)
    print('META', c.execute('SELECT * FROM meta').fetchall())
    print('MIGRATION', c.execute('SELECT version FROM migrations').fetchall())
    c.commit()
    print('QUICK', c.execute('PRAGMA quick_check').fetchone())
    print('AFTER', c.execute('SELECT type,name FROM sqlite_master').fetchall())
except sqlite3.Error as e:
    print('SQL_ERROR', str(e))
    c.rollback()
print('NOTE Python SQLite SQL model only; C repository NOT_RUN; sqlite_version=' + sqlite3.sqlite_version)
~~~

## 附录 C：本轮实际分页字节模型（exit=0）

命令：python -B -X utf8 -c <以下源码>。使用相同文本字段、UTF-8 紧凑 JSON 与整改预算公式，未加载或替代 C 仓储实现。

~~~python
import json, pathlib, sys
sys.path.insert(0, 'scripts')
from check_contracts import validate_value
s = json.loads(pathlib.Path('contracts/internal-data.schema.json').read_text(encoding='utf-8'))
def enc(v):
    return json.dumps(v, separators=(',', ':'), ensure_ascii=False).encode('utf-8')
docs = []
for i in range(100):
    k = f'{i:08x}-0000-4000-8000-{i:012x}'
    v = dict(id=k, actor_id=None, operation_id=None, type='PAGE_BUDGET',
             reason='验'*256, occurred_at=None, time_quality='HOST_SYNCED', source_event=None)
    d = dict(collection='audits', key=k, value=v)
    assert validate_value({'$ref':'#/$defs/RepoDocument'}, d, s['$defs'])
    docs.append(d)
for recovery in (False, True):
    probe = dict(schema_version=1, snapshot_revision=str(2**64-1))
    if recovery:
        probe['collection'] = 'audits'
    probe.update(documents=[], next_after_key=None)
    budget = 65536-1-(len(enc(probe))-2)-(192*6+8)
    pages = []
    start = 0
    while start < len(docs):
        total = 0
        chosen = []
        for d in docs[start:]:
            n = len(enc(d)) + (1 if chosen else 0)
            if total+n > budget:
                break
            total += n
            chosen.append(d)
        assert chosen
        start += len(chosen)
        page = dict(schema_version=1, snapshot_revision='100')
        if recovery:
            page['collection'] = 'audits'
        page.update(documents=chosen, next_after_key=chosen[-1]['key'] if start<len(docs) else None)
        assert len(enc(page)) <= 65535
        name = 'RepositoryRecoveryPage' if recovery else 'RepositoryQueryResult'
        assert validate_value({'$ref':'#/$defs/'+name}, page, s['$defs'])
        pages.append((len(chosen), len(enc(page))))
    print(json.dumps(dict(kind='recovery' if recovery else 'query', budget=budget,
                          doc_bytes=len(enc(docs[0])), pages=pages, reserved_next_key=192*6+8)))
print('NOTE Python byte model only; C execution NOT_RUN')
~~~
