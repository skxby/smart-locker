# S1-02 round-8 独立审计

日期：2026-10-24（Asia/Shanghai）。工作副本：%REPO%。契约：**0.2.1 / REVIEW_DRAFT**。

## 0. 结论与边界

**PASS（限定为本轮代码与证据复审）：未发现尚未修复的阻塞项 B-x；发现 4 项非阻塞问题 N-1～N-4，其中 N-2 在收尾时由外部修改修正，剩余3项交收尾负责人处理。** round-7 B-1 的 CHECK / FOREIGN KEY 接纳漏洞已在当前代码中封住，等长不同文本的回退也有专门正式回归与匹配的 mutation 记录。不能把旧轮 FAIL 原因直接套到当前版本。

这不是本轮 Linux C 运行验收 PASS，也不是 DONE 决定。**本轮 configure/build/CTest、C 测试二进制、ASan+UBSan、TSan、C mutation 复跑全部 NOT_RUN。** 本轮实际执行了 Windows Python 设计检查、抽取器正负控、原始字节指纹核验、日志重算及内存 SQLite 探针。Linux PASS 只按现存交付日志的历史身份引用，未升级为本轮实跑或全部构建输入已被独立认证。

关键数字更正：两份 unit-tests.log 的逐项和均为 **1715 = common 505 + storage 1210**，受审输入快照中用户转述与 summary/handoff 的 1707 = 505 + 1202 有误；收尾时外部已修正两份当前文档。repository 仍为 863；这是合计算术错误，不是测试运行中增加了断言，详见 N-2。

本轮只新增本报告，未修改实现、测试、契约/生成物、stage-plan、README、summary/handoff 或历轮审计。未置 DONE，未开始 S1-03，未 commit/push。S1-01 仍 DONE；S1-02 仍 IN_PROGRESS、report=null，active_task/recommended_next=S1-02；阶段1 gate_result=NOT_RUN。

## 1. 依据、范围与实际环境

核对顺序为 AGENTS.md → 根 README → docs/stages/README → stage-plan → 阶段1公共规则及 S1-02 卡 :48–56，继而核对阶段0持久化规则、internal-interfaces、UART-v1、traceability 与 module-ownership。核查六个 Repository 签名（生成头 :211/:213/:215/:273/:275/:277）、context :190、原生 DTO :143–147、事件批次 :120；对照内部数据源、派生 schema、18 集合和 47 defs 的仓储冻结子集。

审查 host/storage 的唯一 SQL、repository/schema/u64/event_key/regex 相关实现、全部私有头、嵌入器、抽取器和局部 CMake；检查五个 storage 正式测试、公共 test_support、根 CMake 接入、第三方锁与许可。对历轮审计的问题和整改落点逐项回溯，重点复查 round-7。第三方检查限于版本/许可/锁定形态及六个文件的 SHA-256、字节数一致性，不声称完成 SQLite/cJSON 上游安全审计。此次为限定范围只读复审，没有新项目/较大实施方案，没有下载或替换依赖。

rg --files -g AGENTS.md 仅返回根 AGENTS.md。普通 exec_command 首次启动在 helper setup 阶段失败，未启动 shell，无 shell 退出码；随后使用 Node 文件接口及 child_process.execFile 参数数组，cwd 固定为本项目。未提权、安装工具或修改 SSH/WSL 设置。

| 本轮操作 | 实际结果 |
|---|---|
| git status --short | exit 128；fatal: not a git repository (or any of the parent directories): .git |
| python --version | exit 0；Python 3.14.6 |
| cmake --version | ENOENT；PATH 中不可调用，不推断全盘未安装 |
| wsl --list --quiet | exit 4294967295；UTF-16 输出含 Wsl/EnumerateDistros/Service/E_ACCESSDENIED |
| ssh -o BatchMode=yes -o ConnectTimeout=6 locker-vm，执行只读 printf | exit 255，stdout/stderr 为空；未取得 VM 会话，不推断 guest 开关机状态 |
| python -B -X utf8 scripts/check_contracts.py | exit 0 / PASS；0.2.1，24 routes / 82 ports / 28 private RPC，hardware_profile=UNVERIFIED，business_or_hardware_tested=false |
| python -B -X utf8 scripts/check_stage_plan.py | exit 0 / PASS；45卡，done=1，active_task=S1-02 |
| python -B -X utf8 scripts/check_vision_contracts.py | exit 0 / PASS；camera_owned=false，rknn_available=false，hardware_tested=false |
| python -B -X utf8 scripts/check_feasibility.py | exit 0 / PASS；runtime_or_hardware_tested=false |
| python -B -X utf8 host/storage/tools/extract_schema.py --check | exit 0 / OK；27525 B 冻结子集匹配真源 |
| 同一抽取器 main(--check)，仅在内存给 FROZEN 字节尾部加一个空格 | **exit 1，预期负控**；输出 DRIFT，磁盘文件未改 |
| Python 内存 DDL / SQL 探针（附录A） | 最终版 exit 0；SQLite 3.53.2；仅 SQL 语义和 C 算法辅助模型，非 C Repository 执行 |
| Python AST 解析 mutation 定义、Node 日志重算 | 成功；42名称对应、每个 edit 锚点恰好1次、逐条真实 FAIL 行相符 |

本轮 check_contracts 的 c_declaration_syntax 为 PASS (parse only, not compile/link)。历史 WSL/VM 日志对此记 NOT_RUN (optional pycparser unavailable)。这是环境差异，本轮未安装依赖；保留历史输出，不把 parse 当成 C 编译链接。

## 2. round-7 指定问题复核

### 2.1 B-1：表内约束漏洞已静态闭环

repository.c:2037 的真实调用顺序是 verify_table_columns :2061 → verify_key_index :2065 → verify_official_objects :2071 → verify_official_ddl :2077。首次初始化在 BEGIN IMMEDIATE 后执行唯一 DDL，:2409 核结构、:2412 核元数据，然后 :2415 COMMIT；已初始化分支 :2421 同样核结构再核元数据。核验失败沿现有路径关闭连接并保持 out=NULL，未见清库/重建旧库的路径。

verify_official_ddl :2244 对 meta/migrations/documents 参数绑定查询 sqlite_master.sql（:2254），期望文本直接来自嵌入的唯一 schema-v1.sql。不存在第二份手抄官方 CREATE TABLE 常量。:2261 提取失败即 CONFLICT；:2274 无行、:2280 NULL/长度不等、:2281 memcmp 不等、:2286 多行均拒绝或传播 SQLite 错误，没有意外返回 OK 的已见分支。

expected_table_ddl :2157 去掉固定 IF NOT EXISTS 前缀、去掉结尾结构分号，保留中间字节。:2185 检查表名边界，避免 meta 误命中 metadata；:2189 起维护括号深度和引用状态，处理单/双/反引号、方括号以及双写闭合符；:2219 只有 depth==0 且不在引用内的分号结束语句，:2225 无结束及 :2229 超缓冲失败。当前三个真实表定义分别为 96/131/164 B，均小于1024 B缓冲。它是当前固定官方 SQL 的提取器，本审计没有把它认证成任意 SQLite 文法解析器，也未运行完整引号/注释变体的 C 单元测试。table 参数是内部三项常量；不能据其内部非空前提声明它可供任意外部输入直接调用。

