# 后续 AI 开发入口

本项目由用户分阶段评审和开发。当前为契约0.2.1及45张产品卡；S1-01至S1-07已完成（DONE，报告位于docs/evidence/stage-01/各卡目录），阶段1主机模拟门禁PASS。S2-01安全镜像的SWD/时钟/回绕已上板通过；MCU失电、MOS供12V与10kΩ负载时PWM=0V、OUT差分=29mV，双电源上电稳态默认关闭已通过，复位瞬态留S2-05；当前active_task=null、recommended_next=S2-02，S2-02未开始。其他状态以机器索引和实际报告为准。

用户已另行授权开发环境准备，firmware/env是正式保留的非执行器环境测试目标，Keil/ARMCC编译链接通过；不算S1-01或S2-01产品门禁完成。后续复用firmware/third_party及依赖锁，不重复下载或修改原HAL。TIM6已分配HAL tick，SysTick/SVC/PendSV归FreeRTOS；硬件profile仍UNVERIFIED。

## 阅读顺序与真源

1. `README.md`：当前阶段和交付入口。
2. `docs/stages/README.md`、`config/development/stage-plan.json`：单卡规约、阶段依赖、真实实施状态；再读`docs/stages/stage-00-architecture.md`的架构/状态机。
3. `contracts/contract-source.json` 和 `contracts/uart-layouts.json`：枚举/API/端口与串口字段的机器真源。
   `contracts/internal-data-source.json`固定内部仓储与IPC字段；生成器维护C原生DTO映射。硬件profile使用独立schema，不从公开API写入。
4. `docs/architecture/internal-interfaces.md`、`docs/architecture/uart-v1.md`：内置接口、线程/任务与线协议语义。
5. 当前分配的一张任务卡及`docs/architecture/traceability.md`的实施归属；没有开发授权时不自动开始实现。S1-01至S1-07已DONE，S2-01为DONE，S2-02未开始；以机器索引为准。

`docs/architecture/reviews/` 是专家评审输入和审计记录，不是第二套执行规格。建议冲突以阶段 0 决策为准；发现真源冲突先修正文档和契约，不能各端自行选一种解释。

`docs/项目计划.md`是保留的产品讨论背景，文件由用户Word打开时不强制覆盖/移动；其中旧开发顺序不作为当前依据。当前正式执行入口是docs/stages/README.md及机器阶段索引。

## 开发纪律

- VM单独使用$HOME/locker-workspace，src仅源码，build/runtime/logs/artifacts在src之外；按卡/批次分目录。禁止在HOME根下新建临时脚本、日志、安装包、模型产物；按docs/development/vm-home-layout.md放入所属工作区。不在旧共享源或src中生成构建、DB/socket、临时日志，不移动/删除旧源码。目录创建是否实际成功以VM证据为准。

- 主机端默认用户已有VMware Ubuntu（已确认同S1-01历史VM验证guest），不要求WSL/新装Ubuntu。源码和VM快照/日志同步按docs/development/vm-ubuntu-workflow.md；保留原WSL/VM历史证据，整改版需新的VM日志，不重置活跃卡或冒称复验完成。

- 新项目或较大变更先调查 GitHub/官方资料，评估复用、许可、维护、风险和适配成本；明确小改可豁免。
- 一次执行一张卡。用户授权连续完成一个阶段时可依赖序继续该阶段；否则卡完成后交接，不自动跨卡/跨阶段。前置未通过不开始依赖功能，缺条件如实BLOCKED，不用mock替代实物门禁。
- **执行方式**：当前会话统一完成开发、验证、整改和集成，允许按明确职责使用子代理；不启动固定开发B或独立审计流程。详见[执行规则](docs/development/execution-policy.md)；功能依赖、实物门禁及唯一共享写者约束继续有效。
- 功能卡只改其分配目录与相关正式测试/文档。共享接口变更由契约负责人统一更新真源/生成物/调用方；不自行放宽验收或新建同义API。卡状态与真实报告同步，DONE和阶段PASS均需证据。
- RK 业务核心是借还账本唯一写者；MCU 控制任务是锁驱动唯一入口。客户端、HTTP 工作线程和 UI 不直接写账本或 GPIO。
- v11为MobileNetV3-Large七分类，E:/equipment-vision原项目只读；相机接口已定USB UVC定焦候选（未购/页面报价未结算/未接板，见docs/hardware/相机选型与成本.md），不以已有模型代替RKNN/相机验证。Core负责融合，locker-vision不接UART、不改item_state、不授权开锁。
- 阶段2A为S2-01..08、2B为S2-09..16，总45卡；S3-01须S2-16通过。2000ms是首次接纳照片新鲜度；已提交证据按同guard和120秒WAIT_CONFIRM使用，不要求用户2秒确认。
- ACK 不是开门；持久化意图不是已执行；历史补传不产生开锁；结果未知不能自动重放动作。
- 锁脉冲硬截止不能依赖 Linux、Flash 完成或 RTOS 任务及时调度；阶段 2 必须提供硬件定时关断证据。
- 生成文件通过 `scripts/build_contracts.py` 更新，不手改 `contracts/openapi.json` 或 `contracts/include/locker_contracts.h`。
- 更新行为、接口、数据或模块边界时，同步相关当前文档；正文写当前真实规则，不在顶部累积补丁说明。
- 完成前自动检查本轮临时残留。只清理明确临时内容；保留正式测试、证据和诊断日志；不确定项单列说明。
- 用户要求 commit 时，顺序为清残留、检查/测试、commit；不自动 push。
- 新增材料和运费预算 200 元，缺少复用条件必须报告；不暗加云端、第二柜格、UPS 或新屏幕。
- 当前预算不含用户单独购置的F407板。以更新后的selected-bom与材料清单为准，别继续按旧板卡自带DAP/VCP的假设开发。
- STM32固定HAL路线，默认复用本地HAL V1.8.5，不修改用户原库、不另用SPL或业务层寄存器驱动。USE_RTOS在本地HAL中必须保持0，FreeRTOS另行集成。
- 不通过普通 HTTPS 暴露直接开锁、模拟传感器、任意进程命令或数据覆盖接口。不能用关闭证书校验来实现手机连接。

