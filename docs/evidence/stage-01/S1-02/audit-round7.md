# S1-02 round-7 独立审计

日期：2026-10-24（Asia/Shanghai）。工作副本：%REPO%。契约：**0.2.1 / REVIEW_DRAFT**。

## 0. 结论与边界

**FAIL：1 项阻塞问题 B-1/P2，1 项非阻塞问题 N-1/P2。当前快照不满足 S1-02 技术交接或 DONE 条件。**

B-1 是新的具体反例：在 documents 的 CREATE TABLE 内加入 CHECK 或 FOREIGN KEY，不新增用户对象、不改变当前列/键索引检查结果，open 的全部现有接纳条件仍可满足，合法 RepositoryChange 却无法写入。round-6 的指定触发器及隐藏生成列反例已经静态封住，不能继续拿旧反例认定修复无效。

本轮仅新增本报告。不修改实现、正式测试、契约/生成物、stage-plan、README、summary/handoff 或历轮审计；不置 DONE，不开始 S1-03，不 commit/push。S1-01 仍 DONE，S1-02 仍 IN_PROGRESS、report=null，active_task/recommended_next=S1-02，阶段1 gate_result=NOT_RUN。

本轮 Windows Python/Node 的检查和内存 SQL 探针确已执行。**Linux configure/build/CTest、11 个 C 测试二进制、ASan+UBSan、TSan、C mutation 和 C 端反例均 NOT_RUN**。FAIL 的依据是具体实现缺口，环境受限本身不是失败理由。历史 Linux 日志的 PASS 与本轮独立执行分开记录。

## 1. 输入、环境与证据等级

依次核对 AGENTS、根 README、阶段入口、机器索引、阶段1公共规则与 S1-02 卡 :48–56，再核对阶段0持久化/恢复规则、内部接口、UART、模块所有权及 traceability。核对六个 Repository 端口、原生 DTO、DDL、仓储各实现的相关路径、私有头、根/局部 CMake、嵌入器、抽取器、五个正式 storage 测试及 common/test_support，交叉检查六轮审计、summary/handoff、清单和现存证据。没有发现这些路径祖先中新增的 AGENTS.md。

第三方范围为版本、锁定摘要、许可及六个文件与依赖锁的字节数/SHA-256 对照，不宣称完整安全审计了 SQLite/cJSON 上游实现。此次是限定范围审计，不启动新项目或较大实施方案，没有下载/替换依赖；为 SQL 接纳条件查阅 SQLite 官方资料。

受审入口：生成头 context :190；六签名 :211/:213/:215/:273/:275/:277；Repository DTO :143–147；事件/批次 :119–120。内部 schema 实际为 107 defs / 15 x-rules / 18 collections，仓储冻结子集为 47 defs / 18 collections / 27525 B。

证据分三层：本轮实际执行的 Windows 命令/内存探针；当前 C 源码及正式测试的静态推导；开发方保留的 Linux 历史日志。下面“静态闭环”不表示本轮 C 实跑通过；短指纹匹配不提供日志来源真实性或完整构建输入的独立证明。

### 1.1 本轮实际执行

普通 exec_command 首次启动失败：Failed to create unified exec process / helper_unknown_error: setup refresh had errors。进程未启动，没有 shell 退出码。随后通过 Node 文件接口和 child_process.execFile 的参数数组，cwd 固定为工作副本，完成只读操作；没有提权、安装工具或修改连接设置。

| 命令/操作 | 本轮结果 |
|---|---|
| git status --short | exit 128；fatal: not a git repository (or any of the parent directories): .git |
| where.exe python / wsl / ssh | 均 exit 0；Python=E:/anaconda/python.exe；wsl/ssh 为 Windows 系统程序 |
| where.exe cmake | exit 1；PATH 中未找到，不能据此推断所有磁盘都未安装 |
| wsl.exe --list --quiet | exit 4294967295（0xffffffff），输出含 Wsl/EnumerateDistros/Service/E_ACCESSDENIED |
| ssh -o BatchMode=yes -o ConnectTimeout=5 locker-vm，执行只读 printf | exit 255，stdout/stderr 为空；未取得 VM 会话，不推断 guest 是否关机 |
| python -B -X utf8 scripts/check_contracts.py | exit 0；PASS，0.2.1，24 routes / 82 ports / 28 private RPC；C 声明为 PASS (parse only, not compile/link)，hardware_profile=UNVERIFIED，business_or_hardware_tested=false |
| python -B -X utf8 scripts/check_stage_plan.py | exit 0；PASS，45 cards，active_task=S1-02，implementation_cards_done=1 |
| python -B -X utf8 scripts/check_vision_contracts.py | exit 0；PASS，camera_owned=false，rknn_available=false，hardware_tested=false |
| python -B -X utf8 scripts/check_feasibility.py | exit 0；PASS，runtime_or_hardware_tested=false |
| python -B -X utf8 host/storage/tools/extract_schema.py --check | exit 0；冻结子集 27525 B 匹配真源 |
| 内存替换抽取器 FROZEN.read_bytes，追加一个空格，再调用原 main 的 --check | exit 1（预期红灯）；DRIFT，未覆盖任何磁盘文件 |
| 附录 A 的内存 SQLite 探针 | exit 0；Python 3.14.6 / SQLite 3.53.2，合法 DTO 校验成功，CHECK/FK 反例成立 |
| Node 原始字节/SHA-256、日志分段和断言求和 | 源码清单 32/32、两份 sha256 清单各 32/32、mutation 源指纹 11/11 匹配；详见 §4 |

当前 Python SQLite 3.53.2 与项目 vendored SQLite 3.54.1 不同。探针没有链接 vendored SQLite，没有执行 locker_repo_*；接纳结果由 §2 的源码逐条件推导，SQL 后果由内存探针佐证。

## 2. 阻塞发现

### B-1 / P2：表内 CHECK / FOREIGN KEY 仍绕过 schema 接纳核验

**位置**：host/storage/src/repository.c:1849 的 verify_table_columns、:1973 的 verify_key_index、:2036 的 verify_schema_tables、:2079 的 verify_official_objects；已初始化接纳分支 :2261，最终成功返回 :2295。正式结构回归在 tests/unit/storage/test_repository.c:2375。

**触发**：保留官方 meta/migrations 及元数据，把 documents 建成下面这张表。这里只改变表内约束，没有新增列、用户索引、视图或触发器，也不依赖 writable_schema 或损坏文件：

~~~sql
CREATE TABLE documents (
    collection TEXT NOT NULL,
    key TEXT NOT NULL,
    value_json TEXT NOT NULL,
    PRIMARY KEY(collection, key),
    CHECK(key <> '21111111-2222-4333-8444-555555555555')
) WITHOUT ROWID;
~~~

