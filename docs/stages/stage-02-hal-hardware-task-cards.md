# 阶段 2A：HAL 与一个基础柜格

状态：S2-01与S2-02已DONE，S2-03 UART DMA/通信任务已DONE，S2-04真实Journal已DONE，S2-05 Control与硬截止已DONE（空载PB6波形与硬截止通过，真锁/MOS/12V未接），S2-06 RK真实串口/ABI/局域网已DONE（aarch64静态服务、ttyS4真实链路、HTTPS与配对导出通过；物理断线恢复已补证），S2-07台架客户端与主机验证通过，完整实物闭环BLOCKED（供电/profile等证据待补），S2-08未执行；PC0/ADC实物标定按本卡授权后移说明补证，供电资格仍无效。前置S1-07已通过，契约0.2.1。显式DISABLED_FOR_FOUNDATION验收物理底座，再执行[2B视觉八卡](stage-02b-vision-task-cards.md)，S2-16才为阶段2完整门禁。本单元不开发Flutter/LVGL或自动恢复。

## 1. 阶段目标与接口链

交付HAL固件、RK真实UART适配，以及通过正式HTTPS/本机测试客户端操作的一格实物闭环。UI与supervisor未知只告警，主机动作健康资格依据真实Core进展及已有日志/物理/权限前置，不能伪造UI进度。

```text
S2-01 HAL/BSP → S2-02 输入/SPI → S2-03 UART
                            └→ S2-04 真实Journal
S2-03 + S2-04 → S2-05 Control/硬截止
S2-05 → S2-06 RK真实串口/部署 → S2-07 整柜闭环 → S2-08 验证
```

公开入口和状态不变：申请→Core事务→到柜证明→DevicePort→UART EXECUTE_UNLOCK→mcu_control_submit_unlock→mcu_journal_commit_intent→回执→再次核对→mcu_board_start_bounded_pulse。物理事实反向落库，落库后才能ACK。接纳、脉冲、开门、完成借还必须分开。

输入：[阶段0](stage-00-architecture.md)、[内部接口](../architecture/internal-interfaces.md)、[UART](../architecture/uart-v1.md)、[硬件影响](../hardware/材料变更影响与HAL边界.md)、[本地HAL核对](../hardware/本地HAL库核对.md)。C声明及字节布局取机器真源，不复制第二版。

## S2-01：HAL 工程、板型与资源所有权

- **前置/输入**：S1-07；实物板/官方原理图、未验证profile、本地HAL V1.8.5。无实物可完成配置设计，不能把下载/测量验收标通过。
- **允许目录**：firmware/board/、firmware/hal/、firmware/config/、deployment/firmware/；根固件工程由唯一集成负责人维护，不改用户原HAL目录与ZIP。
- **接口/调用链**：启动安全输出→mcu_board_apply_profile→HAL GPIO/DMA/定时器配置→四任务初始化；HAL句柄止于BSP。未验证配置只诊断、不允许真锁。
- **产物**：HAL应用工程、芯片/时钟/引脚/DMA/中断/定时器资源表、静态内存预算和工具链记录。候选VGT6/1MB、8MHz/168MHz须实物核对，不执行课程ZETx/25MHz工程。统一STM32F407xx宏、FreeRTOS异常入口、独立HAL时基；USE_RTOS=0。按组件锁定FreeRTOS/littlefs版本与许可。
- **验收**：SWD下载/断点、上电锁输出默认关闭；HAL时基无冲突；UART DMA缓冲不在CCM；64位时间处理底层计数回绕；未验证profile拒绝驱动。
- **完成证据**：编译器/工程/映射文件、烧录记录、资源冲突检查与原库未修改说明。
- **禁止越界**：不编造空闲引脚、不把one-pulse API存在视为硬截止已实现、不驱动未核对连接。

## S2-02：规范化输入、RC522 与 SPI 仲裁

- **前置/输入**：S2-01；门磁/微动开关/RC522/Flash引脚及额定电平。
- **允许目录**：firmware/drivers/、输入相关firmware/board/、tests/firmware/inputs/。
- **接口/调用链**：HAL输入采样/ADC完成→原始副本→mcu_board_read_inputs→Control去抖/observe_inputs；PeripheralStorage统一调用mcu_board_spi_transfer读取RC522，卡片只产生事实。
- **产物**：门/物品/供电有效性映射、去抖参数、RC522 UID读取、独立片选和SPI仲裁。Flash内部busy期间释放总线，先测延迟再承诺读卡响应。
- **验收**：闭门/开门、有物/无物四项实测；未配置/可诊断采样故障为UNKNOWN；两线无监督电路不能诊断断线，NO断线与未按下、NC断线与开门电气等价，按相应非占位/非闭门事实与视觉冲突处理，不假报断线诊断；UID 4/7/10字节；未授权读卡不开锁；ADC有效范围与毫伏标定。类别阈值细化留S4-02。用户2026-10-29明确授权PC0接线/ADC实物标定后移，不阻塞S2-02软件交付；本卡实现换算与失效保护，实物标定列为S2-08基础门禁前必须补证的待验项，完成前supply_valid保持false，不开放真锁资格。
- **完成证据**：原始电平与规范状态对照、ADC标定、读卡和总线互斥记录。
- **禁止越界**：不按NO/NC猜电平，不用软件输入冒充传感器交付；本卡不接锁线圈、不写借还账本。

