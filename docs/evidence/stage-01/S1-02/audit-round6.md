# S1-02 round-6 独立审计

日期：2026-10-24（Asia/Shanghai）。工作副本：%REPO%。契约：**0.2.1 / REVIEW_DRAFT**。

## 0. 结论与交付边界

**FAIL：2 项阻塞问题（B-1/P1、B-2/P2），2 项非阻塞文档/证据问题（N-1/P2、N-2/P3）。当前快照不满足 S1-02 技术交接或 DONE 条件。**

- **B-1**：已有完整三表时未检查未知触发器；触发器可以静默跳过一次 PUT，而其余 PUT 和 snapshot_revision 仍提交成功。缺陷发生在 schema 接纳和写入成功判定的组合路径。
- **B-2**：结构核验使用 table_info，遗漏生成列；在真实 DDL 上加一列带 NOT NULL 的生成列，即可保留当前全部可见列/主键检查结果，却拒绝合法 AuditRecord。
- **N-1**：新变异的数量/目标有实质修正，但 mutation.log 和 handoff 的旧归因与 summary 的正确说明仍互相矛盾；“24 个独立缺陷”的计数也超出了证据可证明的范围。
- **N-2**：构建路径主体已改正，但 handoff 的保留目录说明仍与实际 VM 日志不符，partial UNIQUE 的影响集合也写错。

本轮只新增 `docs/evidence/stage-01/S1-02/audit-round6.md»。不整改源码/测试，不修改契约、stage-plan、README、summary、handoff 或历轮审计；不置 DONE、不跨入 S1-03、不 commit/push。S1-01 保持 DONE，S1-02 保持 IN_PROGRESS/report=null，active_task/recommended_next 均为 S1-02，阶段1 gate_result=NOT_RUN。

**round-5 B-1 的指定 partial UNIQUE 反例已静态封住，不能继续沿用旧缺陷结论。** 本轮 FAIL 来自新增的具体反例，不是把 Linux 运行环境受限本身当作实现失败。

## 1. 范围、依据与证据分层

按顺序读取 AGENTS、README、阶段入口、机器阶段索引、阶段1公共约束和 S1-02 :48–56，再核对阶段0 :153–174、internal-interfaces :21–43/:98–114、UART :368–372、module-ownership :24 及 traceability 六个仓储端口。授权目录的祖先入口未发现附加 AGENTS.md。S1-01 最新审计顶部为 PASS，summary 收尾为 DONE；本轮不重新验收该卡。

核对 contract-source、internal-data-source、internal-data.schema 和生成头：Repository 六签名在 locker_contracts.h :211/:213/:215/:273/:275/:277，context 在 :190，DTO 在 :143–147，事件/批次在 :119–120。内部 schema 为 **107 defs / 15 x-rules / 18 collections**，冻结子集为 **47 defs / 18 collections / 27525 B**。没有自建替代 API 或放宽验收。

审阅 repository 的事务、查询、分页、恢复、导入及初始化；schema、u64、event_key、regex 的相关路径；DDL、私有头、根/局部 CMake、嵌入器、抽取器、五个 storage 正式测试和 common/test_support。读取五轮审计、summary/handoff、清单及现有 **49 个证据文件，其中 33 个 .log**；新增本报告后为50个。第三方只核对锁、许可和逐文件指纹，不宣称完整审计了 SQLite/cJSON 上游实现。

此次是只读审计，属于明确审计任务，不启动新项目或较大实施方案，不重新选型/下载依赖。为新增反例核对 SQLite 官方语义，引用见 §3。

证据始终分开：

1. **本轮实际执行**：Windows Python/SQLite 内存 SQL、设计检查、Node 文件读取/哈希/日志计数。
2. **静态推导**：当前 C 分支、入参/结构校验及正式回归的对应关系。
3. **历史记录**：开发方保存的 WSL2/VM CTest、sanitizer、mutation 输出。本轮没有重跑这些 C 结果。

## 2. 实际命令与运行边界

exec_command 在创建 shell 前失败：`Failed to create unified exec process / helper_unknown_error: setup refresh had errors»；没有 shell 退出码。随后用 Node fs/crypto 与 child_process.execFile 参数数组完成读取、只读检查和 Python 子进程；cwd 固定为工作副本，windowsHide=true。未安装工具、提权、调整 WSL/SSH 配置或写 VM 源码。

实际 Python 为 **E:/anaconda/python.exe，3.14.6 / Anaconda / AMD64**，Python SQLite 为 **3.53.2**；仓储 vendored SQLite 为 **3.54.1**。SQL 探针未链接项目库、未调用 locker_repo_*，不得称为 C 端复现。

| 实际执行 | 退出码/结果 | 解释 |
|---|---|---|
| git status --short | 128 | fatal: not a git repository；不初始化 Git，不编造 diff |
| where.exe python | 0 | E:/anaconda/python.exe |
| python -B -X utf8 scripts/check_contracts.py | 0 / PASS | 0.2.1，24 routes / 82 ports / 28 RPC；C declaration 仅 parse PASS；hardware_profile=UNVERIFIED |
| python -B -X utf8 scripts/check_stage_plan.py | 0 / PASS | 45卡，done=1，active_task=S1-02；不是业务运行测试 |
| python -B -X utf8 scripts/check_vision_contracts.py | 0 / PASS | camera_owned=false、rknn_available=false、hardware_tested=false |
| python -B -X utf8 scripts/check_feasibility.py | 0 / PASS | runtime_or_hardware_tested=false |
| python -B -X utf8 host/storage/tools/extract_schema.py --check | 0 / OK | 冻结子集 27525 B 匹配真源 |
| Python SQL 探针初版 | 1 | 缺 jsonschema，import 阶段即退出，未执行 SQL；未安装该包 |
| Python SQL 探针正式版（附录A） | 0 | 改用项目 check_contracts.validate_value，两个 AuditRecord 及完整 RepositoryChange 均通过；四组内存 SQL 正反对照完成 |
| Python 漂移负对照（附录B） | 1，预期红灯 | 在内存替换 FROZEN.read_bytes，调用真实 main(--check) 输出 DRIFT；磁盘未改 |
| where.exe cmake / gcc / clang / ctest（分别执行） | 各1 | 当前 PATH 未找到 |
| wsl.exe -e bash -lc 'uname -a' | 4294967295 | 输出含 Wsl/Service/CreateInstance/E_ACCESSDENIED；guest 命令未启动 |
| ssh -o BatchMode=yes -o ConnectTimeout=8 locker-vm 'uname -a' | 255 | stdout/stderr 空；未取得 guest 环境，不推测具体故障 |
| Node 原始字节核验 | 完成 | 源清单32/32、manifest32/32、VM清单32/32、mutation指纹9/9、vendor锁6/6 |

