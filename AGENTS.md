# 后续 AI 开发入口

本项目由用户分阶段评审和开发。当前为契约0.2.1及45张产品卡；S1-01、S1-02、S1-03 已完成（DONE，报告 docs/evidence/stage-01/S1-01/summary.md、docs/evidence/stage-01/S1-02/summary.md、docs/evidence/stage-01/S1-03/summary.md），当前 active_task=S1-04（业务核心，IN_PROGRESS），其他状态以机器索引和实际报告为准。不能把契约、模拟器或静态检查说成硬件功能已经完成。

用户已另行授权开发环境准备，firmware/env是正式保留的非执行器环境测试目标，Keil/ARMCC编译链接通过；不算S1-01或S2-01产品门禁完成。后续复用firmware/third_party及依赖锁，不重复下载或修改原HAL。TIM6已分配HAL tick，SysTick/SVC/PendSV归FreeRTOS；硬件profile仍UNVERIFIED。

## 阅读顺序与真源

1. `README.md`：当前阶段和交付入口。
2. `docs/stages/README.md`、`config/development/stage-plan.json`：单卡规约、阶段依赖、真实实施状态；再读`docs/stages/stage-00-architecture.md`的架构/状态机。
3. `contracts/contract-source.json` 和 `contracts/uart-layouts.json`：枚举/API/端口与串口字段的机器真源。
   `contracts/internal-data-source.json`固定内部仓储与IPC字段；生成器维护C原生DTO映射。硬件profile使用独立schema，不从公开API写入。
4. `docs/architecture/internal-interfaces.md`、`docs/architecture/uart-v1.md`：内置接口、线程/任务与线协议语义。
5. 当前分配的一张任务卡及`docs/architecture/traceability.md`的实施归属；没有开发授权时不自动开始实现。S1-01、S1-02、S1-03 已 DONE，当前为 S1-04（IN_PROGRESS），以机器索引为准。

`docs/architecture/reviews/` 是专家评审输入和审计记录，不是第二套执行规格。建议冲突以阶段 0 决策为准；发现真源冲突先修正文档和契约，不能各端自行选一种解释。

`docs/项目计划.md`是保留的产品讨论背景，文件由用户Word打开时不强制覆盖/移动；其中旧开发顺序不作为当前依据。当前正式执行入口是docs/stages/README.md及机器阶段索引。

## 开发纪律

- VM单独使用$HOME/locker-workspace，src仅源码，build/runtime/logs/artifacts在src之外；按卡/批次分目录。禁止在HOME根下新建临时脚本、日志、安装包、模型产物；按docs/development/vm-home-layout.md放入所属工作区。不在旧共享源或src中生成构建、DB/socket、临时日志，不移动/删除旧源码。目录创建是否实际成功以VM证据为准。

- 主机端默认用户已有VMware Ubuntu（已确认同S1-01历史VM验证guest），不要求WSL/新装Ubuntu。源码和VM快照/日志同步按docs/development/vm-ubuntu-workflow.md；保留原WSL/VM历史证据，整改版需新的VM日志，不重置活跃卡或冒称复验完成。

- 新项目或较大变更先调查 GitHub/官方资料，评估复用、许可、维护、风险和适配成本；明确小改可豁免。
- 一次执行一张卡。用户授权连续完成一个阶段时可依赖序继续该阶段；否则卡完成后交接，不自动跨卡/跨阶段。前置未通过不开始依赖功能，缺条件如实BLOCKED，不用mock替代实物门禁。
- **并发授权（用户长期指示）**：本项目采用「两条开发线 + 一个统一集成负责人 + 独立审计」，调度表与角色分工见 [并发调度方案](docs/stages/parallel-execution-plan.md)。开发A=主开发会话；开发B固定为 DSH 中本项目工作区下新开的那个会话，后续不更换；集成负责人由开发A分时担任，是 `config/development/stage-plan.json`、根 `CMakeLists.txt`、`contracts/` 与生成物的唯一写者；审计只写 audit*.md。只有调度表列出的窗口才并行（S2-03/S2-04、S3-02→04 与 S3-05），其余卡仍按依赖串行；并行前须两支前置 DONE、目录不重叠、共享写者唯一；汇合卡必须两支都通过才发。并发不放宽任何证据门禁。
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

## 长期规则：全自动开发-审计循环（用户长期指示，必须遵守）

- **每张卡必须走完整四段**：开发（`docs/ai-prompts/<卡>.md` 开发段）→ 独立审计（同文件审计段）→ 整改（同文件整改段）→ 交接（同文件交接段）。审计由 **codex CLI** 担任独立审计会话，不使用本会话自审冒充独立审计。
- **全自动执行，不需要逐轮人工确认**：开发段完成后立即把审计段提示词交给 codex；codex 报 FAIL 就把整改段提示词连同 `audit*.md` 交给开发侧整改，再交回审计段复验；循环到审计 **PASS** 后执行交接段，再进入下一张卡。只有缺环境/缺实物/需用户决策时才停下来问。
- **审计固定用 codex `gpt-6.1-sol`（high 推理档）**，并**允许审计使用工作区**（`-s danger-full-access` 或等价），让审计过程可视化、可留痕，不允许只在暗处执行不给证据。
- **不设「联合修复」**：用户已明确废除开发AI与审计AI共同修复的超轮机制（耗 codex 额度）。取而代之的是**审计一次给全、开发一次改透**：审计必须逐项给出文件/行号、触发路径、复现方式、影响与阻塞级别，不许只给结论；开发必须一次性修完本轮全部阻塞项并补正式回归，不许挤牙膏式小改。
- **每轮审计必须归档四件**到本卡证据目录：`audit-roundN-prompt.txt`、`audit-roundN-runner.ps1`、`audit-roundN-run.log`（含 EXIT/START/END）、`audit-roundN-codex-output.txt`（原始转录）。审计报告本身只写 `audit*.md`，不改业务代码、契约、验收或 stage-plan。
- **启动器注意**：`codex.ps1` 的版本横幅走 stderr，PowerShell 会当 `NativeCommandError` 抛出，因此 runner 必须用 `$ErrorActionPreference='Continue'`（不能用 `Stop`）；脚本文件必须存为 **UTF-8 BOM**，否则中文路径按 ANSI 解码；调用形如 `Get-Content -Raw -Encoding UTF8 $in | codex exec --skip-git-repo-check -s danger-full-access - *> $out`。
- **禁用 WSL**：本项目所有 Linux 证据只来自 VMware Ubuntu 干净 guest（`ssh locker-vm`），不得使用 WSL2 产物，也不得把 WSL 结果改名成 VM 结果。
- **进度报告**：长任务期间每小时向用户报告一次当前进度（卡号、已完成、阻塞、下一步）。
- **收尾产物**：S1 阶段完成后，把上述「全自动开发AI」「独立审计AI」「卡交接」流程分别固化为可复用的 skills，供后续阶段直接调用。

## 本阶段检查

```text
python scripts/check_contracts.py
python scripts/check_stage_plan.py
python scripts/check_vision_contracts.py
```

该命令只验证设计产物一致性、帧样例和约束，不证明业务实现或真机可用。任何后续 AI 都必须明确报告本次修改范围、检查结果和未完成的硬件验证。