## S2-03：HAL UART DMA 与 MCU 通信任务

- **前置/输入**：S2-02；UART布局/黄金帧、RK串口额定IO电平。
- **允许目录**：firmware/transport/、通信相关firmware/tasks/、tests/firmware/transport/。
- **接口/调用链**：HAL circular DMA/IDLE通知→Communication流式解析→有界Control/Storage请求→响应/DEVICE_EVENT；ISR只通知，不运行Flash/printf/业务。
- **产物**：11类请求分派、旧session/CRC/长度拒绝、错误计数、RX环形区和发送背压。未完成Journal/Control的动作请求明确NOT_READY，不固定成功。
- **验收**：半帧、多帧、回绕、错误CRC、超长payload；ReceiveToIdle的Size按DMA位置处理，HT/TC/IDLE重复不重复消费；旧会话不入锁入口；2048字节设计缓冲不越界。
- **完成证据**：串口抓取、黄金帧比较、DMA回绕/重复通知和过载结果。
- **禁止越界**：不把一次回调当一帧，不在解析器驱动锁，不把USB调试通路当RK控制UART。

## S2-04：真实 littlefs 与最小动作 Journal

- **前置/输入**：S2-02；真实Flash几何、Journal声明及回执规则。与S2-03目录独立，默认单卡串行。
- **允许目录**：firmware/journal/、firmware/drivers/flash/、tests/firmware/journal/。
- **接口/调用链**：journal_init→绑定sink→append/commit_intent队列→Storage独占littlefs→sync/close提交→token回执；read_page/ack同样经Storage所有者处理。
- **产物**：epoch/boot、动作水位、未决上下文、连续事实、提交屏障、持久ACK。容量预留/拒绝底线在此实现；阶段4扩完整回收策略。首次初始化为受限现场动作，无公开API；挂载失败不自动format。
- **验收**：意图与水位同提交；失败无成功回执；重启保留已提交记录；未ACK日志不回收；挂载失败/满/连续性失效不可READY。掉电试验先不接锁。
- **完成证据**：Flash几何、持久格式/版本、掉电位置与恢复结果、提交回执及未ACK保护记录。
- **禁止越界**：不用RAM冒充持久化，不先跑真锁再补Journal，不format掩盖损坏。

## S2-05：Control 与锁硬件截止

- **前置/输入**：S2-03、S2-04；锁允许脉冲、电源保护和BSP定时输出已核对。
- **允许目录**：firmware/control/、锁相关firmware/board/、firmware/tasks/、tests/firmware/control/。
- **接口/调用链**：submit_unlock→commit_intent→on_journal_result排队→Control复核session/物理版本/Core新鲜度/期限→start_bounded_pulse；故障force_off。只有Control可启动脉冲。
- **产物**：四任务装配、预执行复核、≤1000ms且不超过实物限值的硬截止、快照屏障、上下文释放。先空载/假负载测定时输出，再接有保护的真锁。
- **验收**：存储阻塞/RTOS延迟时硬件仍关断；复位关闭；准备超过2000ms或not_after截止、物理变化、日志错误均不驱动；已接纳但结果不明不退回水位；重复释放幂等。
- **完成证据**：示波器/逻辑分析波形、故障时最长脉冲、队列时序与水位；日志时间差不能替代实际通电波形。
- **禁止越界**：不用RTOS回调/Linux取消实现硬截止，不把ACCEPTED说成开门，不跳过独立试锁和保护核对。

## S2-06：RK 真实串口、ABI 与首次局域网准备

- **前置/输入**：S2-05；已通过的主机端、实际RK用户态ABI/设备节点/权限/网络。
- **允许目录**：host/device/、host/runtime/、host/cli/、deployment/target/及适配测试；根构建只由集成负责人调整。
- **接口/调用链**：DevicePort切换simulated=false→真实UART 115200 8N1→HELLO/屏障补取/核对/RESUME_READY；真实Core进度→locker_core_get_progress→Runtime→locker_device_send_health。UI未知使用零UUID/age未知哨兵，不虚报RUNNING。
- **产物**：目标机编译/启动配置、设备ID匹配、串口权限与恢复；HTTPS局域网地址、证书SAN及公开配对导出工具，格式见[客户端接口](../architecture/client-interfaces.md)。私钥不导出。首用能力在S3前具备，阶段5固化维护。
- **验收**：产物ABI可执行；串口断接先核对、不重发旧动作；HTTP→Core→真设备可追踪；证书地址/SAN匹配，未信任拒绝；配对文件无token/密码/私钥。
- **完成证据**：目标机/二进制信息、部署命令、UART日志、LAN TLS烟测和脱敏导出样例。
- **禁止越界**：不默认systemd或ARM64 ABI，不关证书校验，不把模拟结果当实物。

## S2-07：一个柜格的完整实物闭环