**本轮独立 NOT_RUN**：Linux configure/build/CTest、11个 C 二进制、两个新反例的 locker_repo_open/commit/query、ASan+UBSan、TSan、全部25项 C mutation、实际 WAL/FULL 连接探针、COMMIT/I/O/磁盘满/DDL中途失败注入、进程崩溃/掉电恢复、ARM/RK存储介质、UART/MCU/锁/相机/NPU实物验证。未将 NOT_RUN 当 PASS，也未改写历史 PASS。

### 2.1 保存的 Linux 证据核对

WSL2 与 VM run_id 均为 **20261024-152903**。VM run-summary 记录 configure/build/ctest、ASan、TSan exit=0，source-verify 有32行OK；普通/ASan/TSan均记录12/12，VM时间分别 **0.32s / 0.75s / 1.23s**，WSL2为 **0.26s / 0.47s / 1.05s**。

两个 unit-tests.log 各有11行 PASS，合计均为 **1609 = common 505 + storage 1104**；storage 为 event_key80、regex46、u6490、schema131、repository757。第12项是 schema_source_drift，没有 C 断言数。1583/1601属于旧快照数字，不能继续用来描述本快照。

当前32项源文件与清单/VM清单一致，9项mutation短指纹一致，未发现 round-4 那种已知源码脱节；但这些清单不覆盖全部 common/contracts 构建输入。81 MB VM快照包本轮未重新取得/重算。指纹文本只能绑定所列内容，不能独立证明历史命令真实执行过。

现有 configure/build 日志没有实际 -fsanitize 编译/链接参数、CMakeCache、compile_commands 或二进制插桩依赖证据；本轮无法访问构建目录。因此只确认“保存的日志记录通过”，不能独立确认 sanitizer 插桩、全快照输入或无并发错误。mutation 的原始脚本/定义及53 KB批处理原始输出按交接已删除，本轮只能核对现存派生日志的自洽性。

## 3. 阻塞问题

### B-1 / P1：未知触发器被接纳，PUT 被静默跳过仍提交 revision

**位置**：repository.c :2120–2168（有 documents 表时仅进入列/索引/metadata检查）；:2004–2037（未检查三表的触发器）；:113–119（step_write 只认 SQLITE_DONE）；:405–435（PUT 不核写入效果）；:732–774/:1358–1365（后续 revision 和 COMMIT 仍成功）。

**依据**：S1-02 :53–56、internal-data x-rules 04/13、internal-interfaces :98/:114。schema 不兼容必须明确报错，成功提交应包含全部 mutations 与 revision。官方 DDL 没有任何触发器。

**最小触发条件**：在官方 schema-v1.sql 建出的空库中，保留三表及合法元数据，只增加：

```sql
CREATE TRIGGER skip_second BEFORE INSERT ON documents
WHEN NEW.key='21111111-2222-4333-8444-555555555555'
BEGIN SELECT RAISE(IGNORE); END;
```

然后关闭夹具连接，再以 initialize_empty=false 或 true 打开。新增 trigger 只使 user_objects 增加；当前有 documents 表时没有对象清单/触发器限制，三表列、默认BINARY、全部唯一索引、schema_version、migration、quick_check都不受影响。**这部分 open 接纳是源码推导，C open 本轮 NOT_RUN。** 不需要公共 API 能创建触发器，也不依赖在营业连接上并发改库：不兼容文件在 open 之前已经存在。

**预期**：open 明确 CONFLICT、out=NULL，保留原对象/数据；不能当作正式 schema 交给 Core。即使检查到运行期异常，也应回滚，不可返回成功且漏 mutation。

**实际 SQL 证据**：附录A先用项目正式 Python 校验函数验证两条 AuditRecord 和包含它们的 RepositoryChange（expected=0/new=1），再按仓储原始 UPSERT 参数绑定执行。官方DDL对照两个 INSERT均成功，changes各1，最终两条文档/revision1。加上述 trigger 后：

```text
visible columns = collection TEXT NOT NULL pk1 / key TEXT NOT NULL pk2 / value_json TEXT NOT NULL
key index = collection ASC BINARY / key ASC BINARY
metadata = schema_version1 / snapshot_revision0; migrations=[1]; quick_check=ok
first PUT: no SQL error, changes=1
second PUT: no SQL error, changes=0
revision UPDATE: changes=1
COMMIT_OK
final documents=1, snapshot_revision=1
```

