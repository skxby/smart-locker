# S1-02 round-4 独立审计

日期：2026-10-24（Asia/Shanghai）。主副本 %REPO%；契约 0.2.1 / REVIEW_DRAFT。

## 0. 结论

**FAIL，不满足 S1-02 技术交接或 DONE 条件。最终审计快照有 2 项阻塞问题、3 项非阻塞问题。**

- **B-1 / P2**：新加列默认 BINARY 核验后，仍可用“列默认 BINARY + 主键显式 NOCASE + 额外 BINARY 唯一索引”绕过 schema 检查。原始 UPSERT 可 prepare，但按字节不同的第二个 key 会被另一约束拒绝。
- **B-2 / P2**：审计期间源码由外部改动，当前 repository.c / test_repository.c 已不匹配 source-manifest、VM 清单和 mutation 指纹。历史 664/1498 与 12/12 PASS 只能绑定旧快照，不能验收现有代码。
- **N-1 / P2**：历史 mutation 的目标归因和逐断言摘录仍有问题，m15b 不能据现有失败行证明恢复首次 open 假成功；m17/m18 的四行失败各来自一个夹具；m11 汇总13但仅保留12条明细。
- **N-2 / P3**：交接仍把 MCU 的 last_committed 写成 RK 已提交尾，并有残留清理状态矛盾和旧整改归因错误。
- **N-3 / P3**：私有 regex 的 {0,n} 上界多生成一个可选 atom；当前仓储 U64 另有 common 数值解析保护，未据此认定 u64 越界防护失效。

本轮独立 Linux configure/build、CTest、C 最小复现、ASan+UBSan、TSan、20 个 C mutation 全部 **NOT_RUN**。Windows Python/SQLite/Node 检查及模型确已运行，不能替代 C 仓储或硬件验证。

**本轮仅新增 audit-round4.md。** 未改实现、正式测试、契约、脚本、stage-plan、旧审计或 summary/handoff；未置 DONE、开始 S1-03、commit 或 push。审计期间观察到的其他改动不是本审计写入，详见 §1/§7。S1-01 前置仍为 DONE；S1-02 保持 IN_PROGRESS；阶段1 gate 为 NOT_RUN，门禁卡 S1-07。

## 1. 审计对象、范围与版本变化

### 1.1 输入和依据

核对 README、阶段入口/机器索引、阶段1卡 S1-02 :48–56、阶段0持久化/恢复规则、module-ownership :24、traceability 六端口、internal-interfaces、UART :38–39/:368–372、生成头六签名及原生 DTO；内部 schema 有15条 x-rules、18集合、107 defs，仓储冻结闭包47 defs/18集合。

审查 repository.c 的事务/查询/分页/导入/初始化路径、schema.c、event_key.c、u64.c、regex.c、DDL、局部构建/抽取工具、五个 storage 测试和历轮问题的相关正式回归；核对三轮审计、summary/handoff、源清单及全部37个已有证据文件（26个.log）。S1-01最新独立审计为PASS，不重开该卡。此次只读审计不启动新项目或较大实现方案；为排序语义核对SQLite官方文档，没有重新选型、下载或修改依赖。

证据分为本轮Windows实际执行、源码/测试静态推导、开发方保存的Linux历史记录，三者不混用。

### 1.2 明确区分的审计快照

工作区在审计期间持续有外部写入，因此不能把本轮所有读取当作一个不变快照。本报告按内容指纹区分：

| 快照 | 读取时刻（UTC） | repository.c bytes / SHA-256 | test_repository.c bytes / SHA-256 |
|---|---|---|---|
| A：主要审查及历史证据对应版 | 2026-10-24T05:16:43.248Z | 82519 / a8aec77b9d9d0918099ab4e529c741a5a91019141c311ba98aead91a1c55cf99 | 108047 / d7e2567062bbf023a20540e2693595f457741a438088f50784b0d544b217d3e5 |
| B：中途复核版 | 2026-10-24T05:23:29.195Z | 83378 / 921834cb0378243290f3dc5054d4195cb513aee581f98ffb95fddfa9469d801b | 110050 / fef3da685d545d5e55cb8cd0dd023b492841cad6a03096a9c6f3c4038715abed |
| C：最终复核版 | 2026-10-24T05:28:33.208Z | 83378 / 921834cb0378243290f3dc5054d4195cb513aee581f98ffb95fddfa9469d801b | 111883 / a5e4b53163f93cb40ff2c682c9b95b599366c3ee70783d3a347aa3c36a805657 |

快照A的32项源清单全部匹配。随后B新增 sqlite3_table_column_metadata 列排序检查、NUL分库助手、第五个schema夹具及meta-only夹具；已定向读取复核这些变化。除这两个源文件外，32项范围内其他文件与A相同。C继续补了write_text_file的前向声明，并将schema循环拆成带具名调用点的setup/probe。B中途存在的声明顺序问题已静态修正，**不列为最终未修阻塞**；C新增probe在意外open成功时未关闭非NULL repo，建议变异夹具清理这条意外成功路径以避免泄漏干扰诊断。C的源码清单匹配数为 **30/32**；VM清单仍绑定A，mutation八个短指纹中 **6/8** 匹配C。

summary/handoff/mutation也发生外部变化；没有改动者归属证据，不推测身份。当前结论绑定C与 §7 的对应文档/日志指纹，不适用于此后再改动但未经复核的版本。下文涉及历史日志失败行的行号明确属于A；当前repository缺陷行号属于B/C同一内容；test_repository行号已注明版本。

## 2. 实际命令、环境与 NOT_RUN

普通 exec_command 在创建进程前失败：Failed to create unified exec process / helper_unknown_error: setup refresh had errors，没有shell退出码。随后使用可用Node fs/crypto与child_process.execFile参数数组，没有提权、安装工具或改变WSL/SSH设置。

Python实测 **3.14.6 / Anaconda / AMD64，E:/anaconda/python.exe**，Python SQLite **3.53.2**；项目vendored SQLite为 **3.54.1**。SQL探针使用前者，没有链接项目库或调用locker_repo_*。工作目录均为%REPO%，Python带-B，探针由-c内存执行，没有临时脚本或磁盘DB。

| 命令/检查 | 实际退出码 | 实际结果 |
|---|---:|---|
| git status --short | 128 | fatal: not a git repository；根无.git，未创建仓库 |
| python -B -X utf8 scripts/check_contracts.py | 初次两次1；文档外部修正后0 | A时summary.md:381的代码文字被识别为坏本地链接；B时该段改写，重跑PASS。最终输出C声明仅parse PASS，非编译/链接；hardware_profile=UNVERIFIED、business_or_hardware_tested=false |
| python -B -X utf8 scripts/check_stage_plan.py | 0 | PASS；45卡、active_task=S1-02、implementation_cards_done=1、runtime_or_hardware_tested=false |
| python -B -X utf8 scripts/check_vision_contracts.py | 0 | PASS；camera_owned=false、rknn_available=false、hardware_tested=false |
| python -B -X utf8 scripts/check_feasibility.py | 0 | PASS；runtime_or_hardware_tested=false |
| python -B -X utf8 host/storage/tools/extract_schema.py --check | 0 | OK；27525B，与当前真源一致 |
| python -B -X utf8 -c（抽取器内存漂移负对照） | 1（预期） | 只在内存给冻结文件多一个空格，真实main输出DRIFT；磁盘未变 |
| python -B -X utf8 -c（附录SQL/JSON/链接探针） | 各0 | 探针完成；不是被审功能PASS。坏链接枚举在A发现1项，最终正文修正后checker已通过 |
| wsl.exe -e /bin/true | 4294967295 | UTF-16LE原输出：拒绝访问；Wsl/Service/CreateInstance/E_ACCESSDENIED |
| ssh -o BatchMode=yes -o ConnectTimeout=8 locker-vm 'uname -a' | 255 | stdout/stderr空；未获得guest执行环境，不推测电源/网络原因 |
| where.exe cmake / gcc / clang / ctest（分别执行） | 各1 | PATH均未找到 |
| Node原始字节hash/大小/清单检查 | 不适用 | A32/32匹配；B30/32；6/6 vendor与lock匹配；受审范围无漏项 |

