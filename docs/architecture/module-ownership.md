# 模块所有权与 AI 修改边界

执行先读[阶段导航](../stages/README.md)和[机器阶段索引](../../config/development/stage-plan.json)。45张卡默认一张一张开发，具体路径限制在该卡；下表是模块所有权，不授权一次实现整个目录。逐接口归属见[追踪表](traceability.md)。

状态：阶段 0 设计基线的模块交接约定，尚无业务实现。当前执行语义以[阶段 0 主规格](../stages/stage-00-architecture.md)及[机器真源](../../contracts/contract-source.json)为准。专家评审是设计背景，不是另一套实现依据。

## 1. 先读契约，再划分实现

阅读顺序：阶段 0 → [OpenAPI](../../contracts/openapi.json) → [C 接口声明](../../contracts/include/locker_contracts.h) → [内部接口](internal-interfaces.md) → [UART-v1](uart-v1.md) → [阶段 1 任务卡](../stages/stage-01-task-cards.md)。

`contracts/contract-source.json` 是跨端枚举、错误、字段、消息编号和端口的唯一机器真源；`scripts/build_contracts.py` 生成公开产物。功能开发 AI 不直接编辑生成文件，不复制另一套相似枚举，不为通过自身测试擅自修改契约。发现缺项或矛盾时，提交“位置、冲突、受影响调用方、建议”的变更说明，由架构负责人统一修改真源、重新生成并验证，再继续依赖该变更的实现。

新增模块私有字段不得绕过契约对外输出。私有结构体可用于实现，但不能直接作为公共 JSON 或串口字节。

## 2. 固定目录和责任

下表中的目录是后续开发的修改边界；当前阶段不要求预建空实现。

| 目录/入口 | 所有者角色 | 内容与权限 | 禁止事项 |
|---|---|---|---|
| `contracts/`、`scripts/build_contracts.py`、`scripts/check_contracts.py` | 架构/契约负责人 | 机器真源、生成器、黄金样例、一致性校验 | 普通业务卡手改生成文件或放宽断言 |
| `host/common/` | 基础设施负责人 | 有界队列、单调时钟、UUID/u64 校验、受限日志、内存生命周期规则 | 放业务决策、数据库或锁操作 |
| `host/domain/` | 业务核心负责人 | 借还状态机、权限决策、占位、确认、核验、重连状态、outbox 调度意图 | 包含 CivetWeb/SQLite/HAL 头、调用网络/串口阻塞 IO、让客户端指定真实 actor |
| `host/storage/` | SQLite 负责人 | Repository 适配、DDL/迁移、事务、幂等记录、事件/游标和 outbox | 在 HTTP 线程直接调用；把动作发出放进 SQLite 回调 |
| `host/device/` | 设备协议负责人 | DevicePort、UART-v1 编解码、会话/命令结果、事件导入入口；阶段 1 只接模拟 socket | 改业务账本、重发已可能发送的 EXECUTE、把 CRC 当鉴权 |
| `host/http/` | 网络接口负责人 | CivetWeb HTTPS/WS、body/分页限制、会话提取、领域命令适配 | 直接 SQL、GPIO、公共 presence/模拟注入/任意开锁接口 |
| `host/ipc/` | 本机接口负责人 | `service.sock` 的 SO_PEERCRED 与权限检查、当前用户会话和到柜证明适配 | 仅相信 JSON 的 `source=local`、仅凭本机来源冒充借用人 |
| `host/runtime/` | 集成负责人 | `locker-service` 入口、配置、线程装配、退出清理、真实 Core 进度 | 把所有权不明的共享可变状态塞到全局变量；自动重放旧 outbox |
| `host/cli/` | 集成/安全负责人 | `locker-admin` 本机初始化；阶段 1 测试客户端工具 | 默认账号密码、网络注册、命令行明文密码、无审计状态覆盖 |
| `host/simulator/` | 模拟器负责人 | 独立 `locker-device-sim`、设备模型、私有故障注入、独立模拟持久化 | 连接真实锁、访问业务数据库、向公网暴露控制入口、宣称 littlefs 已验证 |
| `tests/unit/`、`tests/contract/`、`tests/integration/`、`tests/fixtures/` | 对应模块 + 验证负责人 | 正式回归测试、跨模块场景和公开/内部契约断言 | 用只反射实现的断言代替验收；留下真实凭据；替换生产错误语义 |
| `deployment/host/` | 集成负责人 | 无凭据配置样例、主机模拟启动说明和依赖锁定清单 | 把模拟启动脚本当真机部署；隐藏绑定到所有网卡 |
| `CMakeLists.txt`、根级构建配置 | 集成负责人 | 唯一主机构建入口；模块卡通过局部 CMake 文件接入 | 多个 AI 同时改根构建导致入口分裂 |
| `docs/evidence/stage-01/` | 验证负责人 | 正式阶段报告、测试条件、证据索引和已知限制 | 把模拟/静态结果描述为实物通过 |
| `firmware/`、`app/`、`terminal-ui/` | 后续阶段负责人 | MCU、Android、LVGL 实现，当前阶段保留目录名 | 阶段 1 扩写真实固件、完整 App 或 GUI |
| `host/supervisor/` | 阶段 4 监督负责人 | 固定 UI 进程监督与限次恢复 | 首版自动重启 service、硬复位 RK 或控制锁 |
| `app/` | S3客户端负责人 | Android配对/凭据/非敏感意图/页面；按各卡子目录拆分 | 持久化密码、任意URL、直连MCU、第二账本 |
| `terminal-ui/` | S3触摸端负责人 | 单UI线程、异步IPC、实际回路进度 | 在IPC线程改LVGL、重启重放presence、保留旧token |
| `deployment/target/`、`deployment/maintenance/`、`deployment/releases/` | S2首用/S5部署负责人 | 真实目标部署、受限维护和固定发布清单 | 裸复制营业DB、回退水位/额度、远程shell/OTA扩项 |
| `config/development/`、`scripts/build_stage_docs.py`、`scripts/check_stage_plan.py` | 架构/集成负责人 | 单卡依赖/状态、接口归属与派生追踪表 | 填假DONE、并行共改状态、手改生成追踪表 |
| `host/vision-worker/` | S2-11/12/13视觉负责人 | V4L2/RKNN/私有worker服务，逐卡限目录 | 访问UART/账本、公网classify、网络图片路径 |
| `host/vision/` | S2-13适配负责人 | 私有IPC/有界队列/sink/超时/实例 | Core阻塞NPU、写SQL、以分类授权锁 |
| `tools/vision/`、`deployment/vision/`、`config/vision/` | S2-09..12模型/集成负责人 | 离线导出/评估、版本化制品、策略/profile | 覆盖原模型、虚报RKNN/相机/预算 |