附录A读取真实 SQL 和正式测试宏，在 Python SQLite 内存库中独立观察到：

| 定义 | documents.sql 字节 | 与官方文本相同 | 列/键/用户对象/元数据/quick_check 组合与官方相同 |
|---|---:|---|---|
| 官方 | 164 | 是 | 是 |
| SQL_CHECK_CONSTRAINT | 191 | 否 | 是 |
| SQL_FOREIGN_KEY | 185 | 否 | 是 |
| SQL_DDL_SAME_LEN_DIFFERENT_TEXT | 164 | 否 | 是 |

所以旧结构视图确实不能区分这三类夹具；新增文本比对能区分。本轮还用实际 UUID_KEY=99999999-2222-4333-8444-555555555555 构造同一份两条 audits 的合法 change：官方 SQL 事务得到2行/revision1；CHECK SQL 事务第二条失败，回滚到0行/revision0。这是 SQL 语义探针，不是 C open/commit 实跑。

正式 C 负例在 test_repository.c :2542/:2543、:2554/:2555、:2567/:2568，对两种 initialize_empty 都断言 CONFLICT，broken_schema_probe :2250 另断言 repo==NULL；:2550/:2561/:2575 确认用户对象仍3个。对象数保持只证明没有增删对象，不把它夸大为逐字节数据库完全未变。

保留 mutation 中 m33=12/863，m34=4/863，m34b=12/863，逐条匹配 raw batch；m34 只命中等长夹具，说明内容比较被正式回归观察到。各负例两种模式的状态断言和 NULL helper 都会计数：三夹具×两模式×两断言=12，不是12个独立场景。m34b 实际删掉长度和内容两项，只保留 NULL 判断；不能描述成保留长度分支。

### 2.2 N-1：主要证据修正成立，正文仍有残余

m29b 已从实际 POINTS 和42段日志移除，文本只在重复执行说明中提及；未把它算为新覆盖。顺序已在主表中改对；m28/m31/m32 的真实断言分别4/4/3；DELETE失败后 revision1/文档1 的正式断言与当前整改表一致。原始 mutation 定义和诊断日志已正式保留，没有作为临时文件清掉。

但个别正文仍把联合红测整体当作目标分支贡献，详见 N-1。它不推翻已存在的三点联合 partial 证据，也不重新打开 round-7 B-1。

## 3. 非阻塞发现

### N-1 / P2：mutation 归因仍有不成立的概括

位置：summary.md:250/:275/:278，handoff.md:462–464，mutation.log:80；执行描述另见 summary.md:216 对照 mutation-batch.py:162–177。

- m26b 的4条红行全是 trigger 夹具 :2475/:2476 与 helper :2250×2，和 m28 单点相同；没有 partial 的 :2460/:2461。summary:250 仍称它为 round-5 B-1 的可观测形式，:275 又用它与三点联合并列证明 partial 必要，表述过强。
- m26+DDL-off 的12条也全由 m33 的 CHECK/FK/等长夹具贡献，没有 partial 红行。handoff:462 把八个联合点一概说成证明对应分支必要，mutation.log:80 也有此概括，不能成立于 m26。partial 的目标证据应只引用三点联合 m26+m28+m33，20条中具体只有4条归 partial，其余4条是trigger、12条是DDL夹具。
- 联合红测能证明关闭兜底后的可观测行为，不能单凭它断言每一防线在全部现有防线同时开启时不可替代。主表已承认遮蔽，应保持这一层区别。
- summary:216 说 CTest 后又直接运行变异二进制取 FAIL；保留执行器实际从同一次 ctest --output-on-failure 的 stdout 抽 FAIL，没有第二次直接执行命令。应改成实际执行方式。

建议在当前正文直接陈述准确归因，并保留原始日志。验收方法：按各夹具状态行对比 m28、m33、m26b、m26+DDL-off、m26c，不能仅比较总数。**非阻塞理由**：三点联合定义/日志确已存在，partial 原缺陷防护也在，问题是残余说明超出证据；无需为文字勘误重跑全部 C 测试。

### N-2 / P2：断言合计少算8条（发现时成立，收尾已由外部修正）

位置：summary.md:174–187（分项正确但合计错）、handoff.md:406/:508，以及其1707汇总处；实际依据 unit-tests.log:1–22 与 vm-s102-unit-tests.log:1–22。

| 组 | 分项 | 正确小计 |
|---|---|---:|
| common | 97+5+53+164+133+53 | 505 |
| storage | event_key80 + regex46 + repository863 + schema131 + u6490 | **1210** |
| 合计 | 505+1210 | **1715** |

Node和Python分别从两份原始日志提取并求和，得到相同结果；每份11条PASS、11个rc=0。第12个CTest是Python漂移门禁，没有额外C断言，不应拿它补/扣8条。发现此问题时输入复查无变化，不能将此解释成测试文件在本轮漂移；收尾出现的外部文档数字修正另见附录C。

收尾时重新读取确认，summary的合计与handoff当前汇总已由外部统一为1715/1210，N-2的文档勘误已落实；本Agent没有写入这两个文件。原始unit日志和源码保持不变。保留历史审计历史数字，不通过改旧日志凑总数。验收方法是重新解析两个unit日志求和，无需重新运行C测试。**非阻塞理由**：逐项运行记录及源码没有因为合计错误而失效，但不能继续向下一位AI传播1707。

### N-3 / P2：官方 DDL 正对照不是声称的同一份 CHECK 触发输入

位置：test_repository.c:2722–2755；相关描述 summary.md:378、handoff.md:624。

正对照确实提交两条 audits，并断言成功、revision1、documents_count2，能证明官方 DDL 可用。它的文档 key 为11111111-0000-4000-8000-000000000001与21111111-0000-4000-8000-000000000002，而 SQL_CHECK_CONSTRAINT 使用 UUID_KEY（:44，99999999-2222-4333-8444-555555555555）。因此该 change 并不命中当前CHECK谓词，不能声称它就是坏库会失败的同一份输入。辅助 build_audit_record :1729 还按index生成00000001/00000002开头的value.id，与这里手填的key不同；虽然当前schema只校验字段形态，没有比较这两个字段，夹具应遵循其声明的id映射语义。

建议复用一份key=value.id且第二条命中UUID_KEY的合法change，官方路径保留2行/revision1正对照；或先收窄正文为“官方库两条audits成功”，不声称输入相同。本轮附录A已展示同输入的SQL正负对照，仍不能替代正式C回归。**非阻塞理由**：正式坏结构open双模式负例和m33/m34都不是靠这条正对照捕获；这是正对照保真度不足，没有显示生产代码仍接纳坏库。

### N-4 / P3：模块说明仍有构建期/源码入口混称

位置：host/storage/README.md:32/:33/:60/:78，host/storage/CMakeLists.txt:42–72，summary.md:54/:409，handoff.md:485；其余当前正文的同类用语一并检查。

