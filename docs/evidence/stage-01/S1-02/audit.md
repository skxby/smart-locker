# S1-02 独立审计

日期：2026-10-24（Asia/Shanghai）。项目主副本：%REPO%。契约：0.2.1 / REVIEW_DRAFT。

**结论：FAIL，不满足 S1-02 技术交接条件。** 静态审查发现 5 项阻塞缺陷，涉及事件连续水位、批次边界、首次初始化、分页预算和 JSON 无损校验。本轮独立 Linux 构建/CTest 为 **NOT_RUN**，不能声称本轮 11/11 PASS。四项设计检查实际执行并通过，但不验证上述运行语义。

本轮仅新增本文件，不改业务源码、契约、正式测试、验收预期、summary/handoff 或 stage-plan；不置 DONE，不执行 S1-03，不 commit/push。机器状态保持 active_task=S1-02、S1-02=IN_PROGRESS、report=null、阶段 1 gate_result=NOT_RUN。FAIL 是本次审计结论，不是擅自修改卡状态。

## 1. 输入身份与前置

已读取 AGENTS、README、阶段入口、机器阶段索引、阶段 1 卡文及公共约束/验收/附录，定向核对阶段 0、traceability、module-ownership、内部接口、UART-v1、Repository C 声明及内部数据真源；读取 firmware/README，明确环境编译不代替本卡或硬件门禁。已核对 S1-01 summary/handoff/audit 及当前收尾指令，S1-01 为 DONE，最新独立审计 PASS；没有重开或修改该卡。

已读取 S1-02 summary/handoff/source-manifest、仓储实现与正式测试，核查既有构建、CTest、sanitizer 和 mutation 日志。历史日志内容不作为本轮实际执行结果。

| 本轮读取/重算的输入 | SHA-256 |
|---|---|
| contracts/contract-source.json，66553 B | 14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f |
| contracts/include/locker_contracts.h，39925 B | 34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a |
| contracts/internal-data.schema.json，129569 B | e7b8695c49115e2994cf126dc5ab629e5c82460e70d4c292b19576f55c60e386 |
| docs/stages/stage-01-task-cards.md，16460 B | 7f7561e83f9f614fd4ed226cefe3ce78ba367859a55ee92cd82212b25273fc93 |
| config/development/stage-plan.json，24749 B | 1ed89d9f5695c40f965f43d331907295eccca21eaf52d67bee92e5dc4fdf1b47 |
| S1-02/source-manifest.txt | eb5643db6fa41b5a579db7149ed25c309f5956e8e5530c3f9df0909730d5b5a3 |

在 Windows 对清单全部 30 文件重算原始字节 SHA-256 与大小：**30/30 一致**；根 CMakeLists.txt + host/storage/ + tests/unit/storage/ 范围内无清单遗漏文件。清单只能证明此时文件与交付指纹一致，不能证明历史修改全集。

git status --short 实测退出码 **128**，stderr 为 fatal: not a git repository (or any of the parent directories): .git。未创建 Git 仓库，也未用猜测的 git diff 替代证据。

S1-01 清单中的其他 28 文件均仍匹配；从当前根 CMakeLists.txt 在内存去除两个 storage 的 add_subdirectory 行后，得到 3871 B、SHA-256=25d9ca8c06e859a8b8ab85146b15bde3c740aaef215abd71d24d23a4b2db55f7，与 S1-01 基线相同。因此当前共享构建差异可确认为两行接入；历史负责人授权过程无法由 Git 独立重建。

schema_records.json 的 47 个 defs 与 18 个 collection 映射已逐项对比当前内部数据 schema，去 description 后结构一致，无缺失/内容漂移。此结果只覆盖当前冻结副本。

## 2. 阻塞缺陷

以下 C 路径结论来自源码和契约对照，**C 端最小复现均未运行（NOT_RUN）**。给出的输入是供整改负责人直接加入正式回归的复现步骤，不伪称已观察到 C 程序输出。B3/B4 另有本轮实际执行的 SQL/字节计算佐证，见第 5 节；它们也不是 C 端运行结果。

### B1 / P1：严格递增被当作连续，且已有游标忽略新的缺口标志