**NOT_RUN，退出码不适用（未启动）**：Linux configure/build、12个CTest调度、11个C二进制、C示例编译、C open/query/DELETE/import反例、ASan+UBSan、TSan、20个C变异、I/O/磁盘满/DDL中途/COMMIT故障注入。没有同步或覆盖VM源码，也未访问当前guest的WORKSPACE-INDEX、构建cache、编译/链接参数和二进制。

## 3. 阻塞问题

### B-1 / P2：找到好索引不能抵消不兼容主键

**当前位置（B）**：repository.c:1862–1871新增列默认排序检查；verify_key_index 的原逻辑在B的 :1940起、:1970–1972接受任一匹配索引；index_covers_key只检查所选索引。对应A行号为 :1929/:1959–1961。原始UPSERT在:418–419。最终C的SQL_MISSING_COLUMN/SQL_NO_PRIMARY_KEY/SQL_NOCASE_KEY/SQL_EXTRA_COLUMN/SQL_COLUMN_COLLATION及其具名probe没有覆盖下面的反向组合。

**依据**：卡:53/56、internal-data x-rules 03/09/13、schema-v1.sql的字节键及默认BINARY，schema不兼容必须明确拒绝。列默认排序、主键排序及其他唯一索引并非同一属性。见 [SQLite排序规则](https://www.sqlite.org/datatype3.html#collating_sequences)、[index_xinfo](https://www.sqlite.org/pragma.html#pragma_index_xinfo)、[列默认排序元数据](https://www.sqlite.org/c3ref/table_column_metadata.html)。

**最小复现（本轮Python SQLite内存库实际运行；C open仍NOT_RUN）**：执行当前schema-v1.sql，保留合法meta两行和migration version=1，替换空documents：

~~~sql
DROP TABLE documents;
CREATE TABLE documents (
  collection TEXT NOT NULL,
  key TEXT NOT NULL,
  value_json TEXT NOT NULL,
  PRIMARY KEY(collection, key COLLATE NOCASE)
);
CREATE UNIQUE INDEX binary_key
  ON documents(collection COLLATE BINARY, key COLLATE BINARY);
~~~

**预期**：open两种initialize_empty都CONFLICT、out=NULL，保留目标。**源码推导实际**：三列名字/类型/NOT NULL/pk位置完全匹配；列没有声明COLLATE，默认均为BINARY，B新加元数据检查也通过；index_list中binary_key为unique=1/partial=0，xinfo恰为collection/key ASC BINARY，verify_key_index遇到它即OK，忽略原NOCASE主键。因此两种open都可继续通过元数据与quick_check并静态推导返回可用repo，C返回值没有实测。

SQL实际输出：

~~~text
columns [('collection','TEXT',1,1),('key','TEXT',1,2),('value_json','TEXT',1,0)]
binary_key: collection ASC BINARY, key ASC BINARY
sqlite_autoindex_documents_1: collection ASC BINARY, key ASC NOCASE
UPSERT_PREPARE_OK
COLUMN_BINARY_READ_A 0
UPSERT_A_ERROR UNIQUE constraint failed: documents.collection, documents.key
metadata [('schema_version','1'),('snapshot_revision','0')], migration [(1,)], quick [('ok',)]
~~~

探针先用仓储原始UPSERT插入key='a'，查询key='A'得0，证明查询的列比较区分大小写；再用同一UPSERT插入'A'却因NOCASE主键被拒。value是占位JSON，只用于说明SQL约束，不声称经C业务写入成功或真实数据丢失。库在内存中。既有库尚无数据时同样会被兼容性检查误接纳。

**修复与回归建议**：校验实际主键/冲突目标依赖，不能用任一额外BINARY唯一索引替代必要主键的语义；同时识别会改变可接受键集合的额外约束。对上述组合、列NOCASE/主键BINARY、真DDL正对照，两种initialize_empty核对拒绝/保留。不要只给一个SELECT加COLLATE而让不兼容约束继续存在。

**本轮较早发现但B已静态封住的相邻路径**：A中列默认NOCASE、主键显式BINARY也能通过；实际SQL可匹配两行、after_key漏行、DELETE changes()=2。B新加sqlite3_table_column_metadata检查已拒绝这类列默认NOCASE，正式新增struct_colcoll.db。该具体旧路径不再作为当前未修缺陷，C复验仍NOT_RUN；上面的反向组合仍未被封住。

### B-2 / P2：当前源码与保存的验收快照不一致

**位置**：source-manifest.txt中repository/test_repository条目、vm-s102-manifest.sha256.txt对应条目、mutation.log:18–26、summary.md:6/:21/:146及§5的最终快照声称。

A对应清单及VM32条完整SHA全部匹配，八mutation短前缀匹配；C两份源文件分别变为§1的921834…与a5e4b5…，清单仍为a8aec…/d7e256…。因此C匹配 **30/32**，mutation匹配 **6/8**。其余30个源文件及6个vendor文件不因这次差异被否定。

**预期**：对要交接的同一源码快照提供构建/CTest/错误路径与指纹绑定。**实际**：本轮新增列校验和分库回归等变动没有匹配的新Linux日志；旧日志repository664、总1498不能转移到C，B中途声明问题虽在C补上，C仍未实际编译。不能靠只更新manifest文本或改断言计数补证。

**建议**：待源码停止变动并修复B-1并核对当前代码后，清理本批临时残留，生成新完整清单；按VM工作流在新的build/S1-02/<批次>构建/CTest，sanitizer独立目录并保存实际编译/链接参数；mutation保留可复验补丁与完整原始输出。只读审计无法执行的仍标NOT_RUN，不覆盖旧日志凑“当前PASS”。

## 4. 非阻塞问题

### N-1 / P2：历史mutation覆盖归因与完整性

以下行号/计数属于A日志，不用于证明B已验证：

1. **m15/m15b**（mutation.log:248–278）：m15合法table-only单点未捕获、configure/build=0/CTest=0，已如实登记，不再把非法SQL prepare失败算目标kill。m15b三点联合关守卫，记录12/664，但提交前verify_schema_tables仍存在；documents视图的类型为空/pk=0会被结构检查拒绝回滚。首次open(true)断言A:2099没有FAIL，视图唯一FAIL是A:2102的open(false)，CONFLICT变NOT_READY。SQL归因模型实际得到相同结构拒绝与回滚。因此能说明返回分类防线有回归，不能据该日志证明恢复“首次open假成功”。生产视图拒绝静态成立，不由此反推视图漏洞。
2. **m17/m18**（:297–319）：A的缺列/无PK即使跳过列检查也被索引检查拒；即使跳过索引检查，无PK也被pk位置检查拒。仅跳列会接受extra，且每个错误成功夹具同时产生两次status和两次repo!=NULL，共4行；仅跳索引会接受普通NOCASE，亦4行。日志“两夹具各2行”的归因不符。模型实际运行。B又新增列排序核验，普通NOCASE现在还受列检查保护，更不能沿用A的m18覆盖结论。
3. **m11**（:194–213）：保存12条FAIL tests/明细、汇总13/664，summary用“+1”补齐。可能是摘录缺行，不据此推断伪造；但不能声称完整逐断言原始输出。
4. A的20个具名块确有17个CTEST_EXIT=8/3个=0（m6/m9/m15）；逐MUT行configure/build均0。m12注释的旧BUILD_EXIT=2不算最终MUT行。这只证明记录数字存在，非本轮独立重跑。

建议逐夹具标失败、保留准确patch与原始日志；把首次初始化假成功和非初始化返回分类分成目标。缺行补原始证据，找不到明确“只保留12行”；不手改旧日志凑13。本轮C mutation均NOT_RUN。

### N-2 / P3：交接正文与清理说明仍误导

- handoff.md:162、test_repository.c:1301仍写last_committed是“RK已经提交的日志尾”。FETCH_EVENTS响应中的该字段属于**MCU持久日志提交边界**；RK本次落库水位是committed_cursor/through。包络大小关系与test_import_boundary_envelope的1<3<4<9是正确修复，字段归属仍须直接改成当前事实，不能把远端已提交尾等同本端落库水位。
- handoff.md:528–539称已清理并复核0；summary.md:382明确round-2/3新一批尚待清理，且handoff称WSL构建在/var/tmp，当前日志却指向/home/yuanangyu/s102_final2；模块README默认HOME/locker-build也未统一到当前locker-workspace分类规则。本轮根目录未发现.tmp*/s102_名称，未访问VM HOME或全量核验Public/TEMP，外部残留未核销，未删除他人快照或日志。
- summary.md:291仍把na+NUL+me说成截断后合法name；:226把bare.db行称视图、sqlitex行称bare.db；:244称m4锚点换过，但旧/现都是u64.c:51，实际是新增可达输入。handoff:566的前缀回归指向struct/view，实际前缀函数为test_sqlite_prefixed_user_object_rejected。应修归因，不继续在整改表重复旧错误。

README active_task已与索引一致；A时WSL/VM普通CTest0.25/0.26s、664/1498/37均与保存材料一致，不再当数字错误。C源码变动后的新计数未运行，不能继续称“现有源码就是664/1498”。本报告新增前37个证据文件，新增后38，不把报告自身造成的计数增长当旧交付问题。

### N-3 / P3：regex零下界有界重复多接受一项（静态新发现）

位置regex.c:530–535/:559–568，regex.h声明支持{n,m}，test_regex.c无零下界有界重复测试。对^a{0,1}$与aa，预期false；min=0分支先生成optional(atom)，随后循环从index=0到max-1又生成一个optional(atom)，静态推导可接受两个a。{0,19}同理多一位；C执行NOT_RUN，没有伪造匹配输出。

当前冻结schema仅U64用{0,19}，所有仓储U64引用另受schema.c:492–493/common数值解析保护，21位/溢出被拒；未据此认定当前仓储u64防护失效。影响是私有工具声明支持的语义及未来复用，非此次FAIL的独立生产路径理由。建议修可选项计数，补{0,0}/{0,1}/{0,n}/{1,n}的上界与上界+1正式回归，不扩正则功能。

## 5. round-3五项逐条复核

“静态闭环”仅指指定反例和当前源码/测试设计，不等于本轮C运行PASS。

| round-3项 | 最终裁定 | 代码/测试/文档证据与缺口 |
|---|---|---|
| B-1 LIKE前缀 | 原反例静态闭环 | repository:291 NOT GLOB；SQL table/view旧0→新1、index/trigger旧0→新2。正式prefix夹具table true/false、view true拒并数对象。view false及内容/索引触发器正式C全覆盖仍欠补强，不概括“所有对象全实测” |
| B-2 schema兼容 | 部分闭环，仍阻塞 | 列/类型/NOT NULL/pk位置、unique/非partial/ASC/BINARY存在；A四坏夹具模型拒、真DDL接受；B补列默认BINARY及struct_colcoll。仍有本轮B-1反向组合；最终C测试声明顺序已补，运行证据仍缺B-2 |
| N-1 mutation | 部分闭环 | 原非法m15弃用、合法m15未捕获如实保留，A八前缀匹配。m15b/m17/m18归因与m11缺行仍见N-1，C指纹另见B-2 |
| N-2 NUL属性 | 夹具及隔离静态修正；运行证据未闭环 | A已是name+NUL+junk/raw属性且正对照存在；模型na→False/name→True。B新增nul_case_expect_invalid/json/raw，分别建nul_<name>.db、初始化revision1、逐例拒绝后检查revision/文档数/key；不再把旧串行缺陷说成当前仍存在。C的C/独立mutation未运行，旧连带失败不能替代 |
| N-3文档/数字 | 部分闭环 | 包络字段、strict包络成功对照、active_task、A的CTest/664/1498/37已对齐；A坏链接在审计中外部修正后checker实跑0。仍有MCU/RK归属、清理/归因N-2及C快照证据B-2 |

## 6. 首轮及round-2全部问题回溯

下列repository行号在B的前1735行未变化；历史测试行号以A日志为准，最新新增测试见§3/§5。

| 历史项 | 当前落点与裁定 |
|---|---|
| B1 连续/锁存/倒退 | :1397同boot严格seq+1防溢出、:1526页内相邻、:1682/:1687阻断缺口/倒退、:1697锁存gap；boundary/latching/backfill正式回归存在。指定反例静态封住，C未跑；不从boot跳号推断无丢失 |
| B2 零尾/异日志边界 | :1537零尾拒、:1541结构、:1548/:1551同日志上界、:1554/:1560/:1563首边界、:1567末条；原反例静态封住，正常助手真实边界，strict包络对照存在 |
| B3 初始化 | 有痕迹拒、显式BEGIN/DDL/提交前结构核验/COMMIT/回滚存在；原meta-only/视图反例封住。整体兼容性仍有本轮B-1，DDL中途故障注入未补，最新meta-only重复夹具不等于注入 |
| B4 分页预算 | :954信封/1160B余量、:1080完整文档序列化字节、:1083缩页，query/recovery共用；正式pagination_budget及本轮四组字节模型认可，C分页未跑，m9漏检保留 |
| B5 escaped/raw NUL | :483和schema:35–62解析前扫描，奇偶反斜杠区分正确；common规范U64解析存在；生产防护静态成立，B分库测试改进未运行 |
| N1 NULL/ACK示例 | handoff:169传NULL、:195区分OK与ACK充分条件；原错误已修。例子仍靠占位注释填事件，非完整独立程序，本轮未编译；字段归属见N-2 |
| N2 崩溃/重开 | summary/handoff限定正常close/reopen，正式持久化测试存读借用/操作/outbox/幂等；没有把它升级SIGKILL/介质断电；本轮重开NOT_RUN |
| N3 半写SQL失败 | 正式trigger第二次outbox写RAISE(ABORT)，先有效guard；核查空集/revision，移除trigger同输入成功。设计有效，C未跑；FULL同连接断言/m6漏检未补 |
| N4 依赖/模块说明 | 模块README存在，deployment两文件记录vendor路径/消费者/许可/逐文件SHA，6/6匹配；未重新获取归档，不独立认定上游原包逐字节同源 |
| N5 u64/抽取 | u64/schema复用common numeric，抽取器保留；本轮--check正对照0/内存漂移1。缺Python时CTest只SKIPPED，不称所有环境固定12项 |
| R2-B1 全重复gap | :1634分支gap=false回滚；gap=true+stored=true写false并提交，不推进through/不应用projection；duplicate_page_latches_gap存在。指定反例封住；无stored仅gap=true明确CONFLICT，不扩大到全部重复页 |
| R2-B2 半零/零首边界 | :1384两分量同时零/非零，:1510每事件/:1541边界/:1554非空首非零；正式boundary负例存在。指定反例封住；每条事件半零专门负例仍欠补强 |
| R2-B3 同名VIEW | 任意对象查找/GLOB计数/同名非表拒/提交前结构核验；正式view两种open拒，原反例静态闭环；mutation覆盖范围不能扩大见N-1 |
| R2-N1 回归不全 | 健康true起点boot1→5已补；B的NUL分库已补，未取得新运行证据；DDL中途注入仍无，部分闭环 |
| R2-N2 boot/ACK文档 | 实际boot严格增加且seq1，OK≠ACK已说明；跳过boot无事件须可信设备日志/gap证明，不能仅由两个号推断；字段归属N-2 |
| R2-N3 最终快照mutation | A八短前缀匹配、m4新增非规范输入使原:51可达、m9b分母649有记录。C两源改变，旧快照不再是当前证据；N-1归因问题另列 |
| R2-N4 余量/命名/缺口 | 192*6+8=1160B，m8 trace_guard确为有痕迹判定；m6/m9未捕获如实登记。登记不等于验证通过，FULL/DDL注入仍待做 |

## 7. 原始字节指纹和历史证据核验

### 7.1 变化与边界

A32项全部匹配source-manifest，范围为根CMake+host/storage+tests/unit/storage，含26自有文件/6vendor，无漏项。B只有两项不匹配，见下表当前完整SHA。VM保存清单32条完整SHA全匹配A；source-verify32条OK、run-summary标run=20261024-125905。未访问VM原快照/二进制，不能独立重建打包/scp过程。

两份unit-tests.log各11行PASS：common 97+5+53+164+133+53=505；storage80+28+664+131+90=993；总1498。A的test_repository main调用24个函数。当前C也有24个顶层调用，但分库及新夹具会改变运行断言数，**新总数NOT_RUN**。

| 历史记录 | 保存内容（非本轮运行） |
|---|---|
| WSL普通/ASan/TSan | 均12/12，时间0.25/0.49/0.86s |
| VM普通/ASan/TSan | 均12/12，时间0.26/0.56/0.94s，run-summary同值 |
| CTest组成 | common6个C+storage5个C+Python漂移门禁1个；Python缺失不注册第12项 |
| mutation | 20块/17个CTest8/3个0；只确认记录，不等于独立复验；N-1问题保留 |

sanitizer configure/build日志没有展开实际-fsanitize编译/链接命令。目录后缀和0报告计数不是插桩形态证明；本轮未访问CMakeCache/flags.make/link.txt或二进制，只确认记录有PASS，没有认定历史一定未插桩。

S1-01清单29项除根CMake和deployment README/lock外26项仍匹配；根CMake内存去掉两行storage add_subdirectory后3871B / 25d9ca8c06e859a8b8ab85146b15bde3c740aaef215abd71d24d23a4b2db55f7，与S1-01相同。可证两行构建接入和两份依赖材料变化，负责人历史授权不能由Git重建。

契约源/生成头/内部schema/卡文/stage-plan与前三轮关键指纹一致；没有contracts/scripts/config/firmware全目录的完整历史基线，**不能宣称历史零改动全量证明**。本轮只写本审计；外部并发变化明确单列，不能笼统声称工作区其他文件未变。

### 7.2 C的32个源文件（30匹配、2变动）

| 路径 | bytes | SHA-256 |
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
| host/storage/src/regex.c | 24788 | 20883688a8bc8a1710a2d38eb3abb3e769eb3106b1c107739251386260072c01 |
| host/storage/src/repository.c | 83378 | 921834cb0378243290f3dc5054d4195cb513aee581f98ffb95fddfa9469d801b |
| host/storage/src/repository_internal.h | 3344 | f68136968db1af077771b567b93dd974865294bb29c313f1928eec1748c6b64a |
| host/storage/src/schema.c | 26069 | 19bd5b4f9e2497258adc5254c81eaba6deb583194468fb2e0c32f886a93f1723 |
| host/storage/src/schema_embedded.c | 809 | fc0c2a336230556b2016a8f2b4e3987b18f71db717cb3f1e96d12d1c88652f46 |
| host/storage/src/schema_internal.h | 1022 | a5d1059e54b112fb232a1da7917d02b4739f5b2346ac18493dce2ac0e8e9f75a |
| host/storage/src/schema_records.json | 27525 | 781d7b139af23b5783040281e3dacc164a0d51076a11376e6bef1411941888ef |
| host/storage/src/u64.c | 2180 | 875036a0abbf6d49b15ec33128adbe24cae34df74133985f4abbbf3e04b26696 |
| host/storage/tools/extract_schema.py | 5355 | ab5990b55b661b8c7596002783a0f845266f0b5b7d91943d95578f1d117cb218 |
| tests/unit/storage/CMakeLists.txt | 2354 | bc3ccbbc732b04621e2f7a39f7434fc666a12876b0c24e31f86f1bd0e637963b |
| tests/unit/storage/test_event_key.c | 7913 | 159254c9091deb3b8d3c1e888ae8819caf2dcbd50882096cac9ef757feb2ca45 |
| tests/unit/storage/test_regex.c | 3265 | f7722cfbd371d0194ef73819c0547bd1a01b005c990a2e049f516821cb188513 |
| tests/unit/storage/test_repository.c | 111883 | a5e4b53163f93cb40ff2c682c9b95b599366c3ee70783d3a347aa3c36a805657 |
| tests/unit/storage/test_schema.c | 31269 | 4952adc2876f8a1c37d83ad42968ede1cd40d66a6a582862cd0d75941a20e2ca |
| tests/unit/storage/test_u64.c | 6040 | 6919ee37502373a4f0c19ef7ca6134820de333e150df37d353c02bb6ac9652e7 |
| host/storage/third_party/cjson/LICENSE | 1084 | a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c |
| host/storage/third_party/cjson/cJSON.c | 80399 | 298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d |
| host/storage/third_party/cjson/cJSON.h | 16394 | 25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee |
| host/storage/third_party/sqlite/sqlite3.c | 9582521 | 0c657666d819f261cc4007526ad57e1669de9949df7a8ea847aa284a2bf0ee52 |
| host/storage/third_party/sqlite/sqlite3.h | 695729 | d6c84557261893439b341d2e6c35187ae3ea24fbfd843ad94616f72d89242b8c |
| host/storage/third_party/sqlite/sqlite3ext.h | 39547 | 48963abfefb41ac329ef9d706acb5eadba952a99a748285c9ded6b5e0942721b |

### 7.3 C对应文档、契约、状态与37份历史证据

| 路径 | bytes | SHA-256 |
|---|---:|---|
| AGENTS.md | 5839 | bb8658418e3926ad7799f3be148107731c61fc1aa3b668992dd80ee0e1d217de |
| README.md | 7859 | 8c7c835c42208da55a92b702d5ccc7f64bee5ae3744b4a023037f35babeebd54 |
| docs/stages/README.md | 4985 | 24ff25b010d838e05b389b56f825068d499578fa20578d5b286eb67be9c86d06 |
| docs/stages/stage-01-task-cards.md | 16460 | 7f7561e83f9f614fd4ed226cefe3ce78ba367859a55ee92cd82212b25273fc93 |
| docs/stages/stage-00-architecture.md | 22164 | 2e85508b23aff242f1ce6cbd166464e5a85966f8ead4f1dd8398c5231443227b |
| contracts/contract-source.json | 66553 | 14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f |
| contracts/internal-data-source.json | 92193 | ebcfa5859c114e8746a2f235161c17a6dede651f56125d92cc37f259c7d81da1 |
| contracts/internal-data.schema.json | 129569 | e7b8695c49115e2994cf126dc5ab629e5c82460e70d4c292b19576f55c60e386 |
| contracts/uart-layouts.json | 15245 | 28fb958dc8905a55912be16fefeb5f13d1f8c0104008cba1e6adffee26cac0bd |
| contracts/include/locker_contracts.h | 39925 | 34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a |
| docs/architecture/internal-interfaces.md | 29792 | a5b3cdc41c05006e83edc970c46c15f79644adb8fe071b71953f239a64c1c2cf |
| docs/architecture/uart-v1.md | 26022 | 5bb2204f291b259f7b28cc7a2a1473352d5f1d0d501032168924d664c5e3ed8f |
| docs/architecture/module-ownership.md | 10135 | b182fe6e424b605fd4cad1073c85178588ee474ca8ab6f38be84e3129f72bb3f |
| docs/architecture/traceability.md | 12694 | 0903b559a6fc33fa0120d608b5b85783f66327b03a0f8267e7b86309877f2859 |
| config/development/stage-plan.json | 24749 | 1ed89d9f5695c40f965f43d331907295eccca21eaf52d67bee92e5dc4fdf1b47 |
| deployment/host/README.md | 6753 | 9887fe18df7f964a66725cd0ae5ee3a3bc7d37fcc58fcdbde229da3d90c7bc7a |
| deployment/host/dependencies.lock.json | 14075 | 4884c4ca9944ccd66be72db71dcd99b289bba119763c965a7e3b7b7a961fac6d |
| docs/evidence/stage-01/S1-02/asan-build.log | 3328 | f6754139df7e86090d27bc959e2bc121d4cdba2e5771178d7eb590feef2a11f4 |
| docs/evidence/stage-01/S1-02/asan-configure.log | 590 | 90e2749885fd4edf996948ae90c07bc805991caa01d04bc2fafdb0b29fbd60e4 |
| docs/evidence/stage-01/S1-02/asan-ctest.log | 1435 | fa923e5e47d4096e9501d0e0e59f13369ef3ade1490cd551989c967f840a7b9a |
| docs/evidence/stage-01/S1-02/audit-round2.md | 32835 | 3e2475309e154b3071269f4487ecd4ba28993c84cead38c7ee56e098797922f7 |
| docs/evidence/stage-01/S1-02/audit-round3.md | 40534 | 2d0ea66edca835e695ff297d0297c0229d4671bd68863a7596490fdcbc75b46a |
| docs/evidence/stage-01/S1-02/audit.md | 23283 | 80ee21d98ed0877e2a3469a3ae9389f3ddc19663a4bf569bd39a25362ee3c8f8 |
| docs/evidence/stage-01/S1-02/build.log | 3422 | 69e2dca6361deab7eba43c479fe7730b734f0ca948cc0110cf104675dee3fa17 |
| docs/evidence/stage-01/S1-02/check_contracts.json | 535 | 0ac0a396fffb701522e05b2ff5f8c7725834a2641e59c109f41a62401870087d |
| docs/evidence/stage-01/S1-02/check_feasibility.json | 242 | cbe1da1d34c5d29634377f6a55c0199233a144e984417ba06553c9c5e149f02b |
| docs/evidence/stage-01/S1-02/check_stage_plan.json | 319 | 572e375df64f10218f6e440803dcff0162f846df19877d5b77692e91683250d0 |
| docs/evidence/stage-01/S1-02/check_vision_contracts.json | 572 | 3f8b58703a9d039f594749a6924e19eda4e312d97cbdbf0ee1dac3b6325d8778 |
| docs/evidence/stage-01/S1-02/configure.log | 585 | 38946bb9ff0e140e23058e0d9ddc9dcc79a420f82ed1bd5fee3e2a2ba14efd02 |
| docs/evidence/stage-01/S1-02/ctest.log | 1430 | f25b2edb6dbdbf32c72637c6a73a980ffa72c7c72b07f50c942ce14751c28eb9 |
| docs/evidence/stage-01/S1-02/design-checks.log | 1798 | 25048168005b512240b21b0eb0fb0bde7acc6a88c951362854f2cbdd4043f476 |
| docs/evidence/stage-01/S1-02/handoff.md | 42396 | fc55528377447662b3ac50729d8c3c37124399d92d4431009bcd454325269703 |
| docs/evidence/stage-01/S1-02/mutation.log | 20276 | 5c7b43138b7e37a4aae5e805f6542ea2f317712c1a93a94b9be58e5000a6ee92 |
| docs/evidence/stage-01/S1-02/source-manifest.txt | 3596 | 6a90209568b4d5086029ebd463022589a7e42aecf4744736906e3589f4aaf91c |
| docs/evidence/stage-01/S1-02/summary.md | 59559 | a9779c1cd6a28d45b63702183cd6cb547ca1dcb18c4ae14cf0c42ffe856b2ecb |
| docs/evidence/stage-01/S1-02/tsan-build.log | 3422 | 37b332e3467409fa75a0a5f1d8808e009d2bf8f64f15aef4e93555a520ac7176 |
| docs/evidence/stage-01/S1-02/tsan-configure.log | 590 | feae32b4fa8ca9bc3db2909b3c2179c8ad5ea643c0b22fc85c1ffc014d2f5c12 |
| docs/evidence/stage-01/S1-02/tsan-ctest.log | 1435 | d80ebe81b97898ac3f6e86d86a6a1fd87bb8676f737a6cf298dd0f7ecc0ecf16 |
| docs/evidence/stage-01/S1-02/unit-tests.log | 358 | 0c94aaa31eba68c4c61ea3169eeb3ca9d2a0cbd8956940ec6d9b28ac3eae352d |
| docs/evidence/stage-01/S1-02/vm-s102-asan-build.log | 3422 | aeac667a7d0a6ea7ecf51066059cd25e15a4f534b5009fdeff6da17c2acd2fbe |
| docs/evidence/stage-01/S1-02/vm-s102-asan-configure.log | 619 | c0b8b85d07190b5d416d63ca3d597873d52ebb24f862d9b32880e9eb07a3f7b2 |
| docs/evidence/stage-01/S1-02/vm-s102-asan-ctest.log | 1464 | e0167f9159159e63dbd7a41279acc7e65bdd2eabc2801c08b803e800d52d4404 |
| docs/evidence/stage-01/S1-02/vm-s102-build.log | 3422 | 3dc9bbafdc9383de614d9b428906283ded31535b95ce139e768339048aa30a01 |
| docs/evidence/stage-01/S1-02/vm-s102-configure.log | 614 | 7698f991798701cef0e7f2925f1515cb76d534406195331ea6b4283baca7467d |
| docs/evidence/stage-01/S1-02/vm-s102-ctest.log | 1459 | 35d70e0598f80e2bf48c80f87657aba45587f00c0f3c5b3c4661d173de9633f8 |
| docs/evidence/stage-01/S1-02/vm-s102-design-checks.log | 2016 | 769a30cab45dcbec4c836f12e3efc783aaaa42b1895bd326c04bec42127b047a |
| docs/evidence/stage-01/S1-02/vm-s102-environment.log | 279 | 4d1dce345704aedd1d72b0541b5659b7da0a9415ca4a996f29c6774b8139a258 |
| docs/evidence/stage-01/S1-02/vm-s102-manifest.sha256.txt | 3231 | 2f490a91760aa77a75299bd6ca98ede03930e6611643864b5ec42135860b0bf1 |
| docs/evidence/stage-01/S1-02/vm-s102-run-summary.log | 860 | f2a6ec522088a6d8ef8fbf0a10995e37bcb53a1c010055c23c3e49248c4a3ab3 |
| docs/evidence/stage-01/S1-02/vm-s102-source-verify.log | 1247 | eb2aadd332b7dab9d0f6ebe738354798b922c382b5b0915546a6df47956e4652 |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-build.log | 3422 | d814f274064c09be085cf4fdd63501bd63f3800d6f66cb4cbf985813ab7bfb8c |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-configure.log | 619 | 7bacc9e756e05a4595c8a281646d0dbb1887fd6f50658afbafa5e82784f73481 |
| docs/evidence/stage-01/S1-02/vm-s102-tsan-ctest.log | 1464 | 2607e43b7bd81d142a571a58df936c97d5ad13545ee6fb03970b6ae76ec7a48c |
| docs/evidence/stage-01/S1-02/vm-s102-unit-tests.log | 358 | bdc68536c79f16944b3e1f88141c6999e673345d16e128c3d2b5c08bc159c7bd |

## 8. 按卡交接与未完成项

| 卡要求 | 本轮判断 |
|---|---|
| 唯一DDL、18集合、六个正式DTO端口 | 静态存在，生成头:211/:213/:215/:273/:275/:277对应，抽取检查PASS；不等于C构建 |
| 操作/幂等/outbox/revision同事务 | BEGIN IMMEDIATE、checks、mutations、meta、COMMIT/回滚路径及半写trigger设计成立；运行NOT_RUN |
| 两个竞争申请一个成功 | 两个fork进程各自open，事务revision+ABSENT检查；历史A记录通过，C本轮未运行 |
| 正常重开保留数据 | 正式persistence回归存在，范围不外推崩溃/掉电；本轮NOT_RUN |
| 事件/投影/游标原子与重复去重 | 静态路径和gap锁存认可；C导入NOT_RUN |
| 水位/数值顺序/分页预算 | 指定旧反例静态封住，20位补零、原生u64比较；字节模型通过；C分页NOT_RUN |
| WAL/FULL | 真实配置代码存在；WAL返回模式未逐字核、FULL无同连接运行断言/m6未捕获；不推定介质断电可靠 |
| schema不兼容必须拒 | **FAIL，B-1仍在**；没有删库代码不能证明兼容性完成 |
| 可构建正式测试/当前证据 | **FAIL，B-2**；C未实际编译/测试，没有匹配证据 |
| 生命周期/所有权 | Core单所有者，Runtime独占开关；失败out=NULL/close(NULL)安全，busy_timeout5000ms、绑定参数/语句清理，无后台线程/动作发送；不得并发close，THREADSAFE不授权cJSON并发 |

机器状态仍为S1-01=DONE、S1-02=IN_PROGRESS/report=null、active_task=recommended_next=S1-02、阶段1 gate_result=NOT_RUN。**不建议DONE，不进入S1-03。** 修复当前阻塞、取得停止变动的新源码快照及匹配Linux日志后，才能由既定流程重新判断交接。

本轮未生成临时脚本/DB/build、debug print、临时API，未删除正式测试/证据/诊断日志。既有Public/TEMP/WSL残留未全量复核，当前只读审计不核销、不替其他写入者删除快照；由收尾负责人按用途确认清理。只读审计无需改模块运行说明，发现的过期说明已列N-2。

未完成硬件/范围外验证：RK介质断电/fsync、ARM/32位、CMake3.16下限、MCU/littlefs/真实UART/硬件锁脉冲/GPIO/电气、相机/RKNN/NPU、Android/LVGL、真实HTTPS/IPC及完整借还流程。均不由本轮SQL/静态检查或x86历史日志升级为完成。


## 9. 可重复探针源码（本轮实跑，无文件写入）

P2–P4镜像的是A的列/索引条件，不能替代C新加的列默认排序检查；P10使用列默认BINARY并展示C仍会遗漏的主键组合。

以下每段分别以 python -B -X utf8 -c 运行；除漂移负对照预期退出1外，其余退出0。前缀/schema/归因探针使用内存SQLite，NUL是显式标注的C字符串截断模型，分页是紧凑UTF-8字节模型。它们均没有执行本项目C函数。分页段沿用round-3附录P5，本轮对当前预算常量重新核对并实际执行。

### P1：用户对象前缀

```python
import sqlite3,json
for kind,ddl in [('table','CREATE TABLE sqlitex_audit(kept INTEGER); INSERT INTO sqlitex_audit VALUES(7);'),('view','CREATE VIEW sqlitex_audit AS SELECT 7 AS kept;'),('index','CREATE TABLE sqlitex_audit(kept INTEGER); CREATE INDEX sqlitex_idx ON sqlitex_audit(kept);'),('trigger','CREATE TABLE sqlitex_audit(kept INTEGER); CREATE TRIGGER sqlitex_trigger AFTER INSERT ON sqlitex_audit BEGIN SELECT 1; END;')]:
 c=sqlite3.connect(':memory:');c.executescript(ddl)
 print(kind,'old=',c.execute("SELECT count(*) FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'").fetchone()[0],'new=',c.execute("SELECT count(*) FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'").fetchone()[0])
 print('unchanged_objects',c.execute('SELECT type,name FROM sqlite_master').fetchall())
print('SQL only; C open and preservation checks NOT_RUN')
```

### P2：实际schema元数据与NOCASE相邻反例

```python
import sqlite3,pathlib,json
sql=pathlib.Path('host/storage/schema-v1.sql').read_text(encoding='utf8')
spec={'meta':[('name','TEXT',1,1),('value','TEXT',1,0)],'migrations':[('version','INTEGER',1,1),('applied_at','TEXT',1,0),('checksum','TEXT',1,0)],'documents':[('collection','TEXT',1,1),('key','TEXT',1,2),('value_json','TEXT',1,0)]}
keys={'meta':['name'],'documents':['collection','key']}
def verify(c):
 out={}
 for t,expected in spec.items():
  cols=c.execute('SELECT name,type,[notnull],pk FROM pragma_table_info(?) ORDER BY cid',(t,)).fetchall()
  match=cols==expected
  indexes=[]
  good=t not in keys
  for name,unique,partial in c.execute('SELECT name,[unique],partial FROM pragma_index_list(?)',(t,)):
   rows=c.execute('SELECT name,[desc],coll FROM pragma_index_xinfo(?) WHERE [key]=1 ORDER BY seqno',(name,)).fetchall()
   ok=bool(unique and not partial and rows==[(k,0,'BINARY') for k in keys.get(t,[])])
   good=good or ok;indexes.append([name,unique,partial,rows,ok])
  out[t]=dict(columns=cols,columns_ok=match,indexes=indexes,key_ok=good)
 return all(v['columns_ok'] and v['key_ok'] for v in out.values()),out
cases={'official':None,'missing':'CREATE TABLE documents(x TEXT)','nopk':'CREATE TABLE documents(collection TEXT NOT NULL,key TEXT NOT NULL,value_json TEXT NOT NULL)','nocase_pk':'CREATE TABLE documents(collection TEXT NOT NULL COLLATE NOCASE,key TEXT NOT NULL,value_json TEXT NOT NULL,PRIMARY KEY(collection,key))','extra':'CREATE TABLE documents(collection TEXT NOT NULL,key TEXT NOT NULL,value_json TEXT NOT NULL,extra TEXT NOT NULL,PRIMARY KEY(collection,key))','nocase_column_binary_pk':'CREATE TABLE documents(collection TEXT NOT NULL,key TEXT NOT NULL COLLATE NOCASE,value_json TEXT NOT NULL,PRIMARY KEY(collection,key COLLATE BINARY)) WITHOUT ROWID','nocase_pk_plus_binary_unique':'CREATE TABLE documents(collection TEXT NOT NULL,key TEXT NOT NULL COLLATE NOCASE,value_json TEXT NOT NULL,PRIMARY KEY(collection,key)); CREATE UNIQUE INDEX binary_key ON documents(collection COLLATE BINARY,key COLLATE BINARY)'}
for label,ddl in cases.items():
 c=sqlite3.connect(':memory:');c.executescript(sql)
 if ddl:c.executescript('DROP TABLE documents;'+ddl)
 ok,details=verify(c)
 print(json.dumps(dict(case=label,model_accept=ok,details=details,quick=c.execute('PRAGMA quick_check').fetchone())))
 if label=='nocase_column_binary_pk':
  k='aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'; k2=k.upper()
  c.executemany('INSERT INTO documents VALUES(?,?,?)',[('tools',k,'{}'),('tools',k2,'{}')])
  print('NOCASE_BINARY_PK_READ',c.execute('SELECT key FROM documents WHERE collection=? AND key=?',('tools',k)).fetchall())
  print('NOCASE_BINARY_PK_PAGE',c.execute('SELECT key FROM documents WHERE collection=? ORDER BY key',('tools',)).fetchall())
  print('NOCASE_BINARY_PK_AFTER',c.execute('SELECT key FROM documents WHERE collection=? AND key>? ORDER BY key',('tools',k2)).fetchall())
  c.execute('DELETE FROM documents WHERE collection=? AND key=?',('tools',k)); print('DELETE_ROWS',c.execute('SELECT changes()').fetchone()[0])
 if label=='nocase_pk_plus_binary_unique':
  c.execute("INSERT INTO documents VALUES('tools','a','{}')")
  try:c.execute("INSERT INTO documents VALUES('tools','A','{}') ON CONFLICT(collection,key) DO UPDATE SET value_json=excluded.value_json"); print('SECOND_WRITE_ROWS',c.execute('SELECT key FROM documents').fetchall())
  except sqlite3.Error as e:print('SECOND_WRITE_ERROR',str(e))
print('SQLite',sqlite3.sqlite_version,'SQL model only; C open/query NOT_RUN')
```

### P3：类型/NOT NULL/主键顺序/排序相邻输入

```python
import sqlite3,pathlib,json
sql=pathlib.Path('host/storage/schema-v1.sql').read_text(encoding='utf8')
spec={'meta':[('name','TEXT',1,1),('value','TEXT',1,0)],'migrations':[('version','INTEGER',1,1),('applied_at','TEXT',1,0),('checksum','TEXT',1,0)],'documents':[('collection','TEXT',1,1),('key','TEXT',1,2),('value_json','TEXT',1,0)]}
keys={'meta':['name'],'documents':['collection','key']}
def verify(c):
 out={}
 for t,expected in spec.items():
  cols=c.execute('SELECT name,type,[notnull],pk FROM pragma_table_info(?) ORDER BY cid',(t,)).fetchall()
  match=cols==expected
  indexes=[]
  good=t not in keys
  for name,unique,partial in c.execute('SELECT name,[unique],partial FROM pragma_index_list(?)',(t,)):
   rows=c.execute('SELECT name,[desc],coll FROM pragma_index_xinfo(?) WHERE [key]=1 ORDER BY seqno',(name,)).fetchall()
   ok=bool(unique and not partial and rows==[(k,0,'BINARY') for k in keys.get(t,[])])
   good=good or ok;indexes.append([name,unique,partial,rows,ok])
  out[t]=dict(columns=cols,columns_ok=match,indexes=indexes,key_ok=good)
 return all(v['columns_ok'] and v['key_ok'] for v in out.values()),out

cases={'good':'collection TEXT NOT NULL,key TEXT NOT NULL,value_json TEXT NOT NULL,PRIMARY KEY(collection,key)','type':'collection TEXT NOT NULL,key BLOB NOT NULL,value_json TEXT NOT NULL,PRIMARY KEY(collection,key)','nullable':'collection TEXT NOT NULL,key TEXT,value_json TEXT NOT NULL,PRIMARY KEY(collection,key)','reverse_pk':'collection TEXT NOT NULL,key TEXT NOT NULL,value_json TEXT NOT NULL,PRIMARY KEY(key,collection)','desc':'collection TEXT NOT NULL,key TEXT NOT NULL,value_json TEXT NOT NULL,PRIMARY KEY(collection,key DESC)','binary_pk_nocase_column':'collection TEXT NOT NULL,key TEXT COLLATE NOCASE NOT NULL,value_json TEXT NOT NULL,PRIMARY KEY(collection,key COLLATE BINARY)'}
for label,decl in cases.items():
 c=sqlite3.connect(':memory:');c.executescript(sql);c.executescript('DROP TABLE documents; CREATE TABLE documents('+decl+');')
 print(label,verify(c)[0])
print('Metadata SQL model only; C verification NOT_RUN')
```

### P4：m15/m15b/m17/m18归因模型

```python
import sqlite3,pathlib,json
sql=pathlib.Path('host/storage/schema-v1.sql').read_text(encoding='utf8')
spec={'meta':[('name','TEXT',1,1),('value','TEXT',1,0)],'migrations':[('version','INTEGER',1,1),('applied_at','TEXT',1,0),('checksum','TEXT',1,0)],'documents':[('collection','TEXT',1,1),('key','TEXT',1,2),('value_json','TEXT',1,0)]}
keys={'meta':['name'],'documents':['collection','key']}
def verify(c):
 out={}
 for t,expected in spec.items():
  cols=c.execute('SELECT name,type,[notnull],pk FROM pragma_table_info(?) ORDER BY cid',(t,)).fetchall()
  match=cols==expected
  indexes=[]
  good=t not in keys
  for name,unique,partial in c.execute('SELECT name,[unique],partial FROM pragma_index_list(?)',(t,)):
   rows=c.execute('SELECT name,[desc],coll FROM pragma_index_xinfo(?) WHERE [key]=1 ORDER BY seqno',(name,)).fetchall()
   ok=bool(unique and not partial and rows==[(k,0,'BINARY') for k in keys.get(t,[])])
   good=good or ok;indexes.append([name,unique,partial,rows,ok])
  out[t]=dict(columns=cols,columns_ok=match,indexes=indexes,key_ok=good)
 return all(v['columns_ok'] and v['key_ok'] for v in out.values()),out
cases={'official':None,'missing':'CREATE TABLE documents(x TEXT)','nopk':'CREATE TABLE documents(collection TEXT NOT NULL,key TEXT NOT NULL,value_json TEXT NOT NULL)','nocase_pk':'CREATE TABLE documents(collection TEXT NOT NULL COLLATE NOCASE,key TEXT NOT NULL,value_json TEXT NOT NULL,PRIMARY KEY(collection,key))','extra':'CREATE TABLE documents(collection TEXT NOT NULL,key TEXT NOT NULL,value_json TEXT NOT NULL,extra TEXT NOT NULL,PRIMARY KEY(collection,key))','nocase_column_binary_pk':'CREATE TABLE documents(collection TEXT NOT NULL,key TEXT NOT NULL COLLATE NOCASE,value_json TEXT NOT NULL,PRIMARY KEY(collection,key COLLATE BINARY)) WITHOUT ROWID','nocase_pk_plus_binary_unique':'CREATE TABLE documents(collection TEXT NOT NULL,key TEXT NOT NULL COLLATE NOCASE,value_json TEXT NOT NULL,PRIMARY KEY(collection,key)); CREATE UNIQUE INDEX binary_key ON documents(collection COLLATE BINARY,key COLLATE BINARY)'}

for label,ddl in cases.items():
 if label not in ('missing','nopk','nocase_pk','extra'):continue
 c=sqlite3.connect(':memory:');c.executescript(sql);c.executescript('DROP TABLE documents;'+ddl)
 ok,details=verify(c)
 print(label,'skip_columns_accept=',all(v['key_ok'] for v in details.values()),'skip_key_accept=',all(v['columns_ok'] for v in details.values()))
c=sqlite3.connect(':memory:');c.execute("CREATE VIEW documents AS SELECT 'loans' AS collection,'x' AS key,'{}' AS value_json WHERE 0")
for query in ('SELECT 1 FROM sqlite_master WHERE name=?',"SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",'SELECT 1 FROM sqlite_master WHERE type=(char)116 AND name=?'):
 try: print('QUERY',query,c.execute(query,('documents',)).fetchall())
 except sqlite3.Error as e:print('ERROR',str(e))
print('VIEW_TABLE_INFO',c.execute('SELECT name,type,[notnull],pk FROM pragma_table_info(?) ORDER BY cid',('documents',)).fetchall())
c.executescript('BEGIN;'+sql)
print('VIEW_AFTER_DDL_MODEL_ACCEPT',verify(c)[0]);c.rollback()
print('AFTER_ROLLBACK',c.execute('SELECT type,name FROM sqlite_master').fetchall())
print('SQL model only; C m15/m15b/m17/m18 NOT_RUN')
```

### P5：属性NUL截断与正式Python契约校验

```python
import json,pathlib,sys
sys.path.insert(0,'scripts')
from check_contracts import validate_value
s=json.loads(pathlib.Path('contracts/internal-data.schema.json').read_text(encoding='utf8'))
base=dict(id='22222222-2222-4333-8444-555555555555',name='power supply',slot_id=1,tool_revision='2',enabled=True)
for key in ('na\u0000me','name\u0000junk','name\0junk'):
 key=key.replace('\\u0000','\0'); v=dict(base);del v['name'];v[key]='power supply'
 raw_ok=validate_value({'$ref':'#/$defs/ToolRecord'},v,s['$defs'])
 clipped={k.split('\0')[0]:x for k,x in v.items()}
 print(repr(key),'original=',raw_ok,'C_prefix_model=',validate_value({'$ref':'#/$defs/ToolRecord'},clipped,s['$defs']))
print('NUL truncation model only; C/mutation NOT_RUN')
```

### P6：分页字节模型

```python
import json,pathlib,sys
sys.path.insert(0,'scripts');from check_contracts import validate_value
s=json.loads(pathlib.Path('contracts/internal-data.schema.json').read_text(encoding='utf-8'))
def enc(x):return json.dumps(x,separators=(',',':'),ensure_ascii=False).encode('utf-8')
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
  print('P5',json.dumps(dict(reason_bytes=len(reason.encode()),recovery=recovery,reserved_next_key=1160,budget=budget,pages=pages,total=start)))
print('Python JSON byte model only; C query/recovery NOT_RUN')
```

### P7：按现有检查器枚举坏链接

```python
from pathlib import Path
import re
root=Path.cwd();broken=[]
for f in root.rglob('*.md'):
 if 'research' in f.parts:continue
 s=f.read_text(encoding='utf8')
 for m in re.finditer(r'\]\(([^)]+)\)',s):
  link=m.group(1)
  if link.startswith(('http:','https:','#','mailto:')):continue
  if not (f.parent/link.split('#',1)[0]).exists():
   broken.append((str(f.relative_to(root)),s.count('\n',0,m.start())+1,link))
print('BROKEN_LINK_MATCHES',len(broken))
for x in broken:print(x)
```

### P8：真实抽取器内存漂移负对照

```python
import runpy,pathlib,sys
m=runpy.run_path('host/storage/tools/extract_schema.py'); fn=m['main']
original=pathlib.Path.read_bytes
frozen=m['FROZEN']
def fake(p):
 b=original(p)
 return b+b' ' if p==frozen else b
pathlib.Path.read_bytes=fake
sys.exit(fn(['--check']))
```

### P9：原始UPSERT prepare正对照

```python
import sqlite3,pathlib
c=sqlite3.connect(':memory:');c.executescript(pathlib.Path('host/storage/schema-v1.sql').read_text(encoding='utf8'))
c.executescript('DROP TABLE documents; CREATE TABLE documents(collection TEXT NOT NULL,key TEXT COLLATE NOCASE NOT NULL,value_json TEXT NOT NULL,PRIMARY KEY(collection,key COLLATE BINARY)) WITHOUT ROWID;')
sql='INSERT INTO documents(collection, key, value_json) VALUES (?1, ?2, ?3) ON CONFLICT(collection, key) DO UPDATE SET value_json = excluded.value_json'
c.execute('EXPLAIN '+sql,('tools','a','{}')).fetchall();print('EXACT_UPSERT_PREPARE_OK')
print('metadata',c.execute('SELECT * FROM meta ORDER BY name').fetchall(),c.execute('SELECT version FROM migrations WHERE version=1').fetchall())
print('quick',c.execute('PRAGMA quick_check').fetchall())
print('SQL only; C NOT_RUN')
```

### P10：B最终补丁仍遗漏的主键/额外唯一索引组合

```python
import sqlite3,pathlib,json
c=sqlite3.connect(':memory:');c.executescript(pathlib.Path('host/storage/schema-v1.sql').read_text(encoding='utf8'))
c.executescript('DROP TABLE documents; CREATE TABLE documents(collection TEXT NOT NULL,key TEXT NOT NULL,value_json TEXT NOT NULL,PRIMARY KEY(collection,key COLLATE NOCASE)); CREATE UNIQUE INDEX binary_key ON documents(collection COLLATE BINARY,key COLLATE BINARY);')
print('columns',c.execute('SELECT name,type,[notnull],pk FROM pragma_table_info(?) ORDER BY cid',('documents',)).fetchall())
for name,u,p in c.execute('SELECT name,[unique],partial FROM pragma_index_list(?)',('documents',)):
 print('index',name,u,p,c.execute('SELECT name,[desc],coll FROM pragma_index_xinfo(?) WHERE [key]=1 ORDER BY seqno',(name,)).fetchall())
sql='INSERT INTO documents(collection,key,value_json) VALUES (?1,?2,?3) ON CONFLICT(collection,key) DO UPDATE SET value_json=excluded.value_json'
c.execute('EXPLAIN '+sql,('tools','a','{}')).fetchall();print('UPSERT_PREPARE_OK')
c.execute(sql,('tools','a','{}'));print('COLUMN_BINARY_READ_A',c.execute('SELECT count(*) FROM documents WHERE collection=? AND key=?',('tools','A')).fetchone()[0])
try:c.execute(sql,('tools','A','{}'));print('unexpected accepted')
except sqlite3.Error as e:print('UPSERT_A_ERROR',str(e))
print('metadata',c.execute('SELECT * FROM meta ORDER BY name').fetchall(),c.execute('SELECT version FROM migrations WHERE version=1').fetchall(),'quick',c.execute('PRAGMA quick_check').fetchall())
print('Column declarations use default BINARY; good UNIQUE index exists; incompatible NOCASE primary key remains. SQL only, C NOT_RUN')
```
