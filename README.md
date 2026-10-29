# 实验室工具借还柜

当前由本会话完成开发、验证、整改和交接，取消开发AI/独立审计AI分工；按[执行规则](docs/development/execution-policy.md)执行。旧提示词中的独立审计及并发操作段仅作历史参考，功能依赖与实物验收继续有效。

VM入口已建立：[VM检查与两个工作区](docs/development/vm-inspection-and-code-layout.md)。使用ssh locker-vm；桌面及文件侧栏有借还柜、当前源码和内核驱动入口。两批共800个HOME直属条目已集中归档并核验，根下不再散落脚本/日志/安装包，10个root目录也已完成迁移；[目录用途](docs/development/vm-home-layout.md)说明各类资料的位置。NAT互联网与自编译内核告警仍有待修项。

S1-04业务核心已完成：[交付报告](docs/evidence/stage-01/S1-04/summary.md)、[交接](docs/evidence/stage-01/S1-04/handoff.md)。S1-05 HTTPS/IPC已完成：[交付报告](docs/evidence/stage-01/S1-05/summary.md)、[交接](docs/evidence/stage-01/S1-05/handoff.md)。S1-06已完成（[报告](docs/evidence/stage-01/S1-06/summary.md)、[运行说明](host/runtime/README.md)），S1-07未开始。

主机端开发环境：[用户已有VMware Ubuntu工作流](docs/development/vm-ubuntu-workflow.md)；S1-01原成果保留，后续按[VM最终复验指令](docs/stages/S1-01-vm-final-review.md)验证整改版。Windows继续用于源文件/Keil，不要求WSL。

AI开发入口：[完整执行手册](docs/AI开发执行手册.md)与[45卡指令索引](docs/ai-prompts/README.md)。全部45卡均有开发、审计、整改、交接四份已填指令，共180份；按当前卡复制，无需自己替换卡号。执行说明不自动启动额外产品卡。

STM32开发环境入口：[firmware/README.md](firmware/README.md)和[Keil工程](firmware/keil/locker_environment.uvprojx)。HAL/FreeRTOS/littlefs已落盘并通过本机环境目标编译链接；[验证记录](docs/evidence/environment-2026-10-22/summary.md)区分环境测试与尚未实施的借还柜功能。

当前整体可实施性结论见[整体验证](docs/architecture/feasibility-validation.md)与[本轮审计/检查记录](docs/evidence/feasibility-2026-10-22/summary.md)。0.2.1补齐MCU异步回传/Journal管理及视觉健康时效；设计可实施不等于硬件齐备或整机已验证。

项目定位：STM32F407负责柜锁、物理状态和独立记录；RK3568负责业务、LVGL、局域网服务及独立v11视觉进程；Android负责查询、申请、确认和管理。Core综合物理事实、视觉证据及本人确认作决定。

STM32使用用户提供的 **HAL V1.8.5**，路径为 `STM32F4xx_HAL_Code/`；原库保持不变。当前候选板为天空星STM32F407VGT6高配版，BSP/profile尚未实物验证。

当前状态：契约0.2.1，45张产品任务卡；S1-01、S1-02、S1-03、S1-04、S1-05、S1-06 已完成（DONE，[S1-01 报告](docs/evidence/stage-01/S1-01/summary.md)、[S1-02 报告](docs/evidence/stage-01/S1-02/summary.md)、[S1-03 报告](docs/evidence/stage-01/S1-03/summary.md)），S1-07及后续卡未开始，逐卡结果以状态索引和报告为准。STM32环境测试已构建，尚无整柜实物验收。

**后续AI先读[分阶段开发入口](docs/stages/README.md)，一次只执行一张卡。** [机器状态索引](config/development/stage-plan.json)记录实际active_task；当前active_task=null、recommended_next=S1-07，阶段1 gate 仍 NOT_RUN（门禁卡 S1-07，S1-01至S1-06 DONE ≠ 阶段 PASS）。不得把设计产物当成功能交付。

开发、集成与验证由当前会话统一负责，具体流程见[执行规则](docs/development/execution-policy.md)。