SQLite 的 [RAISE(IGNORE) 官方语义](https://www.sqlite.org/lang_createtrigger.html#the_raise_function) 正是放弃触发语句而保留既有数据库变化；不产生 ABORT 式回滚。当前 step_write 把正常结束当成功，apply_change 继续写 revision，故静态推导端口会报告成功。**本轮没有实测 C 返回 OK，也没有实测任何设备动作**；实际证实的是 SQL 静默漏写，C 路径由当前源码逐步推出。

**影响**：这是成功确认与已存内容不一致，严重于“第二条合法记录被拒绝”。同类未知 trigger 可以作用于操作、幂等或 outbox，破坏成功提交的完整性；本报告只实际执行两个审计文档的内存对照，不声称发生了真实借还/开锁。

**整改与回归建议**：schema接纳时拒绝作用于三张受控表的未知持久触发器，或通过明确的官方schema对象清单验证兼容性；不要因元数据版本一致就放行。补“官方DDL + 预存 RAISE(IGNORE) trigger”的双initialize_empty、out=NULL、原对象/原数据保留回归与干净DDL正对照。若增加写后验证，需考虑 UPDATE/DELETE 及 AFTER trigger，不能仅用 changes==1 宣称覆盖所有触发器。现有 test_failed_transaction_is_atomic :587–608 是 open 后注入 RAISE(ABORT)，会返回 CONSTRAINT，**不是**此预存 IGNORE 反例。修复后增加删除触发器拒绝分支的定向变异，再绑定新Linux快照复验。

### B-2 / P2：table_info 隐藏生成列，额外约束绕过精确列数核验

**位置**：repository.c :1836–1837（SELECT FROM pragma_table_info）；:1847–1883（以该结果认定列数精确）；:2016–2025（documents预期3列）；:2165（已有库验证入口）。正式坏结构夹具 test_repository.c :2275–2279 只加普通extra列，尚无生成列夹具。

**最小触发条件**：在官方DDL生成的空 documents 表上执行一条：

```sql
ALTER TABLE documents ADD COLUMN extra TEXT
GENERATED ALWAYS AS (
  CASE WHEN collection='audits' THEN NULL ELSE key END
) VIRTUAL NOT NULL;
```

**预期**：两种 initialize_empty 都在 open 阶段拒绝不兼容schema，out=NULL，原库不补建/清空。

**实际**：附录A实跑时 table_info(documents) 仍恰好报告官方3列，主键索引仍只有collection/key ASC BINARY，metadata/migration/quick_check仍通过；table_xinfo 才多出 `extra TEXT notnull=1 hidden=2»。之后第一条合法 AuditRecord 的原始 UPSERT 报：

```text
NOT NULL constraint failed: documents.extra
final documents=0, snapshot_revision=0  (probe explicitly rolled back)
```

SQLite 官方说明 [table_info 省略生成/隐藏列，table_xinfo 包含它们](https://www.sqlite.org/pragma.html#pragma_table_xinfo)，并允许[生成列带 NOT NULL/CHECK 等约束](https://www.sqlite.org/gencol.html#capabilities)。这不是普通extra列的重复用例，现有“列数精确”的输入视图本身不完整。

**C边界与影响**：open通过、后续commit映射CONFLICT是静态推导；C端均NOT_RUN。SQL约束失败已实际运行。该反例不会静默提交半单，影响是不兼容库被接纳为可用，之后正常审计写入不可用，违反open阶段fail-closed要求。未据此声称数据丢失或锁误动作。

**整改与回归建议**：以 table_xinfo 核完整列集合和hidden标志，固定schema的三表不得多出生成/隐藏列；保留当前主键、排序、partial UNIQUE检查。给VIRTUAL（必要时STORED）生成列加双initialize_empty、out=NULL、对象/数据保留测试；用恢复table_info的定向变异证明新回归确实捕获此路径。不要只加一个普通extra列或新增唯一索引用例代替。

## 4. 非阻塞问题

### N-1 / P2：mutation 正文归因仍相互冲突，计数不能升级为独立缺陷数

**已确认改善**：mutation.log实际有25个具名段，23个ctest=8、2个=0（m6/m9），每段 assertion_failures 与逐条FAIL数量一致。m23/m24已不在批次；m17/m18均12/757，夹具行号对应当前源码；m25已换为真正零下界种子回退，命中test_regex :80/:93；m26为4/757，命中partial双模式；m27命中正下界 :28。不能再沿用round-5“m25没有定向命中零下界”的旧结论。

**仍存在的问题**：

1. mutation.log :38–43仍称m15b三点联合恢复旧不安全行为；summary :252已正确说明首次视图open(true)在test_repository :2122没有FAIL，唯一视图FAIL是 :2125 的open(false)从CONFLICT变NOT_READY。17条红行证明分类/有痕迹保护，不证明首次视图假成功。handoff :452/:575又只写“证明视图路径有回归守住”，与 :594 的收窄说明并存，默认阅读正文未统一。
2. mutation.log :63–66、handoff :453仍把m10/m11后续失败解释为“前一用例推进revision”；summary :253已经纠正。现正式nul_case_expect_invalid*在test_repository :1869–1928每例独立库、revision1基线，不能再用跨用例污染解释它们。用例内的后置文档数/revision检查变红是正常结果。
3. summary :244/:334、handoff :594把25次运行去掉m20b/m22重合后称“24个独立缺陷、22捕获”。现有材料最多支持具名执行次数、编辑点位和失败集合；多个变异可作用于同一缺陷，不能通过简单减一得到独立缺陷数。建议保留“25次运行/23次变红/2次未捕获”，逐项说明重复和同族关系，不填无证明的独立缺陷总数。
4. 现存日志声明是程序摘录，但完整原始输出/精确变异定义已按summary :412清理；本轮不能重放或核对摘录来源。短指纹与明细自洽不等于独立复跑通过。后续应保留正式变异定义和完整诊断输出，不把关键验证证据当临时垃圾删除。本报告不据此推测日志作者或指控造假。

修订当前summary/handoff/证据解释时应写同一事实；既有原始日志若确需保留历史说法，应明确标注其归因已过期，不能让当前使用者同时获得相反解释。本轮不改这些文件。

### N-2 / P3：VM保留路径与partial UNIQUE影响说明仍错误

- handoff :544仍说 `~/locker-workspace/S1-02/{src,build,logs,artifacts}» 是保留目录；同文 :386–388和VM日志实际build/logs是 `~/locker-workspace/build/S1-02/20261024-152903» / `logs/S1-02/20261024-152903»。summary :138使用按卡源码目录S1-02/src，:414却又用顶层src描述本批。应分别给出实际源码、build、logs、artifacts路径和哪些已经验证，不能用一套未验证的括号路径覆盖事实。当前机器无法核实guest的快照包实际位置，不推测或移动它。
- handoff :593称 `WHERE collection='audits'» 的唯一索引会拒掉“outbox/slot_guards”等集合；此谓词只约束audits子集。summary :333和测试注释已经正确写两条AuditRecord冲突，应将handoff改成同一影响描述。
- 已确认修复：summary/handoff的regex行不再被正文标题截断；host/storage/README :68–74正式构建命令已使用工作区build分类；合计1609与两份单测日志一致。历史日志里的旧路径/时长不要求改写。

## 5. 历轮发现逐项复核

“静态封住”仅针对所列旧反例，不表示本轮运行C测试，更不表示整卡PASS。

| 来源 | 当前实现/回归 | round-6判断 |
|---|---|---|
| 首轮B1：连续性/旧水位 | repo :1397–1422/:1526/:1680–1700；test_import_page_boundaries、continuity_latching、backfill | 同boot严格+1、换boot递增且seq1、不倒退及gap锁存静态成立；跨boot完整性仍依赖设备正确报告gap |
| round-2 R2-B1：重复页gap | repo :1634–1677；test_duplicate_page_latches_gap | 全重复页的新gap能降级连续性，不重复应用投影；静态封住 |
| 首轮B2及R2-B2：包络/半零 | repo :1535–1571；test_import_page_boundaries / boundary_envelope | 非空零尾、半零、异设备/epoch、越尾、next_after错配静态拒绝；宽日志首尾包络正对照保留 |
| 首轮B3及R2-B3：空库/有痕迹/视图 | repo :2120–2168；bare/view/prefix fixtures | 指定旧反例静态封住；整体schema接纳仍被本轮B-1/B-2阻塞；DDL中途故障注入仍缺 |
| 首轮B4：分页 | repo :954–1110；test_pagination_budget :1751–1859 | 按序列化文档字节裁页并预留1160B续页键，保留query/recovery正式回归；本轮C/新页模型NOT_RUN |
| 首轮B5、R3-N2：NUL | schema :35–62；repo :483；test_repository :1869–2104 | 原始0x00/转义NUL守卫存在；属性名name+NUL+junk、独立库、合法正对照均在；旧跨用例解释见N-1 |
| R3 B-1：LIKE下划线 | repo :291 NOT GLOB；prefix tests :2188 | 旧sqlitex_*遗漏路径静态封住，m16记录8/757 |
| R3 B-2：列/键缺失 | repo :1830–2037；结构夹具 :2258–2404 | 普通缺列/多列/类型/NOT NULL/pk/排序检查已在；生成列遗漏为本轮B-2 |
| R4 B-1：反向排序+额外唯一 | repo :1867–1869/:1976–1997 | 逐一验证唯一索引；原反向NOCASE组合静态封住，m20b/m22仍有对应红行 |
| R4 B-2：指纹脱节 | 当前源/VM清单32/32、mutation9/9 | 指定失配已关闭，不重复报旧30/32；全构建输入/快照包独立绑定限制见§2.1 |
| R4 N-3：有界regex | regex :526–575；test_regex :72–108 | 零界种子1、正界播种min、循环从produced开始；m25 :80/:93与m27 :28各命中目标；静态封住 |
| R5 B-1：partial UNIQUE | repo :1968–1975；SQL_PARTIAL_UNIQUE_INDEX :2318–2323；:2376/:2377/:2384 | 两种初始化模式CONFLICT/out=NULL及对象数正式回归存在；m26记录4/757，旧缺陷静态封住 |
| R5 N-1/N-2 | summary/handoff/mutation/README | 主要修订成立，残余归因/计数/目录错误见本轮N-1/N-2，不把部分修订说成全部闭环 |

## 6. 审计核查矩阵（34点）

下表“静态”只表示源码/测试设计核对；“日志”只表示保存记录；只有明确标为“本轮执行”的行才是独立执行结果。

| # | 核查点 | 依据/观察 | 结论 |
|---|---|---|---|
| 1 | 版本/状态/前置 | contract-source与stage-plan；S1-01最新报告 | 0.2.1 REVIEW_DRAFT，S1-01 DONE，S1-02 IN_PROGRESS；本轮执行读取 |
| 2 | 单卡/共享边界 | 六端口、目录与traceability | 无新API、HTTP/UI或设备动作实现；静态 |
| 3 | 六端口/DTO | 生成头 :119–120/:143–147/:211等 | 签名对应，无跨模块替代DTO；静态 |
| 4 | 字段/受控集合 | 107 defs、18集合、47冻结defs | 真源/子集对应，抽取--check本轮0 |
| 5 | 漂移门禁负对照 | 附录B真实main | 内存差一个空格即exit1/DRIFT；本轮执行 |
| 6 | 唯一DDL及嵌入 | schema-v1.sql / embed_file / schema_embedded | 真源依赖和build目录生成.inc存在；静态，未独立CMake运行 |
| 7 | 依赖锁/许可 | SQLite3.54.1、cJSON1.7.19、6文件 | 逐字节哈希/大小与锁匹配；未重新下载归档 |
| 8 | SQL参数绑定 | repo :122/:372/:418/:1010/:1218 | 值不拼SQL；集合白名单；静态 |
| 9 | Core唯一写者 | private context、无worker/callback | 生命周期文档明确独占；SQLite线程安全不等于repo可多线程 |
| 10 | CAS/检查条件 | BEGIN IMMEDIATE :138，revision :694，checks :703 | 检查与写在同事务；静态 |
| 11 | 操作+幂等+outbox | apply_change循环及revision | 官方schema正常路径原子；未知trigger漏洞为B-1 |
| 12 | 中途SQL失败回滚 | RAISE(ABORT)正式用例 :569 | 第一写已执行、第二写失败后验证全单回滚；C本轮NOT_RUN |
| 13 | COMMIT失败处理 | repo :165–170 | 显式ROLLBACK；I/O/磁盘满注入NOT_RUN |
| 14 | 两申请竞争 | 两进程fork后各自open :720–795 | 同expected/checks只一成功的正式断言存在；本轮未跑 |
| 15 | 正常重开持久化 | test_persistence_across_reopen :800 | loan/operation/outbox/idempotency与revision核对；不是崩溃/掉电 |
| 16 | 文档类型及未知字段 | schema :370–428，PUT集合校验 :765 | 类型/required/additionalProperties与collection匹配分开校验；静态 |
| 17 | NUL无损边界 | schema :35、repo :483、独立库负例 | 入口先拒原始/解码NUL，保留转义反斜杠对照；静态 |
| 18 | u64规范/溢出 | common numeric薄适配、schema :492 | 零/MAX/超MAX/非法拼写正式测试；不以double存计数 |
| 19 | 事件键/epoch/排序 | event_key.c 103B定宽键 | epoch进入唯一键，20位补零与数值比较相符；静态 |
| 20 | 新事件去重 | repo :1615–1626/:1634 | 重复不再应用投影；读损坏语义未外推 |
| 21 | 事件+投影+游标原子 | repo :1584–1740 | 同事务，失败回滚，成功后才写输出cursor；静态；trigger可破坏写入效果见B-1 |
| 22 | gap/倒退 | strict adjacency与gap锁存 | 首页及全重复页不洗白连续性；旧水位新回填拒绝；静态 |
| 23 | 批次边界 | :1535–1571 | 非空零首/尾、半零、异日志、越尾、next_after均检查；静态 |
| 24 | 查询/恢复DTO | run_query :1123–1304 | key/after互斥、limit、schema_version；结果再按schema校验 |
| 25 | 分页字节与续页 | :954–1110 | 逐条最终序列化计预算；有未读返回last included key；单条过大NO_SPACE |
| 26 | 恢复快照 | 读事务内revision+page | 跨页/集合同revision由Core核对；不自动发送旧outbox |
| 27 | 初始化/损坏分类 | :2120–2168 | 真空库才DDL；有痕迹不补建；失败out=NULL，未知trigger仍漏查 |
| 28 | 列/主键/BINARY | :1830–1999 | 可见列与索引逐一核对；隐藏生成列缺口B-2 |
| 29 | partial UNIQUE | :1972 + 正式夹具/m26 | 指定旧反例静态拒绝；本轮SQL模型确认其有害性 |
| 30 | WAL/FULL | :2106/:2110；test_wal_enabled | 语句存在；WAL历史测试有记录，FULL运行时断言未覆盖，m6未捕获 |
| 31 | regex边界 | produced播种、正式{0,n}/{n}/{m,n}用例 | 新m25/m27归因对应目标；C本轮NOT_RUN |
| 32 | 测试非空洞/计数 | common TEST_REPORT失败返回1；25变异段 | 明细数量自洽，2未捕获如实登记；归因残余N-1 |
| 33 | 源码/VM证据一致性 | 32源/32VM/9短指纹 | 本轮重算全部匹配；不等于独立Linux PASS |
| 34 | 文档/残留/硬件边界 | 当前入口与scope扫描 | 仅新增本报告；scope内无新临时DB/日志/脚本；VM外部清理及RK掉电未核销 |

## 7. 复验要求与交接限制

由S1-02负责人先修B-1/B-2及正式回归，统一N-1/N-2当前说明，清理本轮明确临时物并保留正式测试/关键日志；再按既定VM工作区、固定快照运行configure/build/CTest与必要sanitizer/定向mutation，重出清单。新增两个反例应在真实C端验证open双模式、out=NULL、原数据保留和干净DDL正对照，不能用本报告的Python模型替代。

没有硬件时继续明确RK存储介质掉电可靠性NOT_RUN。本卡仓储通过也不等于S1-07阶段门禁通过，更不等于MCU功能/整柜完成。是否置DONE由修复后独立复审和收尾流程决定，本审计不更改状态。

文档同步范围已检查：此次只审计、不改变产品行为，按授权将发现写入本文件；需要修订的现有正文已在N-1/N-2列出，没有越权改summary/handoff。Node/Python探针只在内存执行，Python带-B，无临时脚本/DB。host/storage与tests/unit/storage未见db/wal/shm、构建、log、pyc/temp残留，根未见.tmp*/s102_/_mut*；未删除他人正式证据或既有build/HAL产物。VM、Public、TEMP等外部残留本轮未全量核查，不认领交接方“0”自述为独立通过。

## 8. 受审输入指纹

以下98项为本轮在内存保存的原始字节大小/完整SHA-256；包含源码清单范围、现有S1-02全部证据及关键入口/真源。不是整个工作区或全部构建输入清单，也不是Git差异。仅本报告不进入自身指纹表。指纹冻结时间：2026-10-24T08:24:06.647Z；结束复核时间见文末。

| 文件 | bytes | SHA-256 |
|---|---:|---|
| CMakeLists.txt | 3943 | f9e1964ee54465f5e29299721a3f342f929cc81e3ff6a6da8d00b77fc34258b4 |
| host/storage/CMakeLists.txt | 3782 | 3acc9e405c66569b988db32b7cb0e135b8a3a4c52fdd53f3e606e01fee49297a |
| host/storage/README.md | 7579 | 09d5c3147b477d21df6d0c7143ddb3a95fcde33becf76816cea57cc1e60517ba |
| host/storage/cmake/embed_file.cmake | 1891 | c64242387b74017b03c9ce9559e5bc245eda0f83d5636ea989a3740ded028c7e |
| host/storage/include/locker/storage/embedded.h | 898 | f8087fd75b43b6c35d17fd400dacf95f6f4177b937e81278783a4d093441ab7a |
| host/storage/include/locker/storage/event_key.h | 1660 | 4d46736869d62e688540dc2c0a10f3c0c71fe6302898266b1d836f143c705d73 |
| host/storage/include/locker/storage/regex.h | 1130 | 39fdb69f6fb58397e82810c3778d497b117944449527fc5b4d040129b1c03ade |
| host/storage/include/locker/storage/schema.h | 2197 | 547f9b203eb922cdb6222c0ec419bfe77b0e6899dfa010b671c90704d6bd987f |
| host/storage/include/locker/storage/u64.h | 835 | 08cdc34af372ce6cb3d0b316b6270a827aa3cdf5f007b8f9f61bd39445959f27 |
| host/storage/schema-v1.sql | 2350 | 0eaa403ba7437c855d35a556443564ee6f1e9fd1cc2d5c967aa71c4f17ed8f70 |
| host/storage/src/event_key.c | 7165 | 2229c561f45518300ce74eff4fdc5d582f8b7c6a5a30f3c07aa1adb532369edb |
| host/storage/src/regex.c | 25127 | b01a90e58f1869c9d884df4ebfe16ab7b536b380924b5742e16f7a6dfe2f50c8 |
| host/storage/src/repository.c | 84509 | 0761eb6d85d5d38fbad5dcd5b9a46426e596b6be08af082bea54b1af2944d5f1 |
| host/storage/src/repository_internal.h | 3344 | f68136968db1af077771b567b93dd974865294bb29c313f1928eec1748c6b64a |
| host/storage/src/schema.c | 26069 | 19bd5b4f9e2497258adc5254c81eaba6deb583194468fb2e0c32f886a93f1723 |
| host/storage/src/schema_embedded.c | 809 | fc0c2a336230556b2016a8f2b4e3987b18f71db717cb3f1e96d12d1c88652f46 |
| host/storage/src/schema_internal.h | 1022 | a5d1059e54b112fb232a1da7917d02b4739f5b2346ac18493dce2ac0e8e9f75a |
| host/storage/src/schema_records.json | 27525 | 781d7b139af23b5783040281e3dacc164a0d51076a11376e6bef1411941888ef |
| host/storage/src/u64.c | 2180 | 875036a0abbf6d49b15ec33128adbe24cae34df74133985f4abbbf3e04b26696 |
| host/storage/tools/extract_schema.py | 5355 | ab5990b55b661b8c7596002783a0f845266f0b5b7d91943d95578f1d117cb218 |
| tests/unit/storage/CMakeLists.txt | 2354 | bc3ccbbc732b04621e2f7a39f7434fc666a12876b0c24e31f86f1bd0e637963b |
| tests/unit/storage/test_event_key.c | 7913 | 159254c9091deb3b8d3c1e888ae8819caf2dcbd50882096cac9ef757feb2ca45 |
| tests/unit/storage/test_regex.c | 5128 | 8bb903aa8b760f76f6614962999c0689a5d43af43cda07d8a634f887ab6a15b2 |
| tests/unit/storage/test_repository.c | 116470 | 94f85ca7ddcc4b6a5df252c8c897fbb6bce00b99331a7995d480f386dc6efafa |
| tests/unit/storage/test_schema.c | 31269 | 4952adc2876f8a1c37d83ad42968ede1cd40d66a6a582862cd0d75941a20e2ca |
| tests/unit/storage/test_u64.c | 6040 | 6919ee37502373a4f0c19ef7ca6134820de333e150df37d353c02bb6ac9652e7 |
| host/storage/third_party/cjson/LICENSE | 1084 | a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c |
| host/storage/third_party/cjson/cJSON.c | 80399 | 298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d |
| host/storage/third_party/cjson/cJSON.h | 16394 | 25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee |
| host/storage/third_party/sqlite/sqlite3.c | 9582521 | 0c657666d819f261cc4007526ad57e1669de9949df7a8ea847aa284a2bf0ee52 |
| host/storage/third_party/sqlite/sqlite3.h | 695729 | d6c84557261893439b341d2e6c35187ae3ea24fbfd843ad94616f72d89242b8c |
| host/storage/third_party/sqlite/sqlite3ext.h | 39547 | 48963abfefb41ac329ef9d706acb5eadba952a99a748285c9ded6b5e0942721b |
| docs/evidence/stage-01/S1-02/asan-build.log | 3422 | 468b78c872547c49152cbe409458f69b88cd4492d655304ad6735a09144c157e |
| docs/evidence/stage-01/S1-02/asan-configure.log | 594 | 0dd997461a1f268e539ebeb0851ef572973c02642f51ef7223376b5d8a51d72c |
| docs/evidence/stage-01/S1-02/asan-ctest.log | 1439 | e155e9360b7d6427b0963207eac350f79f5aa3b606b5f125e30b2045f2259e78 |
| docs/evidence/stage-01/S1-02/audit-round2.md | 32835 | 3e2475309e154b3071269f4487ecd4ba28993c84cead38c7ee56e098797922f7 |
| docs/evidence/stage-01/S1-02/audit-round3.md | 40534 | 2d0ea66edca835e695ff297d0297c0229d4671bd68863a7596490fdcbc75b46a |
| docs/evidence/stage-01/S1-02/audit-round4.md | 52973 | f5c206a55f5a72d61ae4e6ba582c1573e84c77f5224131f50898213587487ac0 |
| docs/evidence/stage-01/S1-02/audit-round5.md | 44086 | c8aef59be2e1bd849d73e5794e505b02bde7a3da0030105e1fb6d3e1d03bd903 |
| docs/evidence/stage-01/S1-02/audit.md | 23283 | 80ee21d98ed0877e2a3469a3ae9389f3ddc19663a4bf569bd39a25362ee3c8f8 |
| docs/evidence/stage-01/S1-02/build.log | 3422 | e2833eb6a3c8275e8a547ccc9802cf7beef0167ae4618e8b0ac492139f5b93ae |
| docs/evidence/stage-01/S1-02/check_contracts.json | 535 | 0ac0a396fffb701522e05b2ff5f8c7725834a2641e59c109f41a62401870087d |
| docs/evidence/stage-01/S1-02/check_contracts.log | 535 | 0ac0a396fffb701522e05b2ff5f8c7725834a2641e59c109f41a62401870087d |
| docs/evidence/stage-01/S1-02/check_feasibility.json | 242 | cbe1da1d34c5d29634377f6a55c0199233a144e984417ba06553c9c5e149f02b |
| docs/evidence/stage-01/S1-02/check_feasibility.log | 242 | cbe1da1d34c5d29634377f6a55c0199233a144e984417ba06553c9c5e149f02b |
| docs/evidence/stage-01/S1-02/check_stage_plan.json | 319 | 572e375df64f10218f6e440803dcff0162f846df19877d5b77692e91683250d0 |
| docs/evidence/stage-01/S1-02/check_stage_plan.log | 319 | 572e375df64f10218f6e440803dcff0162f846df19877d5b77692e91683250d0 |
| docs/evidence/stage-01/S1-02/check_vision_contracts.json | 572 | 3f8b58703a9d039f594749a6924e19eda4e312d97cbdbf0ee1dac3b6325d8778 |
| docs/evidence/stage-01/S1-02/check_vision_contracts.log | 572 | 3f8b58703a9d039f594749a6924e19eda4e312d97cbdbf0ee1dac3b6325d8778 |
| docs/evidence/stage-01/S1-02/configure.log | 589 | 7a74afcba15e55aae5f0bec692205739cb13c832208222bb9904585470c228e2 |
| docs/evidence/stage-01/S1-02/ctest.log | 1434 | 6ce865dd4cbd78414a1754f0dcaf97b42920696353cb76fcbe1827add50c9e18 |
| docs/evidence/stage-01/S1-02/design-checks.log | 1860 | b9e8152e1d7859a0bd35661f817ad3991d536ab2fd790cb86c5737a7f34d1f4f |
| docs/evidence/stage-01/S1-02/environment.log | 211 | 1dc9aff2c9359cb88264445050b1fddcc6879d69791400e1dcd8c4e9188a8d8d |
| docs/evidence/stage-01/S1-02/handoff.md | 49278 | 75616348b81b546251d8aebbe30d4bc59edf57deb0f55377dafb8fe04bc49c0d |
| docs/evidence/stage-01/S1-02/manifest-check.log | 1247 | eb2aadd332b7dab9d0f6ebe738354798b922c382b5b0915546a6df47956e4652 |
| docs/evidence/stage-01/S1-02/manifest.sha256 | 3231 | 5d58d009c860216b93ef457cf638750de00fbcf5ff16f9340024883e639d1e6d |
| docs/evidence/stage-01/S1-02/mutation.log | 29256 | 863aa029f19e6252607533c5a41d8dc1edfb13a3581dbf3faac14e8e40209f9e |
| docs/evidence/stage-01/S1-02/run-id.txt | 20 | 20961bdec6e0a2ffbab3905e8cfd60bca7969a922038b3cc37ed4d040ca50188 |
| docs/evidence/stage-01/S1-02/source-manifest.txt | 3563 | 91150adc7d76bba9d38d55adab7e01fe20b64359bad50d912a0fc0471dee8b85 |
| docs/evidence/stage-01/S1-02/source-verify.log | 1247 | eb2aadd332b7dab9d0f6ebe738354798b922c382b5b0915546a6df47956e4652 |
| docs/evidence/stage-01/S1-02/summary.md | 73908 | 5a37da66632c5fdc5821d662c79b770aa64ff5000421f7f091ab7f912e43bd33 |
| docs/evidence/stage-01/S1-02/tsan-build.log | 3422 | d017754eabb91b3645a25186b8d52e535ac885d339f7c00957c0b52e4a7de20d |
| docs/evidence/stage-01/S1-02/tsan-configure.log | 594 | 8cc74fa6c6245feefa0274eba97477f1a22dbf3c90b1f02406ad46afa37cc97e |
| docs/evidence/stage-01/S1-02/tsan-ctest.log | 1439 | fd5baa253cb76b9d4d3b0c0f7e9638e98480fedc3adb09a0a88799bc9a1fa424 |
| docs/evidence/stage-01/S1-02/unit-tests.log | 413 | 1f9638af41f2ac4a1a4f34ad13f3e0316ed96691ffa136d8d740b29000721df7 |
| docs/evidence/stage-01/S1-02/vm-s102-asan-build.log | 3422 | 6b64eeac29a441368b5df17a98f495ee03350dff2bfda7b88d5a4acc19ec44f9 |
| docs/evidence/stage-01/S1-02/vm-s102-asan-configure.log | 619 | 14e280b3fef70fdc4c355eba5e6afb241f14b844c6a70f4208f9a2e8c4455952 |
| docs/evidence/stage-01/S1-02/vm-s102-asan-ctest.log | 1464 | c44e45e653737e58ff83ba37bb4abf47f604747c1267a32a7668125402407f8c |
| docs/evidence/stage-01/S1-02/vm-s102-build.log | 3422 | 079de4516a4e0b6a7a9c8d8128bf65620d5cfcb11affe9dd673fd689de54e14d |
| docs/evidence/stage-01/S1-02/vm-s102-configure.log | 614 | 7fc81aeb4bc6200656137124a3bc62c35606179e4f2fb7efbd15471c7ac0e5ea |
| docs/evidence/stage-01/S1-02/vm-s102-ctest.log | 1459 | 15a373709dc83d42ec1ba3e66c5df5821e0d08b1cd4b22eef0c485a430f5523e |
| docs/evidence/stage-01/S1-02/vm-s102-design-checks.log | 1790 | 51f256980acac32dae2eb55802c47c36a527bb5956e1fd062b1414ace802f86d |
| docs/evidence/stage-01/S1-02/vm-s102-environment.log | 279 | c295eb610d35a2d9b630ef3d0a7d7e05d03db29fdf779638cb9737758ebafdf6 |
| docs/evidence/stage-01/S1-02/vm-s102-manifest.sha256 | 3231 | 5d58d009c860216b93ef457cf638750de00fbcf5ff16f9340024883e639d1e6d |
| docs/evidence/stage-01/S1-02/vm-s102-run-id.txt | 20 | 20961bdec6e0a2ffbab3905e8cfd60bca7969a922038b3cc37ed4d040ca50188 |
| docs/evidence/stage-01/S1-02/vm-s102-run-summary.log | 819 | 74fa7e3b40dda0adcc17f3166f213692e1e640281f998dafa13a05275b9fe90f |
| docs/evidence/stage-01/S1-02/vm-s102-source-verify.log | 1247 | eb2aadd332b7dab9d0f6ebe738354798b922c382b5b0915546a6df47956e4652 |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-build.log | 3422 | 3b07d22d180fe5f535368fd22bdcfcc62241f3a6feb0e3f72de1e389f1ccfef7 |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-configure.log | 619 | fac1d46e7c563c25d9f02aa5ea4b0f48d7cfaef5812652018ac977b9ecb7c2ff |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-ctest.log | 1464 | df5b3025a42c377398f8f00ed5bcb20175ce006531fbb282c7e1520eb79aacc9 |
| docs/evidence/stage-01/S1-02/vm-s102-unit-tests.log | 358 | 104ac6b7f7fcbe92f7acabb6ce8de800f83d29cc3dd88ad610fec40e20144b60 |
| AGENTS.md | 5839 | bb8658418e3926ad7799f3be148107731c61fc1aa3b668992dd80ee0e1d217de |
| README.md | 7859 | 8c7c835c42208da55a92b702d5ccc7f64bee5ae3744b4a023037f35babeebd54 |
| docs/stages/README.md | 4985 | 24ff25b010d838e05b389b56f825068d499578fa20578d5b286eb67be9c86d06 |
| docs/stages/stage-01-task-cards.md | 16460 | 7f7561e83f9f614fd4ed226cefe3ce78ba367859a55ee92cd82212b25273fc93 |
| docs/stages/stage-00-architecture.md | 22164 | 2e85508b23aff242f1ce6cbd166464e5a85966f8ead4f1dd8398c5231443227b |
| config/development/stage-plan.json | 24749 | 1ed89d9f5695c40f965f43d331907295eccca21eaf52d67bee92e5dc4fdf1b47 |
| contracts/contract-source.json | 66553 | 14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f |
| contracts/internal-data-source.json | 92193 | ebcfa5859c114e8746a2f235161c17a6dede651f56125d92cc37f259c7d81da1 |
| contracts/internal-data.schema.json | 129569 | e7b8695c49115e2994cf126dc5ab629e5c82460e70d4c292b19576f55c60e386 |
| contracts/include/locker_contracts.h | 39925 | 34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a |
| docs/architecture/internal-interfaces.md | 29792 | a5b3cdc41c05006e83edc970c46c15f79644adb8fe071b71953f239a64c1c2cf |
| docs/architecture/uart-v1.md | 26022 | 5bb2204f291b259f7b28cc7a2a1473352d5f1d0d501032168924d664c5e3ed8f |
| docs/architecture/traceability.md | 12694 | 0903b559a6fc33fa0120d608b5b85783f66327b03a0f8267e7b86309877f2859 |
| docs/architecture/module-ownership.md | 10135 | b182fe6e424b605fd4cad1073c85178588ee474ca8ab6f38be84e3129f72bb3f |
| deployment/host/dependencies.lock.json | 14075 | 4884c4ca9944ccd66be72db71dcd99b289bba119763c965a7e3b7b7a961fac6d |
| deployment/host/README.md | 6753 | 9887fe18df7f964a66725cd0ae5ee3a3bc7d37fcc58fcdbde229da3d90c7bc7a |
| tests/unit/common/test_support.h | 5193 | 557bf284fa7336569e61ac29ba5c7e5de9c9d8bdb25bd8010daf6812950145b0 |

## 附录A：本轮执行的内存SQL探针

在工作副本运行 `E:/anaconda/python.exe -B -X utf8 -c <以下代码>»。正式版exit0；输出摘要见§3。SQL全部为 :memory:，不触碰产品数据库。初版缺jsonschema在import阶段退出后，正式版改用项目已有校验器，未安装依赖。

```python
import sys,sqlite3,json,pathlib
sys.path.insert(0,"scripts")
from check_contracts import validate_value
print('PYTHON',sys.version.replace('\n',' ')); print('SQLITE',sqlite3.sqlite_version)
s=json.loads(pathlib.Path('contracts/internal-data.schema.json').read_text(encoding='utf-8'))
ids=['11111111-2222-4333-8444-555555555555','21111111-2222-4333-8444-555555555555']
records=[dict(id=i,actor_id=None,operation_id=None,type='AUDIT',reason='round6',occurred_at=None,time_quality='UNSYNCED',source_event=None) for i in ids]
for r in records: assert validate_value(s['$defs']['AuditRecord'],r,s['$defs'])
change=dict(schema_version=1,expected_snapshot_revision='0',new_snapshot_revision='1',checks=[],mutations=[dict(op='PUT',document=dict(collection='audits',key=r['id'],value=r)) for r in records])
assert validate_value(s['$defs']['RepositoryChange'],change,s['$defs'])
print('VALID_AUDIT_RECORDS',len(records))
ddl=pathlib.Path('host/storage/schema-v1.sql').read_text(encoding='utf-8')
upsert='INSERT INTO documents(collection, key, value_json) VALUES (?1, ?2, ?3) ON CONFLICT(collection, key) DO UPDATE SET value_json = excluded.value_json'
variants={'official':'','partial':"CREATE UNIQUE INDEX extra ON documents(collection) WHERE collection='audits'",'trigger':"CREATE TRIGGER skip_second BEFORE INSERT ON documents WHEN NEW.key='"+ids[1]+"' BEGIN SELECT RAISE(IGNORE); END",'hidden':"ALTER TABLE documents ADD COLUMN extra TEXT GENERATED ALWAYS AS (CASE WHEN collection='audits' THEN NULL ELSE key END) VIRTUAL NOT NULL"}
for name,extra in variants.items():
 c=sqlite3.connect(':memory:'); c.executescript(ddl)
 if extra:c.execute(extra)
 c.commit()
 print('CASE',name,'table_info',c.execute('PRAGMA table_info(documents)').fetchall(),'xinfo',c.execute('PRAGMA table_xinfo(documents)').fetchall())
 print('indices',c.execute('PRAGMA index_list(documents)').fetchall(),'key',c.execute("SELECT name,[desc],coll FROM pragma_index_xinfo('sqlite_autoindex_documents_1') WHERE [key]=1 ORDER BY seqno").fetchall())
 print('metadata',c.execute('SELECT * FROM meta ORDER BY name').fetchall(),'migration',c.execute('SELECT version FROM migrations').fetchall(),'quick_check',c.execute('PRAGMA quick_check').fetchall())
 try:
  c.execute('BEGIN IMMEDIATE')
  for r in records:
   c.execute(upsert,('audits',r['id'],json.dumps(r,separators=(',',':'))));print('step_write DONE changes=',c.execute('SELECT changes()').fetchone()[0])
  c.execute("UPDATE meta SET value='1' WHERE name='snapshot_revision'"); assert c.execute('SELECT changes()').fetchone()[0]==1
  c.commit();print('COMMIT_OK')
 except sqlite3.Error as e:print('SQL_ERROR',e);c.rollback()
 print('END',c.execute('SELECT key FROM documents ORDER BY key').fetchall(),c.execute("SELECT value FROM meta WHERE name='snapshot_revision'").fetchone())
```

该探针故意直接执行SQL以展示不兼容对象的影响；partial组也直接尝试写入，它不是在运行已修复的C open。当前C open会在partial属性处分支拒绝；trigger/hidden两组的open接纳属于§3中的源码推导。

## 附录B：只在内存制造冻结子集漂移

同样使用python -B -X utf8 -c执行，实际exit1，输出DRIFT。没有覆盖磁盘文件。

```python
import importlib.util,pathlib,sys
p=pathlib.Path('host/storage/tools/extract_schema.py')
spec=importlib.util.spec_from_file_location('round6_extract',p)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
real=m.FROZEN
class InMemoryFrozen:
 def exists(self):return True
 def read_bytes(self):return real.read_bytes()+b' '
 def relative_to(self,p):return real.relative_to(p)
m.FROZEN=InMemoryFrozen()
sys.exit(m.main(['--check']))
```

## 附录C：现存mutation逐段计数复核

本表是Node从现存日志解析的结果，不是本轮C mutation执行。configure/build各0及ctest状态是日志自述；全部声明数与实际保留FAIL行数一致。

| 具名段 | 日志ctest | 声明失败断言 | 实有FAIL行 |
|---|---:|---:|---:|
| m1_revision | 8 | 5 | 5 |
| m2_dedup | 8 | 5 | 5 |
| m3_adjacent | 8 | 9 | 9 |
| m4_u64_fallback | 8 | 2 | 2 |
| m5_schema_additional | 8 | 2 | 2 |
| m6_sync | 0 | 0 | 0 |
| m7_gap_latch | 8 | 2 | 2 |
| m8_trace_guard | 8 | 16 | 16 |
| m9_page_budget | 0 | 0 | 0 |
| m9b_doc_bytes | 8 | 4 | 4 |
| m10_nul_guard_raw | 8 | 6 | 6 |
| m11_parse_nul | 8 | 17 | 17 |
| m12_boundary_structure | 8 | 4 | 4 |
| m13_first_available_zero | 8 | 3 | 3 |
| m14_gap_latch_early | 8 | 2 | 2 |
| m15b_view_double | 8 | 17 | 17 |
| m16_sqlite_prefix | 8 | 8 | 8 |
| m17_verify_columns | 8 | 12 | 12 |
| m18_verify_key_index | 8 | 12 | 12 |
| m20b_any_matching_index | 8 | 8 | 8 |
| m21b_skip_column_collation | 8 | 4 | 4 |
| m22_skip_extra_unique_index | 8 | 8 | 8 |
| m25_regex_bounded_zero | 8 | 2 | 2 |
| m26_partial_unique | 8 | 4 | 4 |
| m27_regex_min_seed | 8 | 1 | 1 |

结束复核：2026-10-24T08:31:43.223Z，上述98项输入均与冻结指纹一致，源码清单32/32及VM清单32/32仍匹配。本轮唯一写入为本报告；没有整改、状态变更、commit或push。
