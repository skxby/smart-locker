# S1-02 round-5 独立审计

日期：2026-10-24（Asia/Shanghai）。工作副本：%REPO%。契约：0.2.1 / REVIEW_DRAFT。

## 0. 结论与修改边界

**FAIL，当前快照不满足 S1-02 技术交接或 DONE 条件。1 项阻塞问题（B-1 / P2），2 项非阻塞问题（N-1 / P2、N-2 / P3）。**

- **B-1**：schema 核验跳过所有 partial 唯一索引；合法主键之外的部分唯一约束仍会改变可写入记录集合。真实 DDL 加一个索引就能通过现有核验条件，却让第二条合法 AuditRecord 的原始 UPSERT 失败。
- **N-1**：mutation 的逐条数量已补齐，但 m15b 仍不能证明首次视图 open 假成功被定向捕获，m17/m18 的夹具归因仍错，m25 不是零下界旧缺陷的回退变异；重复点位与独立计数的说明也需分开。
- **N-2**：summary/handoff 的 regex 整改行被正文拼接破坏；模块 README 仍给出违反当前工作区分类规则的构建路径。已清理说明比上一轮一致，但外部残留的“0”本轮无法独立核销。

round-4 B-1 的**原反向排序组合**已静态封住；round-4 B-2 的**源码/证据指纹失配**已闭环，不能把上一轮 30/32、6/8 的结论照搬到本轮。当前 32/32 源文件大小和完整 SHA-256、VM 清单 32/32、mutation 九项短指纹 9/9 均匹配。

本轮独立 Windows Python/SQLite/Node 检查已执行；Linux configure/build、12 项 CTest 调度、11 个 C 测试二进制、ASan+UBSan、TSan、C mutation 与 C 端反例均 **NOT_RUN**。保存的 Linux PASS 是开发方历史记录，本轮未重跑，不能写成本轮独立 PASS。FAIL 有具体实现缺陷依据，不单凭环境受限作结论。

**本轮只新增本文件。** 未修改实现、正式测试、契约/生成物、stage-plan、README、summary/handoff 或前四轮审计；未置 DONE、跨入 S1-03、commit 或 push。源码未授权整改，本报告列修复建议供后续负责人执行。

## 1. 范围、依据与快照

### 1.1 输入依据

读取根 AGENTS、README、阶段入口/机器索引、S1-02 任务卡 :48–56；核对阶段0 :153–174、internal-interfaces :21–43/:98–114、module-ownership :24/:49–57、traceability 六个仓储端口及 S1-01 最新 PASS/DONE 前置。授权路径下未发现额外 AGENTS.md。

生成头的六个签名位于 locker_contracts.h :211/:213/:215/:273/:275/:277，原生 DTO 位于 :57/:119–120/:143–147。内部 schema 共 **107 defs / 15 x-rules / 18 collections**，冻结子集 **47 defs / 18 collections**，实际抽取器 --check 通过。字段/枚举仍以机器真源为准，没有建立替代 API 或放宽卡验收。

核查 repository.c 事务、参数绑定、查询/分页/恢复、事件导入和初始化路径；schema.c、event_key.c、u64.c、regex.c 的相关校验分支；唯一 DDL、嵌入 CMake、抽取工具；五个 storage 正式测试及历轮问题的回归。第三方文件只核验版本/锁/逐文件指纹，不宣称完整安全审计了 SQLite/cJSON 上游源码。

读取本目录已有 **38 个文件（26 个 .log、4 个 check JSON）**，核对日志记录、计数、manifest 和四轮发现；本报告新增后为39个。只读审计不启动新项目或较大实施方案，不重新选型/下载依赖。为核实 partial UNIQUE 和冲突目标，仅查 SQLite 官方资料，见 §3。

### 1.2 快照变化与指纹边界

初始指纹时间为 **2026-10-24T06:25:28.126Z**，复核截点为 **2026-10-24T06:30:37.419Z**。32 项源清单、mutation、VM 清单与日志均保持匹配。观察到 summary.md 在本审计未写入它的情况下变化：

| 对象 | bytes | SHA-256 |
|---|---:|---|
| summary.md 初读版 | 66949 | f8831a774e430a8d6ff789d2e03de2cd77338cd8db621e8633a30ac90fb04715 |
| summary.md 截点复核版 | 67072 | cece41588b3df21333415c706d56e27f88d953e8ae2248cc3b937e0e810e42f8 |

已重新读取其关键证据、整改和结论段，§4 的错误仍存在。没有写入者身份或修改动机证据，不作归属推测。该文档变化没有改变32项源文件绑定，**不另报源码脱节阻塞**。完整受审文件指纹见 §7；结论只适用于这些内容，不自动覆盖后续改版。

## 2. 实际执行与 NOT_RUN

exec_command 在 shell 启动前失败：Failed to create unified exec process / helper_unknown_error: setup refresh had errors，**无 shell 退出码**。随后通过可用 Node fs/crypto 与 child_process.execFile 参数数组执行读取/检查；未提权、安装编译器、改 WSL/SSH 配置或覆盖 VM 源码。

实际 Python：E:/anaconda/python.exe，3.14.6 / Anaconda / AMD64；Python SQLite **3.53.2**。项目 vendored SQLite **3.54.1**，因此本轮 SQL 探针不是项目库的 C 运行结果。工作目录始终为 %REPO%；Python 带 -B，-c 探针只用内存 DB，没有落临时脚本/DB。

