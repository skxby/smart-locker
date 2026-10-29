# 阶段 1 任务卡：主机骨架、SQLite 与模拟设备

状态：S1-01至S1-07已完成（DONE），阶段一主机模拟门禁PASS；active_task=null、recommended_next=S2-03。逐卡结果以stage-plan和真实报告为准。契约0.2.1，7张卡按[单卡规则](README.md)执行，规格文档不替代验收。

## 1. 目标、输入与验收环境

本阶段交付可在 Linux 主机运行的 `locker-service`、本机初始化工具 `locker-admin`、独立模拟设备 `locker-device-sim`，以及契约/集成测试。使用一柜格、一工具，在没有 MCU、柜锁、Android 或 LVGL 的情况下验证借还状态、数据库、接口和恢复语义。

必读输入：

1. [阶段 0 主规格](stage-00-architecture.md)及[模块所有权](../architecture/module-ownership.md)。
2. [契约机器真源](../../contracts/contract-source.json)、[OpenAPI](../../contracts/openapi.json)、[C 接口头](../../contracts/include/locker_contracts.h)。
3. [内部接口](../architecture/internal-interfaces.md)、[UART-v1](../architecture/uart-v1.md)和[正式消息样例](../../contracts/examples/)。

实现语言为C，POSIX主机开发/验证默认使用用户已有VMware Ubuntu x86_64；用户确认复用S1-01历史VM验证的同一台guest，不要求WSL或新装Ubuntu。构建采用CMake/CTest。源码主副本当前在Windows，固定快照同步到VM本地Linux文件系统构建；见[VM工作流](../development/vm-ubuntu-workflow.md)。S1-01保留已写代码与历史证据，只对最新整改源在该VM补复验；不改成Windows或Python服务。缺连接/工具条件如实记录，不改写历史PASS。

依赖复用既定 CivetWeb、SQLite、cJSON、libsodium 及 TLS 库，不自研 HTTP、密码算法或数据库。实际版本与来源在实施时锁定到提交/tag，并记录 license；禁止使用浮动 `master` 获取不可复现的产物。

本阶段全部设备结果均为模拟。启动日志、配置样例和测试报告必须明确 `device backend: simulator`；不能为标识模拟擅自给公共 schema 添加字段。

本阶段只实现原22个API与无视觉基础流程；OpenAPI中的getVisionStatus/getOperationVisionEvidence按x-first-implementation-card在S2-14实现，不能用空成功占位。测试报告标明后续接口未交付。CoreConfig.vision_policy必须明确DISABLED_FOR_FOUNDATION，不启动worker；82个声明不意味着本卡需全部实现。

## 2. 执行顺序与并行前提

```text
S1-01 构建与基础设施
  ├── S1-02 SQLite 仓储 ─────┐
  └── S1-03 设备端口/模拟器 ─┤
                            └── S1-04 业务核心
                                  └── S1-05 HTTPS/本机 IPC
                                        └── S1-06 装配/初始化
                                              └── S1-07 集成审计
```

S1-02 与 S1-03 可并行，前提是 S1-01 已固定构建入口、公共类型和内存生命周期，双方只改自己的目录。S1-05 可在 S1-04 的调用端口冻结后先做鉴权/边界测试，不能自行补业务规则。S1-06 由唯一集成负责人维护根 CMake 与运行配置，其余模块提交局部构建文件。

每张卡开始前运行契约检查；发现接口矛盾就登记变更请求，由契约负责人统一修正。所有卡不得自行改 `contracts/`、生成器或将不通过的测试删去。生成接口只有声明，不能靠空实现或固定成功返回完成卡片。

### S1-01：构建入口与基础设施

- **输入**：主规格、公共 C 接口、资源上限和模块所有权。
- **可改目录**：根 `CMakeLists.txt`、`host/common/`、`tests/unit/common/`、`deployment/host/` 的依赖说明；根入口仅该卡/集成负责人维护。
- **产出**：模块化 CMake；有界队列、单调时钟、UUID/u64 边界工具、可注入测试时钟、受限日志；依赖锁定清单。业务生产路径使用真实时钟，测试替身不能通过公共 API 控制。
- **验收**：干净 Linux 环境可配置并构建基础库；队列满时明确失败；消息生命周期不依赖调用方栈；u64 零、最大值、越界和非法文本有测试；日志不输出 token/密码。
- **禁止越界**：不建业务状态、不编造 REST 路由、不实现锁驱动、不先搭一个与正式类型不同的临时框架。
- **DONE 证据**：运行环境、锁定依赖来源/许可、配置/构建/CTest 命令和实际结果。