.inc 由 add_custom_command 在构建期生成，不是配置期；它只写 binary/generated 目录，这个实现边界正确。模块 README:78 把VM canonical源码入口写为顶层locker-workspace/src，而本卡当前源码在locker-workspace/S1-02/src，summary/handoff和VM run-summary均写明顶层src不含host/storage。

建议当前说明直接写“构建期嵌入”和本卡实际源码目录；不要修改历史批次或把模块README当成另一套VM入口。验收为逐处对照custom command与当前run-summary。**非阻塞理由**：实际生成目标与VM日志路径一致，没有显示构建使用错目录。

## 4. 源码与证据一致性

### 4.1 指纹和范围

- source-manifest.txt 的32项逐项校验SHA-256与字节数：**32/32匹配**；根CMake+host/storage+tests/unit/storage实际文件数也为32，无清单漏项。
- manifest.sha256、vm-s102-manifest.sha256 与上述32行同序同摘要；既有两份source-verify记32/32 OK。清单本身SHA-256为8be11115a6223191666125c580728389d9420dfc6c89377c25efd9b88e213f60，3563 B。
- mutation.log 的11行SOURCE_FINGERPRINT均与当前字节数和SHA-256前12位一致。repository.c为b9ab2abaa41e / 95832 B，test_repository.c为6ab9a68d1c99 / 137410 B。不能继续套用旧e0e96016/56c56b39快照结论。
- 六个vendor文件与dependencies.lock内逐文件大小/全SHA-256全部匹配。SQLite 3.54.1 Public Domain与cJSON 1.7.19 MIT的锁、许可证据存在；本轮未重新取得归档去证明archive member来源。
- 写报告前证据目录**55文件/35个.log**；加本报告应为56文件。summary/handoff记录55是写入前正确的基数，不据新增审计文件制造旧正文计数缺陷。

32项仅覆盖其标明的卡范围，不含全部common源码、契约头、编译器、测试harness等构建输入。11个短指纹也不能独立认证42个变异目录、时间顺序或完整二进制来源。本轮没有取得VM快照包/二进制/CMakeCache或读VM实盘，因此不把“32/32匹配”外推成所有1016文件及sanitizer编译标志均已独立验证。无Git历史也无法独立证明根CMake相对开工版本仅改两行，只能确认当前两处接入与S1-01已有入口兼容。

### 4.2 保留Linux运行记录（非本轮复跑）

| 环境 | configure/build/CTest记录 | ASan+UBSan记录 | TSan记录 |
|---|---|---|---|
| WSL2，s102_r7ev | 三步exit0；12/12，0.29s；普通配置/构建警告数0 | 三步exit0；12/12，1.16s；ASAN_REPORTS=0 | 三步exit0；12/12，1.81s；TSAN_HITS=0 |
| VMware，20261024-193500 | 三步exit0；12/12，0.55s；普通配置/构建警告数0 | 三步exit0；12/12，1.27s；ASAN_REPORTS=0 | 三步exit0；12/12，2.10s；TSAN_HITS=0 |

分别核对configure/build/ctest各自退出标记，不把某个汇总文件说成包含所有命令。VM run-summary记录CTEST/ASAN_CTEST/TSAN_CTEST及0报告；source-verify为32/32；run_id与输出目录吻合。保留日志没有完整编译命令和CMakeCache，本轮无法确认二进制实际插桩。

VM记录的源码为/home/%USER%/locker-workspace/S1-02/src，build/logs为各自S1-02/20261024-193500，artifacts为/home/%USER%/locker-workspace/artifacts/S1-02。快照记录4cf41b51bc3f95ed70b33ac0ff10c4c97f1422c6acc36c25c031966a33396abf / 81766424 B；源码计数1016、tar条目1113来自交付记录，非本轮重数。152903/174830及更早批次在当前正文作为历史列出，不当作当前验收；旧审计的1681/829等历史值不应覆盖成新值。

### 4.3 mutation 独立重算

通过ast.literal_eval只读解析POINTS赋值，按实际8个JOINTS名称在内存追加；没有执行复制/删除/构建部分。对每一变异依次模拟文本替换，每个old在当时文本内恰好出现1次；42名称与两份日志对应。所有记录patch=OK、configure=0、build=0；32次CTest=8、10次=0。这里是保留记录重算，不是42次新的C执行。

mutation.log为26594 B、663个换行终止的文本行（split('\n')得到664项，末项为空）；mutation-batch.log为46572 B、562文本行（同理split得到563项）。两种行计数不矛盾。42=34具名项+8个后缀联合项；34具名项中本来已有m15b/m26b多点形式，不能把34说成34个单点缺陷。另有m26c三点联合一次，位于独立脚本/日志，**不在42批次内**；若汇总本包所有保留运行，应明确为42+1，而不是把三点联合暗算进42。

逐点真实FAIL行（扣掉尾部suite summary）与mutation.log完全一致：

| 变异 | CTest记录 | 真实断言失败 |
|---|---:|---:|
| m1_revision | 8 | 5 |
| m2_dedup | 8 | 5 |
| m3_adjacent | 8 | 9 |
| m4_u64_fallback | 8 | 2 |
| m5_schema_additional | 8 | 2 |
| m6_sync | 0 | 0 |
| m7_gap_latch | 8 | 2 |
| m8_trace_guard | 8 | 3 |
| m9_page_budget | 0 | 0 |
| m9b_doc_bytes | 8 | 4 |
| m10_nul_guard_raw | 8 | 6 |
| m11_parse_nul | 8 | 17 |
| m12_boundary_structure | 8 | 4 |
| m13_first_available_zero | 8 | 3 |
| m14_gap_latch_early | 8 | 2 |
| m16_sqlite_prefix | 8 | 1 |
| m17_verify_columns | 0 | 0 |
| m18_verify_key_index | 0 | 0 |
| m20b_any_matching_index | 0 | 0 |
| m21b_skip_column_collation | 0 | 0 |
| m22_skip_extra_unique_index | 0 | 0 |
| m25_regex_bounded_zero | 8 | 2 |
| m26_partial_unique | 0 | 0 |
| m27_regex_min_seed | 8 | 1 |
| m28_official_objects | 8 | 4 |
| m29_table_info | 0 | 0 |
| m30_hidden_only | 0 | 0 |
| m31_put_changes | 8 | 4 |
| m32_delete_changes | 8 | 3 |
| m15b_view_double | 8 | 4 |
| m26b_partial_plus_objects | 8 | 4 |
| m33_ddl_skipped | 8 | 12 |
| m34_ddl_length_only | 8 | 4 |
| m34b_ddl_text_not_compared | 8 | 12 |
| m17_verify_columns_plus_ddl_off | 8 | 32 |
| m18_verify_key_index_plus_ddl_off | 8 | 24 |
| m20b_any_matching_index_plus_ddl_off | 8 | 16 |
| m21b_skip_column_collation_plus_ddl_off | 8 | 16 |
| m22_skip_extra_unique_index_plus_ddl_off | 8 | 16 |
| m26_partial_unique_plus_ddl_off | 8 | 12 |
| m29_table_info_plus_ddl_off | 8 | 16 |
| m30_hidden_only_plus_ddl_off | 8 | 16 |