并行 AI 只能改分配目录及其模块测试，不共改根 CMake、契约、同一份 DDL 或跨模块头文件。需要扩展共享声明时由集成/契约负责人先提交统一版本，再解除依赖任务阻塞；不得双方各造一份兼容层。

## 3. 运行时单写者和数据流

- `locker-service` 的 Core 线程拥有借还状态和唯一 SQLite 写连接。仓储调用只发生在该线程；同步事务必须有错误返回，提交失败不发动作。
- HTTP/IPC 工作线程、设备接收线程把复制后的命令/事实放入有界队列，不保留请求栈或 CivetWeb 缓冲区指针。排队成功与持久化完成分开发回。
- 设备事实由 Core 幂等导入；只有事件与游标同事务提交，才能确认 MCU 日志水位。慢 WS 客户端不能阻塞 Core。
- Runtime仅经core_get_progress读源副本，经core_apply_supervisor_health提交监督观察；Core唯一更新健康投影。Progress采样原时刻随副本保留，不能搬运时刷新或重复累计年龄。
- 业务 outbox 表示“需要发送的持久意图”，不意味着重启后自动重发。可能发送过的动作进入查询/核对；服务新实例必须建立新 session，旧动作不得迁移。
- 模拟器独占自己的设备模型和模拟持久化文件，业务服务与模拟器不得共享同一个 SQLite 文件或写连接。
- 模拟器通过私有 `AF_UNIX SOCK_STREAM` 向 DevicePort 提供真实 UART-v1 字节流；测试控制走另一条权限 `0600` 的本地控制入口，不能混入公共 HTTPS。这样可以测试半帧/连续帧等逻辑，仍不能证明真实 DMA、Flash 或电气行为。

SQLite DDL 只有 `host/storage/schema-v1.sql` 一处执行真源。阶段 1 的建表职责属于仓储卡，状态语义必须完全来自主规格和契约；数据库迁移版本单调递增，不自动删库重建来绕过不兼容。外部 u64 可能超过 SQLite 有符号整数范围，存储适配应以经过边界校验的十进制文本保存；排序/水位比较按数值处理，不能直接按普通文本字典序比较。

## 4. 监督与物理控制的边界

首版自动恢复对象只有 `locker-ui`。`locker-service` 失联由独立 MCU 记录，人工重启服务后必须先核对，不能自动重放开锁。阶段 1 不实现监督守护进程，只保留契约端口和真实业务进度观测，相关自动恢复验收到阶段 4。

MCU Control 是锁输出唯一入口；PeripheralStorage 是 littlefs/SPI 唯一所有者；Communication 只能提交消息，Health 不能代替业务授权。阶段 1 的模拟器实现这些角色的逻辑结果，不允许被当成 HAL/BSP 的替代实现交付真机。

到柜证明、用户确认和物理事实是三种输入：App 不能伪造到柜证明，管理员不能上传原始传感器事件，实时读卡和历史补传必须区分。管理员核验保留人工理由及证据缺口，不改变原始设备事件。

## 5. 修改接口与交付证据

接口变更必须一起评估调用方、错误/超时语义、幂等、存储兼容、样例和验收。只把编译改通过而留下旧文档不算完成。对新观察到的问题，实施 AI 先写最小复现，再提交契约变更请求，不可静默放宽原规则。

每张任务卡交付：改动范围、入口/构建命令、使用的契约版本、正式测试及结果、未验证项、临时残留检查结果。DONE 证据来自实际运行或明确的静态检查；“代码已生成”“AI 自查通过”不是证据。阶段报告保留测试日志索引、提交/文件指纹和运行环境，不保存密码、token 或私钥。

架构、协议、运行方式或模块边界改变时，同步这里、阶段主规格及对应模块 README。专家评审文档只保留评审背景，不在默认执行链中引入第二套规则。
