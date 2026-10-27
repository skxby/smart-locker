# S1-02 round-9 独立审计

日期：2026-10-24（Asia/Shanghai）。工作副本：%REPO%。契约：**0.2.1 / REVIEW_DRAFT**。

## 0. 结论

**PASS（限定为当前源码、正式回归与交付证据的独立复审）：未发现尚未修复的阻塞项 B-x；保留 2 项非阻塞收尾问题 N-1/P3、N-2/P3。** round-8 四项重点中，mutation 的主要归因、断言合计、正对照输入和模块构建/VM入口均有实际修正；不能说当前所有文档已完全同步，具体残项见第5节。

本结论不因前七轮 FAIL 自动延续，也不把审计机无法连接 Linux 当作实现失败。本轮未发现需要再次修改生产 C 代码或正式 C 测试的具体反例；文字和暂存项问题不要求重跑全部 Linux 测试或再开一轮同范围代码审计。

**本轮 configure/build/CTest、C 测试二进制、ASan+UBSan、TSan、C mutation 复跑全部 NOT_RUN。** 本轮实际执行的是 Windows Python 设计检查、抽取器正负控、Node/Python 字节与日志核验及内存 SQLite 探针。已有 Linux PASS 按保留运行记录复核，不能改称本审计实际运行 PASS。

本Agent只新增本报告，不修改实现、测试、契约、根/模块README、stage-plan、summary/handoff或旧审计；未置 DONE、未启动 S1-03、未 commit/push。S1-01仍DONE；S1-02仍IN_PROGRESS、report=null，active_task/recommended_next=S1-02；阶段1 gate_result=NOT_RUN。硬件profile仍UNVERIFIED。

## 1. 审计依据和实际执行

按 AGENTS.md → README.md → docs/stages/README.md → stage-plan → stage-01-task-cards.md 公共规则及S1-02卡:48–56核对；随后交叉核查阶段0持久化规则、internal-interfaces、UART-v1、traceability和module-ownership。本轮是既有实现的离线复审，无新项目/较大实施方案，不下载、替换或重新引入依赖。

核对六个Repository声明、原生DTO和事件批次；审阅host/storage唯一DDL、repository/schema/u64/event_key/regex相关路径、私有头、嵌入器/抽取器及局部CMake；核对五个storage正式测试、common断言harness、第三方锁定/许可和历轮问题落点。第三方核验是锁定内容一致性核验，不是上游安全审计。

普通exec_command在创建进程的helper setup阶段失败，没有启动shell或产生shell退出码；后改用Node文件接口及child_process.execFile参数数组，cwd固定为本项目。未提权、安装软件或更改WSL/SSH设置。

| 操作 | 本轮实际结果 |
|---|---|
| git status --short | exit 128；fatal: not a git repository (or any of the parent directories): .git |
| rg --files -g AGENTS.md | exit 0，仅根AGENTS.md |
| python --version | exit 0，Python 3.14.6 |
| cmake --version | ENOENT，PATH不可调用；不推断全盘未安装 |
| wsl --list --quiet | exit 4294967295，输出含Wsl/EnumerateDistros/Service/E_ACCESSDENIED |
| ssh -o BatchMode=yes -o ConnectTimeout=6 locker-vm，执行只读printf | exit 255，未取得VM会话；不推断guest开关机状态 |
| python -B -X utf8 scripts/check_contracts.py | exit 0/PASS；0.2.1、24 routes、82 ports、28 private RPC；business_or_hardware_tested=false |
| python -B -X utf8 scripts/check_stage_plan.py | exit 0/PASS；45卡、done=1、active_task=S1-02 |
| python -B -X utf8 scripts/check_vision_contracts.py | exit 0/PASS；camera_owned=false、rknn_available=false、hardware_tested=false |
| python -B -X utf8 scripts/check_feasibility.py | exit 0/PASS；runtime_or_hardware_tested=false |
| python -B -X utf8 host/storage/tools/extract_schema.py --check | exit 0；27525 B与真源匹配 |
| 抽取器内存负控：只在read_bytes返回值尾部附加空格，再调用真实main(--check) | exit 1/DRIFT，预期负控；未改磁盘文件 |
| Python AST解析变异定义、Node逐段核算日志 | 42名称、53次文本编辑锚点全部count=1；日志核算结果见第2节 |
| Python内存SQLite正负对照 | exit 0，SQLite 3.53.2；同输入官方成功、CHECK/FK失败并回滚；不是C Repository执行 |
| 实际快照包只读SHA-256与tar成员比对 | exit 0；摘要/大小/1113条目/1016文件成立；卡范围32/32、额外输入18/18匹配 |