- 位置：host/storage/src/repository.c:1356–1359、1408–1414、1444–1455、1478–1494；辅助判断在 :1243–1259。
- 依据：本卡明确要求“游标不能越过缺口、倒退”；internal-data 的 x-rules 第 11 条要求连续推进；uart-v1.md:370–372 规定缺口不证明完整历史。
- 复现 A：新库，同设备/epoch/boot=1，一页包含 seq=1、3，设置正确 last_committed=(1,3)、gap_before_page=false，投影指针 NULL。页内比较只要求大于前条，随后只用 first_new=(1,1) 判断首次连续；代码会提交 1、3，保存 through=(1,3)、continuity_ok=true，遗漏 2。
- 复现 B：先正常导入 (1,1)，再导入 (1,2)，第二页 gap_before_page=true、边界合法。stored_exists 分支直接继承 stored_continuity，完全不消费本页缺口标志，仍把连续性保存为 true。已有游标不能抹掉新发现的历史缺口。
- 复现 C：按现有正式测试允许的首次缺口方式导入 (1,5)，continuity_ok=false；随后补到此前缺失的 (1,1)、(1,2)。first_new.seq=1 满足 <= stored.seq+1，最终 last_key=(1,2) 会覆盖 (1,5)，同 boot 水位倒退。缺陷不需要并发或外部 SQL 删除。
- 影响：可能形成跨缺口的“已持久化”水位，或丢失缺口锁存、倒退 ACK 依据，破坏恢复/补取屏障。
- 修复建议：分别校验页内相邻记录、已存水位与新记录的连续关系；区分历史回填与向前推进，保存水位不得倒退。消费并锁存 gap_before_page。需要保存不完整历史时保持显式不连续，不能把可用记录末尾当作已证明连续水位；语义有歧义交契约负责人统一，不自行放宽。
- 测试缺口：test_repository.c:975 起覆盖单条跨页跳号、乱序和首次 gap，没有覆盖“页内递增但缺号”、已有游标收到 gap=true、同 boot 历史回填。三组均需正式回归，并核对事件/投影/游标的原子结果。
- 级别：阻塞，P1。

### B2 / P1：last_committed 可被零游标或另一设备/epoch 绕过

- 位置：host/storage/src/repository.c:1425–1431；host/storage/src/event_key.c:163 起的比较先比较 device_id/epoch；tests/unit/storage/test_repository.c:317–330。
- 复现 A：新库 count=1，事件为合法 (1,1)，batch.last_committed 保持零值，projection=NULL。代码以 is_zero 为条件跳过整个上界检查，随后照常提交并返回非零 committed_cursor。非空事件页不能以空日志尾证明事件已提交。
- 复现 B：事件 device_id 为 24 个 '1'，last_committed 的 device_id 为 24 个 'f'，其余填合法 epoch/游标。比较函数先比较设备文本，因此 last_key < last_committed，无须证明它属于同一日志就通过。换成排序更大的其他 epoch 也存在同类问题。
- 影响：可把未由该设备/epoch 已提交边界覆盖的事件当作持久事实，返回错误 ACK 依据。first_available/next_after 等边界也没有形成完整一致性校验。
- 修复建议：先验证批次边界的结构、设备/epoch 归属和范围，再在同一日志内比较数值水位；明确非空页与零尾、首尾/next_after 不一致的错误语义并拒绝，失败保持原事件/投影/水位不变。不能通过忽略这些字段让测试通过。
- 测试缺口：当前 import_events 测试助手 memset batch 后从不填写 last_committed/first_available/next_after，大部分成功场景实际走跳过上界检查分支。应让正常夹具具有真实合法边界，再增加上述负例。
- 级别：阻塞，P1。

### B3 / P1：缺少 documents 表被视作首次初始化，可悄悄恢复为空账本