与官方 DDL 相比，当前实际检查的所有结构结果相同：

| 核验面 | 官方表与反例共同结果 |
|---|---|
| table_xinfo 的 name/type/notnull/pk/hidden | collection/TEXT/1/1/0；key/TEXT/1/2/0；value_json/TEXT/1/0/0 |
| 列默认排序规则 | 未声明 COLLATE，默认 BINARY；不会被 :1895 的列 metadata 检查拒绝 |
| index_list | sqlite_autoindex_documents_1，unique=1，partial=0 |
| index_xinfo 键列 | collection、key；desc=0，coll=BINARY |
| 用户对象清单 | 仅 documents/meta/migrations 三张表 |
| meta / migrations | schema_version=1、snapshot_revision=0；迁移行 version=1、applied_at 和 checksum 与官方一致 |
| quick_check | ok（空表没有违反 CHECK 的既存行） |

verify_table_columns 不读取 CHECK/FK；verify_key_index 只核唯一索引；verify_official_objects 只选 type/name，不比较 sqlite_master.sql 或其他表内约束；verify_schema_metadata 也不补这一检查。因而两种 initialize_empty 配置在 documents 已存在时都会进入 :2261，同样通过这些条件，**推导 open 返回 OK 且 out 非 NULL**。本轮 C 端双模式 open 没有实跑。

随后提交一份经当前项目 Python schema 校验通过的 RepositoryChange：expected=0、new=1，两个 PUT 都是 audits 集合的合法 AuditRecord，id 分别以 1111 与 2111 开头。官方 DDL 下两个 PUT 都 changes=1，COMMIT 后两条记录、revision=1；反例下第一个 PUT changes=1，第二个失败 CHECK constraint failed。当前 :71 将 SQLITE_CONSTRAINT 映射为 CONFLICT，外层回滚；**不是 DTO 非法或业务竞争，却被作为可用仓储打开后才失败**。

附录 A 另测表内 FOREIGN KEY(collection) REFERENCES meta(value)：列/索引/对象/metadata/quick_check 同样不变，第一次合法 PUT 即报 foreign key mismatch。生产 open 在 :2199 明确启用 foreign_keys；该变体同属“表内约束没有纳入接纳检查”这个根因，不另凑一项缺陷。

**影响与边界**：这是 schema 不兼容库被假接纳，违反生成头 :272 的 schema-checked open 和 x-rules 13 的不兼容明确报错规则，影响后续恢复/营业接入。当前事务回滚仍有效，探针中 CHECK 组最终文档数 0、revision=0；本轮没有据此声称静默丢失、半事务提交或开锁。严重性按 P2，与此前隐藏生成列/额外约束类问题一致。