| 实际命令/检查 | 退出码 | 结果与边界 |
|---|---:|---|
| git status --short | 128 | fatal: not a git repository；未创建仓库 |
| where.exe python | 0 | E:/anaconda/python.exe |
| python -B -c（sys.version / sqlite3.sqlite_version） | 0 | 上述版本 |
| python -B -X utf8 scripts/check_contracts.py | 0 | PASS；0.2.1，24公开入口、82端口、28 RPC；C declaration 仅 parse PASS，非编译/链接；hardware_profile=UNVERIFIED |
| python -B -X utf8 scripts/check_stage_plan.py | 0 | PASS；45卡，active_task=S1-02，done=1，runtime_or_hardware_tested=false |
| python -B -X utf8 scripts/check_vision_contracts.py | 0 | PASS；camera_owned=false、rknn_available=false、hardware_tested=false |
| python -B -X utf8 scripts/check_feasibility.py | 0 | PASS；runtime_or_hardware_tested=false |
| python -B -X utf8 host/storage/tools/extract_schema.py --check | 0 | OK，27525 bytes，匹配真源 |
| python -B -X utf8 -c（附录 P1 + partial-only 负对照） | 各0 | SQL/元数据模型完成；发现 B-1；不是 C open/commit 实测 |
| python -B -X utf8 -c（附录 P2） | 0 | 视图仍被列结构守卫拒绝的 SQL 归因模型 |
| python -B -X utf8 -c（附录 P3） | 1（预期） | 只在内存给冻结文件加空格，真实 main(--check) 输出 DRIFT；磁盘未改 |
| python -B -X utf8 -c（附录 P4） | 0 | 四组分页字节模型均收齐100条、每页含NUL不超65536；不是 C query/recovery |
| wsl.exe -e bash -lc "cd /mnt/e/stm32项目 && cmake --version && gcc --version" | 4294967295 | Wsl/Service/CreateInstance/E_ACCESSDENIED，命令未在 guest 启动 |
| ssh -o BatchMode=yes -o ConnectTimeout=8 locker-vm 'uname -a' | 255 | stdout/stderr空，未取得guest环境，不推测故障原因 |
| where.exe rg / cmake / gcc / clang / ctest（分别执行） | 各1 | PATH均未找到；因此以Node读取/查找 |
| Node 原始字节 SHA/大小/范围/日志统计 | 不适用 | 32/32源清单、32/32 VM清单、9/9 mutation、6/6 vendor与lock匹配；源清单范围无漏项 |

**本轮 NOT_RUN（未启动，无退出码）**：独立 Linux CMake configure/build/CTest、11个 C 二进制和完整 C 示例、实际 locker_repo_open/commit/query/import 反例、ASan+UBSan、TSan、25批次 C mutation；COMMIT/I/O/磁盘满/DDL中途故障注入；VM当前 WORKSPACE-INDEX/cache/二进制/运行目录核验。没有将 Python SQL/JSON 模型充作 C 覆盖率或 sanitizer 结果。

### 2.1 保存的历史日志核对

VM run_id=**20261024-135713** 与 summary/handoff、普通/ASan/TSan CTest 路径一致；run-summary 中 configure/build/ctest 与 sanitizer exit 均记录0，source-verify保存32个OK。普通/ASan/TSan各 **12/12**，时间分别 **0.38s / 0.74s / 1.37s**；WSL普通为 **0.28s**。11个二进制的断言和为 **1601**：common 505，storage 1096（repository749、regex46、event_key80、schema131、u6490）。漂移门禁是第12个CTest，没有C断言数。

这批日志的源清单确实匹配当前32项内容，足以修正 round-4 B-2 的旧指纹失配事实；但清单不覆盖全部 common/contracts 构建输入，不能单靠32项证明整个src快照绝无其他变化。保存的81MB快照包未在本轮重新取到并计算摘要，不把报告中列出的包摘要当本轮独立验证。

configure/build/ctest 文本没有实际 -fsanitize 编译/链接参数、CMakeCache 或二进制插桩依赖证据；本轮也无法访问构建目录。故只确认“日志记录了这些通过”，不独立确认 ASan/UBSan/TSan 插桩真的启用，且不把没有报告外推为无并发问题。

## 3. 阻塞问题

### B-1 / P2：跳过 partial UNIQUE 仍会接纳改变合法键集合的库

**位置**：host/storage/src/repository.c :1940–1991，直接缺口为 **:1965–1967**；原始 UPSERT 为 :418–419；既有库验证入口 :2158–2161。正式坏结构夹具在 test_repository.c :2258–2360，只有全局额外 UNIQUE，没有 partial UNIQUE 反例。