- 位置：host/storage/src/repository.c:1608–1625；host/storage/schema-v1.sql:19–43；test_repository.c:1330–1344。
- 复现：初始化库并留下 schema_version=1、migrations version=1、snapshot_revision=7；构造部分损坏夹具，移除 documents 表但保留这些元数据。再次以 initialize_empty=true 打开。实现仅以 documents 是否存在决定初始化，执行 CREATE TABLE IF NOT EXISTS，并用 INSERT OR IGNORE 保留旧 revision=7；元数据检查和 quick_check 都能通过，空 documents 被当作可用旧库。
- 另一路径：既有 meta 的 schema_version=2、缺 documents，initialize_empty=true 时先执行建表/插入 migrations，再发现版本冲突。DDL 整体没有显式事务，失败前已经改变旧库，不能保证初始化失败原样保留。
- 影响：损坏或不完整迁移可被当成首次空库，用户/借用/未决操作等记录缺失被掩盖；初始化中途失败也可留下半套结构。
- 修复建议：区分真正未初始化的空目标与带既有业务/迁移痕迹的库；既有部分结构/不兼容元数据明确失败并保留。首次 DDL 和元数据写入置于完整事务，校验失败回滚；不得以重建 documents 处理旧库损坏。并校验必要表结构与迁移元数据。
- 佐证：本轮 Python SQLite 内存库执行同一 DDL，部分结构反例得到 documents_rows=0、revision=7、version=1、migration_v1_exists=true、quick_check=ok。此为 SQL 路径佐证，非仓储 C 执行。
- 测试缺口：现有 bare.db 场景只用 initialize_empty=false；损坏非 SQLite 文件和单独改版本的测试无法覆盖上述结构损坏初始化路径。
- 级别：阻塞，P1。

### B4 / P2：分页只累计 key+value 字节，正常记录集合会返回 NO_SPACE 而非缩页

- 位置：host/storage/src/repository.c:939–959、1140–1165、559–575。
- 依据：internal-data 的 x-rules 第 9 条要求页受 limit 和 65535 字节限制，未读完返回 next_after_key。
- 复现：分事务写入 100 条合法 audits，id/key 依次为 %08x-2222-4333-8444-555555555555，actor_id/operation_id/occurred_at/source_event=null，type=T，reason 为 256 个“验”，time_quality=UNSYNCED；以 limit=100 查询或恢复。每次提交最多 32 条，单次 JSON 仍可容纳。
- 源码路径：collect_page 只计算 key_bytes+value_bytes，遗漏 collection/key/value 的 JSON 包装、逗号、转义、页头和 next_after_key。该输入会选择 67 条，计数 65191 B，而完整查询结果 JSON 为 68119 B；write_json_result 最后返回 NO_SPACE，整页和续页游标都不返回。每条记录远小于 64 KiB，缩小页即可成功。
- 本轮佐证：复用项目 Python validate_value 确认 100 条 RepoDocument 和结果页结构均满足当前契约，再按相同累计公式与 UTF-8 紧凑 JSON 计算，得到上述数字。不是 C 程序实测。
- 影响：记录增长后正常分页/启动恢复可能失败；调用方给合法 limit 也无法获得服务端应提供的有界页。该问题不同于“单条记录过大”的已知限制。
- 修复建议：按最终序列化预算选取记录，预留页头、完整文档、续页键和 NUL 空间；容不下下一条时返回已装入的有效非空页及续页键。只有连一条完整文档都容不下时返回 NO_SPACE。query/recovery 都覆盖中文、转义和边界长度。
- 级别：阻塞，P2。

### B5 / P2：escaped NUL 被 cJSON 解码后截断，非法 U64 可通过仓储校验

- 位置：host/storage/src/repository.c:413–440、443–448；host/storage/src/schema.c:264、297–298、471–474；vendored cJSON.c:714–810、905–926。
- 复现：构造其余字段合法的 RepositoryChange，expected_snapshot_revision 为 JSON 字符串 "0\u0000junk"，new_snapshot_revision="1"，mutations PUT 一个合法 slot_guards 文档；设置 length 为完整原始 JSON 长度。真源 U64 不允许该文本。
- 源码路径：cJSON 支持把 Unicode U+0000 写进 valuestring，继续消费原始串的 junk 和结尾；require_complete 检查仍通过。随后 schema/U64/字符串比较都用 strlen 或 NUL 终止接口，只看到 "0"，因此非法输入被当作合法 revision=0，且序列化会丢掉后缀。相同问题可影响文档 key 和记录字段。
- 影响：不符合契约的数据可能静默变成另一份合法数据，违反“已校验十进制文本”和精确 key/值语义。这里没有声称存在公开 HTTP 直达仓储；这是内部 DTO 校验与持久化完整性缺陷。
- 修复建议：在 cJSON 丢失显式长度之前，拒绝无法无损表示的解码 NUL（键和值），或采用能保留长度的校验路径；不能修改原始第三方库掩盖适配问题。原始转义扫描须正确区分字面反斜杠和真正 Unicode 转义，不能简单按子串误拒合法文本。
- 正式回归应断言 INVALID、revision/所有文档不变，并覆盖 NUL 在开头/中间、属性名、key、U64 与合法转义反斜杠。C 端复现本轮 NOT_RUN。
- 级别：阻塞，P2。