未捕获10项准确为m6_sync、m9_page_budget，以及m17/m18/m20b/m21b/m22/m26/m29/m30。m6仅静态看到FULL；m9夹具没有逼近缩小预留后的边界，不能因未捕获就称测试失败或这段已被证明无必要。m9b实际分母848，不能用完整绿测863替代。

对联合结果进一步按m33基线拆分：

| 联合项 | 总红行 | 其中m33的DDL夹具行 | 其余目标夹具行 |
|---|---:|---:|---:|
| m17+DDL-off | 32 | 12 | 20 |
| m18+DDL-off | 24 | 12 | 12 |
| m20b+DDL-off | 16 | 12 | 4 |
| m21b+DDL-off | 16 | 12 | 4 |
| m22+DDL-off | 16 | 12 | 4 |
| m26+DDL-off | 12 | 12 | **0** |
| m29+DDL-off | 16 | 12 | 4（追加生成列） |
| m30+DDL-off | 16 | 12 | 4（替换生成列） |

这些是同一套夹具中的断言行归类，不折算独立缺陷数量。m26c日志中20条真实断言=4 partial+4 trigger+12 DDL，suite summary为20 of863。m26b只有4 trigger；两个总数不能全归partial。m20b/m22虽edit不同，但命中同一if块且红行集合相同。m29b旧重复点删除已确认。

## 5. 按卡审计核查矩阵（40项）

“静态符合”只针对列出的代码与回归设计；C运行按本报告§1/§4区分，没有隐含实跑。

| # | 核查项 | 结果及定位 |
|---|---|---|
| 1 | 前置S1-01 | 机器索引DONE，summary与当前audit顶部PASS；本轮未重验该卡 |
| 2 | 当前卡/阶段 | S1-02 IN_PROGRESS/report=null；S1-07 gate NOT_RUN；未跨卡 |
| 3 | 契约版本 | 0.2.1/REVIEW_DRAFT，生成头一致，设计检查实跑PASS |
| 4 | 六端口/DTO | 生成头签名与实现一致；无新增同义公开API |
| 5 | 受控集合 | 18集合映射，value按collection再校验，repository:779–785 |
| 6 | 冻结闭包 | 47defs/27525B，extract --check实跑OK |
| 7 | 漂移负控 | 原main仅替换内存FROZEN，实跑exit1/DRIFT |
| 8 | 根构建接入 | 当前:87/:92两处接入；历史只改两行无法由无Git目录证明 |
| 9 | 局部构建/嵌入 | generated仅在binary目录；原始字节数组加尾NUL；N-4订正文案 |
| 10 | 依赖锁/许可 | 六vendor逐文件大小/摘要匹配，无浮动下载 |
| 11 | 单写与模块边界 | Core单线程拥有仓储；无HTTP/UI/GPIO/设备发送实现 |
| 12 | 提交事务 | :1377 BEGIN IMMEDIATE，apply_change后commit；失败rollback |
| 13 | revision检查 | :704期望+1防溢出，:713比较当前；m1对应5真实红行 |
| 14 | 中途SQL失败回归 | test_failed_transaction_is_atomic:569，第二次INSERT触发ABORT；不只是非法入参 |
| 15 | 并发占位 | 顺序竞争:656，两个fork独立open同库:720；正式目标一OK一CONFLICT；本轮NOT_RUN |
| 16 | 重开保留 | test_persistence_across_reopen:800；正常close/reopen，不能证明崩溃/掉电 |
| 17 | 新事实去重 | :1635存在检查，:1636只插新；全重复不应用projection |
| 18 | 重复页新gap | :1658分流，:1678写false；不推进through；正式重复页回归在:1451 |
| 19 | 事件原子边界 | 新事件、可选Core投影、cursor同一事务；失败回滚；无借还业务猜测 |
| 20 | 页内连续 | :1416同boot seq+1防MAX；:1545逐对核查 |
| 21 | 跨页/不倒退 | :1701连续检查、:1706历史回填拒；continuity=false不被后页洗白 |
| 22 | boot跳号 | boot递增且seq1；完整性仍依赖可信设备gap语义，不由两个号独立证明无缺失 |
| 23 | 半零哨兵 | :1403结构判定，事件与边界均调用；每条事件半零专用负例仍可补强 |
| 24 | 日志包络 | :1556非空尾，:1567同日志，:1570不越尾，:1573首非零，:1586 next_after==页尾 |
| 25 | MCU/RK水位区分 | last_committed为MCU已提交尾；committed_cursor/through为RK，OK不自动等于可ACK |
| 26 | epoch事件键 | device/epoch/20位boot/20位seq，uint64比较，不经浮点 |
| 27 | u64边界 | 复用common numeric，溢出/规范文本测试；m4只证明非法回退分支 |
| 28 | 分页预算 | :973信封+1160B续键余量，:1099完整序列化长度；缩页非空/续页正式回归在:1751 |
| 29 | query/recovery | 参数绑定，key/after互斥，limit1..100，恢复分页，同读事务revision，输出自校验 |
| 30 | NUL防护 | schema:35原始0x00/奇偶转义扫描，repo:502也守；每例独立DB与正对照 |
| 31 | regex有界重复 | :531零界种子1，:562正界播种min；test_regex:72边界齐，m25/m27目标分别可观测 |
| 32 | 空库/有痕迹 | :2380–2394任意对象/GLOB计数；部分库不自动补建；DDL中途注入仍未覆盖 |
| 33 | 视图/前缀 | 同名非表拒绝，NOT GLOB避免LIKE下划线；m15b不证明首次视图假成功 |
| 34 | 列/键结构 | xinfo含hidden；unique逐一匹配/partial拒绝，BINARY/升序校验 |
| 35 | 官方对象清单 | :2086恰好三官方表，无额外index/view/trigger；round-6触发器反例已静态封住 |
| 36 | 写入效果 | :438 PUT changes==1；:477仅已存在DELETE要求1；DROP TRIGGER正对照在:2604用例 |
| 37 | 隐藏/生成列 | :1856 xinfo及:1881 hidden拒；两类夹具与m29/m30联合归因成立 |
| 38 | 完整DDL接纳 | :2244逐字节与唯一SQL比对；round-7 B-1静态闭环；m33/m34/m34b相符 |
| 39 | WAL/FULL与恢复 | :2362/:2366设置；WAL另连接正式回归；FULL无运行时断言，WAL返回行未核，既有缺口不夸大 |
| 40 | 证据/文档/残留 | 32/32、11/11、42/32/10准确；1715算术已由外部订正；仅本报告新增；硬件仍NOT_RUN |

## 6. 交接处理与未验证项

本轮没有据以重开旧阻塞项的反例，也未发现新的阻塞项。N-2已由外部完成数字勘误；N-1/N-4可通过当前正文勘误解决；N-3若仅收窄正对照的描述，不需要重新跑产品C代码；若补改正式测试，应按固定快照重新跑相应测试并更新匹配证据，而不能只改指纹。完成文档收尾后的卡状态由获授权负责人处理，本审计不置DONE、不继续S1-03。