### S1-02：SQLite 单写仓储与事务

- **输入**：主规格的四类状态、幂等/outbox、事件键、水位和恢复规则；Repository 声明。
- **可改目录**：`host/storage/`、`tests/unit/storage/`；只提供局部 CMake 接入文件。
- **产出**：唯一 `schema-v1.sql`、迁移元数据、Repository 实现；逻辑覆盖用户/会话/卡绑定、工具/借用、操作、动作/outbox、设备事件/游标、幂等记录和人工核验。不允许 HTTP/UI 直接调用仓储。
- **固定约束**：按internal-data.schema.json实现受控集合、事务/查询/分页恢复DTO，不另造跨模块结构。操作占位与账本变化由事务保护；操作+幂等+outbox 一起提交；新事件+Core提供的投影变化+游标一起提交；事件唯一键含 journal_epoch；u64 按已校验十进制文本存储并按数值比较。WAL、`synchronous=FULL`；首次初始化与损坏/迁移失败区分，失败不能自动清库。
- **验收**：同一事件重复导入不重复推进；失败事务不留下半个占位/outbox；两个竞争申请只能一个成功；数据库重新打开仍保留有效借用、未决操作和幂等结果；游标不能越过缺口、倒退或把 10 排在 2 前。
- **禁止越界**：不自行决定借还成功、不发送设备动作、不更改公共 schema 或状态枚举。
- **DONE 证据**：DDL 与字段映射说明、事务/重开数据库测试、错误路径结果；注明尚未验证 RK 存储介质掉电可靠性。

### S1-03：UART 字节端口与独立模拟设备

- **输入**：DevicePort、UART-v1、黄金帧、会话和重连屏障规则。
- **可改目录**：`host/device/`、`host/simulator/`、`tests/unit/device/`、`tests/contract/device/`；不改业务数据库。
- **产出**：协议编码/流式解析；DevicePort 的模拟 socket 实现；`locker-device-sim` 经 `AF_UNIX SOCK_STREAM` 提供 UART-v1 字节，另用权限 `0600` 的私有控制入口注入门/物品、读卡、断链、拒绝、丢 ACK、事件缺口与模拟重启。模拟设备使用独立 SQLite 文件保存设备日志/动作水位，不共享业务库。
- **验收**：黄金帧逐字节一致；半帧、多帧、CRC/长度错误不越界；旧 session、重复 action_no、跳号都不重复动作；模拟器重启后动作查询及事件键仍可核对；ACK 和 PULSE_STARTED 可独立控制；日志提交失败时拒绝新动作。
- **禁止越界**：不创建公共 HTTP 模拟接口，不接串口实物，不用真实 HAL，不把进程/SQLite 模拟重启结果称为 MCU/littlefs 掉电验收。
- **DONE 证据**：字节解析测试、控制入口说明、独立持久化路径及重启场景记录；明确“硬件脉冲只模拟计数”。

### S1-04：单线程业务核心与恢复核对

- **输入**：S1-02、S1-03 已通过的端口；主规格完整状态表和默认超时。
- **可改目录**：`host/domain/`、`tests/unit/domain/`、核心相关 `tests/integration/`。
- **产出**：Core 单所有者循环、申请/取消/确认/核验、有效借用投影、实时到柜证明、outbox 调度、事件幂等与恢复屏障；对外只使用统一类型，不依赖 CivetWeb 或 SQLite 的实现类型。
- **健康基线接口**：真实Core循环完成后发布instance/counter/sample副本，locker_core_get_progress非阻塞读取，不经Core等待队列。没有supervisor/UI时保持未知；同counter重取不续命，停滞测试不能只证明Runtime还活着。locker_core_apply_supervisor_health的实现归S4-04，本阶段不空实现冒充。
- **验收**：正常借还、只开门不取物、提前确认、错误用户、两个申请竞争、15 秒证明过期、各业务超时和 UNKNOWN 输入均有确定结果；仅 ACK 不进入开门；物理变化与本人确认都满足才完成；历史读卡不开柜；重启后先核对，可能执行过的动作不重放。
- **禁止越界**：不把“有当前离位快照”代替完整开门/物品变化证据；不允许管理员无理由覆盖；不调用阻塞网络；不能自动恢复或重放旧 EXECUTE。
- **DONE 证据**：状态转移场景矩阵与实际测试；对“准备→发送→ACK→脉冲→落库”中断位置记录预期和结果。

