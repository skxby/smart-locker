# 分阶段开发入口

当前由本会话完成开发、验证、整改和交接，取消开发AI/独立审计AI分工；按[执行规则](../development/execution-policy.md)执行。旧提示词中的独立审计及并发操作段仅作历史参考，功能依赖与实物验收继续有效。

阶段0为设计基线；阶段一S1-01至S1-07已完成（DONE），主机模拟门禁PASS。当前active_task=null，recommended_next=S2-01，S2-01尚未开始。其余进度以机器索引与真实报告为准。阶段号表示依赖，文档存在不代表功能完成。主机端默认用户已有VMware Ubuntu，见[VM工作流](../development/vm-ubuntu-workflow.md)。

## 1. 固定路线

| 阶段 | 单独交付的能力 | 任务卡 | 开发门禁 |
|---|---|---|---|
| 0 | 架构/状态/公开/内部/串口契约 | [设计基线](stage-00-architecture.md) | 静态一致性；不是运行验收 |
| 1 | Linux C服务、SQLite、模拟设备 | [7张卡](stage-01-task-cards.md) | S1-07真实HTTPS/IPC/字节流模拟闭环 |
| 2A | HAL、真实Journal、硬截止、RK串口/基础柜格 | [S2-01—08](stage-02-hal-hardware-task-cards.md) | S2-08仅基础子门禁，显式无视觉模式 |
| 2B | v11模型/相机/RKNN/私有IPC/Core融合 | [S2-09—16](stage-02b-vision-task-cards.md) | S2-16为阶段2完整门禁；相机/预算未知不能通过 |
| 3 | Android与LVGL、同一业务接口 | [7张卡](stage-03-clients-task-cards.md) | S3-07双端实物/TLS/权限/幂等 |
| 4 | 容量/供电/真实进度/IWDG/固定UI恢复 | [8张卡](stage-04-monitoring-task-cards.md) | S4-08故障与恢复实测 |
| 5 | 固定部署/备份/更新/维护/现场交付 | [7张卡](stage-05-delivery-task-cards.md) | S5-07交付证据完整 |

仍为5个开发阶段，阶段2拆成2A/2B；共45卡，S3-01依赖S2-16。当前单会话开发与验证，不启用旧并发调度。

机器索引[stage-plan.json](../../config/development/stage-plan.json)记录卡ID/依赖/目录/规格/真实实施状态；[调用链追踪表](../architecture/traceability.md)记录接口何时实现和验收。不另建一份覆盖这些入口的TODO。

## 2. 一次只执行一张卡

1. 读本文件、README、该卡及它引用的接口/模块README。核对契约和前置证据；前置未过不实现依赖功能。
2. 报告当前卡目标、修改路径和验收；不把所有未来阶段展开成一个编码任务。
3. 索引中本卡设IN_PROGRESS。卡内可以细分步骤，不能改变目标/验收或建另一套接口。active_task取实际执行卡，不能用静态手册覆盖正在进行的任务。
4. 完成实现、必要回归、文档和残留检查。缺环境/实物标BLOCKED并说明，不能用mock冒充验收。
5. 交付卡报告，记录实际变更/命令/结果/限制及下一张可执行卡；通过才标DONE。阶段末审计卡核查全部前置证据。
6. 单卡完成停在交接点。若用户授权连续完成一个阶段，可依赖序继续该阶段；没有该授权不自动跨卡/跨阶段。证据门禁不意味着每一步另要用户批准。

卡状态：NOT_STARTED→IN_PROGRESS→DONE，或IN_PROGRESS→BLOCKED；修复返回IN_PROGRESS。DONE必须有卡报告。门禁为NOT_RUN/PASS/FAIL；静态检查PASS不能改变实施状态或实物门禁。

单卡报告固定为docs/evidence/stage-XX/卡ID/summary.md，阶段门禁另在docs/evidence/stage-XX/summary.md汇总并链接各卡。审计卡自身也有单卡报告；不能拿README或任意存在文件充当验收证据。下一阶段开工要求上一阶段gate_result=PASS，不能只看最后一张卡写了DONE。

## 3. 固定目标和变更边界

- 一柜格、一固定工具、同LAN Android、RK3568 C/LVGL、STM32F407 HAL/FreeRTOS；预算和板卡另算规则保持既定范围。
- RK Core唯一写账本，MCU Control唯一启动锁；监督只恢复固定UI。
- 排队/持久化/接纳/脉冲/开门/借还完成分别记录；可能执行过不重放，补传不产生到柜证明。
- 引脚/极性只在BSP/profile适配；未知不填常见默认值。依赖先复用已锁定环境，按当前卡补齐；用户原包保持只读。
- 共享接口缺口提交变更请求，负责人统一修正再继续依赖部分；不能各AI自行补相似API。
- 字段/签名以机器真源为准，业务语义以主规格为准，专题接口和任务卡细化实施；出现冲突先统一修正。专家报告/历史证据不是第二套规则。

## 4. 给新 AI 的入口

用户操作见[完整执行手册](../AI开发执行手册.md)，全部45卡的180份现成指令见[索引](../ai-prompts/README.md)；每卡均含开发/审计/整改/交接，直接复制对应段落即可。

使用[单卡交接模板](task-handoff-template.md)，只填写当前卡ID，不一次要求实现45张卡。阶段一7卡均DONE，报告位于`docs/evidence/stage-01/`各卡目录；[S1-07报告](../evidence/stage-01/S1-07/summary.md)记录门禁PASS证据。当前active_task=null、recommended_next=S2-01，S2-01尚未开始。

重新打开对话，从active_task和最近报告恢复。状态与文件冲突先核对事实；不重复已发生的物理动作。