仍未完成：本轮Linux独立配置/编译/CTest及sanitizer/mutation复跑、sanitizer实际编译标志确认、完整VM快照/全构建输入/二进制独立绑定、DDL初始化中途故障注入、FULL运行时断言、CMake3.16最低版本、ARM/32位、RK介质fsync/真实掉电、MCU/UART/柜锁/Android/LVGL/NPU/相机和业务闭环。未运行项不写PASS；WAL/FULL代码和正常重开日志不能代表硬件持久性。

文档同步检查已做，需修正文案已按N项列出；遵守本轮仅写审计的范围，未越权覆盖summary/handoff/README。所有本轮取证脚本在Node内存或python -B -c执行，SQL库均:memory:；没有建立磁盘DB、一次性脚本、构建目录或临时日志。host/storage与tests/unit/storage扫描无db/wal/shm/log/obj/o/a/exe/pyc/tmp；根目录未见本卡.tmp/s102_/_mut/_anchors/mut_build/mut_test/build2残留。保留正式测试、mutation定义/输出与历史诊断证据。既有build/HAL目录不属于本轮临时内容，未删；VM/Public/TEMP外部历史清理声明未全量复核，不替其“已清0”签字。

## 附录A：本轮成功执行的内存SQL辅助探针

执行方式：python -B -X utf8 -c，cwd为项目根。以下为最终成功版本，exit0。此前一次Python字符串引号语法错误、一次宏抽取正则错误均发生在内存探针中，已纠正；不计为产品失败或成功运行。SQLite版本3.53.2，不是vendor3.54.1；expected函数是对当前C提取逻辑的辅助模型，不能代替C函数实测。

~~~python
import sqlite3,pathlib,re,json,sys
schema=pathlib.Path('host/storage/schema-v1.sql').read_bytes().decode('utf8')
tests=pathlib.Path('tests/unit/storage/test_repository.c').read_text(encoding='utf8')
key=re.search(r'#define UUID_KEY "([^"]+)"',tests).group(1)
def macro(name):
 m=re.search(r'^#define '+name+r'[^\n]*(?:\n[ \t]+[^\n]*)*',tests,re.M)
 # collect only continuation lines, stopping at first line without trailing backslash
 rows=tests[tests.index('#define '+name):].splitlines(); raw=[]
 for row in rows:
  raw.append(row)
  if not row.rstrip().endswith(chr(92)): break
 return ''.join(key if t=='UUID_KEY' else json.loads(t) for t in re.findall(r'"[^"]*"|UUID_KEY',chr(10).join(raw)))
def expected(table,out_size=1024):
 prefix='CREATE TABLE ';needle='CREATE TABLE IF NOT EXISTS ';offset=0
 while offset+len(needle)<=len(schema):
  if not schema.startswith(needle,offset): offset+=1;continue
  start=offset+len(needle)
  while start<len(schema) and schema[start] in ' \t\r\n':start+=1
  if start+len(table)>=len(schema) or schema[start:start+len(table)]!=table:offset+=1;continue
  cursor=start+len(table)
  if schema[cursor].isascii() and (schema[cursor].isalnum() or schema[cursor]=='_'):offset+=1;continue
  depth=0;quote='';scan=cursor;closed=False
  while scan<len(schema):
   c=schema[scan]
   if quote:
    closing=']' if quote=='[' else quote
    if c==closing:
     if scan+1<len(schema) and schema[scan+1]==closing:scan+=2;continue
     quote=''
    scan+=1;continue
   if c in chr(39)+chr(34)+chr(96)+'[':quote=c;scan+=1;continue
   if c=='(':depth+=1
   elif c==')':depth=max(0,depth-1)
   elif c==';' and depth==0:closed=True;break
   scan+=1
  if not closed:return None
  result=prefix+schema[start:scan]
  return result if len(result.encode('utf8'))<out_size else None
 return None
c=sqlite3.connect(':memory:');c.executescript(schema)
expected_map={t:expected(t) for t in ('meta','migrations','documents')}
for t,s in expected_map.items():
 actual=c.execute("SELECT sql FROM sqlite_master WHERE type='table' AND name=?",(t,)).fetchone()[0]
 assert actual==s
 print('OFFICIAL_DDL',t,len(actual.encode()),'MATCH')
assert expected('metadata') is None and expected('meta',2) is None
print('EXTRACT_MISSING_AND_OVERFLOW',True)
def views(c):
 return dict(cols=c.execute("SELECT name,type,[notnull],pk,hidden FROM pragma_table_xinfo('documents') ORDER BY cid").fetchall(), idx=[c.execute('SELECT name,[desc],coll FROM pragma_index_xinfo(?) WHERE [key]=1 ORDER BY seqno',(r[0],)).fetchall() for r in c.execute("SELECT name FROM pragma_index_list('documents')")],objects=c.execute("SELECT type,name FROM sqlite_master WHERE name NOT GLOB 'sqlite_*' ORDER BY name").fetchall(),meta=c.execute('SELECT * FROM meta ORDER BY name').fetchall(),migration=c.execute('SELECT * FROM migrations').fetchall(),quick=c.execute('PRAGMA quick_check').fetchall())
baseline=views(c)
for name in ('SQL_CHECK_CONSTRAINT','SQL_FOREIGN_KEY','SQL_DDL_SAME_LEN_DIFFERENT_TEXT','SQL_PARTIAL_UNIQUE_INDEX','SQL_TRIGGER_IGNORE','SQL_GENERATED_COLUMN','SQL_GENERATED_APPENDED'):
 d=sqlite3.connect(':memory:');d.executescript(schema);d.executescript(macro(name));actual=d.execute("SELECT sql FROM sqlite_master WHERE name='documents'").fetchone()[0]
 print(name,'bytes',len(actual.encode()),'same_ddl',actual==expected_map['documents'],'same_preddl_views',views(d)==baseline,'objects',len(views(d)['objects']))
 if name=='SQL_DDL_SAME_LEN_DIFFERENT_TEXT':assert len(actual.encode())==len(expected_map['documents'].encode())==164 and actual!=expected_map['documents']
 if name in ('SQL_CHECK_CONSTRAINT','SQL_FOREIGN_KEY','SQL_DDL_SAME_LEN_DIFFERENT_TEXT'):assert views(d)==baseline and actual!=expected_map['documents']
 d.close()