## 长期规则：单会话开发与验证

- 当前会话完成实现→开发者验证→必要整改→交接，按[执行规则](docs/development/execution-policy.md)执行，允许在当前会话内按明确职责使用子代理，不启动固定独立审计AI或开发B流程。
- 不以缺少设计预审、审计PASS、审计四件套或审计流程skills阻断交付。历史审计指出的产品缺陷仍须通过实际代码与回归关闭，保留历史证据。
- 用户要求推进阶段二；按真实功能依赖补齐阶段一必要能力后进入阶段二，接线核对和环境准备可先行。不得伪造DONE/PASS或降低实物验收。
- Linux证据仅来自VMware Ubuntu（ssh locker-vm），禁用WSL。缺实物/环境/用户决策时明确记录，不能拿mock替代。

## 版本控制（用户已授权）

- 仓库已初始化（基线提交 `cd4505e`，`main` 分支，`core.autocrlf=false`；基线部分文件为 CRLF，本卡保留已跟踪格式，新增源码使用 LF）。**每完成一张卡做一个本地提交，不 push**；提交信息写清卡号、改动范围、检查命令与结果。
- 验证以本卡git diff、新增正式测试和验收清单为依据，不在提示词中复制源码。
- 整改回滚用 `git checkout`/`git reset`，不再依赖脚本式的锚点回退（S1-04 的 `.s104-stage/revert.py` 属于一次性手段，不要固化成流程）。
- `build/`、`.s104-stage/`、`STM32F4xx_HAL_Code/`、`STM32F4xx_HAL_Code.zip`、模型产物已在 `.gitignore` 排除；这些东西不能进提交。

## 开发提速规则（P1–P5，用户已授权，不放宽任何验收门禁）

- **P1 聚焦差异**：围绕本卡diff、验收项和相关契约核查，报告具体文件位置、触发、影响及验证结果。
- **P2 sanitizer 每卡只跑一次**：在收尾轮跑 ASan/UBSan（必跑）；TSan 仅当本卡改动涉及线程、共享状态或锁时才跑。中间整改轮只跑普通 Debug 构建 + 全量 CTest。
- **P3 证据模板化并设上限**：`handoff.md` ≤ 8 KB、`summary.md` ≤ 10 KB，用固定表格填字段（卡号/契约版本/入口与命令/退出码/失败路径/未实现项/改动文件清单/下一卡），不写成长篇叙述。`audit*.md` 只列问题条目，不复制转录。
- **P4 开发前核对**：当前会话核对卡正文、相关契约和设计，处理阻塞项，不启动审计模型。
- **P5 阻塞项才阻断**：只有阻塞级问题阻断通过并触发整改回合；非阻塞项列入本卡 `debt.md` 随卡交接，不反复扩展整改范围。
- **度量留痕**：每卡交接时在 `handoff.md` 记录本卡实际耗时与开发者验证轮数，用于判断提速是否真的生效。

## 本阶段检查

```text
python scripts/check_contracts.py
python scripts/check_stage_plan.py
python scripts/check_vision_contracts.py
```

该命令只验证设计产物一致性、帧样例和约束，不证明业务实现或真机可用。任何后续 AI 都必须明确报告本次修改范围、检查结果和未完成的硬件验证。
