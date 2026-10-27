# 阶段 4：独立记录、真实进度与固定 UI 监护

状态：阶段性设计，尚未执行。前置为 S3-07 最终验收和 S2-08 的 HAL/真实存储/硬截止证据。本阶段不重做阶段 2 的安全底座，不扩展柜格、云端或材料预算。

当前单卡执行规则见[阶段导航](README.md)。技术说明见[监护接口](../architecture/monitoring-interfaces.md)，机器真源仍为 `contracts/`。每次只交接一张任务卡：未完成卡的证据不得用“代码生成”代替，依赖卡未通过不启动其下游。

## 阶段边界与依赖

| 卡 ID | 本卡唯一目标 | 前置 |
|---|---|---|
| S4-01 | 已有 Journal 的容量、保留与 ACK 回收 | S3-07、S2-08 |
| S4-02 | ADC 供电变化的可信事实与时间关联 | S4-01 |
| S4-03 | MCU 各任务真实进展和 IWDG | S4-02 |
| S4-04 | Core/UI 进度接线与健康副本搬运 | S4-03；0.2.1监督观察真源检查通过 |
| S4-05 | 固定 UI 的限次监督和恢复 | S4-04 |
| S4-06 | 健康、历史补传与账本隔离接线 | S4-05 |
| S4-07 | 日志/进程/供电故障矩阵 | S4-06 |
| S4-08 | 独立阶段审计与交接 | S4-07 |

S2-08未证明意图持久化早于脉冲、硬截止独立于调度和真实littlefs时，回到阶段2修复；禁止以阶段4“以后补日志”放行真锁。supervisor观察回传统一使用0.2.1受限只读health.get，实施卡不得自行发明额外RPC。

## S4-01：日志容量、保留与可重复回收

- **前置/输入**：上表前置；阶段 2 实测 Flash 几何、文件格式/检查点、共享 SPI 延迟、Journal sink；UART FETCH/ACK 布局和阶段 1 事件/游标事务。
- **允许目录**：`firmware/journal/`、该模块私有测试、`tests/integration/monitoring/`、`docs/evidence/stage-04/S4-01/`；模块 README。不能改普通 API、线布局或根构建。
- **接口/调用链**：`mcu_journal_read_page(journal,after,limit,result)` 最多 4 条；`mcu_journal_ack(journal,cursor)` 在 Storage 独占执行。`repo_import_events` 提交 → `device_ack_persisted` → ACK_PERSISTED → 持久 ACK → 逐段安全回收。私有容量/回收 helper 签名按接口文档第 3 节。
- **产物**：含元数据/检查点预留的容量表、字节与文件数上限、已 ACK 完整段回收、不可回收未决引用、满容量拒绝策略；有界诊断日志轮转。
- **验收**：未 ACK 不删；ACK 倒退/越界/跨缺口拒绝；ACK 丢失多保留；回收前/中/后掉电不回退动作水位；空间不足时新 EXECUTE 被 LOG_UNAVAILABLE 拒绝。满容量期间丢失事实可见，event_seq 连续不能掩盖 dropped_events/连续性失效。
- **完成证据**：实际容量/预留依据、填满到边界前后文件索引、持久游标与保留记录、回收故障注入日志；区分主机文件后端测试和真实 Flash 结果。
- **禁止越界**：新建另一套 Journal、按时间删除未 ACK 记录、mount失败format、回收未决/去重检查点、删 SQLite/WAL 来“释放空间”、报告未经测试的保留天数。

## S4-02：供电类别、滞回和故障关联