# identical official transaction vs CHECK fixture: target actual UUID_KEY from source
sys.path.insert(0,'scripts');from check_contracts import validate_value
contract=json.loads(pathlib.Path('contracts/internal-data.schema.json').read_text(encoding='utf8'))
keys=['11111111-2222-4333-8444-555555555555',key]
records=[dict(id=k,actor_id=None,operation_id=None,type='AUDIT',reason='round8',occurred_at=None,time_quality='UNSYNCED',source_event=None) for k in keys]
change=dict(schema_version=1,expected_snapshot_revision='0',new_snapshot_revision='1',checks=[],mutations=[dict(op='PUT',document=dict(collection='audits',key=r['id'],value=r)) for r in records])
assert validate_value(contract['$defs']['RepositoryChange'],change,contract['$defs'])
for variant in ('official','check'):
 d=sqlite3.connect(':memory:');d.executescript(schema)
 if variant=='check':d.executescript(macro('SQL_CHECK_CONSTRAINT'))
 d.execute('BEGIN IMMEDIATE');error=None
 try:
  for r in records:d.execute('INSERT INTO documents VALUES (?,?,?) ON CONFLICT(collection,key) DO UPDATE SET value_json=excluded.value_json',('audits',r['id'],json.dumps(r,separators=(',',':'))))
  d.execute("UPDATE meta SET value='1' WHERE name='snapshot_revision'");d.commit()
 except sqlite3.Error as e:error=str(e);d.rollback()
 print('TX',variant,'error',error,'documents',d.execute('SELECT count(*) FROM documents').fetchone()[0],'revision',d.execute("SELECT value FROM meta WHERE name='snapshot_revision'").fetchone()[0])
print('SQLITE',sqlite3.sqlite_version,'PYTHON',sys.version.split()[0])

~~~

关键原始输出：

~~~text
OFFICIAL_DDL meta 96 MATCH
OFFICIAL_DDL migrations 131 MATCH
OFFICIAL_DDL documents 164 MATCH
EXTRACT_MISSING_AND_OVERFLOW True
SQL_CHECK_CONSTRAINT bytes 191 same_ddl False same_preddl_views True objects 3
SQL_FOREIGN_KEY bytes 185 same_ddl False same_preddl_views True objects 3
SQL_DDL_SAME_LEN_DIFFERENT_TEXT bytes 164 same_ddl False same_preddl_views True objects 3
SQL_PARTIAL_UNIQUE_INDEX bytes 123 same_ddl False same_preddl_views False objects 4
SQL_TRIGGER_IGNORE bytes 164 same_ddl True same_preddl_views False objects 4
SQL_GENERATED_COLUMN bytes 171 same_ddl False same_preddl_views False objects 3
SQL_GENERATED_APPENDED bytes 270 same_ddl False same_preddl_views False objects 3
TX official error None documents 2 revision 1
TX check error CHECK constraint failed: key <> '99999999-2222-4333-8444-555555555555' documents 0 revision 0
SQLITE 3.53.2 PYTHON 3.14.6
~~~

漂移负控同样只运行内存代码，预期exit1：

~~~python
import importlib.util,pathlib,sys
p=pathlib.Path('host/storage/tools/extract_schema.py');s=importlib.util.spec_from_file_location('round8extract',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);real=m.FROZEN
class MemoryFrozen:
 def exists(self):return True
 def read_bytes(self):return real.read_bytes()+b' '
 def relative_to(self,p):return real.relative_to(p)
m.FROZEN=MemoryFrozen();sys.exit(m.main(['--check']))

~~~

## 附录B：本轮冻结的106项受审输入

冻结时间（UTC）：2026-10-24T12:41:21.777Z。以下为原始字节数与完整SHA-256；报告自身不进入自身表。该集合包含32项卡范围源码、55项既有S1-02证据和关键入口/真源/架构/前置文件，去重后106项，不是全盘或全部构建输入清单。