本轮check_contracts的c_declaration_syntax为PASS (parse only, not compile/link)；保留WSL/VM日志为NOT_RUN (optional pycparser unavailable)。这是环境差异，本轮未安装依赖，不改历史日志，也不把parse当编译链接。

## 2. round-8四项及用户八组重点复核

### 2.1 N-1：主要归因修正成立，交接仍有少量旧陈述

只读解析mutation-batch.py的POINTS初始赋值，并按:113–120的八个名称追加JOINTS；未运行复制/删除/构建部分。42名称与原始42段一一对应，每项依次替换的全部53个old锚点在当时的内存文本中恰好出现1次。执行器:150–158的count!=1会记录PATCH_PATTERN_COUNT并跳过该项，不静默替换错位。

逐段读取mutation-batch.log：42段全部patch=OK configure=0 build=0；32段ctest=8、10段ctest=0。对32段红测，assertion_failures等于真实FAIL断言行数+1，额外1行是TEST_REPORT套件汇总；绿测为0，不能再减成负数。42段归一化日志的真实失败行号（含重复次数）均与原始日志一致。

下表“helper”专指test_repository.c:2250，“DDL状态行”为:2542/:2543/:2554/:2555/:2567/:2568，其余为目标状态行；三列之和为真实断言失败数：

| 联合项 | helper | DDL状态行 | 目标状态行 | 合计 |
|---|---:|---:|---:|---:|
| m17+ | 16 | 6 | 10 | 32 |
| m18+ | 12 | 6 | 6 | 24 |
| m20b+ | 8 | 6 | 2 | 16 |
| m21b+ | 8 | 6 | 2 | 16 |
| m22+ | 8 | 6 | 2 | 16 |
| m26+ | 6 | 6 | 0 | 12 |
| m29+ | 8 | 6 | 2 | 16 |
| m30+ | 8 | 6 | 2 | 16 |

m26b的4条仅为:2250×2与:2475/:2476，全部来自trigger夹具，没有partial的:2460/:2461。m26+的12条与m33完全相同，目标贡献为0。另存mutation-joint-m26c.py/.log的三点联合m26+m28+m33有20条真实断言：4 partial（两个状态行+两个helper）、4 trigger、12 DDL。此脚本的assertion_failures=20直接取套件汇总中的真实N，**不适用批处理K-1规则**。

这一次三点联合在42次批次之外；全包保留运行是42+1，不能把它暗算进42，也不能把42或32说成独立缺陷数。10个未捕获项确为m6/m9及m17/m18/m20b/m21b/m22/m26/m29/m30八个单点，未隐藏零目标行数。m29b、m23/m24及m15_object_type不在实际42项中。

summary §5.4与mutation.log:72–96的分解、m26b及三点联合归因已正确。真实执行器:162–177从同一次ctest stdout抽FAIL，summary:216已改正。handoff部分正文尚未同步，见N-1；因此只确认主要证据修正成立，不认定所有文字完全闭环。

### 2.2 N-2：断言合计闭环

两份unit-tests.log各有11条PASS、11条rc=0，逐项与顺序完全相同：

| 分组 | 分项 | 和 |
|---|---|---:|
| common | 97+5+53+164+133+53 | 505 |
| storage | event_key80 + regex46 + repository865 + schema131 + u6490 | 1212 |
| 总计 | 505+1212 | **1717** |

summary §5.1及handoff:406/:509/:519/:611采用1717/1212/865。第12个CTest是Python漂移门禁，没有额外C断言。历史1715=505+1210（repository863）是round-8当时实际值；旧1707/1202是当时算错，不能把1707→1717解释为新增10条测试：本次正对照仅新增2条，真实变化为1715→1717。

### 2.3 N-3：正对照输入闭环

test_repository.c:2726新增build_audit_record_with_id，直接把传入id写为value.id；:2752–2762用k_first_id和UUID_KEY同时构造record与PUT key，不再复用按索引产生不同id的build_audit_record。:2759/:2760新增两条strstr断言；源码构造链保证当前两份key=value.id，第二条UUID_KEY与SQL_CHECK_CONSTRAINT:2385谓词完全相同。strstr本身只检查字符串包含，不能把它单独称为通用JSON字段相等证明。