- **前置/输入**：S4-01；阶段 2 ADC 校准、分压/额定输入与 supply_valid；Control 单写物理版本规则；原始单调时间与 TIME_ANCHOR。
- **允许目录**：`firmware/control/`、`firmware/drivers/adc/`、`firmware/config/` 的已核验参数、相应模块测试、`docs/evidence/stage-04/S4-02/`；参数调整不得占用锁截止定时器。
- **接口/调用链**：`mcu_board_read_inputs(locker_board_inputs_t *result)` → `mcu_control_observe_inputs(control,inputs)` → `mcu_journal_append(journal,event,token)` → SUPPLY_CHANGED 事件 → DEVICE_EVENT/FETCH_EVENTS → Core 唯一事实事务。
- **产物**：供电类别编号映射、实际阈值/滞回/持续时间、样本过期判据、UNKNOWN传播、同boot单调时间关联说明；只解释观察不自动诊断根因。
- **验收**：正常噪声不刷屏/不连续涨物理版本；越界/过期为未知；有效0mV不当未知；电压异常关闭新动作资格；恢复不清日志缺口/未知操作。突断 MCU 电源不得声称掉电前最后事件必落盘或断电期间可记录。
- **完成证据**：标定与真实输入对照、类别转换与event_seq/physical_revision时间线、ADC无效/过期场景、供电与主机失联时间质量记录；无实测不能标VERIFIED。
- **禁止越界**：凭12V标签猜阈值、更新公开schema/添加云端分析、以到达时间改原始mono_ms、把“供电变化同时出现”写成已证明原因、增加UPS或新传感器。

## S4-03：MCU 真实任务进展与 IWDG

- **前置/输入**：S4-02；Control/Communication/PeripheralStorage/Health 实际周期，最坏擦写延迟与阶段 2 硬截止证据；本地 HAL V1.8.5 IWDG。
- **允许目录**：`firmware/health/`、`firmware/tasks/` 的进度点、`firmware/hal/` 的私有IWDG适配、模块测试、`docs/evidence/stage-04/S4-03/`；不修改原HAL库。
- **接口/调用链**：`mcu_health_report_progress(health,task_id,progress)` 报告已完成循环 → `mcu_health_evaluate(health,monotonic_ms)` 检查进度期限 → Health 私有HAL刷新。退化事实用既有HEALTH_CHANGED/JOURNAL_FAULT，经Control/Storage有界路径记录；Health不能直接抢占littlefs或锁。
- **产物**：统一4任务私有ID表、64位原子副本、任务期限及测量依据、唯一IWDG刷新点、复位原因记录、状态变化去抖。主机离线与任务卡死分开。
- **验收**：冻结任一关键任务即停止有效进展，Health撤销喂狗；Health自身卡住由IWDG超时；重复progress/中断Tick不能保持健康；空闲串口/空日志队列正常推进。主机断线或可处理日志满不能使MCU无故复位；锁截止不随冻结延长。
- **完成证据**：各任务冻结条件、IWDG配置/LSI误差与实际复位窗口、复位后安全输出与boot记录、对照“仅失联不复位”数据；调试冻结状态单列。
- **禁止越界**：ISR/定时器直接喂狗、一个线程替所有任务加计数、因Flash忙无限延长期限、自动主机复位、把IWDG当成锁脉冲硬截止。

## S4-04：真实 Core/UI 进度及健康搬运

- **前置/输入**：S4-03；阶段3已有UI真实进度生产与非阻塞快照接线；0.2.1 health.get、SupervisorHealthResponse、C端口、Core观察入口及样例检查通过。源或接线缺失时回契约负责人，不实现越界通道，不标本卡完成。
- **允许目录**：`host/runtime/`、`terminal-ui/`的进度/IPC、`host/supervisor/`观察缓存、`host/domain/`的core_apply_supervisor_health观察入口和投影、相应测试及`docs/evidence/stage-04/S4-04/`；不重写借还状态机，共享定义只消费生成契约。
- **接口/调用链**：Core实际回路→get_progress→Runtime→progress.report；UI报告同源不可变样本；supervisor_report_progress接收。service→health.get→supervisor_get_health→core_apply_supervisor_health入队→Core唯一更新Device投影。HOST_HEALTH的Core始终直取get_progress，UI取监督源样本，各累计源年龄一次→device_send_health；监督缺席不覆盖直接Core健康。
- **产物**：已有真实进度生产点的接线、原始sampled_mono_ms/年龄搬运、未知与溢出处理、5秒停滞判据；可独立启动的supervisor观察模式、socket权限/实例登记/查询及生命周期，恢复功能保持禁用。此卡不等待S4-05的子进程恢复才能验收。
- **验收**：观察模式可启动、停止、查询，任意恢复请求明确拒绝；阻塞业务Core而HTTP线程仍活时应判Core停滞；阻塞LVGL而IPC线程仍活时应判UI停滞；重复值不刷新进展，旧实例晚到不复活；在socket队列延迟后收到的旧progress仍过期。未知年龄映射UINT32_MAX，缺席的UI/supervisor只告警；新动作主机健康前置仍取真实Core新鲜度。UI等待服务期间循环继续推进；启动宽限不授予READY。
- **完成证据**：生产点代码索引、冻结/正常两组进度时间线、supervisor接收样本与HOST_HEALTH字节解码、年龄上界计算、0.2.1真源指纹与入口映射。
- **禁止越界**：新增定时器假进度、supervisor共享UART、service直读监督私有文件、把ACK当整体健康、未取得UI样本时填0伪装健康、放宽证书或peer检查。