| 路径 | bytes | SHA-256 |
|---|---:|---|
| AGENTS.md | 5839 | bb8658418e3926ad7799f3be148107731c61fc1aa3b668992dd80ee0e1d217de |
| CMakeLists.txt | 3943 | f9e1964ee54465f5e29299721a3f342f929cc81e3ff6a6da8d00b77fc34258b4 |
| README.md | 7859 | 8c7c835c42208da55a92b702d5ccc7f64bee5ae3744b4a023037f35babeebd54 |
| config/development/stage-plan.json | 24749 | 1ed89d9f5695c40f965f43d331907295eccca21eaf52d67bee92e5dc4fdf1b47 |
| contracts/contract-source.json | 66553 | 14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f |
| contracts/include/locker_contracts.h | 39925 | 34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a |
| contracts/internal-data-source.json | 92193 | ebcfa5859c114e8746a2f235161c17a6dede651f56125d92cc37f259c7d81da1 |
| contracts/internal-data.schema.json | 129569 | e7b8695c49115e2994cf126dc5ab629e5c82460e70d4c292b19576f55c60e386 |
| deployment/host/README.md | 6753 | 9887fe18df7f964a66725cd0ae5ee3a3bc7d37fcc58fcdbde229da3d90c7bc7a |
| deployment/host/dependencies.lock.json | 14075 | 4884c4ca9944ccd66be72db71dcd99b289bba119763c965a7e3b7b7a961fac6d |
| docs/architecture/internal-interfaces.md | 29792 | a5b3cdc41c05006e83edc970c46c15f79644adb8fe071b71953f239a64c1c2cf |
| docs/architecture/module-ownership.md | 10135 | b182fe6e424b605fd4cad1073c85178588ee474ca8ab6f38be84e3129f72bb3f |
| docs/architecture/traceability.md | 12694 | 0903b559a6fc33fa0120d608b5b85783f66327b03a0f8267e7b86309877f2859 |
| docs/architecture/uart-v1.md | 26022 | 5bb2204f291b259f7b28cc7a2a1473352d5f1d0d501032168924d664c5e3ed8f |
| docs/evidence/stage-01/S1-01/audit.md | 50244 | 959d1614d590de99a72f1fb66e909aa15d8d3fe5c33d905d71d729c50fa01df8 |
| docs/evidence/stage-01/S1-01/summary.md | 37101 | 22951612245088deffb8fbb07552f9c7e30642374fc24cecad9de10745b23abc |
| docs/evidence/stage-01/S1-02/asan-build.log | 3567 | f8a44efd1ec66be0e2cb3f3fc3fd4374d9ae06219cdced8b5e3322c291fb66f7 |
| docs/evidence/stage-01/S1-02/asan-configure.log | 616 | 0b371ef48312dfa192e8dd09bb2453c40bf28d2d42e50946d353ab0d17975e91 |
| docs/evidence/stage-01/S1-02/asan-ctest.log | 1472 | 696e736573dbb886b00134a590e9b01d6d68c34b17f00895a0d22586b049261f |
| docs/evidence/stage-01/S1-02/audit-round2.md | 32835 | 3e2475309e154b3071269f4487ecd4ba28993c84cead38c7ee56e098797922f7 |
| docs/evidence/stage-01/S1-02/audit-round3.md | 40534 | 2d0ea66edca835e695ff297d0297c0229d4671bd68863a7596490fdcbc75b46a |
| docs/evidence/stage-01/S1-02/audit-round4.md | 52973 | f5c206a55f5a72d61ae4e6ba582c1573e84c77f5224131f50898213587487ac0 |
| docs/evidence/stage-01/S1-02/audit-round5.md | 44086 | c8aef59be2e1bd849d73e5794e505b02bde7a3da0030105e1fb6d3e1d03bd903 |
| docs/evidence/stage-01/S1-02/audit-round6.md | 43048 | 8dce31fdbf90a88442321744b18621dd9024f39fc85bcc7028cb146069b09598 |
| docs/evidence/stage-01/S1-02/audit-round7.md | 43331 | bba41358a208c432d7ee55aed1d3dc59b6f849e2321dee12afc018651d9ffdb2 |
| docs/evidence/stage-01/S1-02/audit.md | 23283 | 80ee21d98ed0877e2a3469a3ae9389f3ddc19663a4bf569bd39a25362ee3c8f8 |
| docs/evidence/stage-01/S1-02/build.log | 3574 | 713cb4c133f4d95ae8cd247f859ae6ed67cff8db1672989424cef3149e8e1b79 |
| docs/evidence/stage-01/S1-02/check_contracts.json | 535 | 0ac0a396fffb701522e05b2ff5f8c7725834a2641e59c109f41a62401870087d |
| docs/evidence/stage-01/S1-02/check_contracts.log | 535 | 0ac0a396fffb701522e05b2ff5f8c7725834a2641e59c109f41a62401870087d |
| docs/evidence/stage-01/S1-02/check_feasibility.json | 242 | cbe1da1d34c5d29634377f6a55c0199233a144e984417ba06553c9c5e149f02b |
| docs/evidence/stage-01/S1-02/check_feasibility.log | 242 | cbe1da1d34c5d29634377f6a55c0199233a144e984417ba06553c9c5e149f02b |
| docs/evidence/stage-01/S1-02/check_stage_plan.json | 319 | 572e375df64f10218f6e440803dcff0162f846df19877d5b77692e91683250d0 |
| docs/evidence/stage-01/S1-02/check_stage_plan.log | 319 | 572e375df64f10218f6e440803dcff0162f846df19877d5b77692e91683250d0 |
| docs/evidence/stage-01/S1-02/check_vision_contracts.json | 572 | 3f8b58703a9d039f594749a6924e19eda4e312d97cbdbf0ee1dac3b6325d8778 |
| docs/evidence/stage-01/S1-02/check_vision_contracts.log | 572 | 3f8b58703a9d039f594749a6924e19eda4e312d97cbdbf0ee1dac3b6325d8778 |
| docs/evidence/stage-01/S1-02/configure.log | 632 | aa4e97eb47a1ca294d035dbae1d7ab5fadea5a82e7873b05779db04115a3f2dc |
| docs/evidence/stage-01/S1-02/ctest.log | 1447 | e7eebb41ffd7790f693b497ebb00187ab6e28c05d77c61db390c1e84f472c402 |
| docs/evidence/stage-01/S1-02/design-checks.log | 1860 | 91c8fe5b446d83574445153ec5e89b07f15405154514eae9cc2125c93c00692d |
| docs/evidence/stage-01/S1-02/environment.log | 211 | 1dc9aff2c9359cb88264445050b1fddcc6879d69791400e1dcd8c4e9188a8d8d |
| docs/evidence/stage-01/S1-02/handoff.md | 63443 | ac746ed29e13b91c6f63db0f55d4e275489200e3914b18269fe0be5b63e92fcb |
| docs/evidence/stage-01/S1-02/manifest-check.log | 1320 | e35fb8f00f5b7a8a9623462baf5574bb13888ff6ba9c9450791ee9bfc04d1a4b |
| docs/evidence/stage-01/S1-02/manifest.sha256 | 3231 | e815882113b32229e796aaa8a4ba6a5a1933904b11059c07871c21bbd3fda9be |
| docs/evidence/stage-01/S1-02/mutation-batch.log | 46572 | 6e94330213c8e7c190936ba387ff1bf84412cdc23a9d1b07d5b54fb7a47e9b1e |
| docs/evidence/stage-01/S1-02/mutation-batch.py | 12622 | 0d8ea41179c2cd2cae74692137e1a3dafa5bc49a18e6226424e9bc6ecdb7ffec |
| docs/evidence/stage-01/S1-02/mutation-joint-m26c.log | 2178 | 4c41c0e08b3b3b8d9b138fe2f553ed1405c0fb3d59da23069ea8f314d02eed9c |
| docs/evidence/stage-01/S1-02/mutation-joint-m26c.py | 2290 | 1dbda07c1520c462de9444b8fb4444ceb862d2c1414f204086c0aefecca29d4f |
| docs/evidence/stage-01/S1-02/mutation.log | 26594 | c3b64261508a8ee75fedb64f7161d17cda33938661bebd6d5f2c2025b21e9203 |
| docs/evidence/stage-01/S1-02/run-id.txt | 20 | 00f9924b4cecc4dafa212944078bd55179ba5c883957437539a9298bd8665118 |
| docs/evidence/stage-01/S1-02/source-manifest.txt | 3563 | 8be11115a6223191666125c580728389d9420dfc6c89377c25efd9b88e213f60 |
| docs/evidence/stage-01/S1-02/source-verify.log | 1517 | d9301de7ec12de2cbac0029bd6261cb1d85fc77f2fc804faf0b464a6840b7345 |
| docs/evidence/stage-01/S1-02/summary.md | 96088 | d72ecd13fb8c22f4f301715542cbdba44f0cb246c6cfcce7ab6a2644bffb20c1 |
| docs/evidence/stage-01/S1-02/tsan-build.log | 3567 | 5d63f41620c2a6442c6b237ab84a5ba308a76dbc8fa45b726eb2f637288513c2 |
| docs/evidence/stage-01/S1-02/tsan-configure.log | 616 | 0500b1103bbedea23c6171d348f726e8cf17bec97b803180c5a8687b92d59be4 |
| docs/evidence/stage-01/S1-02/tsan-ctest.log | 1469 | 73609a704d589139068500db9e7f8a7d8abd28a23fdca5519b9a5fac58eb89a3 |
| docs/evidence/stage-01/S1-02/unit-tests.log | 413 | 68d5998b703f09f65c4c2091e81c5f36c2c62fd15cc6e20cbdc790cdf99a7505 |
| docs/evidence/stage-01/S1-02/vm-s102-asan-build.log | 3440 | 2ebbc977b616b4a8eebb73b836040be18178c67b26f66110b34fcafe18ae3727 |
| docs/evidence/stage-01/S1-02/vm-s102-asan-configure.log | 641 | d7b6131fef847baacb935407c39b5744f5ceea703fe6b30600b4df17d4b3b392 |
| docs/evidence/stage-01/S1-02/vm-s102-asan-ctest.log | 1497 | 5246daa06bf0cfd2a8b3736dfad47c79707bf4970a651a23c913bd763f493bb7 |
| docs/evidence/stage-01/S1-02/vm-s102-build.log | 3457 | 6a1bcecc73e90b3e1547add6b8599d8745c6439391a15790996caed6e961db61 |
| docs/evidence/stage-01/S1-02/vm-s102-configure.log | 657 | 6dc44321c4d979d8f08f438a330118752db34543a6b3d9f48819e68baad7057d |
| docs/evidence/stage-01/S1-02/vm-s102-ctest.log | 1472 | cb146fc1aec559325f5fc754c0f8859c456ffbf713a01a55a43c60f02c0d9ab6 |
| docs/evidence/stage-01/S1-02/vm-s102-design-checks.log | 1860 | 50966ebf1f8691609f7ad75080779b448ed96469b3a684fee1ba5d6320956879 |
| docs/evidence/stage-01/S1-02/vm-s102-environment.log | 204 | 0be4f7b93ee0b9bc3e2dca535f9386f89ec0678c2e97d27a1d5fa21b55432807 |
| docs/evidence/stage-01/S1-02/vm-s102-manifest.sha256 | 3231 | e815882113b32229e796aaa8a4ba6a5a1933904b11059c07871c21bbd3fda9be |
| docs/evidence/stage-01/S1-02/vm-s102-run-id.txt | 20 | 00f9924b4cecc4dafa212944078bd55179ba5c883957437539a9298bd8665118 |
| docs/evidence/stage-01/S1-02/vm-s102-run-summary.log | 579 | ba0c7796ec02a0971f631c899b5f1c5530578400d330c571bc2a9660203e2fd6 |
| docs/evidence/stage-01/S1-02/vm-s102-source-verify.log | 1336 | ec66896391df9f2740e8b0ab11216fddb04c2344e43f5736d1d7a7d2c3246be9 |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-build.log | 3440 | 9538eb39f1be72d063bb64b759b3326c06ee9eb5b78ff4329f843c5cc87fcce3 |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-configure.log | 641 | 8378c87c8c063f040d0e9be46bbc60b02be43913b326b03f523bb8066cc7247e |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-ctest.log | 1494 | d79be267f2b3a4432674633b15998ca532e17a642d83f58b46209d9d4d149014 |
| docs/evidence/stage-01/S1-02/vm-s102-unit-tests.log | 413 | 68d5998b703f09f65c4c2091e81c5f36c2c62fd15cc6e20cbdc790cdf99a7505 |
| docs/stages/README.md | 4985 | 24ff25b010d838e05b389b56f825068d499578fa20578d5b286eb67be9c86d06 |
| docs/stages/stage-00-architecture.md | 22164 | 2e85508b23aff242f1ce6cbd166464e5a85966f8ead4f1dd8398c5231443227b |
| docs/stages/stage-01-task-cards.md | 16460 | 7f7561e83f9f614fd4ed226cefe3ce78ba367859a55ee92cd82212b25273fc93 |
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
| host/storage/src/repository.c | 95832 | b9ab2abaa41e0d9795ebdee3b654ab9265efb9e16d7af03154542a7e34c556c5 |
| host/storage/src/repository_internal.h | 3344 | f68136968db1af077771b567b93dd974865294bb29c313f1928eec1748c6b64a |
| host/storage/src/schema.c | 26069 | 19bd5b4f9e2497258adc5254c81eaba6deb583194468fb2e0c32f886a93f1723 |
| host/storage/src/schema_embedded.c | 809 | fc0c2a336230556b2016a8f2b4e3987b18f71db717cb3f1e96d12d1c88652f46 |
| host/storage/src/schema_internal.h | 1022 | a5d1059e54b112fb232a1da7917d02b4739f5b2346ac18493dce2ac0e8e9f75a |
| host/storage/src/schema_records.json | 27525 | 781d7b139af23b5783040281e3dacc164a0d51076a11376e6bef1411941888ef |
| host/storage/src/u64.c | 2180 | 875036a0abbf6d49b15ec33128adbe24cae34df74133985f4abbbf3e04b26696 |
| host/storage/third_party/cjson/LICENSE | 1084 | a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c |
| host/storage/third_party/cjson/cJSON.c | 80399 | 298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d |
| host/storage/third_party/cjson/cJSON.h | 16394 | 25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee |
| host/storage/third_party/sqlite/sqlite3.c | 9582521 | 0c657666d819f261cc4007526ad57e1669de9949df7a8ea847aa284a2bf0ee52 |
| host/storage/third_party/sqlite/sqlite3.h | 695729 | d6c84557261893439b341d2e6c35187ae3ea24fbfd843ad94616f72d89242b8c |
| host/storage/third_party/sqlite/sqlite3ext.h | 39547 | 48963abfefb41ac329ef9d706acb5eadba952a99a748285c9ded6b5e0942721b |
| host/storage/tools/extract_schema.py | 5355 | ab5990b55b661b8c7596002783a0f845266f0b5b7d91943d95578f1d117cb218 |
| tests/unit/common/test_support.h | 5193 | 557bf284fa7336569e61ac29ba5c7e5de9c9d8bdb25bd8010daf6812950145b0 |
| tests/unit/storage/CMakeLists.txt | 2354 | bc3ccbbc732b04621e2f7a39f7434fc666a12876b0c24e31f86f1bd0e637963b |
| tests/unit/storage/test_event_key.c | 7913 | 159254c9091deb3b8d3c1e888ae8819caf2dcbd50882096cac9ef757feb2ca45 |
| tests/unit/storage/test_regex.c | 5128 | 8bb903aa8b760f76f6614962999c0689a5d43af43cda07d8a634f887ab6a15b2 |
| tests/unit/storage/test_repository.c | 137410 | 6ab9a68d1c993d45c3fcc0ade8cd64b1c175385a316455663df163823524e2d0 |
| tests/unit/storage/test_schema.c | 31269 | 4952adc2876f8a1c37d83ad42968ede1cd40d66a6a582862cd0d75941a20e2ca |
| tests/unit/storage/test_u64.c | 6040 | 6919ee37502373a4f0c19ef7ca6134820de333e150df37d353c02bb6ac9652e7 |