正对照:2765要求commit OK、:2769 revision1、:2773文档2行；main:2969实际注册该函数。两份当前保留C日志均为repository865；本轮C执行NOT_RUN。

本轮另读取真实schema-v1.sql和三个C宏，用同样id/reason/字段构造两条audits执行内存SQL探针：

| documents定义 | 存储DDL字节 | 与官方相同 | SQL事务结果（行数,revision） |
|---|---:|---|---|
| 官方 | 164 | 是 | (2,1) |
| CHECK | 191 | 否 | CHECK失败并回滚为(0,0) |
| FOREIGN KEY | 185 | 否 | foreign key mismatch并回滚为(0,0) |
| 等长小写DDL | 164 | 否 | SQL语义可写，(2,1)；按当前受控DDL策略仍须由C open拒绝 |

四库用户对象均3、quick_check均ok。这验证同一输入确实能触发CHECK，也说明等长并不足以识别定义。探针没有执行C open/commit，不替代正式坏库双模式CONFLICT回归或C mutation。

### 2.4 N-4：模块构建/VM入口闭环

host/storage/CMakeLists.txt:43/:57是add_custom_command，:71的目标与:84依赖把两个.inc放在CMAKE_CURRENT_BINARY_DIR/generated，属于构建期。README:32/:33/:60、embedded.h、schema.h、schema_embedded.c及DDL注释已写构建期；根CMake的契约版本校验仍正确属于配置期，不能一概替换。

模块README:78与当前VM run-summary一致：源码$HOME/locker-workspace/S1-02/src；build/S1-02/<run>；logs/S1-02/<run>；artifacts/S1-02。顶层src不作为本卡入口。实际VM目录存在性和目录内容来自保留日志，本轮未连VM实盘。

### 2.5 当前批次、快照及指纹

summary/handoff的当前验收主体、run-id.txt、vm-s102-run-id.txt和vm-s102-run-summary.log均指向**20261024-213500**。WSL构建路径为/home/yuanangyu/s102_r9ev2，VM为/home/%USER%/locker-workspace/build/S1-02/20261024-213500；不是互相改名的同一路径。

本轮实际读取C:/Users/Public/s102_snapshot_r9.tar.gz，不解包落盘：**81796615 B，SHA-256=f4279ba59ad7b013bdc9811a8e3f6eafc093e214ea3ea92a79ad92784b47c69f；1113个tar条目，1016个普通文件**。归档精确路径优先比对，32个卡内清单成员逐字节匹配；额外核对契约源/内部数据源/schema/生成头、common test_support及host/common全部13个文件，18/18匹配。初版辅助探针用后缀匹配误把根CMakeLists.txt匹配成6项，改为精确路径优先后32/32；这是探针选择器修正，不是源文件失配。

实际快照与本地字节一致加强了输入绑定；仍未独立访问VM盘、确认VM实际运行命令或认证二进制插桩。日志未保留完整编译命令/CMakeCache，故不能仅凭sanitizer文件名及0报告断言所有二进制已被独立验证插桩。

source-manifest.txt为3563 B，generated=2026-10-24T20:59:57+08:00，自身SHA-256 **817ddb073ba2a35b3b69e83e5fc3d8f72429ad27d3f22e076a967edecfa6dd0a**。实际范围32项全部摘要与字节数匹配、无漏项；两份manifest.sha256与其同序一致。mutation.log与原始batch各11个SOURCE_FINGERPRINT全部匹配当前文件。

| 文件 | SHA-256前12位 | 字节 | 换行计数 |
|---|---|---:|---:|
| repository.c | b9ab2abaa41e | 95832 | 2469 |
| test_repository.c | f1cc609cf4cf | 138538 | 2983 |
| host/storage/CMakeLists.txt | 3d184f052d2b | 3832 | 102 |
| host/storage/README.md | 45133f6c1483 | 7850 | 125 |
| schema-v1.sql | c2d3dfcf8cf8 | 2350 | 43 |

mutation.log为26243 B、693个换行；mutation-batch.log为46309 B、562个换行。末尾换行使split('\n')分别得到694/563项，所谓“563行”与562个换行是计数口径差异，不是缺一段执行。归一化日志有42个批次段加1个三点联合附录段。