SQLite 的 CREATE TABLE 除列和 UNIQUE 外还支持 CHECK、FOREIGN KEY；CHECK 在 INSERT/UPDATE 执行时检查，名称清单不等同于完整 DDL 兼容性。[SQLite CREATE TABLE](https://www.sqlite.org/lang_createtable.html#check_constraints)；列枚举本身的范围见 [PRAGMA table_xinfo](https://www.sqlite.org/pragma.html#pragma_table_xinfo)。

**修复与验收建议**：从唯一 schema-v1.sql 建立可复核的官方表定义接纳策略，把影响写入的表内约束纳入校验；不能继续只追加已知危险对象名称黑名单。可以选择只接受受控官方定义，或完整验证所允许的等价定义，不要求自研通用 SQL parser。补正式 C 端 CHECK/FK 夹具：open(false)/open(true) 均 CONFLICT 且 out=NULL，原表定义/原数据不被清空，官方 DDL 正对照能提交同一份两个 audits 的 change。保留已有触发器、隐藏列、内联 UNIQUE/NOCASE/DESC 回归。修复后需新指纹绑定的 Linux 构建/CTest及必要定向 mutation。

## 3. 非阻塞发现

### N-1 / P2：mutation 的精确含义、执行顺序与整改表仍互相矛盾

1. **m29 与 m29b 在保留日志中是完全相同的变异。** mutation.log:389 与 :464 的 edit 都把同一条 SELECT 改为 SELECT name, type, [notnull], pk, 0 FROM pragma_table_info(?1) ORDER BY cid，均保留后续 sqlite3_column_int(stmt,4) 读取，均产生同四条 FAIL（:2250 两次、:2470、:2472）。summary.md:251/:266/:356 却说 m29b 与 m29 的区别是“只回退列视图、保留 hidden 读取”；现有 edit 无法支持此差别。按现有材料应将它记为同一变异的重复执行；32 次运行/29 次变红仍可以保留，但需披露重复，不能把它当第二种独立单行形式。若实际脚本不同，应保留正确 edit 和对应新证据。
2. **对象清单是在键索引检查之后执行。** repository.c:2060 先核列、:2064 核键索引，循环结束后 :2070 才调用 verify_official_objects。summary.md:265、handoff.md:456、mutation.log:62–63 及 test_repository.c:2350/:2482 的说明把它说成“在 verify_key_index 之前/先把夹具拒掉”。真实路径是：原版 partial 在 :2000 就拒；m26 把它放行后，后面的对象清单再拒；所以单点呈绿、联合 m26b 呈红。遮蔽结论正确，先后解释错误，m26 原分支并未因此变成不可达。
3. **§5.4 的真实断言数正确，但 round-6 整改表仍多算汇总行。** summary.md:355–356 与 handoff.md:602–603 写 m28/m31=5、m32=4、m29/m29b/m30=5；实际分别为 4、4、3、4、4、4。mutation.log 的 assertion_failures 包括被计数的尾部汇总，§5.4 已说明减1，整改表必须用同一口径。summary.md:355 还把 DELETE 失败后状态统称 documents 仍为0；正式 DELETE 测试 :2609/:2612 要求 revision=1、文档仍为1，只有 PUT 失败时文档为0。

这些问题不推翻代码里的触发器/隐藏列修复，也不推翻日志的原始运行数；会误导后续判断到底哪一道防线被正式测试独立覆盖。建议统一修改现行说明，并明确 m26b 的 8 条中只有 4 条属于 partial 夹具，另4条来自同时关闭 m28 后的 trigger 夹具，不能把全部8条解释为 partial 单独贡献。

### 审计期间已核销的文档观察（不计未解决发现）

初读 handoff.md:6 时，它写“五轮整改、等 round-6”。结束前指纹复核发现该文件被外部更新，当前已写“六轮整改、等 round-7”，与summary一致，因此撤销这项过期状态发现。该变化不是本审计写入，来源未判定；本轮不覆盖外部修改。

初次输入 handoff 为54951 B，SHA-256=b060a1e672f325985fa8184c53c81264e9650c63b89fa90c6a57a7f6c7a44445；最终读到55025 B，SHA-256=e2046c4ae5dae719577aa9560b090afc6f9bb7a6450145cfdb2cbdf0a19d2606。当前N-1引用的:444–458/:602–603仍保留同样矛盾。其余98项受审输入未变；附录B采用最终handoff指纹。

其余重点数字已复核：7579 B的模块README是当前值，不是757条断言；1609/1583/旧run152903主要出现在明确历史说明或清理路径中，不能一概当成当前证据；当前source-manifest摘要ac6891fe…e4f0，不再是旧91150adc/0cca50dc；既有证据确为50个（本报告新增后51个）。当前VM四类路径主体与run-summary一致，partial UNIQUE影响也已改成两条audits，不继续沿用round-6 N-2原结论。

## 4. round-6 六项重点复核与证据核算

### 4.1 官方对象清单与静默写入

verify_schema_tables:2070 确实返回 verify_official_objects 的状态，未无条件 OK。后者 :2089 排除字面 sqlite_* 内部前缀，:2106 核 table 类型、:2114 防重、:2124 限定三表、:2130 核总数。首次初始化在 :2249 核验后才 :2255 COMMIT；已有库 :2261 同样覆盖。预存触发器双模式拒绝和对象保留回归在 test_repository.c:2441/:2442/:2449。**指定 round-6 B-1 的 open 缺口静态闭环**；不代表 B-1 中所有可能不兼容 DDL 已穷尽，新增问题见 §2。

PUT 在 step_write 成功后 :438 检查 changes==1；DELETE 在 :472 先查 existed，:477 仅对确实存在的 key 要求 changes==1。外层 commit_change 在任一步失败回滚，revision 只在 mutations 全部完成后写入。正式回归 :2548/:2553/:2556 检查 PUT IO_ERROR、revision=0、0条文档；:2564/:2567/:2569/:2572 检查移除 trigger 后成功。DELETE :2605/:2609/:2612 检查 IO_ERROR、revision=1、原1条文档保留，:2617 检查删不存在 key 合法，:2628/:2632/:2634 检查 DROP 后成功、revision=3。

sqlite3_changes 只统计语句直接修改，不统计触发器/外键等辅助变化，所以这是一道针对 RAISE(IGNORE) 无直接写入的防线，不是任意外部 DDL 副作用的完整检测器。[SQLite changes API](https://www.sqlite.org/c3ref/changes.html)

### 4.2 table_xinfo / hidden

repository.c:1856 的查询有 hidden；:1876 读取；:1881 在列属性比较前拒 hidden!=0。三表都复用该检查。test_repository.c:2337 把官方 value_json 换成生成列，使 xinfo 仍报三列但 hidden=2；:2346 追加额外 VIRTUAL NOT NULL 列，使 table_info 漏列；两种都测双 initialize_empty 和对象数仍3。**指定 round-6 B-2 静态闭环**。m30 对应替换 value_json 的夹具，m29 对应追加列夹具；m29b 现存定义与 m29 重复，见 N-1。

### 4.3 源清单、mutation 分段与失败明细

按实际格式解析 source-manifest.txt 的 hash/bytes/path 三列，32/32 字节数与完整 SHA-256 匹配当前文件；manifest.sha256 与 vm-s102-manifest.sha256 两列格式各32/32匹配。首次把三列格式误当两列的本轮内存解析已纠正，没有据此登记伪失配。source-manifest 本身3563 B，SHA-256=ac6891fe0ac0af1093d63a7868479097e260e021a51763c5d73860396542e4f0。

mutation.log 实际25020 B、472行，32个具名段；每段 patch=OK、configure=0、build=0。29段 ctest=8，3段 ctest=0；红段的实有 FAIL 明细数恰好等于 assertion_failures-1。三个未捕获点确为 m6_sync、m9_page_budget、m26_partial_unique，不能把它们写成捕获。11条 SOURCE_FINGERPRINT 的12位摘要和字节数全部匹配当前源；它们是日志内的绑定信息，本轮只重算一致性，没有重跑变异。

| 具名段 | 日志 ctest | 实有失败断言 | 执行分母 |
|---|---:|---:|---:|
| m1_revision | 8 | 5 | 829 |
| m2_dedup | 8 | 5 | 829 |
| m3_adjacent | 8 | 9 | 829 |
| m4_u64_fallback | 8 | 2 | 90 |
| m5_schema_additional | 8 | 2 | 131 |
| m6_sync | 0 | 0 | 未记录；不能按0断言解释 |
| m7_gap_latch | 8 | 2 | 829 |
| m8_trace_guard | 8 | 9 | 829 |
| m9_page_budget | 0 | 0 | 未记录；不能按0断言解释 |
| m9b_doc_bytes | 8 | 4 | 814 |
| m10_nul_guard_raw | 8 | 6 | 829 |
| m11_parse_nul | 8 | 17 | 829 |
| m12_boundary_structure | 8 | 4 | 829 |
| m13_first_available_zero | 8 | 3 | 829 |
| m14_gap_latch_early | 8 | 2 | 829 |
| m16_sqlite_prefix | 8 | 1 | 829 |
| m17_verify_columns | 8 | 20 | 829 |
| m18_verify_key_index | 8 | 12 | 829 |
| m20b_any_matching_index | 8 | 4 | 829 |
| m21b_skip_column_collation | 8 | 4 | 829 |
| m22_skip_extra_unique_index | 8 | 4 | 829 |
| m25_regex_bounded_zero | 8 | 2 | 46 |
| m26_partial_unique | 0 | 0 | 未记录；不能按0断言解释 |
| m27_regex_min_seed | 8 | 1 | 46 |
| m28_official_objects | 8 | 4 | 829 |
| m29_table_info | 8 | 4 | 829 |
| m30_hidden_only | 8 | 4 | 829 |
| m31_put_changes | 8 | 4 | 829 |
| m32_delete_changes | 8 | 3 | 829 |
| m15b_view_double | 8 | 10 | 829 |
| m26b_partial_plus_objects | 8 | 8 | 829 |
| m29b_xinfo_to_info_only | 8 | 4 | 829 |

m9b 为4/814，失败后依赖 root 的断言跳过，不能改成4/829。m15b 为10/829，唯一视图红行是 open(false) 的 :2125，首次 open(true) :2122 未红；m10=6/829、m11=17/829，当前 NUL 负例独立建库并建立 revision=1 基线，不用跨用例连带解释。m25=2/46、m27=1/46，对应零/正下界 atom 两种播种分支。m26b 的8条拆成 partial :2426/:2427 加 helper :2250 两条，以及 trigger :2441/:2442 加 helper :2250 两条。

### 4.4 Linux 日志与断言总数

以下是保留日志核对，**不是本轮复跑结果**：

| 环境记录 | configure/build/CTest 记录 | ASan+UBSan 记录 | TSan 记录 |
|---|---|---|---|
| WSL2，/home/yuanangyu/s102_r6ev2 | 三步exit0；12/12，0.37s；配置/构建 WARNING_LINES=0 | 12/12，1.21s；ASAN_REPORTS=0 | 12/12，1.76s；TSAN_HITS=0 |
| VM，run 20261024-174830 | 三步exit0；12/12，0.48s；配置/构建 WARNING_LINES=0 | 12/12，1.33s；ASAN_REPORTS=0 | 12/12，1.79s；TSAN_HITS=0 |

保存的 configure/build 日志不含完整编译命令和 CMakeCache；本轮未访问 guest 或检查二进制插桩，不把 sanitizer 文件名及0报告升级为独立验证过编译标志。vm-s102-run-summary.log 的普通 configure/build/ctest 退出码与对应日志一致；sanitizer 退出码需看其各自日志，不能说 run-summary 一个文件包含所有退出码。

两个 unit-tests.log 均有11条 PASS、11条rc=0。程序独立求和：

| 组 | 分项断言 | 合计 |
|---|---|---:|
| common | bounded_queue 97、build_info 5、clock 53、log 164、numeric 133、uuid 53 | 505 |
| storage | event_key 80、regex 46、repository 829、schema 131、u64 90 | 1176 |
| 总计 | 11个C二进制；第12个CTest为Python漂移门禁，不另计断言 | **1681** |

VM 的当前路径为 /home/%USER%/locker-workspace/S1-02/src、build/S1-02/20261024-174830、logs/S1-02/20261024-174830、artifacts/S1-02。run-summary 记录快照81f6103de8b312bc390e660c6149b614a7f59ef2e821291dc012efc710d2ab22、81735056 B；source-verify 记录32条清单全OK、SRC_FILES=1015、SRC_STRAY_ARTIFACTS=0。本轮只核对保留记录与当前32项内容，没有重读 VM 实盘或快照归档，不能独立确认1015个文件的完整内容。32项清单也不包含全部common/契约/测试harness构建输入，不能据32/32推出整个打包树全量一致。

## 5. 交付核对清单（36项）

“静态符合”只说明所列当前路径与正式测试目标相符；任何 C 运行结果均按 §1/§4 区分。

| # | 核查项 | 本轮结果与依据 |
|---|---|---|
| 1 | S1-01 前置 | summary 状态DONE、最新audit顶部PASS；本轮不重验该卡 |
| 2 | 当前卡与门禁 | S1-02 IN_PROGRESS/report=null；S1-07 gate NOT_RUN，未跨卡 |
| 3 | 契约基线 | 0.2.1 / REVIEW_DRAFT，生成头版本一致 |
| 4 | 六个端口及原生DTO | 签名与生成头一致，没有新建同义公开API |
| 5 | 集合与冻结闭包 | 18集合/47冻结defs，--check实跑通过 |
| 6 | 漂移门禁非恒真 | 内存添加一个空格，原--check实跑exit1 |
| 7 | 根CMake接入 | :87/:92接入两个模块目录；无Git历史，不能独立证明相对开工版仅改两行 |
| 8 | 局部CMake/嵌入 | 生成inc只写binary目录，字节数组外壳补NUL；生成机制实际为构建期custom command，文档称配置期不够精确 |
| 9 | 第三方锁与许可 | 六个文件字节/SHA匹配lock；SQLite Public Domain、cJSON MIT记录存在；未重下载归档 |
| 10 | 单写所有者 | Core拥有仓储；内部不建业务线程/不发设备动作，HTTP/UI不直连 |
| 11 | 事务边界 | BEGIN IMMEDIATE后检查revision/checks，mutations与meta提交；失败回滚 |
| 12 | 中途SQL失败回归 | test_failed_transaction_is_atomic :569用第二次INSERT的RAISE(ABORT)，有成功正对照；不是仅非法入参测试 |
| 13 | 竞争占位 | :656顺序竞争、:720两个fork各自open同库；目标为一OK一CONFLICT；本轮运行NOT_RUN |
| 14 | 重开保留 | 正式test_persistence_across_reopen :800；属于正常close/reopen，不据此证明崩溃/掉电 |
| 15 | 事件键/epoch | device+epoch+20位boot+20位seq，compare为uint64；不经浮点 |
| 16 | 页内/跨页连续 | :1416/:1436相邻规则、:1701跨页检查；首建与缺口锁存分开 |
| 17 | 重复导入 | :1636去重，new_count=0不重复应用投影；正式dedup回归存在 |
| 18 | 重复页带gap | :1658早退前分流，:1678持久锁存false；正式test_duplicate_page_latches_gap |
| 19 | 批次包络 | last_committed非零、同日志、上界检查；first_available可早于页首，next_after等于页尾 |
| 20 | 投影事务 | 新事件、Core projection、cursor同事务；NULL才表示无projection，未决定业务成功 |
| 21 | u64规范与顺序 | u64.c复用common numeric；合法十进制溢出检查；非法回退另测，不据m4声称合法数值主路径覆盖 |
| 22 | 分页字节预算 | :973计算信封/续页余量，:1099用真实序列化文档长度；m9未捕获与m9b变红分别保留 |
| 23 | query/recovery DTO | 查询参数绑定、key/after互斥、恢复分页；由当前契约定义输出 |
| 24 | cJSON嵌入NUL | schema.c:35原始字节与转义奇偶守卫，repository parse也调用；m10/m11独立负例不再用旧连带解释 |
| 25 | 私有regex atom | :531/:562正确播种，正式{0,1}/{0,0}/{0,19}/{1,3}/{2,}边界；无C实跑冒充 |
| 26 | 首次初始化 | 有痕迹不自动补库；DDL与核验在COMMIT前，失败回滚；DDL中途失败注入仍未覆盖 |
| 27 | 已存视图/前缀对象 | db_object_exists认任意对象，用户对象计数用NOT GLOB，视图和sqlitex_负例仍在 |
| 28 | 唯一索引/排序 | partial拒绝、所有unique要求键覆盖/升序/BINARY；内联UNIQUE/NOCASE/DESC夹具保留 |
| 29 | 官方对象清单 | round-6触发器原反例静态闭环，m28真实4/829 |
| 30 | PUT/DELETE效果 | :438/:477守卫及DROP TRIGGER正对照成立；DELETE不存在key允许空操作 |
| 31 | 生成/隐藏列 | table_xinfo+hidden拒绝，替换/追加两类回归均在；m29b重复定义需订正 |
| 32 | 完整DDL兼容性 | **不符合，B-1/P2**：表内CHECK/FK不可见于当前检查组合 |
| 33 | WAL/FULL | open明确发出WAL与FULL；WAL正式另连接回归；FULL无运行时断言，m6未捕获；内存探针不证明WAL |
| 34 | 证据数量与指纹 | 既有50文件/33log；三份清单各32/32、mutation源11/11；不替代Linux实跑 |
| 35 | 文档同步 | **待修N-1**；正确的1681及当前VM路径已确认，旧审计保持历史结论 |
| 36 | 残留与硬件边界 | 本轮未建临时文件/DB/构建；RK掉电、ARM/32位、MCU/UART/相机/整柜均NOT_RUN |

## 6. 后续处理与本轮写入自检

由S1-02负责人修复B-1及正式C回归，同时统一N-1的当前正文，保留旧审计原结论。先清明确临时残留，再按既定VM工作区对固定修复快照运行configure/build/CTest、必要sanitizer/定向mutation，更新source-manifest、日志与交接，重新独立复审。未经这些步骤，不据本报告或旧12/12把本卡置DONE。

硬件验证仍未完成：RK介质fsync/掉电原子性、ARM/32位、MCU/柜锁/UART/NPU/相机等不在本轮运行证据内。阶段1门禁仍由S1-07验收，仓储测试不能代替整柜功能。

文档同步检查已做：本轮没有产品行为变更，授权产物是本审计；需要整改的既有当前正文已列N-1，未越权改summary/handoff/README。本轮脚本全部在Node内存或python -B -c执行，SQL数据库全部:memory:，没有新增一次性脚本、DB、日志或模型产物。扫描host/storage与tests/unit/storage未见db/wal/shm、构建、log、pyc/temp残留；根未见.tmp、s102_、_mut、_anchors临时条目。未删除既有正式测试、fixture、日志或用户构建/HAL文件。VM/Public/TEMP外部残留未全量复核，不替开发方“已清0”签字。

## 附录 A：本轮实际执行的内存探针

命令为 E:/anaconda/python.exe -B -X utf8 -c，下列完整代码在内存执行，exit0。它直接展示SQL语义，不冒充C仓储open/commit测试。CHECK组失败后revision仍0且documents为空；FK组第一次PUT失败；官方组2条记录/revision1。

~~~python
import sys, sqlite3, json, pathlib
sys.path.insert(0,'scripts')
from check_contracts import validate_value
schema=json.loads(pathlib.Path('contracts/internal-data.schema.json').read_text(encoding='utf-8'))
ids=['11111111-2222-4333-8444-555555555555','21111111-2222-4333-8444-555555555555']
records=[dict(id=i,actor_id=None,operation_id=None,type='AUDIT',reason='round7',occurred_at=None,time_quality='UNSYNCED',source_event=None) for i in ids]
change=dict(schema_version=1,expected_snapshot_revision='0',new_snapshot_revision='1',checks=[],mutations=[dict(op='PUT',document=dict(collection='audits',key=r['id'],value=r)) for r in records])
assert all(validate_value(schema['$defs']['AuditRecord'],r,schema['$defs']) for r in records)
assert validate_value(schema['$defs']['RepositoryChange'],change,schema['$defs'])
print('PYTHON',sys.version.split()[0],'SQLITE',sqlite3.sqlite_version,'VALID_RECORDS',len(records),'VALID_CHANGE',True)
ddl=pathlib.Path('host/storage/schema-v1.sql').read_text(encoding='utf-8')
upsert='INSERT INTO documents(collection,key,value_json) VALUES (?,?,?) ON CONFLICT(collection,key) DO UPDATE SET value_json=excluded.value_json'
for variant in ['official','inline_check','inline_fk']:
 c=sqlite3.connect(':memory:'); c.executescript(ddl)
 if variant!='official':
  c.execute('DROP TABLE documents')
  constraint="CHECK(key <> '"+ids[1]+"')" if variant=='inline_check' else 'FOREIGN KEY(collection) REFERENCES meta(value)'
  c.execute('CREATE TABLE documents(collection TEXT NOT NULL,key TEXT NOT NULL,value_json TEXT NOT NULL,PRIMARY KEY(collection,key),'+constraint+') WITHOUT ROWID')
 c.commit()
 print('CASE',variant)
 print('xinfo',c.execute("SELECT name,type,[notnull],pk,hidden FROM pragma_table_xinfo('documents') ORDER BY cid").fetchall())
 print('indices',c.execute("SELECT name,[unique],partial FROM pragma_index_list('documents')").fetchall())
 print('key',c.execute("SELECT name,[desc],coll FROM pragma_index_xinfo('sqlite_autoindex_documents_1') WHERE [key]=1 ORDER BY seqno").fetchall())
 print('objects',c.execute("SELECT type,name FROM sqlite_master WHERE name NOT GLOB 'sqlite_*' ORDER BY name").fetchall())
 print('meta',c.execute('SELECT * FROM meta ORDER BY name').fetchall(),'migration',c.execute('SELECT * FROM migrations').fetchall(),'quick_check',c.execute('PRAGMA quick_check').fetchall())
 try:
  c.execute('BEGIN IMMEDIATE')
  for r in records:
   c.execute(upsert,('audits',r['id'],json.dumps(r,separators=(',',':'))));print('PUT',r['id'][0], 'changes',c.execute('SELECT changes()').fetchone()[0])
  c.execute("UPDATE meta SET value='1' WHERE name='snapshot_revision'");c.commit();print('COMMIT_OK')
 except sqlite3.Error as e: print('SQL_ERROR',str(e));c.rollback()
 print('END',c.execute('SELECT key FROM documents ORDER BY key').fetchall(),c.execute("SELECT value FROM meta WHERE name='snapshot_revision'").fetchone())
 c.close()
~~~

关键原始输出：

~~~text
PYTHON 3.14.6 SQLITE 3.53.2 VALID_RECORDS 2 VALID_CHANGE True
CASE official
xinfo [('collection', 'TEXT', 1, 1, 0), ('key', 'TEXT', 1, 2, 0), ('value_json', 'TEXT', 1, 0, 0)]
indices [('sqlite_autoindex_documents_1', 1, 0)]
key [('collection', 0, 'BINARY'), ('key', 0, 'BINARY')]
objects [('table', 'documents'), ('table', 'meta'), ('table', 'migrations')]
meta [('schema_version', '1'), ('snapshot_revision', '0')] migration [(1, '1970-01-01T00:00:00Z', 'schema-v1')] quick_check [('ok',)]
PUT 1 changes 1
PUT 2 changes 1
COMMIT_OK
END [('11111111-2222-4333-8444-555555555555',), ('21111111-2222-4333-8444-555555555555',)] ('1',)
CASE inline_check
xinfo [('collection', 'TEXT', 1, 1, 0), ('key', 'TEXT', 1, 2, 0), ('value_json', 'TEXT', 1, 0, 0)]
indices [('sqlite_autoindex_documents_1', 1, 0)]
key [('collection', 0, 'BINARY'), ('key', 0, 'BINARY')]
objects [('table', 'documents'), ('table', 'meta'), ('table', 'migrations')]
meta [('schema_version', '1'), ('snapshot_revision', '0')] migration [(1, '1970-01-01T00:00:00Z', 'schema-v1')] quick_check [('ok',)]
PUT 1 changes 1
SQL_ERROR CHECK constraint failed: key <> '21111111-2222-4333-8444-555555555555'
END [] ('0',)
CASE inline_fk
xinfo [('collection', 'TEXT', 1, 1, 0), ('key', 'TEXT', 1, 2, 0), ('value_json', 'TEXT', 1, 0, 0)]
indices [('sqlite_autoindex_documents_1', 1, 0)]
key [('collection', 0, 'BINARY'), ('key', 0, 'BINARY')]
objects [('table', 'documents'), ('table', 'meta'), ('table', 'migrations')]
meta [('schema_version', '1'), ('snapshot_revision', '0')] migration [(1, '1970-01-01T00:00:00Z', 'schema-v1')] quick_check [('ok',)]
SQL_ERROR foreign key mismatch - "documents" referencing "meta"
END [] ('0',)
~~~

漂移负控同样只替换内存中的FROZEN对象，调用原抽取器main的--check。实际exit1，stdout以DRIFT开头；磁盘冻结文件不变。

~~~python
import importlib.util,sys,pathlib
p=pathlib.Path('host/storage/tools/extract_schema.py')
s=importlib.util.spec_from_file_location('r7extract',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
real=m.FROZEN
class MemoryFrozen:
 def exists(self): return True
 def read_bytes(self): return real.read_bytes()+b' '
 def relative_to(self,p): return real.relative_to(p)
m.FROZEN=MemoryFrozen()
sys.exit(m.main(['--check']))
~~~

## 附录 B：受审输入指纹

初次冻结时间：2026-10-24T10:14:31.023Z。以下99项为最终复核采用的原始字节数及完整SHA-256（handoff的外部变化见§3，其余98项与初次冻结相同），范围包含32项源码清单、50项既有S1-02证据及关键入口/真源/架构文件；不是整个工作区、全部构建输入或Git差异。报告自身不进入自身指纹表。

| 文件 | bytes | SHA-256 |
|---|---:|---|
| AGENTS.md | 5839 | bb8658418e3926ad7799f3be148107731c61fc1aa3b668992dd80ee0e1d217de |
| CMakeLists.txt | 3943 | f9e1964ee54465f5e29299721a3f342f929cc81e3ff6a6da8d00b77fc34258b4 |
| config/development/stage-plan.json | 24749 | 1ed89d9f5695c40f965f43d331907295eccca21eaf52d67bee92e5dc4fdf1b47 |
| contracts/contract-source.json | 66553 | 14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f |
| contracts/include/locker_contracts.h | 39925 | 34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a |
| contracts/internal-data-source.json | 92193 | ebcfa5859c114e8746a2f235161c17a6dede651f56125d92cc37f259c7d81da1 |
| contracts/internal-data.schema.json | 129569 | e7b8695c49115e2994cf126dc5ab629e5c82460e70d4c292b19576f55c60e386 |
| deployment/host/dependencies.lock.json | 14075 | 4884c4ca9944ccd66be72db71dcd99b289bba119763c965a7e3b7b7a961fac6d |
| deployment/host/README.md | 6753 | 9887fe18df7f964a66725cd0ae5ee3a3bc7d37fcc58fcdbde229da3d90c7bc7a |
| docs/architecture/internal-interfaces.md | 29792 | a5b3cdc41c05006e83edc970c46c15f79644adb8fe071b71953f239a64c1c2cf |
| docs/architecture/module-ownership.md | 10135 | b182fe6e424b605fd4cad1073c85178588ee474ca8ab6f38be84e3129f72bb3f |
| docs/architecture/traceability.md | 12694 | 0903b559a6fc33fa0120d608b5b85783f66327b03a0f8267e7b86309877f2859 |
| docs/architecture/uart-v1.md | 26022 | 5bb2204f291b259f7b28cc7a2a1473352d5f1d0d501032168924d664c5e3ed8f |
| docs/evidence/stage-01/S1-02/asan-build.log | 3569 | c167d84d08bdda72c51c795d3bfbdd027307d538eab9ea6f7059ce33570b4089 |
| docs/evidence/stage-01/S1-02/asan-configure.log | 617 | ea00efdd74ca6b79cb3a5b3da17124a092c791c3fc0e59b9a06db1772c5738c3 |
| docs/evidence/stage-01/S1-02/asan-ctest.log | 1473 | 38962ee57d71df2414cdf66857fef3ffbfd668768a86afc394cb22792728816f |
| docs/evidence/stage-01/S1-02/audit-round2.md | 32835 | 3e2475309e154b3071269f4487ecd4ba28993c84cead38c7ee56e098797922f7 |
| docs/evidence/stage-01/S1-02/audit-round3.md | 40534 | 2d0ea66edca835e695ff297d0297c0229d4671bd68863a7596490fdcbc75b46a |
| docs/evidence/stage-01/S1-02/audit-round4.md | 52973 | f5c206a55f5a72d61ae4e6ba582c1573e84c77f5224131f50898213587487ac0 |
| docs/evidence/stage-01/S1-02/audit-round5.md | 44086 | c8aef59be2e1bd849d73e5794e505b02bde7a3da0030105e1fb6d3e1d03bd903 |
| docs/evidence/stage-01/S1-02/audit-round6.md | 43048 | 8dce31fdbf90a88442321744b18621dd9024f39fc85bcc7028cb146069b09598 |
| docs/evidence/stage-01/S1-02/audit.md | 23283 | 80ee21d98ed0877e2a3469a3ae9389f3ddc19663a4bf569bd39a25362ee3c8f8 |
| docs/evidence/stage-01/S1-02/build.log | 3576 | e19260295b691098f64c53b7a4f67716bf7b3301a6364a1c4dc7818f320be440 |
| docs/evidence/stage-01/S1-02/check_contracts.json | 535 | 0ac0a396fffb701522e05b2ff5f8c7725834a2641e59c109f41a62401870087d |
| docs/evidence/stage-01/S1-02/check_contracts.log | 535 | 0ac0a396fffb701522e05b2ff5f8c7725834a2641e59c109f41a62401870087d |
| docs/evidence/stage-01/S1-02/check_feasibility.json | 242 | cbe1da1d34c5d29634377f6a55c0199233a144e984417ba06553c9c5e149f02b |
| docs/evidence/stage-01/S1-02/check_feasibility.log | 242 | cbe1da1d34c5d29634377f6a55c0199233a144e984417ba06553c9c5e149f02b |
| docs/evidence/stage-01/S1-02/check_stage_plan.json | 319 | 572e375df64f10218f6e440803dcff0162f846df19877d5b77692e91683250d0 |
| docs/evidence/stage-01/S1-02/check_stage_plan.log | 319 | 572e375df64f10218f6e440803dcff0162f846df19877d5b77692e91683250d0 |
| docs/evidence/stage-01/S1-02/check_vision_contracts.json | 572 | 3f8b58703a9d039f594749a6924e19eda4e312d97cbdbf0ee1dac3b6325d8778 |
| docs/evidence/stage-01/S1-02/check_vision_contracts.log | 572 | 3f8b58703a9d039f594749a6924e19eda4e312d97cbdbf0ee1dac3b6325d8778 |
| docs/evidence/stage-01/S1-02/configure.log | 633 | 279d6593ebaa8870befa98d463bf5d8dc847f99a1fba11dcbead5e0afb571339 |
| docs/evidence/stage-01/S1-02/ctest.log | 1448 | f2175ecac7331afaa433961c215f507360b70639d893cd50d78fb88d244b1754 |
| docs/evidence/stage-01/S1-02/design-checks.log | 1860 | 5655e72083b3d4a10f8775e26a0f604b3c07cea2077b755dbb29c43035f57982 |
| docs/evidence/stage-01/S1-02/environment.log | 211 | 1dc9aff2c9359cb88264445050b1fddcc6879d69791400e1dcd8c4e9188a8d8d |
| docs/evidence/stage-01/S1-02/handoff.md | 55025 | e2046c4ae5dae719577aa9560b090afc6f9bb7a6450145cfdb2cbdf0a19d2606 |
| docs/evidence/stage-01/S1-02/manifest-check.log | 1302 | c1e42ec1a157f1b033810f4af88f94b5a89209e3de572c8bbfdde53c438e85c4 |
| docs/evidence/stage-01/S1-02/manifest.sha256 | 3231 | 5e99687a286c9e1c976a0c6b643e90f408dc26cff1656e291c9b5ee650ead73b |
| docs/evidence/stage-01/S1-02/mutation.log | 25020 | 0ef1eb7f87234ec09a61b4484fc54091c60c5b535f9b07da673c98ab558cc4a1 |
| docs/evidence/stage-01/S1-02/run-id.txt | 20 | cf86d749edf43c16ad2a8effd27758557bf334b46eac39e347106d9d3b8ad4eb |
| docs/evidence/stage-01/S1-02/source-manifest.txt | 3563 | ac6891fe0ac0af1093d63a7868479097e260e021a51763c5d73860396542e4f0 |
| docs/evidence/stage-01/S1-02/source-verify.log | 1426 | b230d9ab77a00372cee33c5c50978ae449ed49686ec99f60341240aa5641baf7 |
| docs/evidence/stage-01/S1-02/summary.md | 85381 | fdb076ab362fff9549a024dc3d7ef181349aafa9c551f605b3231533a85bc3bd |
| docs/evidence/stage-01/S1-02/tsan-build.log | 3569 | 54aaa76b91f28138ef80308fd9634f2ae9d22822791e8bb61b7073d9778034cf |
| docs/evidence/stage-01/S1-02/tsan-configure.log | 617 | a787cc760cf25fb4052c2056508e04493cc77d5072ab04236b398b2111460e55 |
| docs/evidence/stage-01/S1-02/tsan-ctest.log | 1470 | 0e4ff0d8b4c7f9446d13bac75bf8c35e201b9e42181cd1a60a5e01af2392ba49 |
| docs/evidence/stage-01/S1-02/unit-tests.log | 413 | 802d089a4cd53981f0ee5720455aecb356adff6533281188d713a128cc4e5c1b |
| docs/evidence/stage-01/S1-02/vm-s102-asan-build.log | 3440 | 63370f7d93f780f9772d182df5c3a8d35c4cf73c17fb60b04c30921f9b730393 |
| docs/evidence/stage-01/S1-02/vm-s102-asan-configure.log | 641 | 87a6abe015d204b2ef30bbc33dbf7d92a05b6490e6a05eeddce4ef386bdaf2bc |
| docs/evidence/stage-01/S1-02/vm-s102-asan-ctest.log | 1497 | df67e5e068cc5c69e622f22a2585592d954900f0476126fa5e5f095008f97307 |
| docs/evidence/stage-01/S1-02/vm-s102-build.log | 3457 | 0d11ff64a8e26cc6e05a709608b95f730202c55c4a2a3cd0afcd819838a4bc18 |
| docs/evidence/stage-01/S1-02/vm-s102-configure.log | 657 | 07810d8048dc3f8122fa4ba58281b35a7b54bbcd705286cf1a71dc75ef13f5ee |
| docs/evidence/stage-01/S1-02/vm-s102-ctest.log | 1472 | f96245c4f49b22c577fd83a0f27aef38f723b59a9c5c3168295b7ddd6d873d28 |
| docs/evidence/stage-01/S1-02/vm-s102-design-checks.log | 1860 | 87fb723000b8246df28c144207eb834dffe692a6449264c6a6d78be4c889bdc8 |
| docs/evidence/stage-01/S1-02/vm-s102-environment.log | 430 | fb13320276a45f4a5843d26146a7eef6bb7897a310bc5b9fd7037694bcc73449 |
| docs/evidence/stage-01/S1-02/vm-s102-manifest.sha256 | 3231 | 5e99687a286c9e1c976a0c6b643e90f408dc26cff1656e291c9b5ee650ead73b |
| docs/evidence/stage-01/S1-02/vm-s102-run-id.txt | 16 | 7e9212cbb28149870dedceb5ddbecccbb7058ae2ef9e2733c8c691629b066fc2 |
| docs/evidence/stage-01/S1-02/vm-s102-run-summary.log | 654 | 62617eb728869d46baf242d29db246aed682778279f5ece400d29a35f7ba8710 |
| docs/evidence/stage-01/S1-02/vm-s102-source-verify.log | 1351 | f392914835e8ebb5242775d468c77e1178fcbe0187c69bed38bd4d88087f7c21 |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-build.log | 3440 | 7f21603a0cdede209a02383b4699ac0eab999a5de1c46395e4a70cba05373f61 |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-configure.log | 641 | bc3831aab1a316bb006be9cd641a6dbfd31203a3788ae415b426f936cbcc1886 |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-ctest.log | 1494 | 6d6d58ff3b18c4933ede26e0325334ea8c6aff39fbe276c544ef10be2e496747 |
| docs/evidence/stage-01/S1-02/vm-s102-unit-tests.log | 413 | 802d089a4cd53981f0ee5720455aecb356adff6533281188d713a128cc4e5c1b |
| docs/stages/README.md | 4985 | 24ff25b010d838e05b389b56f825068d499578fa20578d5b286eb67be9c86d06 |
| docs/stages/stage-00-architecture.md | 22164 | 2e85508b23aff242f1ce6cbd166464e5a85966f8ead4f1dd8398c5231443227b |
| docs/stages/stage-01-task-cards.md | 16460 | 7f7561e83f9f614fd4ed226cefe3ce78ba367859a55ee92cd82212b25273fc93 |
| host/storage/cmake/embed_file.cmake | 1891 | c64242387b74017b03c9ce9559e5bc245eda0f83d5636ea989a3740ded028c7e |
| host/storage/CMakeLists.txt | 3782 | 3acc9e405c66569b988db32b7cb0e135b8a3a4c52fdd53f3e606e01fee49297a |
| host/storage/include/locker/storage/embedded.h | 898 | f8087fd75b43b6c35d17fd400dacf95f6f4177b937e81278783a4d093441ab7a |
| host/storage/include/locker/storage/event_key.h | 1660 | 4d46736869d62e688540dc2c0a10f3c0c71fe6302898266b1d836f143c705d73 |
| host/storage/include/locker/storage/regex.h | 1130 | 39fdb69f6fb58397e82810c3778d497b117944449527fc5b4d040129b1c03ade |
| host/storage/include/locker/storage/schema.h | 2197 | 547f9b203eb922cdb6222c0ec419bfe77b0e6899dfa010b671c90704d6bd987f |
| host/storage/include/locker/storage/u64.h | 835 | 08cdc34af372ce6cb3d0b316b6270a827aa3cdf5f007b8f9f61bd39445959f27 |
| host/storage/README.md | 7579 | 09d5c3147b477d21df6d0c7143ddb3a95fcde33becf76816cea57cc1e60517ba |
| host/storage/schema-v1.sql | 2350 | 0eaa403ba7437c855d35a556443564ee6f1e9fd1cc2d5c967aa71c4f17ed8f70 |
| host/storage/src/event_key.c | 7165 | 2229c561f45518300ce74eff4fdc5d582f8b7c6a5a30f3c07aa1adb532369edb |
| host/storage/src/regex.c | 25127 | b01a90e58f1869c9d884df4ebfe16ab7b536b380924b5742e16f7a6dfe2f50c8 |
| host/storage/src/repository_internal.h | 3344 | f68136968db1af077771b567b93dd974865294bb29c313f1928eec1748c6b64a |
| host/storage/src/repository.c | 89119 | e0e96016db0fc86588c38dd37a052e51d3007cd5279a049de2a13224c29b13a1 |
| host/storage/src/schema_embedded.c | 809 | fc0c2a336230556b2016a8f2b4e3987b18f71db717cb3f1e96d12d1c88652f46 |
| host/storage/src/schema_internal.h | 1022 | a5d1059e54b112fb232a1da7917d02b4739f5b2346ac18493dce2ac0e8e9f75a |
| host/storage/src/schema_records.json | 27525 | 781d7b139af23b5783040281e3dacc164a0d51076a11376e6bef1411941888ef |
| host/storage/src/schema.c | 26069 | 19bd5b4f9e2497258adc5254c81eaba6deb583194468fb2e0c32f886a93f1723 |
| host/storage/src/u64.c | 2180 | 875036a0abbf6d49b15ec33128adbe24cae34df74133985f4abbbf3e04b26696 |
| host/storage/third_party/cjson/cJSON.c | 80399 | 298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d |
| host/storage/third_party/cjson/cJSON.h | 16394 | 25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee |
| host/storage/third_party/cjson/LICENSE | 1084 | a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c |
| host/storage/third_party/sqlite/sqlite3.c | 9582521 | 0c657666d819f261cc4007526ad57e1669de9949df7a8ea847aa284a2bf0ee52 |
| host/storage/third_party/sqlite/sqlite3.h | 695729 | d6c84557261893439b341d2e6c35187ae3ea24fbfd843ad94616f72d89242b8c |
| host/storage/third_party/sqlite/sqlite3ext.h | 39547 | 48963abfefb41ac329ef9d706acb5eadba952a99a748285c9ded6b5e0942721b |
| host/storage/tools/extract_schema.py | 5355 | ab5990b55b661b8c7596002783a0f845266f0b5b7d91943d95578f1d117cb218 |
| README.md | 7859 | 8c7c835c42208da55a92b702d5ccc7f64bee5ae3744b4a023037f35babeebd54 |
| tests/unit/common/test_support.h | 5193 | 557bf284fa7336569e61ac29ba5c7e5de9c9d8bdb25bd8010daf6812950145b0 |
| tests/unit/storage/CMakeLists.txt | 2354 | bc3ccbbc732b04621e2f7a39f7434fc666a12876b0c24e31f86f1bd0e637963b |
| tests/unit/storage/test_event_key.c | 7913 | 159254c9091deb3b8d3c1e888ae8819caf2dcbd50882096cac9ef757feb2ca45 |
| tests/unit/storage/test_regex.c | 5128 | 8bb903aa8b760f76f6614962999c0689a5d43af43cda07d8a634f887ab6a15b2 |
| tests/unit/storage/test_repository.c | 130380 | 56c56b396dcf7798f67b57859855fccc7635456dd70729fc8645f98e7d21526b |
| tests/unit/storage/test_schema.c | 31269 | 4952adc2876f8a1c37d83ad42968ede1cd40d66a6a582862cd0d75941a20e2ca |
| tests/unit/storage/test_u64.c | 6040 | 6919ee37502373a4f0c19ef7ca6134820de333e150df37d353c02bb6ac9652e7 |

最终输入复核时间：2026-10-24T10:22:30.299Z。98项输入与初次冻结一致，handoff采用§3所列外部修订后的指纹，99项均匹配附录B。本审计唯一写入为本报告；未整改、改状态、commit或push。

交付校验：报告写入后，python -B -X utf8 scripts/check_contracts.py、scripts/check_stage_plan.py、scripts/check_vision_contracts.py 均exit0/PASS；99项最终受审输入无漂移，证据目录为51个文件。全文UTF-8字节往返检查通过。未新增本报告以外的本轮文件。