## S4-05：固定 UI 监督、限次和恢复确认

- **前置/输入**：S4-04；目标机init/固定UI程序位置与UID；`locker_supervisor_config_t`、ProgressReport、RestartRequest、SupervisorRpcAck；状态与检查点规则。
- **允许目录**：`host/supervisor/`、`deployment/host/` 的监督配置、监督模块测试、`docs/evidence/stage-04/S4-05/`；不修改业务数据库schema。
- **接口/调用链**：`locker_supervisor_create(config,out)` → 监督循环；私有 `ui.restart.request` → `locker_supervisor_request_ui_restart(supervisor,request)` → incident/额度持久化 → 确认旧UI退出 → 固定exec新UI → 登录页可交互+真实完整循环两次新鲜递增 → OBSERVED_RUNNING；登录/重连后业务按钮另由fresh getSnapshot门禁。停止循环及工人后destroy。
- **产物**：在S4-04观察模式上启用固定UI子进程监督；固定程序/参数/环境白名单、受控子进程身份、10分钟最多3次/60秒冷却、30秒UI启动宽限、独立恢复检查点、同incident回查、检查点损坏的LIMITED状态。
- **验收**：伪造peer/实例/任意target拒绝；同incident不重复拉起；并发故障不生成两份UI；监督器自身重启仍限次；检查点写失败不拉起；冷却和限次有效；进程创建成功不能当恢复。新UI清旧token回登录页，progress从0开始，登录页可交互且完整真实UI循环完成即可非零；期望新instance至少两次非零严格增加且源样本新鲜，30秒启动宽限内判OBSERVED_RUNNING，无需用户登录。该状态不代表业务同步，登录/重连后须fresh getSnapshot才开业务按钮；不新增匿名snapshot。service不健康或无人登录时不反复恢复UI；新UI不重放presence/确认。
- **完成证据**：受控PID/进程身份、恢复前后的instance/进度、限次窗口与掉电/重启检查点、错误peer和重复incident测试、UI实际恢复后的界面与当前业务状态。
- **禁止越界**：自动重启service、调用shell、按手机路径exec、删除监督检查点绕过限次、自动Linux断电/硬复位、UI重启时发开锁/重建借还操作。

## S4-06：监护事实、历史补传与业务接线

- **前置/输入**：S4-05；既有event键/连续游标、LIVE/REPLAY区分、UI_RESTART_REQUEST事实、私有maintenance.reconcile与管理员resolve规则。
- **允许目录**：`host/domain/`、`host/device/`、`host/runtime/` 的既有端口接线、相关正式测试、`docs/evidence/stage-04/S4-06/`；仓储变更须交唯一Storage所有者评估，不另开写连接。
- **接口/调用链**：Device sink → `locker_core_apply_device_event(core,event)` → `locker_repo_import_events(repo,batch,projection_change,cursor)` → `device_ack_persisted`。仅当前LIVE/新鲜UI恢复事实可映射持久incident → supervisor请求；REPLAY只补记录。人工重启service先HELLO/屏障/FETCH/落库/ACK/核对/RESUME_READY。
- **产物**：健康原因到公开Device投影、ui_restart_count确切来源、UUIDv5完整事件键到incident确定性映射、恢复中业务保留、日志缺口和未决动作核验路径；App沿用既有GET/WS重查。
- **验收**：同事实重复导入只落一次；历史UI恢复请求不重启UI、不生成柜前证明、不触锁；旧session晚到被拒；DB事务失败不ACK；服务重启不重发EXECUTE；UI恢复不改账本。尾杆访问仍hold，维护不清损坏/连续性故障。
- **完成证据**：同一故障LIVE/REPLAY对照、数据库事件/游标/incident索引、设备发送计数、未决操作保持和人工核验审计、公开查询的权限与敏感字段检查。
- **禁止越界**：以健康恢复完成借还、以新session清日志故障、自动放弃占位/未决操作、历史补传触发任何动作、公开HTTP增加重启/清日志/强制READY接口。