## 3. 保留Linux运行记录（本轮均未复跑）

| 环境 | configure/build/CTest | ASan+UBSan日志 | TSan日志 |
|---|---|---|---|
| WSL2 s102_r9ev2 | 各exit0；12/12，0.39s；普通配置/构建warning计数0 | 各exit0；12/12，1.18s；ASAN_REPORTS=0 | 各exit0；12/12，1.98s；TSAN_HITS=0 |
| VMware 20261024-213500 | 各exit0；12/12，0.51s；普通配置/构建warning计数0 | 各exit0；12/12，1.37s；ASAN_REPORTS=0 | 各exit0；12/12，1.92s；TSAN_HITS=0 |

逐一核对对应configure/build/ctest退出标记及12个测试名；两份source-verify均32/32 OK，VM另记SRC_STRAY_ARTIFACTS=0和SRC_FILES=1016。四项设计检查的独立log/json、合并design-checks及VM合并输出一致，保留输出中可选pycparser未运行的边界没有改写。

## 4. 交付实质核查矩阵（32项）

“静态成立”指本轮源码/正式测试映射成立；“记录相符”指既有日志复核，均不等于本轮C实跑。

| # | 核查 | 落点与结果 |
|---|---|---|
| 1 | 前置/状态 | S1-01报告DONE；S1-02 IN_PROGRESS；阶段1门禁NOT_RUN，未越级 |
| 2 | 真实契约 | contract-source 0.2.1/REVIEW_DRAFT，生成头版本一致；设计检查PASS |
| 3 | 六个端口/opaque | 生成头:190/:211/:213/:215/:273/:275/:277与实现一致，无同义API |
| 4 | 原生DTO | 生成头:120/:143–147；有界JSON、四事件批次，未另造跨模块DTO |
| 5 | 模块职责 | Core单线程调用；仓储不创建业务线程、不发设备动作；对照ownership:24/:49 |
| 6 | 唯一DDL | schema-v1.sql三表和迁移元数据；运行时用嵌入数组，未另建DDL真源 |
| 7 | 受控集合 | schema_records 18 collections/47 defs，抽取器--check正负控有效 |
| 8 | 集合与value同型 | repository.c:779–787按集合记录校验，不仅依赖RepoDocument.anyOf |
| 9 | SQL参数绑定 | PUT/DELETE/query使用固定语句和bind；不接任意SQL |
| 10 | revision CAS | :699–715校验new=expected+1、拒溢出/旧revision；m1记录5条红断言 |
| 11 | 操作/幂等/outbox原子 | :1373–1384事务包住apply_change；持久化测试一单写六类记录 |
| 12 | 真正半写失败回滚 | test_repository:569用第二条outbox触发ABORT，验证前一slot_guard和revision回滚；有去trigger正控 |
| 13 | 竞争占位 | :720测试两个fork进程各自open；要求恰一OK一CONFLICT、revision1一条占位；不冒称线程并发安全 |
| 14 | 重开持久化 | :800–899核有效借用、未决operation/outbox、幂等响应及revision；只证明正常close/reopen |
| 15 | 事件键完整性 | device_id+journal_epoch+boot+seq；20位补零键，避免epoch碰撞/10排在2前 |
| 16 | 新事件/投影/游标原子 | :1603开始事务，:1635去重、:1722应用Core投影、:1748写游标，成功后才写输出cursor |
| 17 | 重复页不重复投影 | :1653分支早退；有gap时只锁存continuity=false；m14记录2条目标红断言 |
| 18 | 连续性/倒退 | 同boot严格+1、换boot递增且seq1；跨boot可跳号；已有水位不倒退，gap不会被后页洗白 |
| 19 | 包络/半零 | :1556–1588拒零last_committed、半零、异日志、越尾和next_after错配；保留边界正控 |
| 20 | u64数值语义 | u64.c复用common，规范十进制/溢出检查；非规范回退有独立输入，m4记录2/90 |
| 21 | 分页与恢复 | :975预留1160 B续页键，:1099按最终序列化文档计预算，未读完返回最后已含key；单条超限NO_SPACE |
| 22 | NUL保护 | schema.c:35–62原始/转义NUL及反斜杠奇偶守卫；repository:502解析前调用；独立负例/合法正控存在 |
| 23 | schema fail-closed | 支持关键字白名单、未知def/keyword失败、集合受控；正式schema测试有正负例 |
| 24 | regex有界重复 | regex.c:526零零特判、:531/534种子1、:562播种min；m25两条、m27一条目标红断言 |
| 25 | 初始化分类 | 有痕迹/非表documents拒绝，真正空库才显式初始化；NOT GLOB修复LIKE下划线问题 |
| 26 | 列与排序 | :1856 table_xinfo含hidden，:1881拒生成/隐藏列；:1895核列BINARY；正式双模式回归存在 |
| 27 | 唯一键/partial | :1973逐一核unique、:2000拒partial、:2010不匹配即CONFLICT；三点联合证据归因正确 |
| 28 | 未知对象/静默写入 | :2086只接官方三表；PUT :438、DELETE :477核changes；DELETE失败后revision1/文档1，未写成0 |
| 29 | CHECK/FK/等长定义 | :2244查三表sqlite_master.sql，:2280–2287拒NULL/长度/字节/行数不符；首次与已有库均调用；m33/m34记录相符 |
| 30 | 构建/依赖复用 | 根:87/:92两处接入；局部目标/生成依赖合理；vendor六文件摘要大小与lock匹配，许可存在 |
| 31 | 测试可失败性 | common TEST_REPORT失败return1；11个C测试+1漂移门禁注册；42变异及另1三点联合记录不恒真 |
| 32 | 边界与文档/残留 | 源码树扫描无本卡临时物；文档及外部暂存残项见N-1/N-2；RK掉电、ARM、硬件等仍NOT_RUN |