## 3. 非阻塞问题及交接勘误

这些项不单独作为本次运行正确性 FAIL 的理由；收尾负责人仍须按职责处理，不能让旧说明误导下一 AI。

| ID / 级别 | 位置、触发与影响 | 建议 |
|---|---|---|
| N1 / P2 | handoff.md:159–165 用 length=0 的 projection 并传 &projection 表示“无投影”；repository.c:1457–1460 对新事件明确返回 INVALID。按示例调用无法导入新事实。 | 文档无投影应传 NULL；补一个可编译运行的完整示例，明确 OK 与 continuity_ok/可 ACK 条件的区别，不写“任何 OK 都可 ACK”。 |
| N2 / P2 | summary.md:223、handoff.md:458 声称验证到“进程崩溃后重开”；test_repository.c:767 起是正常 close/reopen；唯一 fork 场景为竞争，未找到 kill/SIGKILL 或故障退出重启证据。 | 将描述限定为实际正常重开；若保留进程崩溃结论，补正式异常退出/重开日志。真实介质掉电仍 NOT_RUN。 |
| N3 / P2 | test_repository.c:570–581 使用非法 outbox state，RepositoryChange 在 repository.c:596 就被拒绝，尚未 BEGIN；因此该断言不直接证明“写入一半发生 SQL 错误”后的 commit_change 回滚。现有事件导入投影冲突确实会走插入后的回滚，但不等价于覆盖全部 SQL 故障。 | 增加仅测试夹具中的 SQLite trigger/约束或其他可控写失败，使第一条有效写已发生、第二条 SQL 失败；验证所有记录与 revision 不变。另补同连接同步策略的回归观测，现有 m6_sync 变异漏检如实保留；不需要公共调试 API。 |
| N4 / P3 | deployment/host/dependencies.lock.json 与 deployment/host/README.md 保留“尚未 vendor”、cJSON 消费卡仅 S1-05 的旧说明；host/storage/README.md 缺失，说明仅在证据 handoff。 | 由集成负责人同步真实使用者/落盘路径和模块入口；本轮审计不越权修改。既有许可原文与固定版本可复用，不重复下载。 |
| N5 / P3 | host/storage/src/u64.c:5–72、schema.c:259–284 重复实现 host/common/src/numeric.c 已有的规范十进制校验；冻结 schema 的抽取工具未保留且无自动真源比对门禁。当前冻结副本确实一致。 | 优先薄适配复用 common，定宽事件键补零可保留专用编码；保留可重复抽取或等价检查，防止同为 0.2.1 的字段变动绕过仅版本比较。本项不要求引入新框架。 |

WAL/FULL 已存在真实配置代码；未把缺少 FULL 断言直接说成当前配置为 OFF。但 journal_mode 仅检查 sqlite3_exec 返回码而不核实返回值，WAL 被拒绝或退化的路径没有独立执行；应在相关错误路径回归中核对。

## 4. 按卡评价、接口与生命周期