- [项目执行计划](docs/stages/README.md)：当前阶段、45张卡、依赖门禁和单卡交接。docs/项目计划.md作为产品讨论背景保留，其旧开发顺序不作为执行依据。
- [到货检测与确认](docs/hardware/到货检测与确认.md)：2026-10-27用户报告器件到齐，逐件型号/数量/电压/功能待验；[本批记录](docs/evidence/hardware-receipt-2026-10-27/summary.md)用于填写真实测量结果。
- [完整材料与搜索比价](docs/材料清单与搜索比价-2026-10-20.md)：所有器件、辅料、复用项、工具、报价来源与成本分支；视觉采集件在独立小节。
- [14页逐引脚接线图册](docs/hardware/接线图册/接线图册.html)：80孔索引、47条连接、安装位置、工具与逐点验证；[系统总览](docs/hardware/系统接线图.svg)是安装示意，图册由 `scripts/build_wiring_guide.py` 生成，需Pillow。
- [引脚确定与接线表](docs/hardware/引脚确定与接线表.md)：候选GPIO/连接器针号、官方来源、实物核对边界。
- [保护板接线与布局](docs/hardware/保护板接线与布局.md)：保留原尺寸试摆、器件分区、网络与焊接要求；[验证操作与记录](docs/hardware/接线验证操作与记录.md)给出表笔测点和预期值。
- [手机选购清单](docs/hardware/手机选购清单.html)：手机可直接点的卡片式清单（`python -B scripts/build_shopping_list.py` 生成）。
- [淘宝核对关键词](docs/hardware/淘宝核对关键词.md)：按 BOM 编号的手动搜索词、判定要点与参考价，用于回填清单。
- [相机选型与成本](docs/hardware/相机选型与成本.md)：为什么选USB UVC定焦模组、硬性规格、机位补光、已选定HBVCAM-F2170HD具体SKU、SKU取舍、到货验证与50.56元增量预算。
- [阶段 0 设计](docs/stages/stage-00-architecture.md)：本轮重点评审入口，定义状态机、进程/任务、职责和故障规则。
- [API链接与调用链](docs/architecture/api-links.md)：手机/终端地址、公开入口到内部函数及MCU消息的对应。
- [各阶段调用链](docs/architecture/stage-call-chains.md)与[接口追踪表](docs/architecture/traceability.md)：24个公开入口、82个内部C端口、11类UART请求和28个业务/监督RPC的实施/验收归属。
- [接口契约](contracts/README.md)：24 个公开入口、内部 C 端口、11 类串口请求、样例与检查方式。
- [v11视觉架构](docs/architecture/vision-architecture.md)、[接口/地址](docs/architecture/vision-interfaces.md)、[AI接线说明](docs/architecture/vision-ai-handoff.md)：MobileNetV3-Large七分类，独立vision.sock，不改MCU UART。
- [模块与 AI 所有权](docs/architecture/module-ownership.md)：后续 AI 的目录与修改边界。
- [阶段 1 任务卡](docs/stages/stage-01-task-cards.md)：主机骨架、仓储、模拟器与验证；S1-01至S1-06已DONE，下一卡S1-07。
- [材料与HAL影响](docs/hardware/材料变更影响与HAL边界.md)、[本地HAL核对](docs/hardware/本地HAL库核对.md)：材料替换不翻转业务状态，HAL适配不改公开协议。
- [阶段 2 HAL任务卡](docs/stages/stage-02-hal-hardware-task-cards.md)：后续硬件门槛，尚未执行。
- [阶段2B视觉八卡](docs/stages/stage-02b-vision-task-cards.md)：S2-09—16接模型/相机/worker/融合；S2-08只过基础子门禁，S2-16为阶段2完整门禁。
- [阶段 3 客户端任务卡](docs/stages/stage-03-clients-task-cards.md)：Android/LVGL的7张卡及配对/幂等/异步IPC。
- [阶段 4 监护任务卡](docs/stages/stage-04-monitoring-task-cards.md)：容量、供电、真实进度、IWDG与固定UI恢复的8张卡。
- [阶段 5 交付任务卡](docs/stages/stage-05-delivery-task-cards.md)：部署、离线备份、兼容更新和现场验收的7张卡。
- [单卡交接模板](docs/stages/task-handoff-template.md)：交给其他AI的提示词、证据和变更请求格式。
- [当前视觉设计验证](docs/evidence/vision-design-2026-10-22/summary.md)：0.2.1核对、静态检查和未验证项；早期0.1.2记录保留为历史证据。

按用户更新清单，新增配件和运费上限为 **200元，待购F407板单独预算**。候选金额与补件预留不是成交价，最终核算见材料清单；不能删保护件或漏运费凑数。

相机接口已定为USB UVC免驱定焦模组（候选CAM-UVC-01）。用户已报告器件全部到货，相机实际型号/SKU与候选匹配、结算金额和RK采集仍待核对；判据与到货验证见[相机选型与成本](docs/hardware/相机选型与成本.md)。视觉增量为独立预算（已选定HBVCAM-F2170HD 75°手动调焦33.76元+补光灯9.80元+运费7元=50.56元，单独上限55元；补光走用户已有充电头），不占用也不从200元扣减。已有PT/ONNX不代表RKNN/柜内识别通过，模型源目录保持不变。

设计检查：`python -B scripts/check_contracts.py`、`python -B scripts/check_stage_plan.py`、`python -B scripts/check_vision_contracts.py`。它们不代表业务构建、推理或真机通过。