历轮旧阻塞的指定路径已被当前上述防线和正式回归覆盖：round-1事件/初始化/分页/NUL；round-2重复gap/半零/视图；round-3对象前缀/列键；round-4排序/源证据失配；round-5 partial；round-6 trigger/生成列；round-7 CHECK/FK。没有用旧版本行号或旧分母认定当前仍FAIL。

## 5. 非阻塞收尾发现

### N-1 / P3：主要证据正确，但交接与部分说明残留旧口径

以下为落报告前复读仍存在的具体矛盾：

| 位置 | 当前陈述 | 应依据的当前事实 |
|---|---|---|
| handoff:447 | CTest后再直接执行失败二进制取FAIL | batch.py:162–177只有同一次ctest stdout抽取；summary:216已正确 |
| handoff:449/:512 | test_repository指纹6ab9a68d1c99配138538 B | 当前及两份mutation指纹均f1cc609cf4cf；6ab是137410 B旧输入 |
| handoff:513 | 保留脚本常量仍s102_r8mutI/s102_r8buildI | 实际脚本:5–7为s102_r9mut/s102_r9build及s102_r9mut.log |
| handoff:626第五点 | m26b共8条=4 partial+4 trigger | 当前仅4条trigger/helper；partial仅三点联合可观测，:463已正确 |
| summary:253 | m34b关文本比较但保留长度分支 | 实际edit为整个条件换成stored==NULL，同时去掉长度和内容检查 |
| summary:469 | 本卡所有构建在s102_r7ev及193500 | 当前主体及日志为s102_r9ev2与213500；早批次应明确写作历史 |

建议由收尾负责人按右列直接改当前正文；无需修改原始日志或旧审计，不需要为文字勘误重跑C测试。summary/handoff整改历史里的1707→1717可同时写清“旧算术勘误+实际新增2条”，不把它当新增10条。

**非阻塞依据**：原始日志、当前代码、正确的summary §5.4和mutation.log足以重算真实结论；这些残项没有显示当前生产逻辑不满足验收，不重新打开旧B项。但不能继续对外声称“全部当前正文已同步”。

### N-2 / P3：外部暂存清零声明与现存条目不符

summary:468及handoff:549–556称Public/TEMP相关临时项已删除并复核0。本轮只读目录枚举实际观察到C:/Users/Public有34个s102_条目，系统TEMP有6个相关条目；其中Public的当前快照已实际成功读取并验证。故不能为当前时刻PUBLIC_LEFT=0/TEMP已清零背书。