| 卡要求/边界 | 本轮评价 |
|---|---|
| 唯一 DDL、受控集合、六个契约端口 | 源码存在且使用正式 DTO；冻结 schema 当前与真源一致。未发现新造跨模块业务 API；C 头签名没有漂移。 |
| 操作/幂等/outbox 原子提交 | 源码为 BEGIN IMMEDIATE → checks/revision → mutations → revision → COMMIT，失败 rollback；参数绑定，无 SQL 字符串注入入口。真实故障执行本轮 NOT_RUN；见 N3。 |
| 新事件/投影/游标同提交、重复去重 | 有单事务与去重路径；全部重复时不应用投影。连续水位和批次边界存在 B1/B2，不能按通过交接。混合重复/新记录、缺口回填等需补回归。 |
| 竞争只能一个成功 | 有事务内 revision/ABSENT 检查；正式测试含两个 fork 子进程各自开库。历史结果通过，本轮并发执行 NOT_RUN。 |
| 重开保留借用/操作/幂等 | 正式 close/reopen 测试存在；本轮执行 NOT_RUN，异常崩溃/介质掉电不能由它推定。 |
| 初始化与损坏分离 | 非 SQLite 文件/版本不符有拒绝路径；部分 schema 初始化存在 B3，FAIL。 |
| query/分页恢复与资源上限 | 限定 18 集合、limit=1..100、65536 B 输出，key/after_key 互斥；完整页预算存在 B4。SQL statement 与 JSON 分配均有清理路径，OOM/磁盘满注入未执行。 |
| u64/事件顺序 | 正常规范十进制解析有溢出检查，事件文档键 20 位补零，不经浮点；escaped NUL 无损校验存在 B5。 |
| 生命周期/线程/回调 | repo 由 open 分配、close 释放；失败 *out=NULL，close(NULL) 安全。Core 单所有者调用、Runtime 独占构造/关闭；close 不得并发。无后台线程/用户回调/串口/socket/GPIO。不能把 SQLite THREADSAFE=1 当成 cJSON 或整体 repo 可多线程并发调用的许可。 |
| 超时/错误 | SQLite busy_timeout=5000ms；BUSY/LOCKED→BUSY，FULL/NOMEM→NO_SPACE，其他按失败返回。无异步超时取消/自动动作重放。该等待可能阻塞调用者，后续 Core 必须按既定模型处理，不在本卡扩写调度器。 |
| 权限/副作用 | 仓储只持久化 Core 变化，未见借还决策、锁动作、公共调试/模拟入口、默认凭据或 HTTP/UI 直达；未触碰 HAL/vendor 原库和 equipment-vision。 |
| 平台/硬件 | x86 Linux 的历史测试不代表 ARM ABI、RK eMMC/SD 掉电、MCU/锁/相机/NPU/Android 已实现或已验收。 |

## 5. 本轮实际执行与 NOT_RUN

Windows Python 为 3.14.6（Anaconda，AMD64）。普通命令执行工具启动失败：Failed to create unified exec process / helper_unknown_error: setup refresh had errors；随后用可用 Node 文件/子进程工具完成本地读取、SHA 和 Python 检查，未提权或修改系统配置。

| 实际命令/检查 | 退出码/结果 |
|---|---|
| git status --short | 128；非 Git 仓库 |
| python -B scripts/check_contracts.py | 0，PASS，0.2.1，24 routes / 82 ports，hardware_profile=UNVERIFIED，business_or_hardware_tested=false |
| python -B scripts/check_stage_plan.py | 0，PASS，45 cards，active_task=S1-02，implementation_cards_done=1，runtime_or_hardware_tested=false |
| python -B scripts/check_vision_contracts.py | 0，PASS，camera_owned=false，rknn_available=false，hardware_tested=false |
| python -B scripts/check_feasibility.py | 0，PASS，runtime_or_hardware_tested=false |
| SHA-256/大小及冻结 schema 结构比对 | 30/30 文件匹配；当前 47 defs / 18 collections 匹配 |
| ssh -o BatchMode=yes -o ConnectTimeout=10 locker-vm 'uname -a; cat "$HOME/locker-workspace/WORKSPACE-INDEX.md"' | 255，stdout/stderr 均为空；未取得 VM 索引或路径验证结果 |
| ssh -V | 255，stdout/stderr 均为空；不能据此断言 VM 关机或网络故障 |
| wsl.exe -e bash -lc 'uname -a; test -d /mnt/e/stm32项目 && printf "SOURCE_EXISTS\n"' | 4294967295（0xffffffff）；后续最小命令确认 Wsl/Service/CreateInstance/E_ACCESSDENIED，拒绝访问 |
| where.exe cmake / clang / gcc（分别执行） | 各 1，当前 PATH 未找到；未安装另一套工具链 |
| python -B -X utf8 -c <本节 SQL/字节计算脚本> | 0；输出如下，仅为辅助计算 |