## 附录C：收尾外部变更与最终校验

在报告写入后的复核中，summary.md随后handoff.md出现外部修改；本轮写工具仅指向audit-round8.md。2026-10-24T12:51:29.802Z 复查，106项初始输入中104项仍与附录B相同，下面2项为外部数字更正后的最终指纹：

| 文件 | bytes | 最终SHA-256 |
|---|---:|---|
| docs/evidence/stage-01/S1-02/handoff.md | 63443 | e5811033acaadc2f9ae6878ee8513baefc6b33312681d50e83a4b657665ef2e5 |
| docs/evidence/stage-01/S1-02/summary.md | 96088 | bbf9eeec286e70c2b0128764ab0c5363687b1514498ec81ac836dead4ba18caf |

对这两个最终文本在内存将1715反向替回1707、1210替回1202，分别重新得到附录B原SHA-256，证明差异仅为这两类数字替换（summary只需合计单元格的1715→1707即匹配）。未见实现、正式测试、契约、状态、其余证据字节变化。N-2按最终当前正文记已修正；N-1/N-3/N-4仍保留。附录B保留原始受审输入身份，不能假称全106项始终未变，也不把外部勘误认作本Agent越权写入。

报告写入后三项必需检查check_contracts/check_stage_plan/check_vision_contracts均exit0/PASS；仅证明各自设计一致性。报告UTF-8严格解码及字节往返通过、无替换字符，40项核查矩阵齐备。证据目录新增项仅audit-round8.md，未删文件，现56项。C构建/CTest/sanitizer/mutation独立复跑和硬件仍为NOT_RUN。未置DONE、未commit/push。