| 位置/组 | 用途和处理边界 |
|---|---|
| Public/s102_snapshot_r9.tar.gz、s102_vm_evidence_r9.tgz、s102_vm_ev_r9、s102_r9ev、s102_r9ev2 | 快照/VM及WSL证据暂存；已有快照与正式日志保留价值，先确认归档完整，不能盲删 |
| Public/s102_r9mut.py/.log、s102_r9mut_full.log、s102_r9_m26c.py/.log | 变异脚本/原始日志暂存；正式四文件已保留，确认无额外诊断信息后再由收尾方清理副本 |
| Public/s102_round8_*、s102_round9_* | 审计启动及输入输出/运行日志；round-9可能仍在使用，应保留运行中与关键诊断日志 |
| Public其余s102_* | 清单/辅助脚本等；全部名称见附录B，按实际用途分类，不能把目录名当成可删除证明 |
| TEMP/s102_r3_in.txt、s102_r3_out.log、s102_r4_in.txt、s102_r4_out.log、vm125905、vm135713 | 历史输入/诊断/VM暂存；需核对归档后分类处理 |

这些位置在本轮可写工作区之外；本轮只授权审计报告，未删除、移动或覆盖任何外部条目。未知是否仍被另一进程使用，不要求用户在此轮逐文件决定；建议收尾方把有意保留项记为保留，把实际清理数字写成有批次/时间的事实。Windows用户HOME目录枚举EPERM，Linux/VM HOME与/var/tmp未访问，不能外推全局清零。

**非阻塞依据**：本卡源码目录没有这些临时物，正式回归和证据完整性未因此失效；这是收尾记录/外部暂存分类问题，不是仓储功能缺陷。

## 6. 文档、输入漂移与交接边界

本轮开始证据目录56个文件/35个.log；新增本报告后57个。原文“56个（含round-8）”是写入本报告前的正确基数，不据新增审计制造新缺陷。旧1681/829/1609/1583、152903/174830/193500等大多在明确历史整改/清理段，不能一概判成当前错误；第5节仅列仍把旧值当当前的具体位置。旧审计历史数字与结论保持原样。

本轮用内存脚本或python -B -c，SQLite库全部:memory:，无一次性脚本/DB/构建/新测试日志落盘；卡范围及根目录临时命名扫描均无匹配。本轮不清理现有正式测试、fixture、mutation脚本/日志、旧审计、用户build/HAL或其他诊断材料。

文档同步检查已执行：本轮没有产品行为/接口/数据变化，授权产物为本审计；相关当前正文需同步之处已列N-1/N-2，没有代改summary/handoff。没有Git历史，不能独立证明根CMake相对开工版本只改两行或contracts历史零修改；能确认当前接入位置、契约一致性及本轮入口/出口字节稳定。

审计期间确实观察到外部修改summary/handoff，未由本Agent写入。初始摘要分别为af48a817173e452e5997f75a51e50c11f3a8080180957af3edf0652ed542afb2（103017 B）和a31c4893c38256168727d3e3ff00f7b8501c1712e5b617e181e2b8ce63bea7d7（66879 B）；落报告前重读摘要为：

- summary.md：b73979df884b76e494209e7e1fd6f5332d129664243d30bf9f62c3de33b30541，103034 B。
- handoff.md：798ebbf59a22b77f641879cd9ccaf7b18fd3c26707ea9c01358581954302e544，66879 B。

已确认的外部勘误包括test_repository行计数2984改为2983、summary范围表改为实际字节数及README历史值说明；本报告采用末次重读，未把这些已改项计作未解决发现。其余被入口指纹覆盖的源码、测试、契约、stage-plan、README及保留日志/历轮审计没有漂移；不将文档修改虚报为生产源码失配。

本次技术复审可交接为上述限定PASS。收尾负责人可完成N-1文字同步及N-2暂存分类；文字/清理记录修正本身不需要新增功能回归或无休止同范围审计。仍未完成的实物验证包括RK介质fsync/掉电、ARM/32位、MCU/UART/锁/相机/NPU等；synchronous=FULL无运行时断言、m6和m9未捕获、初始化事务中途失败无注入式回归等既有缺口继续明示，不补写PASS。本报告不作DONE决定，不改变任何任务/阶段状态，也不授权跨卡。

## 附录A：本轮SQL探针（原样，可在项目根以python -B执行）

此脚本只读源文件并创建内存数据库。它验证SQL与夹具输入语义，不运行C端口。