### S1-05：HTTPS/WS、认证与本机到柜接口

- **输入**：OpenAPI、已冻结 Core 端口、认证与本机信任边界。
- **可改目录**：`host/http/`、`host/ipc/`、对应 `tests/contract/` 和接口集成测试。
- **产出**：CivetWeb HTTPS/WS；Argon2id 密码验证、会话 token 哈希存储、角色/停用检查；REST 适配；受限 `service.sock`；WS 变化提示和重查。阶段 1 无真实 UI，由正式测试客户端代表已认证本机 UI。
- **验收**：响应字段/状态码符合契约；未知/越权身份拒绝；超大 body/分页边界正确；同键同意图返回原结果、同键异正文冲突；普通 HTTPS 无到柜证明或模拟入口；IPC 同时检查 SO_PEERCRED、socket 权限及当前用户会话；慢 WS 不阻塞 Core。
- **禁止越界**：不关 TLS、不提供默认密码/公网注册、不接受 actor_id 代替鉴权、不直接 SQL、不上传传感器事件、不添加任意开锁/进程命令路由。
- **DONE 证据**：OpenAPI 契约测试、TLS 信任失败测试、权限矩阵与 IPC 负例；全部测试证书只用于测试，密钥不进入正式版本库。

### S1-06：可启动装配、本机初始化与文档

- **输入**：前述模块通过的测试、资源上限、既定进程边界。
- **可改目录**：`host/runtime/`、`host/cli/`、`deployment/host/`、根 CMake、README 当前状态及模块使用文档。
- **产出**：`locker-service`、`locker-admin` 和模拟器的统一启动/退出流程；首位管理员和单工具的显式本机初始化；无凭据配置样例，独立数据库路径、受限 socket 路径和模拟模式醒目标识。
- **基线装配**：Runtime每秒搬运Core不可变源样本到HOST_HEALTH；UI实例零UUID、年龄UINT32_MAX、监督UNKNOWN。真实Core停止递增后即使搬运还运行，也不能发新鲜Core年龄。共享单实例/离线维护互斥先取得再打开营业DB，退出关闭DB后释放，为S5维护排除竞态。
- **默认运行边界**：主机模拟 HTTPS 默认仅绑定 loopback；若为局域网人工测试，必须在配置显式改变绑定并使用正式证书信任流程。不会启动真实 UART、GPIO、MCU 或 UI 监督进程。
- **验收**：全新状态目录可初始化、启动、退出、重启；错误配置/端口占用/数据库错误明确退出或降级；密码从终端安全输入/受限 stdin 取得，不出现在命令行和日志；重启后先 RECONCILING；Core 队列默认 64、HTTP 工作线程 4、WS 缓冲 16 的上限生效。
- **禁止越界**：不自动建立默认管理员；不清库掩盖失败；不新增公开调试路由；不把 service 纳入自动重启或引入 shell 指令执行。
- **DONE 证据**：从空目录运行的完整可复现说明、真实输出与退出码、已知平台限制及依赖锁定清单。

### S1-07：跨模块契约、故障走查与阶段报告

- **输入**：所有卡的 DONE 证据；主规格的故障/幂等/恢复验收项。
- **可改目录**：`tests/integration/`、`tests/contract/`、正式 `tests/fixtures/`、`docs/evidence/stage-01/`；若需改业务实现退回对应卡，不通过改预期隐藏缺陷。
- **产出**：可重复的端到端测试及正式报告；测试从私有控制入口驱动模拟器，通过真实 HTTPS/IPC/UART 字节和数据库验证结果，不能只直接调用领域函数。
- **验收**：执行下节阶段门禁；报告每场景是通过、失败或未运行及原因，不用“部分通过”代替清单。运行中断后测试资源可以清理，但正式 fixture、故障证据和回归测试必须保留。
- **禁止越界**：不得把模拟日志提交视为 SPI Flash 掉电安全，不得把静态 checker 通过视为服务运行正常，不得为赶进度删去故障验收。
- **DONE 证据**：`docs/evidence/stage-01/summary.md` 包含环境、契约版本、构建/运行命令、场景结果、日志位置、缺陷/限制、临时残留自查及文档同步结论。