## S4-07：故障矩阵与独立记录实物验证

- **前置/输入**：S4-06；S2-08已核准接线/保护/硬截止；以上各卡的版本/配置/正式测试；不购置预算外器件。
- **允许目录**：`tests/integration/monitoring/`、`tests/fixtures/monitoring/`、`docs/evidence/stage-04/S4-07/`；测试注入入口保持私有且生产禁用，不开放HTTPS。
- **接口/调用链**：仅通过前卡公开/内部既有端口和受控测试适配器注入；正式设备走真实UART/Flash，测试替身结果单列。
- **产物**：故障矩阵覆盖：Core卡住、UI卡住/UI崩溃、监督器重启、MCU任一关键任务冻结、主机失电、串口拔断、Flash满/故障/回收中断、ADC未知/波动、历史补传、ACK丢失、同时发生故障。
- **验收**：每场景同时判定锁脉冲上界、账本/操作、日志连续性、恢复对象/次数、状态可见性；冻结任务仍满足阶段2硬截止。不能写事件时明确丢失/未知，不能以无记录认定无故障；主机恢复后先核对，历史不动作。
- **完成证据**：逐场景步骤/时间/版本/真实或模拟标识、原始串口与事实记录、文件/DB检查点、脉冲波形和复位窗口、期望/实际/差异。失败保留原始诊断，不能删日志重测后仅报成功。
- **禁止越界**：短接电源等破坏性注入、未经S2核准真锁测试、放宽阈值掩盖卡死、模拟结果冒充实物、以自动重启service令测试通过。

## S4-08：阶段审计与交接

- **前置/输入**：S4-07完成；S4-01..07证据和0.2.1监督观察接口检查；当前契约/生成器及阶段2安全证据。
- **允许目录**：`docs/evidence/stage-04/`、阶段/模块README和本阶段设计说明；审计发现实现问题回所属卡处理，审计卡不顺手扩功能。
- **接口/调用链**：逐条追踪“原始输入→生产进度/事实→持久化→观察搬运→恢复/拒绝→公开投影”；对照统一C/JSON/UART真源，不以单端能编译当三端一致。
- **产物**：通过/未通过/未测项表、架构偏移检查、接口变更清单、容量与任务期限测量结论、后续阶段前置清单。
- **验收**：契约检查通过；正式测试和实物矩阵有证据；无假进度、历史动作、自动service/主机复位、未ACK回收、格式化掩盖故障、直接锁入口。所有未决缺口有owner与阻塞范围，未通过项不能标DONE。
- **完成证据**：`docs/evidence/stage-04/summary.md`、每卡证据索引、契约/固件/主机/UI版本和指纹、预算变化（应无新增器件）、临时残留检查。保留正式日志/fixture/测试，不修改用户原HAL包。
- **禁止越界**：声称未执行测试通过、把生成声明当实现、删失败证据、自动commit/push、把下一阶段整项开发夹入审计卡。

## 阶段退出条件

S4-08审计通过才可进入后续整体验收。交接必须分别报告软件检查、真实硬件验证和未测项：主机离线时 MCU 能继续观察和尽可能持久记录；资源耗尽/不确定性可见且禁新动作；关键任务停滞可检测；仅固定 UI 有限恢复；UI/service重启都不重放锁动作。供电关联不宣称根因诊断，也不宣称在 MCU 断电期间持续记录。