辅助计算使用项目 scripts/check_contracts.py 的 validate_value 检查 B4 文档与结果结构，紧凑 UTF-8 JSON，按 collect_page 原公式累加；B3 使用 Python 自带 SQLite 3.53.2 的内存库执行当前 schema-v1.sql、置 revision=7、DROP documents 再执行同一 DDL。未生成文件，没有用 Python 替代仓储功能实现。真实输出：

~~~text
PAGE_MODEL {"rows": 67, "counted_bytes": 65191, "serialized_bytes": 68119, "capacity": 65536, "all_docs_contract_valid": true}
INIT_SQL_MODEL {"documents_rows": 0, "revision": "7", "version": "1", "migration_v1_exists": true, "quick_check": "ok", "sqlite_version": "3.53.2"}
NOTE=Python contract/SQL calculation only; C repository execution NOT_RUN
~~~

本轮 **未执行** 以下 Linux 命令，退出码均为“不适用（未启动）”，不是 0，也不是测试失败退出码：

~~~bash
# VM 可访问后，先读 WORKSPACE-INDEX.md 并核对 S1-02 当前快照与本地清单；
# 下述 src 仅来自开发日志，尚未在本轮 VM 访问中确认，不得直接改用旧顶层 src。
src="$HOME/locker-workspace/S1-02/src"
run_id="independent-audit-$(date +%Y%m%d-%H%M%S)"
build_dir="$HOME/locker-workspace/build/S1-02/$run_id"
log_dir="$HOME/locker-workspace/logs/S1-02/$run_id"
# 核对固定快照后才创建目录/配置；所有生成物在 src 与 HOME 根之外。
mkdir -p "$build_dir" "$log_dir"
cd "$src"
cmake -S . -B "$build_dir" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
cmake --build "$build_dir" -j2
ctest --test-dir "$build_dir" --output-on-failure
~~~

未向 VM/WSL 写入文件、未覆盖活跃快照、未改网络或 SSH 主机密钥设置。VM 的 WORKSPACE-INDEX、当前 S1-02 快照指纹和文件系统确认本轮均 NOT_RUN；host-environment.json 的 S1-01 路径不能当作 S1-02 快照。后续应在 build/S1-02、logs/S1-02、artifacts/S1-02 归档新批次，src 只放源码。

历史证据读取结果：VM run_id=20261024-014545 日志确实写有 configure/build/CTest exit=0、11/11；ASan+UBSan 和 TSan 各 11/11、SANITIZER_REPORTS=0；WSL ctest.log 也记录 11/11。mutation.log 的 m1..m5 为 TEST=8，m6_sync 为 TEST=0。保留其历史身份，本轮未重新运行、未取得 VM 原始快照作独立比对，不能将它们复制成新的 PASS。30 文件本地匹配也不能证明 VM 的 620 文件仍匹配。

本轮 C 端 B1–B5 复现、正式 CTest、ASan/UBSan、TSan、mutation、真实 SQL I/O 故障注入均 **NOT_RUN**。RK 介质掉电/fsync、ARM/32 位、CMake 3.16 下限以及全部硬件能力继续 **NOT_RUN**。单卡 FAIL 与这些未运行项分别记录，不把缺环境当成源码无缺陷的依据。

## 6. 文档、残留与交接决定

本轮只创建正式审计文档。未留下临时脚本、mock、构建目录、DB/socket 或调试输出；host/storage/ 与 tests/unit/storage/ 扫描未见 .o/.a/.so/.exe/.obj/.tmp/.bak/.orig/.pyc/DB/socket/CMakeCache 残留。内存计算脚本随工具调用结束，没有落盘。

开发报告列出的旧 /var/tmp/s102_*、%TEMP%/s102_* 和 VM 保留产物，本轮没有可靠访问或归属核验，未删除，也不能声称已清理；由开发负责人按范围核对。正式测试、既有诊断/证据及第三方源码保持原样。需要同步的过期说明与交接示例已在 N1–N5 列明；依据本次“仅写 audit.md”授权，由对应负责人修正，不由审计 AI 越权更新其他入口。

交接条件：由 S1-02 开发负责人复现并修复 B1–B5，保留正式失败/回归证据、同步源码清单和 summary/handoff；取得可用 Linux 环境后核对固定快照，全新构建并执行 CTest 和上述针对性回归，再交独立复审。未解决阻塞项时不得 DONE，不将阶段 1 设 PASS，不自动开始下一卡。