```python
import ast,json,pathlib,re,sqlite3
src=pathlib.Path('tests/unit/storage/test_repository.c').read_text(encoding='utf-8')
ddl=pathlib.Path('host/storage/schema-v1.sql').read_bytes().decode('utf-8')
uuid=ast.literal_eval(re.search(r'^#define UUID_KEY\s+("[^"]+")',src,re.M).group(1))
first=ast.literal_eval(re.search(r'k_first_id = ("[^"]+");',src).group(1))
def macro(name):
    block=re.search(r'^#define '+name+r'\s+([^\n]*(?:\n[^\n]*)*?)(?=\n\s*\n)',src,re.M).group(1)
    block=block.replace('UUID_KEY',json.dumps(uuid)).replace('\\\n','')
    return ''.join(ast.literal_eval(t) for t in re.findall(r'"(?:\\.|[^"\\])*"',block))
def new_db():
    db=sqlite3.connect(':memory:',isolation_level=None)
    db.executescript(ddl)
    return db
base=new_db()
expected={t:base.execute('SELECT sql FROM sqlite_master WHERE name=?',(t,)).fetchone()[0] for t in ['meta','migrations','documents']}
records=[]
for ident,reason in [(first,'positive control one'),(uuid,'positive control two')]:
    record=dict(id=ident,actor_id=None,operation_id=None,type='PAGE_BUDGET',reason=reason,occurred_at=None,time_quality='HOST_SYNCED',source_event=None)
    records.append(('audits',ident,json.dumps(record,separators=(',',':'))))
assert all(k==json.loads(v)['id'] for _,k,v in records)
assert records[1][1]==uuid
print('SQLite',sqlite3.sqlite_version,'ids_match=True second_matches_CHECK=True')
for name in ['OFFICIAL','SQL_CHECK_CONSTRAINT','SQL_FOREIGN_KEY','SQL_DDL_SAME_LEN_DIFFERENT_TEXT']:
    db=new_db()
    if name!='OFFICIAL': db.executescript(macro(name))
    stored=db.execute("SELECT sql FROM sqlite_master WHERE name='documents'").fetchone()[0]
    print(name,'ddl_bytes='+str(len(stored.encode())),'ddl_equal='+str(stored==expected['documents']),'objects='+str(db.execute("SELECT count(*) FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'").fetchone()[0]),'quick_check='+str(db.execute('PRAGMA quick_check').fetchone()[0]))
    db.execute('BEGIN IMMEDIATE')
    error=None
    try:
        for values in records:
            db.execute('INSERT INTO documents(collection,key,value_json) VALUES(?,?,?) ON CONFLICT(collection,key) DO UPDATE SET value_json=excluded.value_json',values)
        db.execute("UPDATE meta SET value='1' WHERE name='snapshot_revision'")
        db.execute('COMMIT')
    except sqlite3.DatabaseError as e:
        error=str(e);db.execute('ROLLBACK')
    state=(db.execute('SELECT count(*) FROM documents').fetchone()[0],db.execute("SELECT value FROM meta WHERE name='snapshot_revision'").fetchone()[0])
    assert state==((0,'0') if name in ['SQL_CHECK_CONSTRAINT','SQL_FOREIGN_KEY'] else (2,'1'))
    print(' result',state,'error',error)
    db.close()
print('SQL_PROBE_OK (not C Repository execution)')
```

## 附录B：只读枚举到的Public匹配名称

以下是观察值，不是删除清单；保留活动审计与关键运行诊断日志。

```text
s102_cpdc.sh
s102_dc2
s102_dc2.sh
s102_ls.sh
s102_manifest.sha256
s102_manifest_paths.txt
s102_mkmanifest.sh
s102_r8ev.sh
s102_r9ev
s102_r9ev.sh
s102_r9ev2
s102_r9ev2.sh
s102_r9mut.log
s102_r9mut.py
s102_r9mut_full.log
s102_r9quick.sh
s102_r9_m26c.log
s102_r9_m26c.py
s102_round8.ps1
s102_round8_in.txt
s102_round8_out.txt
s102_round8_run.log
s102_round9.ps1
s102_round9_in.txt
s102_round9_out.txt
s102_round9_run.log
s102_snapshot_r9.tar.gz
s102_snap_r9.sh
s102_source-manifest.txt
s102_sv.sh
s102_vm_evidence_r9.tgz
s102_vm_ev_r9
s102_vm_fix.sh
s102_vm_r9.sh
```