## 3. 阶段门禁：必须实跑的场景

| 场景 | 必须观察到的结果 |
|---|---|
| 完整借出/归还 | 正确用户、到柜证明、开关门与物品变化、本人人工确认；借用账本正确闭环 |
| 竞争与重试 | 两用户争同柜只一人占位；网络响应丢失后同幂等键不产生新操作；同键异正文冲突 |
| 用户和管理员权限 | 越权操作、原始卡信息读取、匿名/过期会话拒绝；人工核验留下理由与原事件 |
| 传感器与确认冲突 | 只有 ACK、只开关门、物品 UNKNOWN、提前确认和再次变位不被写成成功 |
| 实时与历史读卡 | 当前有效证明只消费一次；历史补取、重复或旧 session 的卡事件均不开柜 |
| 副作用响应丢失 | EXECUTE 发送后丢 ACK：只查询/核对，不重发；模拟动作计数最多一次 |
| 服务重启 | 未决动作转待核验；不把旧 outbox 迁到新 session；终结操作和有效借用保留 |
| 设备重连屏障 | 先补取落库再 ACK；期间物理版本变化导致 RESUME_READY 失败并重新核对 |
| 日志与事件缺口 | 去重不重复推进；缺口保持不可新操作；日志失败不能开锁；journal_epoch 改变不碰撞旧事件 |
| 数据库事务失败 | 无半条借用/占位/outbox；没有未提交业务意图驱动设备 |
| 网络/IPC边界 | TLS 不信任失败；公共接口无 presence/模拟/直接开锁；私有 IPC 不能仅凭 source 字符串通过 |
| 背压与退出 | 队列满显式失败，慢 WS 不阻塞业务，正常退出可重新打开库和 socket |

超时测试使用注入的单调时钟，避免真实等待数分钟；集成测试至少另保留一项真实时钟烟测，防止仅在替身时间下工作。故障注入通过正式测试控制入口实现，不在生产路径堆一次性开关。

预期统一验证入口：

```text
python scripts/check_contracts.py
python scripts/check_stage_plan.py
cmake -S "$HOME/locker-workspace/src" -B "$HOME/locker-workspace/build/host" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON
cmake --build "$HOME/locker-workspace/build/host"
ctest --test-dir "$HOME/locker-workspace/build/host" --output-on-failure
```

CMake 选项由 S1-01 创建；当前阶段只有契约检查属于阶段 0，不能宣称以上构建命令已可运行。依赖缺失或环境不支持时真实报告，不写虚构通过记录。VM构建/测试产物进入工作区build（与src同级），不写源码目录，正式测试证据写入阶段报告；不得把临时数据库、运行 socket、测试凭据和调试打印提交为正式代码。

## 4. 后续路线：只保留接口，不在阶段 1 执行

| 阶段 | 后续工作 | 前置门禁 |
|---|---|---|
| [2A：基础实物](stage-02-hal-hardware-task-cards.md) | S2-01—08基础8卡，无视觉模式 | S2-08基础子门禁 |
| [2B：视觉](stage-02b-vision-task-cards.md) | S2-09—16模型/相机/worker/融合8卡 | S2-16阶段2完整门禁 |
| [3：手机与触摸端](stage-03-clients-task-cards.md) | 7张卡：类型/配对/借还/管理/LVGL/双端/审计 | S3-07双端实物证据 |
| [4：独立记录与监护](stage-04-monitoring-task-cards.md) | 8张卡：容量/供电/IWDG/观察/恢复/接线/故障/审计 | S4-08监护故障实测 |
| [5：现场交付](stage-05-delivery-task-cards.md) | 7张卡：制品/启动/备份/更新/现场/文档/审计 | S5-07最终证据 |

阶段 1 完成后先向用户报告可运行证据与剩余风险，再进入下一阶段。不得把“后续路线已设计”报告成 MCU、App 或监护能力已经完成。