**依据**：S1-02 :53/56；x-rules 03/13；DDL的 documents(collection,key) 字节主键。部分唯一索引虽然不保证全表唯一，却仍约束满足WHERE的子集，不能因此视为不会改变写入语义。[SQLite partial UNIQUE](https://www.sqlite.org/partialindex.html#unique_partial_indexes) 说明子集唯一性；[index_list/index_xinfo](https://www.sqlite.org/pragma.html#pragma_index_list) 区分唯一性与partial属性；[UPSERT冲突目标](https://www.sqlite.org/lang_upsert.html) 只处理匹配的冲突约束。

**最小复现**：先执行当前 schema-v1.sql，保留合法三表、schema_version=1、snapshot_revision=0和migration version1，再执行：

~~~sql
CREATE UNIQUE INDEX partial_extra
ON documents(collection) WHERE collection='audits';
~~~

这是在真实 WITHOUT ROWID DDL 上只加一个索引。所有列仍是预期类型、NOT NULL/主键位置和默认 BINARY；原主键的 index_xinfo 仍恰好 collection/key ASC BINARY。新增索引在 index_list 中是 **unique=1、partial=1**，当前 :1966 continue，原主键使 matched_unique=1，因此结构检查静态推导仍OK；元数据和quick_check也通过。

**预期**：两种 initialize_empty 都 CONFLICT、out=NULL，目标不补建/清空；不兼容必须在open明确报错。

**实际证据**：Python内存SQL已运行（附录P1），两条不同UUID的 RepoDocument/AuditRecord 先通过当前正式 Python 契约验证，再用仓储原始UPSERT写入。官方DDL正对照两条成功；加partial索引后prepare成功、首条成功、第二条失败：

~~~text
official model_accept=true
  EXACT_UPSERT_PREPARE_OK; SECOND_VALID_AUDIT_OK; document_count=2
partial_extra model_accept=true
  index partial_extra unique=1 partial=1 key=[collection ASC BINARY] SKIP
  index sqlite_autoindex_documents_1 unique=1 partial=0 key=[collection,key ASC BINARY] CHECK
  metadata=[schema_version=1,snapshot_revision=0]; quick_check=ok
  EXACT_UPSERT_PREPARE_OK
  SECOND_VALID_AUDIT_ERROR UNIQUE constraint failed: documents.collection
  document_count=1
~~~

**C返回值边界**：本轮没有运行 C open/commit，不能说已经实测locker_repo_open返回OK；这是由现有分支确定的源码推导，SQL约束失败是实际运行结果。失败按 :71–72 映射CONFLICT，事务可回滚，所以这里不声称静默丢数据或错误开锁；实际影响是损坏/不兼容库被当成可用库，之后合法账本/审计事务被额外约束拒绝。

相邻对照已核查：真WITHOUT ROWID主键与等价rowid主键模型通过；round-4反向NOCASE主键+好BINARY索引、额外全局UNIQUE模型拒绝；只有partial而无主键的表因pk位置及matched_unique=0拒绝。这些对照说明新代码不是完全没修，漏洞集中在“跳过额外partial UNIQUE”。另测partial NOCASE索引被跳过，仅作元数据相邻模型，不将大小写非规范UUID作为合法业务反例。

**修复建议**：不把partial作为额外唯一约束的免检理由。最简单的fail-closed策略是拒绝未知partial UNIQUE；若接受某类，需证明其约束完全冗余且不改变合法记录集合。保留非唯一索引的合理放行。给documents和meta补有害partial唯一夹具、两个initialize_empty、out=NULL和原对象保留检查，保留真DDL正对照；用删除这条拒绝的定向变异验证。不能仅补一个SELECT的COLLATE或把索引改成普通全局UNIQUE用例冒充覆盖。

## 4. 非阻塞问题与整改证据评价

### N-1 / P2：mutation 明细数量修正，目标归因仍不成立

1. **m15/m15b**：mutation.log :33–39仍说三点联合能恢复旧不安全行为。实际m15b :249–271的17条明细与汇总一致，但 test_documents_view_rejected 的首次 open(true) 在 test_repository.c :2122，**没有这条FAIL**；唯一视图FAIL是 :2125 的open(false)，CONFLICT变NOT_READY。:2469/:2473/:2474/:2485是 bare.db；:2195–2216是前缀对象；:2378/:2379是struct_bare。即使关掉类型/视图/痕迹三处，初始化前的 verify_schema_tables :2146仍核列，视图列 type为空、notnull=0、pk=0，拒绝并ROLLBACK。附录P2实际SQL模型也得到只剩原视图。可据此证明返回分类/有痕迹保护有回归，**不能据此声称首次视图假成功被这个mutation捕获**。生产路径本身静态拒绝视图，不把变异归因错误写成当前生产视图漏洞。
2. **m17/m18**：数量分别12/8且每条明细齐全；分组已能按调用点精确定位。m17来自 struct_extra :2343/2344、struct_colcoll :2347/2348、struct_wrongtype :2351/2352，每组两次status+两次helper :2250，共4条；m18来自 struct_revcoll :2355/2356与struct_extrauniq :2359/2360，共8条。summary :236却写“缺列/多列/NOCASE”，:237/240写成四组；mutation.log :55–57又说共享站点无法归因。缺列/无PK仍被其他守卫拒，普通NOCASE仍受列排序检查，不是这两段的目标kill。
3. **m23/m24和计数**：明确承认它们重复m17/m18，是改善。25个具名记录确有23条ctest=8、2条=0（m6/m9）；按这两组声明的重复点位去重后是**23个点位、21捕获、2未捕获**。summary :255称不计独立覆盖却仍用23作捕获计数，应区分“25次运行/23红”与“去重后的独立点位”；m20b/m22的逻辑和失败集合还高度重合，不能用点位数代替独立缺陷数。
4. **m25**：mutation.log :397–403把循环回退到index=1，唯一FAIL是 test_regex.c :28（正下界{8}/{14}/{20}分支错误接受9位），不是新增 test_bounded_repeat :73–108 的{0,1}/{0,19}断言。当前代码 :532–534在min=0已经produced=1，统一从1循环不会恢复“再多生一个零下界可选atom”的旧缺陷。应将零下界produced回退到0等精确定向变异，并保留{n,n}/{m,n}边界对照；不能把m25红行当旧零下界缺陷的定向非空洞证据。
5. **m11/NUL**：现在17条明细与17/749一致，旧“13但仅12条”不再成立。正式nul_case_expect_invalid* :1869–1928每例独立DB、先建立revision1基线，属性名改为name+NUL+junk，:2043–2058有干净正对照，key头NUL明确仅补充。旧串行revision污染已从测试设计消除；mutation.log :58–61和summary :230/:254仍把后段红行说成跨用例连带推进，解释已过期。一个用例内的revision/文档数断言变红是预期后置检查，不能与跨用例污染混淆。

建议按最终调用行/夹具重写证据说明，给m15b和零下界单独明确目标，不手改旧日志凑目标FAIL。保留变异补丁/定义与完整原始输出以便复验；脚本已按报告删除，仅edit摘要不能证明本轮独立复跑。未发现明细数不一致不等于确认记录来源；本报告不据此指控伪造。**本轮全部C mutation仍NOT_RUN。**

### N-2 / P3：交接文档内容损坏与工作区入口过期

- summary.md **:324–329** 的N-3行在正则“^a{0,1}”后直接拼入文档标题、日期和状态正文，表格中断，缺少完整整改/回归列；handoff.md **:583** 同样拼入自身标题后直接进入§13。这不是完整的regex整改记录。应由负责人直接重写为当前事实，避免继续追加补丁段。check_contracts通过不证明Markdown表格或说明内容正确。
- summary.md **:186** 与 handoff.md **:401/:493/:501** 写合计1583，实际两个 unit-tests.log 的11行求和均为 **1601**（505+1096）。各测试单项数字匹配，错误在总和，不能继续沿用。
- host/storage/README.md **:68–71**仍使用HOME/locker-build/s1-02，summary :160还说HOME/s102_ev符合当前工作区纪律。当前AGENTS与vm-home-layout要求项目生成物归HOME/locker-workspace的build/logs/artifacts分类；“源码外”不是全部条件。summary :407、handoff :540还写S1-02/{src,build,logs,artifacts}，与实际新VM日志的locker-workspace/build/S1-02/批次不一致。修正当前执行命令/目录说明，不改写既有历史日志。
- 已修内容予以确认：handoff :162–164和test_repository :1303–1307正确区分MCU last_committed与RK through/committed_cursor；summary/handoff清理说明已不再互相矛盾，m4锚点解释也已改正。但summary :226仍把 :2469–2485的bare.db叫struct_bare，属于N-1的同类误归因。
- “HOME_LEFT/PUBLIC_LEFT/VARTMP_LEFT/REPO_TMP=0”在summary :405是开发方自述；本轮无法访问WSL/VM、未全量检查Public/TEMP，不能核销全部外部残留。主机根未见.tmp*/s102_，storage两个源码目录无构建/DB/log/pyc残留；未删除他人快照、正式测试或日志。没有把未知残留直接当垃圾处理。

## 5. 历轮逐项复查

下表“静态封住”只表示指定旧反例不再走原错误路径；C运行均NOT_RUN，历史日志等级见§2.1，不能据一条关闭推断整卡PASS。

| 原编号 | 当前落点/正式回归 | round-5判断 |
|---|---|---|
| 首轮 B1 | repository :1397–1422/:1526/:1680–1700；test_repository :1160/:1349/:1415/:1494 | 页内+1、换boot递增且seq1、旧水位回填拒绝、gap锁存静态成立；跨boot语义仍依赖设备正确报gap，不宣称仓储能证明跳过boot没有事件 |
| 首轮 B2 | repository :1535–1571；test_repository :1160–1298/:1308–1346 | 非空零尾/异设备epoch/越尾/next_after/包络旧反例静态封住 |
| 首轮 B3 | repository :2125–2161，DDL显式BEGIN/COMMIT/ROLLBACK；bare/view/结构测试 | 原meta-only补建已封；兼容性整体仍因本轮B-1未通过；DDL中途故障注入仍缺 |
| 首轮 B4 | repository :954–1110；test_pagination_budget :1751–1859 | 完整序列化字节+1160B续页余量；四组字节模型通过；m9仍漏检，不外推C通过 |
| 首轮 B5 | repository :483、schema :35–62/:730/:749；独立NUL夹具 :1869–2099 | 入口防护和非空洞测试设计成立；旧属性名/串行污染已修；说明仍见N-1 |
| 首轮 N1 | handoff :170传NULL、:196区分OK/ACK条件 | 核心调用与ACK说明修正；示例 :158–159仍需UART填事件，未作为完整独立可编译例运行 |
| 首轮 N2 | 正常close/reopen测试 :800–899 | 证据限定正常重开，不外推SIGKILL/掉电 |
| 首轮 N3 | trigger在第二次写ABORT :587–589；失败无半写、删除trigger正对照 :608–647 | 正式测试设计有效；FULL同连接运行观测/m6仍未覆盖 |
| 首轮 N4 | deployment lock/README、storage README | 6文件vendor与lock一致；旧未vendor事实已修；当前构建路径仍见N-2 |
| 首轮 N5 | u64复用common，schema :304复用解析；extract_schema/CTest | 当前47defs/18集合一致；内存漂移负对照exit1；缺Python时CTest只消息SKIPPED，不能称无条件12项 |
| R2-B1 | repository :1634–1677；test_duplicate_page_latches_gap :1451–1489 | 全重复gap锁存并保留through、无投影；指定旧反例静态封住 |
| R2-B2 | repository :1384/:1510/:1541/:1554，边界正式负例 :1243–1278 | 半零/非空零首静态封住；每条事件半零检查有代码，仍无独立专用正式负例 |
| R2-B3 | repository :256/:291/:2125/:2146；view测试 :2106–2143 | 当前生产视图首次open静态拒；m15b归因仍见N-1 |
| R2-N1 | 独立NUL夹具、bootjump起点true :1434/1443 | 两者设计已补；DDL中途失败仍未注入，不能笼统称全闭环 |
| R2-N2 | handoff :186/:192/:196 | boot递增和OK≠可ACK主要说明已修；示例未独立编译 |
| R2-N3 | mutation9指纹、m4非规范输入、m9b分母734 | 指纹/分母已改正；目标归因与点位统计仍N-1 |
| R2-N4 | 1160B、m8 trace_guard、m6/m9诚实未捕获 | 数值/命名已修；不是FULL或DDL故障的运行证明 |
| round-3 B-1 | repository :291 GLOB；前缀表/视图正式测试 :2188–2217 | 原LIKE下划线漏对象静态封住；m16记录8红，未独立重跑 |
| round-3 B-2 | repository :1830–2030；结构测试 :2314–2381 | 缺列/无PK/类型/列排序/原全局索引反例静态封住；partial仍本轮B-1 |
| round-3 N-1 | m15移出批次、m15b逐行记录 | 无效单点已如实登记；首次视图目标未获m15b证明，N-1未全闭环 |
| round-3 N-2 | name+NUL+junk、每例干净DB、合法正对照 | 旧空洞属性名和跨用例revision污染静态修正 |
| round-3 N-3 | UART包络、README active_task、当前日志数 | 主要字段归属/状态已修；文档破损/误归因仍N-1/N-2 |
| round-4 B-1 | 每一个全局unique都核、反向/extrauniq两夹具 | 原反向组合已封；不能据此宣称所有唯一约束兼容，见本轮B-1 |
| round-4 B-2 | 32/32源和VM清单、9/9短指纹、新run_id | 指定源码/证据脱节闭环；保存Linux结果仍非本轮独立执行 |
| round-4 N-1 | m11明细已齐、重复点位声明 | 数量改善；m15b/m17/m18目标解释仍未闭环，见N-1 |
| round-4 N-2 | MCU/RK归属已修、清理表述统一 | 归属闭环；目录/清理核销/夹具误名仍见N-2 |
| round-4 N-3 | regex :526–575，test_bounded_repeat :73–108 | {0,n}产生n个可选atom；{n,n}产生n个必选；{m,n}产生m必选+n-m可选，静态正确；C未运行，m25目标不对应旧零下界缺陷 |

## 6. 按卡交接判定

| 卡要求 | 结论 |
|---|---|
| 唯一DDL/18受控集合/六端口/原生DTO | 静态存在、抽取漂移检查PASS；未改共享契约 |
| 占位/操作/幂等/outbox/revision原子性 | BEGIN IMMEDIATE内检查revision/checks、顺序mutation、写meta、COMMIT/回滚；正式半写SQL失败正负对照成立；C执行NOT_RUN |
| 竞争申请一个成功 | 两fork进程独立open后竞争；保存日志称通过；本轮NOT_RUN |
| 正常重开保留借用/未决/幂等 | 正式测试核查借用者、closed_at、操作状态、outbox和响应状态；本轮NOT_RUN；不证明崩溃/介质掉电 |
| 事件去重/投影/游标同事务 | 重复输入不重复投影；gap锁存和回填拒绝静态成立；本轮C导入NOT_RUN |
| 查询/分页/恢复/u64 | 受控参数绑定；单键/after互斥；数值比较和20位补零；正式分页+模型存在；本轮C运行NOT_RUN |
| WAL/FULL | 实现显式设置；WAL正式测试存在；返回mode未在生产逐字核，FULL同连接运行断言缺失；不宣称实际介质fsync/掉电可靠 |
| 损坏/不兼容不自动清库 | 原损坏/视图/半初始化反例防护存在；**B-1仍FAIL** |
| 当前源码证据绑定 | 32/32、9/9成立；旧round-4失配已关闭；独立Linux复跑NOT_RUN |
| 生命周期/职责边界 | Core单所有者、Runtime独占开关、out=NULL/close(NULL)、busy_timeout5000ms、绑定/语句清理；无后台线程/动作发送；THREADSAFE不授权cJSON并发 |

状态保持 S1-01=DONE；S1-02=IN_PROGRESS、report=null；active_task=recommended_next=S1-02；阶段1 gate_result=NOT_RUN（门禁S1-07）；S1-03=NOT_STARTED。**不建议技术交接/DONE，不自动跨卡。**

## 7. SHA-256 与输入清单

以下为原始字节摘要，不做换行归一化。源码行号以上文对应的 repository2206行、test_repository2584行、regex768行、test_regex120行等版本为准。source-manifest中的32个文件均复算大小和完整摘要；其中6项vendor也匹配dependency lock。没有重新下载归档，归档同源仅保留开发方历史声明。

### 7.1 当前32项源文件

| 文件 | bytes | SHA-256 |
|---|---:|---|
| CMakeLists.txt | 3943 | f9e1964ee54465f5e29299721a3f342f929cc81e3ff6a6da8d00b77fc34258b4 |
| host/storage/CMakeLists.txt | 3782 | 3acc9e405c66569b988db32b7cb0e135b8a3a4c52fdd53f3e606e01fee49297a |
| host/storage/README.md | 7289 | 0fc68d2ea2825ac5b6fa05e96d3d168e50fe8f603ca11da6dc8c3e5bb0f779e3 |
| host/storage/cmake/embed_file.cmake | 1891 | c64242387b74017b03c9ce9559e5bc245eda0f83d5636ea989a3740ded028c7e |
| host/storage/include/locker/storage/embedded.h | 898 | f8087fd75b43b6c35d17fd400dacf95f6f4177b937e81278783a4d093441ab7a |
| host/storage/include/locker/storage/event_key.h | 1660 | 4d46736869d62e688540dc2c0a10f3c0c71fe6302898266b1d836f143c705d73 |
| host/storage/include/locker/storage/regex.h | 1130 | 39fdb69f6fb58397e82810c3778d497b117944449527fc5b4d040129b1c03ade |
| host/storage/include/locker/storage/schema.h | 2197 | 547f9b203eb922cdb6222c0ec419bfe77b0e6899dfa010b671c90704d6bd987f |
| host/storage/include/locker/storage/u64.h | 835 | 08cdc34af372ce6cb3d0b316b6270a827aa3cdf5f007b8f9f61bd39445959f27 |
| host/storage/schema-v1.sql | 2350 | 0eaa403ba7437c855d35a556443564ee6f1e9fd1cc2d5c967aa71c4f17ed8f70 |
| host/storage/src/event_key.c | 7165 | 2229c561f45518300ce74eff4fdc5d582f8b7c6a5a30f3c07aa1adb532369edb |
| host/storage/src/regex.c | 25127 | b01a90e58f1869c9d884df4ebfe16ab7b536b380924b5742e16f7a6dfe2f50c8 |
| host/storage/src/repository.c | 84009 | b431d035de62e3d1ac4721300cd35ce16586d63a5d9f14bca2c77c490e54f965 |
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
| tests/unit/storage/test_repository.c | 114784 | 9cadec28c58de51a1253b4419c0c8f256baeeabc6bc61fe4792583b248f57d73 |
| tests/unit/storage/test_schema.c | 31269 | 4952adc2876f8a1c37d83ad42968ede1cd40d66a6a582862cd0d75941a20e2ca |
| tests/unit/storage/test_u64.c | 6040 | 6919ee37502373a4f0c19ef7ca6134820de333e150df37d353c02bb6ac9652e7 |
| host/storage/third_party/cjson/LICENSE | 1084 | a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c |
| host/storage/third_party/cjson/cJSON.c | 80399 | 298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d |
| host/storage/third_party/cjson/cJSON.h | 16394 | 25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee |
| host/storage/third_party/sqlite/sqlite3.c | 9582521 | 0c657666d819f261cc4007526ad57e1669de9949df7a8ea847aa284a2bf0ee52 |
| host/storage/third_party/sqlite/sqlite3.h | 695729 | d6c84557261893439b341d2e6c35187ae3ea24fbfd843ad94616f72d89242b8c |
| host/storage/third_party/sqlite/sqlite3ext.h | 39547 | 48963abfefb41ac329ef9d706acb5eadba952a99a748285c9ded6b5e0942721b |

### 7.2 契约、文档与已有证据

| 文件 | bytes | SHA-256 |
|---|---:|---|
| docs/evidence/stage-01/S1-02/asan-build.log | 3422 | ca5bf9298075f90d5b175577689cf6a587f1cab2b945f63e4b03531d01851728 |
| docs/evidence/stage-01/S1-02/asan-configure.log | 592 | b00a669179ac3d6fe1850f318d66a51cf57ebf2f7a868af6e4c351281371497f |
| docs/evidence/stage-01/S1-02/asan-ctest.log | 1437 | 6d2eb15e5fe2b081dd02e755ccd1cdf21ebed3a2d0b74312cfe96630392e32cd |
| docs/evidence/stage-01/S1-02/audit-round2.md | 32835 | 3e2475309e154b3071269f4487ecd4ba28993c84cead38c7ee56e098797922f7 |
| docs/evidence/stage-01/S1-02/audit-round3.md | 40534 | 2d0ea66edca835e695ff297d0297c0229d4671bd68863a7596490fdcbc75b46a |
| docs/evidence/stage-01/S1-02/audit-round4.md | 52973 | f5c206a55f5a72d61ae4e6ba582c1573e84c77f5224131f50898213587487ac0 |
| docs/evidence/stage-01/S1-02/audit.md | 23283 | 80ee21d98ed0877e2a3469a3ae9389f3ddc19663a4bf569bd39a25362ee3c8f8 |
| docs/evidence/stage-01/S1-02/build.log | 3422 | 203a5ab2f07306c6a73909a93b8e70b125cf288dc7b0f9269a80671286ecbfd1 |
| docs/evidence/stage-01/S1-02/check_contracts.json | 535 | 0ac0a396fffb701522e05b2ff5f8c7725834a2641e59c109f41a62401870087d |
| docs/evidence/stage-01/S1-02/check_feasibility.json | 242 | cbe1da1d34c5d29634377f6a55c0199233a144e984417ba06553c9c5e149f02b |
| docs/evidence/stage-01/S1-02/check_stage_plan.json | 319 | 572e375df64f10218f6e440803dcff0162f846df19877d5b77692e91683250d0 |
| docs/evidence/stage-01/S1-02/check_vision_contracts.json | 572 | 3f8b58703a9d039f594749a6924e19eda4e312d97cbdbf0ee1dac3b6325d8778 |
| docs/evidence/stage-01/S1-02/configure.log | 587 | 6c4cbdcfb174028bdaa23c8961be1d6395bc5a34ebcd531609b6c2df094d38fd |
| docs/evidence/stage-01/S1-02/ctest.log | 1432 | 2539b436f52706a71389d45d1df11fbd7259289dccd1784a82c8aadc3334470d |
| docs/evidence/stage-01/S1-02/design-checks.log | 1770 | d835968bf62ff8c1a93292a1bfe71e573a4d7627c46d61ec8f4c73e0235d13c7 |
| docs/evidence/stage-01/S1-02/handoff.md | 45538 | 5ddc0f7362771769030ff0923c6e3d244af626dcf6a3e5c7ac29ee4ae52673ce |
| docs/evidence/stage-01/S1-02/mutation.log | 30885 | a3f29e305271c580ef5d30e22209356f014bef5afdb0d516485364719303c734 |
| docs/evidence/stage-01/S1-02/source-manifest.txt | 3624 | 10c578151abed165ef23b0b589c45eb13a043040833f38ba9d6d211824c9c0e3 |
| docs/evidence/stage-01/S1-02/summary.md | 67072 | cece41588b3df21333415c706d56e27f88d953e8ae2248cc3b937e0e810e42f8 |
| docs/evidence/stage-01/S1-02/tsan-build.log | 3422 | 73fd11e55982a7746842af17e1813ed139268d1499ac3ab955ea54af2e172414 |
| docs/evidence/stage-01/S1-02/tsan-configure.log | 592 | 7e37f6f15a08373c45bfc4e41c66329e8afcb122eeaa68bc4d2c731072e21f76 |
| docs/evidence/stage-01/S1-02/tsan-ctest.log | 1437 | 986ce8d960de01e5835118bc8c88eea826f69d5e668f59dbab059eec2d658ed1 |
| docs/evidence/stage-01/S1-02/unit-tests.log | 424 | 4866a0847ac2a71ae246f3741a1a9f458bdb581f7ae2d62a8b1b880a399b31f2 |
| docs/evidence/stage-01/S1-02/vm-s102-asan-build.log | 3422 | 2db3d245f07bb42cfd0e936d8743c046a9d8c4d0426df3d2387bfb541b0eb6d1 |
| docs/evidence/stage-01/S1-02/vm-s102-asan-configure.log | 619 | e929c9c94a2418f91db9a6be63ffa32483f698543a3abc0622914f611cf2faa0 |
| docs/evidence/stage-01/S1-02/vm-s102-asan-ctest.log | 1464 | c7af7f71364a66658bd9942e1a669c415a20371b78c792eefd9fdf7b2a0a3879 |
| docs/evidence/stage-01/S1-02/vm-s102-build.log | 3422 | e30a17f12ca6325a904adcfed1529148831f3c5c70a4bdc56249784e7586e472 |
| docs/evidence/stage-01/S1-02/vm-s102-configure.log | 614 | bae3b2f3db42692383b24ad917064475c31dc5817a13a255748861cd2a9d61ba |
| docs/evidence/stage-01/S1-02/vm-s102-ctest.log | 1459 | e8fdb58b3eb361580c23eaab7df050aa9a39f138bd32b4d0050004697ee23fef |
| docs/evidence/stage-01/S1-02/vm-s102-design-checks.log | 1984 | 21696c1399e3e7c975709f95260c88db478f37a8e10deceef85ef7a1b4940ad0 |
| docs/evidence/stage-01/S1-02/vm-s102-environment.log | 279 | c295eb610d35a2d9b630ef3d0a7d7e05d03db29fdf779638cb9737758ebafdf6 |
| docs/evidence/stage-01/S1-02/vm-s102-manifest.sha256.txt | 3231 | f9a25aadb948ee7f90e30ea2d9a8c380899f548b667f8628488579230d70d4a2 |
| docs/evidence/stage-01/S1-02/vm-s102-run-summary.log | 819 | 1116bb7a1602cd4ffbff687715d2fa6b5bfef396f58cce2ce371a72373615207 |
| docs/evidence/stage-01/S1-02/vm-s102-source-verify.log | 1247 | eb2aadd332b7dab9d0f6ebe738354798b922c382b5b0915546a6df47956e4652 |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-build.log | 3422 | 7f2b8b47252e335a3150b1099e01ef040169dbff48eeba9e6ea972886c14991d |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-configure.log | 619 | 9a08865d41d5ec9b58534494a9001da45ec92692ecb1299e3b9ef37b49528fa0 |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-ctest.log | 1464 | c48f4428f626bda5635b56f78f949c9f01fca0db8be7cebcc5a71e96a35bf62a |
| docs/evidence/stage-01/S1-02/vm-s102-unit-tests.log | 358 | 50393d668f7c651a0264ae0e56dab0f95337ddb1941a4066df0cb7858d9f6cb4 |
| AGENTS.md | 5839 | bb8658418e3926ad7799f3be148107731c61fc1aa3b668992dd80ee0e1d217de |
| README.md | 7859 | 8c7c835c42208da55a92b702d5ccc7f64bee5ae3744b4a023037f35babeebd54 |
| docs/stages/README.md | 4985 | 24ff25b010d838e05b389b56f825068d499578fa20578d5b286eb67be9c86d06 |
| config/development/stage-plan.json | 24749 | 1ed89d9f5695c40f965f43d331907295eccca21eaf52d67bee92e5dc4fdf1b47 |
| contracts/internal-data.schema.json | 129569 | e7b8695c49115e2994cf126dc5ab629e5c82460e70d4c292b19576f55c60e386 |
| contracts/internal-data-source.json | 92193 | ebcfa5859c114e8746a2f235161c17a6dede651f56125d92cc37f259c7d81da1 |
| contracts/include/locker_contracts.h | 39925 | 34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a |
| docs/architecture/uart-v1.md | 26022 | 5bb2204f291b259f7b28cc7a2a1473352d5f1d0d501032168924d664c5e3ed8f |
| contracts/contract-source.json | 66553 | 14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f |
| docs/stages/stage-01-task-cards.md | 16460 | 7f7561e83f9f614fd4ed226cefe3ce78ba367859a55ee92cd82212b25273fc93 |
| docs/stages/stage-00-architecture.md | 22164 | 2e85508b23aff242f1ce6cbd166464e5a85966f8ead4f1dd8398c5231443227b |
| docs/architecture/internal-interfaces.md | 29792 | a5b3cdc41c05006e83edc970c46c15f79644adb8fe071b71953f239a64c1c2cf |
| docs/architecture/module-ownership.md | 10135 | b182fe6e424b605fd4cad1073c85178588ee474ca8ab6f38be84e3129f72bb3f |
| docs/architecture/traceability.md | 12694 | 0903b559a6fc33fa0120d608b5b85783f66327b03a0f8267e7b86309877f2859 |
| deployment/host/dependencies.lock.json | 14075 | 4884c4ca9944ccd66be72db71dcd99b289bba119763c965a7e3b7b7a961fac6d |
| deployment/host/README.md | 6753 | 9887fe18df7f964a66725cd0ae5ee3a3bc7d37fcc58fcdbde229da3d90c7bc7a |
| docs/development/vm-home-layout.md | 3198 | eca7e274e3a0476b7019228b5ffcd853853e89002b9c864128d9ccc6ecd56228 |
| docs/evidence/stage-01/S1-01/summary.md | 37101 | 22951612245088deffb8fbb07552f9c7e30642374fc24cecad9de10745b23abc |
| docs/evidence/stage-01/S1-01/audit.md | 50244 | 959d1614d590de99a72f1fb66e909aa15d8d3fe5c33d905d71d729c50fa01df8 |

## 8. 后续整改与残留

1. 在本卡授权范围内修复B-1，补partial唯一约束的正负对照和两种开库/保留检查；保持真源与公共API不变。
2. 更正N-1目标归因/去重统计，恢复summary/handoff完整表格，统一当前构建路径到正式工作区。保存旧日志，补新证据时用新批次。
3. 停止源码变动后，先清本批明确临时残留，再生成完整清单，在既有VMware Ubuntu按工作区规则独立configure/build/12项CTest；保留sanitizer实际编译/链接参数/插桩证据和mutation补丁/原始输出。审核通过前维持IN_PROGRESS。本报告不代执行这些未获整改授权的步骤。

报告写入后重跑四项设计检查与抽取器 --check 均退出0；89项已记录输入在截点后的再次摘要核对没有变化。

本轮只创建audit-round5.md；没有debug print、临时API、mock、脚本、磁盘DB、构建产物或pycache需要清理。只读模型放在本报告附录，属于正式可复验证据。既有Public/TEMP/WSL/VM的残留状态无法核销，位置/用途/风险已列N-2，应由能访问环境的收尾负责人核查；本轮不要求用户盲目确认删除，也不删除正式测试、诊断日志或快照。

未完成硬件/范围外验证：RK eMMC/SD掉电与fsync、ARM/32位、CMake3.16下限、MCU/littlefs/真实UART/锁脉冲硬截止/GPIO/电气、相机/RKNN/NPU、Android/LVGL、真实HTTPS/IPC及完整借还闭环。静态检查、Python模型和x86历史日志均不把这些升级为完成。

## 附录：本轮实际 -c 探针

以下分别从项目根通过 E:/anaconda/python.exe -B -X utf8 -c 执行。P1/P2/P4退出0；P3预期退出1。均在内存运行，没有写测试库或覆盖契约。P1复制当前列/索引核验条件用于解释分支，不是C实现替身验收；这些DDL的列默认均为BINARY。

### P1：partial唯一索引反例与相邻正负对照

~~~python
import sqlite3,pathlib,json,sys
sys.path.insert(0,'scripts')
from check_contracts import validate_value
s=json.loads(pathlib.Path('contracts/internal-data.schema.json').read_text(encoding='utf8'))
sql=pathlib.Path('host/storage/schema-v1.sql').read_text(encoding='utf8')
cols={'meta':[('name','TEXT',1,1),('value','TEXT',1,0)],'migrations':[('version','INTEGER',1,1),('applied_at','TEXT',1,0),('checksum','TEXT',1,0)],'documents':[('collection','TEXT',1,1),('key','TEXT',1,2),('value_json','TEXT',1,0)]}
keys={'meta':['name'],'documents':['collection','key']}
def check(c):
 ok=True; details=[]
 for t,spec in cols.items():
  ok &= c.execute('SELECT name,type,[notnull],pk FROM pragma_table_info(?) ORDER BY cid',(t,)).fetchall()==spec
  if t not in keys: continue
  matched=0
  for n,u,p in c.execute('SELECT name,[unique],partial FROM pragma_index_list(?)',(t,)):
   xx=c.execute('SELECT name,[desc],coll FROM pragma_index_xinfo(?) WHERE [key]=1 ORDER BY seqno',(n,)).fetchall()
   skipped=not u or bool(p)
   details.append([t,n,u,p,xx,'SKIP' if skipped else 'CHECK'])
   if skipped:continue
   if xx!=[(k,0,'BINARY') for k in keys[t]]:ok=False
   else:matched+=1
  ok &= matched>0
 return bool(ok),details
cases={'official':None,'rowid':'DROP TABLE documents; CREATE TABLE documents(collection TEXT NOT NULL,key TEXT NOT NULL,value_json TEXT NOT NULL,PRIMARY KEY(collection,key));','reverse':'DROP TABLE documents; CREATE TABLE documents(collection TEXT NOT NULL,key TEXT NOT NULL,value_json TEXT NOT NULL,PRIMARY KEY(collection,key COLLATE NOCASE)); CREATE UNIQUE INDEX binary_key ON documents(collection,key);','full_extra':"CREATE UNIQUE INDEX full_extra ON documents(collection);",'partial_extra':"CREATE UNIQUE INDEX partial_extra ON documents(collection) WHERE collection='audits';",'partial_nocase':"CREATE UNIQUE INDEX partial_nc ON documents(collection,key COLLATE NOCASE) WHERE collection='audits';"}
up='INSERT INTO documents(collection, key, value_json) VALUES (?1, ?2, ?3) ON CONFLICT(collection, key) DO UPDATE SET value_json = excluded.value_json'
for name,ddl in cases.items():
 c=sqlite3.connect(':memory:');c.executescript(sql)
 if ddl:c.executescript(ddl)
 accepted,details=check(c)
 print(json.dumps(dict(case=name,model_accept=accepted,indexes=details,metadata=c.execute('SELECT * FROM meta ORDER BY name').fetchall(),quick=c.execute('PRAGMA quick_check').fetchall())))
 if name not in ('official','partial_extra'):continue
 docs=[]
 for i in (1,2):
  k=f'{i:08x}-0000-4000-8000-{i:012x}'
  value=dict(id=k,actor_id=None,operation_id=None,type='AUDIT_PROBE',reason='probe',occurred_at=None,time_quality='UNSYNCED',source_event=None)
  d=dict(collection='audits',key=k,value=value)
  assert validate_value({'$ref':'#/$defs/RepoDocument'},d,s['$defs'])
  docs.append(d)
 c.execute('EXPLAIN '+up,('audits',docs[0]['key'],json.dumps(docs[0]['value']))).fetchall();print(name,'EXACT_UPSERT_PREPARE_OK')
 c.execute(up,('audits',docs[0]['key'],json.dumps(docs[0]['value'])))
 try:c.execute(up,('audits',docs[1]['key'],json.dumps(docs[1]['value'])));print(name,'SECOND_VALID_AUDIT_OK')
 except sqlite3.Error as e:print(name,'SECOND_VALID_AUDIT_ERROR',str(e))
 print('document_count',c.execute('SELECT count(*) FROM documents').fetchone()[0])
print('SQLite',sqlite3.sqlite_version,'SQL/metadata model only; column defaults in these declarations are BINARY. C open/commit NOT_RUN')
~~~

partial-only无主键负对照（退出0）：

~~~python
import sqlite3,pathlib
sql=pathlib.Path('host/storage/schema-v1.sql').read_text(encoding='utf8')
c=sqlite3.connect(':memory:');c.executescript(sql)
c.executescript("DROP TABLE documents; CREATE TABLE documents(collection TEXT NOT NULL,key TEXT NOT NULL,value_json TEXT NOT NULL); CREATE UNIQUE INDEX only_partial ON documents(collection,key) WHERE collection='audits';")
print('only_partial_columns',c.execute('SELECT name,type,[notnull],pk FROM pragma_table_info(?) ORDER BY cid',('documents',)).fetchall())
print('only_partial_indexes',c.execute('SELECT name,[unique],partial FROM pragma_index_list(?)',('documents',)).fetchall())
print('expected_rejected: pk positions wrong and matched_unique=0; SQL model only, C NOT_RUN')
~~~

### P2：m15b三守卫变异仍受结构检查保护

~~~python
import sqlite3,pathlib,runpy,sys
c=sqlite3.connect(':memory:')
c.execute("CREATE VIEW documents AS SELECT 'loans' AS collection,'x' AS key,'{}' AS value_json WHERE 0")
c.executescript('BEGIN IMMEDIATE;'+pathlib.Path('host/storage/schema-v1.sql').read_text(encoding='utf8'))
a=c.execute('SELECT name,type,[notnull],pk FROM pragma_table_info(?) ORDER BY cid',('documents',)).fetchall()
print('view_columns_after_three_guard_mutation',a)
assert a!=[('collection','TEXT',1,1),('key','TEXT',1,2),('value_json','TEXT',1,0)]
c.rollback()
print('after_structure_rejection_rollback',c.execute('SELECT type,name FROM sqlite_master').fetchall())
print('m15b first open still rejected by verify_table_columns; SQL attribution model only, C mutation NOT_RUN')
~~~

### P3：实际抽取器的内存漂移负对照

~~~python
import runpy,pathlib,sys
m=runpy.run_path('host/storage/tools/extract_schema.py');fn=m['main'];original=pathlib.Path.read_bytes;frozen=m['FROZEN']
def fake(p):
 b=original(p)
 return b+b' ' if p==frozen else b
pathlib.Path.read_bytes=fake
sys.exit(fn(['--check']))
~~~

### P4：当前预算常量的UTF-8分页字节模型

~~~python
import json,pathlib,sys
sys.path.insert(0,'scripts');from check_contracts import validate_value
s=json.loads(pathlib.Path('contracts/internal-data.schema.json').read_text(encoding='utf8'))
def enc(x):return json.dumps(x,separators=(',',':'),ensure_ascii=False).encode('utf8')
for reason in ('验'*256,('验"\\\n'*64)):
 docs=[]
 for i in range(100):
  k=f'{i:08x}-0000-4000-8000-{i:012x}'
  d=dict(collection='audits',key=k,value=dict(id=k,actor_id=None,operation_id=None,type='PAGE_BUDGET',reason=reason,occurred_at=None,time_quality='HOST_SYNCED',source_event=None))
  assert validate_value({'$ref':'#/$defs/RepoDocument'},d,s['$defs']);docs.append(d)
 for recovery in (False,True):
  env=dict(schema_version=1,snapshot_revision=str(2**64-1))
  if recovery:env['collection']='audits'
  env.update(documents=[],next_after_key=None)
  budget=65536-1-(len(enc(env))-2)-(192*6+8)
  pages=[];start=0
  while start<len(docs):
   chosen=[];n=0
   for d in docs[start:]:
    size=len(enc(d))+(1 if chosen else 0)
    if n+size>budget:break
    chosen.append(d);n+=size
   assert chosen;start+=len(chosen);page=dict(env);page['snapshot_revision']='100';page['documents']=chosen;page['next_after_key']=chosen[-1]['key'] if start<len(docs) else None
   assert len(enc(page))+1<=65536
   assert validate_value({'$ref':'#/$defs/'+('RepositoryRecoveryPage' if recovery else 'RepositoryQueryResult')},page,s['$defs'])
   pages.append((len(chosen),len(enc(page))))
  print(dict(reason_bytes=len(reason.encode()),recovery=recovery,budget=budget,pages=pages,total=start))
print('UTF-8 byte model only; C pagination NOT_RUN')
~~~

实际P4结果：中文reason768B时query页62/38条、63852/39146B，recovery63874/39168B；含引号/反斜杠/换行的reason384B时query76/24条、63652/20146B，recovery63674/20168B。四组都收齐100条并通过正式Python结果schema核验；不是C序列化/SQLite函数实测。