- **前置/输入**：S2-06；真实硬件台架、供电/保护/已验证profile、测试管理员和普通用户。台架锁体与锁扣需稳定定位、板卡绝缘与线束固定；本卡不要求正式柜体，门磁可通过移动真实磁铁、微动可通过真实工具取放触发，并记录操作方式。不能以软件注入替代物理输入。
- **允许目录**：本阶段模块缺陷修复、tests/hardware/、docs/evidence/stage-02/；共享契约变更走统一流程。
- **接口/调用链**：HTTPS申请→实时卡或受限本机测试客户端presence→唯一command→真实脉冲/开门/物品变化/关门→GET operation→本人confirm；归还对称，落库终结再RELEASE_CONTEXT。
- **产物**：借还、NO_CHANGE、未确认取物待核验、尾杆无指令访问hold和本机maintenance指导。
- **验收**：竞争、只ACK、提前确认、UNKNOWN、取消遇可能执行、主机支路断电、MCU重启、ACK丢失、日志失败；恢复查账本与实物，不重复驱动。
- **完成证据**：operation/command/事件键关联、脱敏录像/照片、脉冲计数、数据库与实物对照；报告明确台架配置与人工操作边界。台架通过不证明柜体安装、防盗或实际门机构通过。正式柜体部署后须补安装验收，纳入S5-05现场验收，不计入当前台架结论。
- **禁止越界**：不把纸箱当防盗柜，不为演示改传感器/删故障，不扩写App/UI。

## S2-08：硬件门禁与交接审计

- **前置/输入**：S2-07及全部真实证据。
- **允许目录**：正式测试、docs/evidence/stage-02/、接线/模块README；缺陷退回所属卡。
- **接口/调用链**：对照API→Core→UART→Journal→锁→事实→事务→ACK各段证据，查旁路/重放。
- **产物**：summary、逐场景PASS/FAIL/NOT_RUN、接线/profile/工程指纹、实付成本和未验证项；测量满足schema才标VERIFIED。
- **验收**：基础各卡通过，硬截止/持久意图/真实Core进度/不重放均有证据，只解锁S2-09。基础汇总为foundation-summary.md，完整阶段summary由S2-16形成。
- **完成证据**：实际构建/静态检查/硬件结果、残留自查。课程原HAL和Objects/Listings不作为本轮证据或临时文件删除。
- **禁止越界**：不用静态checker/模拟掉电替代硬件验收，不自动跨入阶段3。

## 固件回传接线与启动门槛

S2-03冻结mcu_request_context_t，S2-05实际绑定mcu_control_bind_sink后再启任务。submit_unlock/make_snapshot/release_context均复制session/request_no/message_type/received_mono_ms；Control保存关联到Journal提交回执/快照屏障完成，sink.reply把已解码响应按值复制进Communication发送队列。request_no/type/session必须是原请求，旧session晚回执不得重开资格；locker_device_reply_t中host专用两个时间字段在MCU置0，不编码上线。回调只借用到返回，依赖为静态生命周期；队列满关闭新动作并诊断，不阻塞Control或在回调驱动锁。

Journal sink的committed(token,status)仍送Control；新增event_committed在Storage持久化后送Communication，携带实际分配的epoch/boot/seq和完整事实。不能把append前未分配序号的对象当LIVE发布。丢发送通知不删已持久事件、不ACK，后续补取仍按原规则且历史卡片不授权。S2-05/S2-08验收包含两并发request_no、旧session迟到、提交失败、快照屏障和释放响应关联，不靠全局最后请求号。

## Journal管理操作与回传

mcu_journal_control采用同一Storage有界队列，操作编号RECOVER=0、BARRIER=1、FINALIZE=2、RELEASE=3；request包含token/barrier_after_token、epoch/session/operation/command/boot/action与terminal_state，不适用字段置0。请求/状态都是具体原生结构，禁止跨任务读取opaque Journal内部指针。

RECOVER返回已提交epoch/boot、动作水位、首尾/ACK游标、活动意图/结果、日志状态/连续性与丢失计数；journal_init成功创建本次启动检查点后才返回，失败不READY。BARRIER由Control在所有此前观察事件append之后排入同一FIFO，barrier_after_token标识最后观察提交；Storage确认该边界持久后返回一致状态，Control才组合冻结物理快照并回原request_no。未提交/失败不能造快照。

FINALIZE仅持久化该活动command已明确未驱动/完成等终结状态，不能反悔已接纳水位；无法确认则UNKNOWN_RESULT。RELEASE只在Control已验证闭门/无脉冲/原guard与关联后提交清空活动检查点，保留水位和日志。管理回执control_result(token,status,state)仅借用state，复制入Control队列再调用on_journal_result；普通committed回执的state参数传NULL。失败不发成功响应/不清活动状态。token在全部回执收敛前不重用，不按单个全局变量关联。

S2-04实现这四操作与断电/失败测试，S2-05绑定sink与Control处理，S2-08验证恢复元数据、屏障和持久释放后才响应。Journal/Control/Communication上下文存活到任务全部停止，不靠将同名JSON/结构裸发串口实现。
