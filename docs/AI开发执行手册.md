# AI开发完整执行手册

本手册包含全部45张任务卡及180份已填卡号的开发、审计、整改、收尾指令。
每卡给出真实前置、修改范围、接口定位、原卡验收、固定交付路径与下一卡。
只需找到当前卡，复制其当前步骤的一个指令块；不用自己替换卡号或拼接架构文件。

生成内容来自正式阶段卡、stage-plan、interface-map与通用交接流程；契约基线0.2.1。
这份手册没有启动产品开发或修改任何卡状态。STM32环境测试完成不等于产品卡完成。

## 使用顺序

开发指令→开发报告→独立审计指令→必要时整改指令→复核→收尾交接指令→下一卡。
默认串行，普通实现/回归在当前卡授权内持续完成；需要连续阶段时按通用流程明确授权。
同一时间由唯一集成负责人写状态/根构建/共享DDL/契约；独立审计只读业务。

## 45卡导航

| 卡号 | 目标 | 前置 | 完整指令 |
|---|---|---|---|
| S1-01 | 构建与基础设施 | 无 | [开发/审计/整改/交接](ai-prompts/S1-01.md) |
| S1-02 | SQLite仓储 | S1-01 | [开发/审计/整改/交接](ai-prompts/S1-02.md) |
| S1-03 | 设备协议与模拟器 | S1-01 | [开发/审计/整改/交接](ai-prompts/S1-03.md) |
| S1-04 | 业务核心 | S1-02、S1-03 | [开发/审计/整改/交接](ai-prompts/S1-04.md) |
| S1-05 | HTTPS/IPC | S1-04 | [开发/审计/整改/交接](ai-prompts/S1-05.md) |
| S1-06 | 装配与初始化 | S1-05 | [开发/审计/整改/交接](ai-prompts/S1-06.md) |
| S1-07 | 主机集成审计 | S1-06 | [开发/审计/整改/交接](ai-prompts/S1-07.md) |
| S2-01 | HAL与BSP | S1-07 | [开发/审计/整改/交接](ai-prompts/S2-01.md) |
| S2-02 | 输入与SPI | S2-01 | [开发/审计/整改/交接](ai-prompts/S2-02.md) |
| S2-03 | UART DMA | S2-02 | [开发/审计/整改/交接](ai-prompts/S2-03.md) |
| S2-04 | 真实Journal | S2-02 | [开发/审计/整改/交接](ai-prompts/S2-04.md) |
| S2-05 | Control与硬截止 | S2-03、S2-04 | [开发/审计/整改/交接](ai-prompts/S2-05.md) |
| S2-06 | RK串口及首用部署 | S2-05 | [开发/审计/整改/交接](ai-prompts/S2-06.md) |
| S2-07 | 整柜闭环 | S2-06 | [开发/审计/整改/交接](ai-prompts/S2-07.md) |
| S2-08 | 硬件阶段审计 | S2-07 | [开发/审计/整改/交接](ai-prompts/S2-08.md) |
| S2-09 | 模型资产/导出来源 | S2-08 | [开发/审计/整改/交接](ai-prompts/S2-09.md) |
| S2-10 | 统一预处理/量化验证 | S2-09 | [开发/审计/整改/交接](ai-prompts/S2-10.md) |
| S2-11 | 相机与现场profile | S2-10 | [开发/审计/整改/交接](ai-prompts/S2-11.md) |
| S2-12 | RKNN视觉worker | S2-11 | [开发/审计/整改/交接](ai-prompts/S2-12.md) |
| S2-13 | 私有视觉IPC适配 | S2-12 | [开发/审计/整改/交接](ai-prompts/S2-13.md) |
| S2-14 | Core持久证据与API | S2-13 | [开发/审计/整改/交接](ai-prompts/S2-14.md) |
| S2-15 | 视觉实物借还联调 | S2-14 | [开发/审计/整改/交接](ai-prompts/S2-15.md) |
| S2-16 | 视觉与阶段2审计 | S2-15 | [开发/审计/整改/交接](ai-prompts/S2-16.md) |
| S3-01 | 客户端类型与生命周期 | S2-16 | [开发/审计/整改/交接](ai-prompts/S3-01.md) |
| S3-02 | Android配对会话 | S3-01 | [开发/审计/整改/交接](ai-prompts/S3-02.md) |
| S3-03 | 借还与幂等恢复 | S3-02 | [开发/审计/整改/交接](ai-prompts/S3-03.md) |
| S3-04 | 管理与变化重查 | S3-03 | [开发/审计/整改/交接](ai-prompts/S3-04.md) |
| S3-05 | LVGL异步IPC | S3-01 | [开发/审计/整改/交接](ai-prompts/S3-05.md) |
| S3-06 | 双端真机闭环 | S3-04、S3-05 | [开发/审计/整改/交接](ai-prompts/S3-06.md) |
| S3-07 | 客户端阶段审计 | S3-06 | [开发/审计/整改/交接](ai-prompts/S3-07.md) |
| S4-01 | 日志容量与回收 | S3-07、S2-08 | [开发/审计/整改/交接](ai-prompts/S4-01.md) |
| S4-02 | 供电关联 | S4-01 | [开发/审计/整改/交接](ai-prompts/S4-02.md) |
| S4-03 | MCU进度与IWDG | S4-02 | [开发/审计/整改/交接](ai-prompts/S4-03.md) |
| S4-04 | 监督观察及健康搬运 | S4-03 | [开发/审计/整改/交接](ai-prompts/S4-04.md) |
| S4-05 | 固定UI恢复 | S4-04 | [开发/审计/整改/交接](ai-prompts/S4-05.md) |
| S4-06 | 监护与业务接线 | S4-05 | [开发/审计/整改/交接](ai-prompts/S4-06.md) |
| S4-07 | 故障矩阵 | S4-06 | [开发/审计/整改/交接](ai-prompts/S4-07.md) |
| S4-08 | 监护阶段审计 | S4-07 | [开发/审计/整改/交接](ai-prompts/S4-08.md) |
| S5-01 | 制品与平台清单 | S4-08 | [开发/审计/整改/交接](ai-prompts/S5-01.md) |
| S5-02 | 启动权限配置 | S5-01 | [开发/审计/整改/交接](ai-prompts/S5-02.md) |
| S5-03 | 离线维护备份 | S5-02 | [开发/审计/整改/交接](ai-prompts/S5-03.md) |
| S5-04 | 更新与程序回退 | S5-03 | [开发/审计/整改/交接](ai-prompts/S5-04.md) |
| S5-05 | 现场验收 | S5-04 | [开发/审计/整改/交接](ai-prompts/S5-05.md) |
| S5-06 | 文档与演示 | S5-05 | [开发/审计/整改/交接](ai-prompts/S5-06.md) |
| S5-07 | 最终交付审计 | S5-06 | [开发/审计/整改/交接](ai-prompts/S5-07.md) |

## 通用操作与验收方法

### 通用开发与交接流程

本文件说明操作流程；全部45张卡的180份已填写指令见[指令索引](ai-prompts/README.md)或[完整手册](AI开发执行手册.md)。本文件不启动产品开发、不改变卡状态。S1-01、S1-02 已完成（DONE），下一卡 S1-03，具体状态以stage-plan和报告为准；STM32环境已编译通过，不替代其他产品卡验收。

#### 1. 你负责什么，AI负责什么

你负责决定本次做哪张卡、提供真实硬件条件、查看演示/测试证据及决定是否继续。开发AI负责实现、必要测试、文档和交接；审计AI独立核对实际文件与测试；架构负责人统一处理跨模块接口变更。

| 角色       | 每次给它什么              | 应交付什么                      | 不能做什么                 |
| -------- | ------------------- | -------------------------- | --------------------- |
| 开发AI     | 一张卡、项目路径、前置报告、对应契约  | 代码、真实构建/测试、summary和handoff | 顺手开发后续卡、另造接口、修改验收来过关  |
| 审计AI     | 同一张卡、当前文件/变更清单、测试报告 | audit.md，列阻塞/非阻塞/未验证项及证据   | 只读开发AI总结就判通过、悄悄改业务    |
| 架构/集成负责人 | 已证实的接口冲突或审计结果       | 统一契约修正/集成/真实状态             | 多个模块各自维护同一DDL/公共头/根构建 |

可以用同一AI工具的两个不同对话担任开发与审计。没有独立审计条件就如实注明“开发者自检”，不能改名为“独立审计”。前期默认串行，不需要同时开许多AI对话。

#### 2. 每张卡的固定流程

```text
选当前卡 → 检查前置/工作区 → 开发AI实现并实测
→ 开发报告与交接包 → 审计AI只读复核
→ 有问题退回同一开发卡修复 → 重验受影响项
→ 集成负责人确认DONE → 发下一张已解锁卡
```

常规实现、测试和修复在本卡授权内持续完成，不为每个函数另向你要确认。单卡交付点用于审阅结果，不能用“先问你下一步”代替本卡必要工作。

推荐你每轮只做四件事：发开发提示词；把完成结果交审计；把问题交原开发AI整改；通过后发下一卡。新聊天看不到旧聊天全部细节时，以文件中的真实报告交接。

#### 3. 每次发哪些文件

能直接访问%REPO%的AI：给项目路径和下表，不必把整个仓库粘贴进对话。先读索引和当前卡，再按接口名定向读取schema/函数，不一次读全部45卡/模型数据/课程源码。

| 顺序 | 文件                                                       | 用途              |
| -- | -------------------------------------------------------- | --------------- |
| 1  | AGENTS.md、README.md                                      | 当前规则、状态与入口      |
| 2  | docs/stages/README.md、config/development/stage-plan.json | 当前卡、前置及门禁       |
| 3  | 当前卡所在阶段文档中的该卡和阶段公共约束                                     | 目标/路径/交付/验收     |
| 4  | docs/architecture/traceability.md、module-ownership.md    | 接口归属与修改边界       |
| 5  | 该卡引用的contract-source/internal-data/uart/vision源、生成头和专题规格 | 精确字段、函数、失败与生命周期 |
| 6  | 前置卡的summary.md、handoff.md、audit.md与相关模块README            | 已实现事实与调用方法      |

无法访问本机文件的AI：只提供必要文本/文件包，并要求它列出不能读取的输入；它可以产建议，但不能替你完成本机构建、修改状态或真机验收。不要上传整个训练数据集、token、私钥或登录凭据。

#### 4. 现在应该发哪张卡

**从S1-01开始。** 可直接复制[首次开发提示词](stages/S1-01-start-prompt.md)。

S1是RK业务服务的Linux主机开发，第一卡做CMake、基础库和依赖，不做STM32业务固件。Keil环境通过只说明MCU工具可用，不能代替Linux CMake/CTest。后续默认复用用户已有VMware Ubuntu，不要求WSL。VM单独建立$HOME/locker-workspace，src/build/runtime/logs/artifacts分开，生成物不写源码目录。S1-01已经有实现与历史VM构建记录，应保留成果，按[VM工作流](development/vm-ubuntu-workflow.md)补当前整改源复验，不重做基础库、不迁移历史日志标签。

前7卡推荐串行：

| 顺序 | 发给开发AI的卡         | 验收时你重点看什么                           |
| -- | ---------------- | ----------------------------------- |
| 1  | S1-01 构建/基础设施    | Linux配置、编译、CTest实际输出；队列满/u64边界/生命周期 |
| 2  | S1-02 SQLite仓储   | 事务回滚、重开数据库、并发占位/事件去重、水位数值比较         |
| 3  | S1-03 字节协议/独立模拟器 | 黄金帧、拆粘包、重启去重、丢ACK无重复动作              |
| 4  | S1-04 业务核心       | 完整状态转移、权限/确认冲突、可能已执行不重放             |
| 5  | S1-05 HTTPS/IPC  | 实际接口返回、TLS/权限负例、幂等、受限本机证明           |
| 6  | S1-06 装配/初始化     | 空目录初始化→启动→退出→重启，恢复先核对               |
| 7  | S1-07 阶段审计       | 通过真实HTTPS/IPC/字节流联调，阶段完整报告          |

S1-02与03在01通过后具备并行依赖，但初期仍建议串行。S1-04必须等两者通过。通过S1-01不表示整个阶段1通过。

#### 5. 开发AI完成时必须交什么

以下是未来每卡的正式交付，不要现在创建空报告或假PASS：

```text
docs/evidence/stage-01/S1-01/
  summary.md    实际实现/环境/命令/退出码/测试/限制/文件变更
  handoff.md    下一AI如何调用、构建、复验，以及禁止误用的边界
  audit.md      独立审计实际发生后才写
  正式日志/必要截图/测试证据（按需要存，不塞密码/私钥）
```

阶段2A和2B均用stage-02，不用虚构stage-02b证据目录。其他阶段换成stage-03/04/05。

handoff.md至少写清：

```text
卡ID、当前契约版本、输入源文件/代码指纹
实现的函数/API/消息，源码和对应正式测试在哪里
调用约定：参数、返回、谁持有内存、在哪个线程、何时释放
从干净环境怎么构建/运行/测试，实际结果与日志路径
失败/超时/重复/重启如何处理，哪些功能没有实现
本卡允许修改和实际改动文件，文档/残留自查
下一张可执行卡及前置；下一AI先读哪些文件/复用哪些接口
```

有Git时可记录真实commit或diff；未建Git时记录文件清单和SHA256，不能捏造提交号。本项目没有默认commit/push授权。

#### 6. 交给独立审计AI的提示词

每张卡的审计指令已经在[指令索引](ai-prompts/README.md)中填写完整，直接复制对应卡的“独立审计指令”。下面仅演示S1-01的操作形式：

```text
只审计%REPO%的S1-01，不开始下一卡。
先读AGENTS、当前卡及其前置、当前契约和相关模块README，
再读docs/evidence/stage-01/S1-01/summary.md与handoff.md。
独立查看实际代码/变更清单/正式测试，不能只相信开发AI的自述。
按卡核对职责、接口字段、错误/超时/重复/重启、内存/线程生命周期和资源上限。
检查有没有未授权的公共API、占位成功、业务旁路、后续卡代码或凭据泄露。
在环境支持时独立运行相关构建/测试；不能运行的项写NOT_RUN，不借历史PASS顶替。
只写本卡audit.md，不修改业务实现、契约、验收或stage-plan状态。
每项问题给出文件/行号、触发方式、影响、建议及阻塞级别。
最终列：阻塞项、非阻塞项、未验证项、实际命令与结果、是否满足本卡交接条件。
这是实现审计；静态检查、模拟运行与真机结果分开报告。
```

审计者不是“找不到问题就必须造几个问题”；也不能用“AI感觉没问题”作结论。只有证据支持的缺陷才退回修，范围外改进另列，不无限扩大本卡。

#### 7. 审计有问题，怎样退回

把audit.md路径交回原开发AI：

```text
继续修复S1-01的审计阻塞项，审计文件为
docs/evidence/stage-01/S1-01/audit.md。
逐项复现、修复、运行相关回归，更新summary和handoff，并说明各问题对应证据。
不要改验收预期、删失败测试、扩大范围或开始S1-02。
若根因是共享接口冲突，提交CR说明，由契约负责人统一处理；
可继续独立部分，不在本模块另造兼容JSON/函数。
完成后交回审计复核，未验证项保持NOT_RUN。
```

新对话重做整改时同样从文件恢复，不要求它猜原对话内容。接口变更应同时更新真源、生成器/生成物、调用方、正式样例、测试、版本与当前文档；一端私改后“能编译”不算解决。

#### 8. 状态由谁改，何时开始下一卡

推荐开发AI兼任本轮唯一集成负责人维护stage-plan；审计AI只读。等待独立复核时本卡保留IN_PROGRESS，不发下游任务。审计无阻塞且本卡必要验收已实跑后，再由同一负责人收尾标DONE；不引入机器索引不支持的REVIEWED等状态。

| 时机              | 单卡状态/报告                                    |
| --------------- | ------------------------------------------ |
| 尚未开始            | NOT_STARTED，report=null                    |
| 开发/整改/待本轮审计     | IN_PROGRESS，active_task=当前卡                |
| 缺环境/材料，无法继续关键工作 | BLOCKED，说明缺什么；active_task=null，不伪装成仍在执行    |
| 本卡验收及交接完成       | DONE，report指向本卡summary.md，active_task=null |

每次同步recommended_next与阶段implementation_status。阶段最后一张审计卡通过时，才写阶段summary.md，并一起设置gate_result=PASS及固定evidence路径；阶段内普通卡DONE不能擅自把整阶段设PASS。

标DONE前可给集成负责人这条短指令：

```text
按本卡真实测试和独立审计结果完成收尾；阻塞项必须关闭。
同步相关README、summary、handoff及stage-plan，运行适用的设计检查。
满足条件才标DONE；说明下一张已解锁卡后停止，不开始下一卡。
```

下一AI的提示词例子：

```text
只执行S1-02。
先核对stage-plan中S1-01已DONE，以及它的summary/handoff/audit实际证据。
读取阶段1的S1-02卡、Repository契约和模块所有权。
复用已有基础库和局部构建入口，只改本卡目录及相关正式测试/文档。
不要重建基础设施、修改根CMake、Core业务规则或公共契约；
需要根入口接入时交唯一集成负责人处理。
按本卡完成实现/测试/交接，不继续S1-03。
```

#### 9. 整个项目什么时候换阶段

```text
S1-01..07 → S2-01..08（2A基础实物）
→ S2-09..16（2B模型/相机/视觉）
→ S3-01..07（Android/LVGL）
→ S4-01..08（记录/监护）
→ S5-01..07（部署/维护/交付）
```

S2-08只解锁视觉卡S2-09；完整S2-16通过才进入S3。未购/未接板的材料不能用模拟验收替代；模型有PT/ONNX不代表RKNN、相机新鲜度或柜内精度已验证。

想少发指令，可以只授权连续完成一个阶段：

```text
连续完成阶段1，按任务卡依赖顺序逐卡执行，不进入阶段2。
每卡分别交付代码/真实测试/文档/报告，允许独立审计子代理只读复核。
发现阻塞先在当前卡修复或如实记录，不能跳卡/降低验收。
只有本卡通过才开始下一张；完成S1-07阶段门禁后报告并停止。
根构建/DDL/契约/状态各由唯一负责人维护，不让多个AI并发覆盖。
若没有独立审计工具，明确说明实际复核方式，不冒称独立。
```

这是可选的阶段授权模板，本轮说明手册不等于已经授予连续实现权限。

#### 10. 你自己验收时看这六件事

1. 本卡要求是否实际完成，是否顺手写了下一卡？
2. 构建/测试有没有实际环境、完整命令、退出码与日志？
3. 是否把模拟、静态检查、声明存在当真机可用？
4. 错误/超时/重复/重启有没有对应正式测试，而不只成功截图？
5. 下一AI能否只凭handoff找到源码、接口和复现方法？
6. 文档/状态是否等于事实，有无空成功、调试路由、假数据或凭据残留？

遇到“只差硬件所以全部通过”“先关TLS跑通”“先重发一次开锁”“换个字段就行”“没跑但应该能过”，均不能作为交接完成依据。先回本卡按既定边界处理。


## 全部卡片的可复制执行指令

以下每卡4份指令互相独立。Ctrl+F搜索“# S2-11”等卡标题，或使用上表单卡文件。

### S1-01：构建与基础设施

前置：无前置实施卡。下一张计划：S1-02 SQLite仓储。

[原始卡与公共约束](stages/stage-01-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S1-01 构建与基础设施；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- 无前置实施卡；仍需检查当前工作区、契约和本卡运行环境。
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-01/批次、logs/S1-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S1-01，不自动开始S1-02。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/common/
- CMakeLists.txt
- deployment/host/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **输入**：主规格、公共 C 接口、资源上限和模块所有权。
- **可改目录**：根 `CMakeLists.txt`、`host/common/`、`tests/unit/common/`、`deployment/host/` 的依赖说明；根入口仅该卡/集成负责人维护。
- **产出**：模块化 CMake；有界队列、单调时钟、UUID/u64 边界工具、可注入测试时钟、受限日志；依赖锁定清单。业务生产路径使用真实时钟，测试替身不能通过公共 API 控制。
- **验收**：干净 Linux 环境可配置并构建基础库；队列满时明确失败；消息生命周期不依赖调用方栈；u64 零、最大值、越界和非法文本有测试；日志不输出 token/密码。
- **禁止越界**：不建业务状态、不编造 REST 路由、不实现锁驱动、不先搭一个与正式类型不同的临时框架。
- **DONE 证据**：运行环境、锁定依赖来源/许可、配置/构建/CTest 命令和实际结果。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-01/S1-01/summary.md和docs/evidence/stage-01/S1-01/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S1-01 构建与基础设施；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- 无前置实施卡；仍需检查当前工作区、契约和本卡运行环境。
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-01/批次、logs/S1-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S1-01；不接着实现后续卡。
读取docs/evidence/stage-01/S1-01/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-01/S1-01/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S1-01 构建与基础设施；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- 无前置实施卡；仍需检查当前工作区、契约和本卡运行环境。
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-01/批次、logs/S1-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S1-01，问题来源为docs/evidence/stage-01/S1-01/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S1-01 构建与基础设施；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- 无前置实施卡；仍需检查当前工作区、契约和本卡运行环境。
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-01/批次、logs/S1-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S1-01；依据本卡真实验收与docs/evidence/stage-01/S1-01/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-01/S1-01/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S1-02，规格文件docs/stages/stage-01-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S1-02：SQLite仓储

前置：S1-01。下一张计划：S1-03 设备协议与模拟器。

[原始卡与公共约束](stages/stage-01-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S1-02 SQLite仓储；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-01: docs/evidence/stage-01/S1-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-02/批次、logs/S1-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S1-02，不自动开始S1-03。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/storage/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: locker_repo_commit_change（归属：implementation）
internal_ports: locker_repo_import_events（归属：implementation）
internal_ports: locker_repo_load_recovery（归属：implementation）
internal_ports: locker_repo_open（归属：implementation）
internal_ports: locker_repo_query（归属：implementation）
internal_ports: locker_repo_close（归属：implementation）

本卡验收依据全文如下，不得缩减：
- **输入**：主规格的四类状态、幂等/outbox、事件键、水位和恢复规则；Repository 声明。
- **可改目录**：`host/storage/`、`tests/unit/storage/`；只提供局部 CMake 接入文件。
- **产出**：唯一 `schema-v1.sql`、迁移元数据、Repository 实现；逻辑覆盖用户/会话/卡绑定、工具/借用、操作、动作/outbox、设备事件/游标、幂等记录和人工核验。不允许 HTTP/UI 直接调用仓储。
- **固定约束**：按internal-data.schema.json实现受控集合、事务/查询/分页恢复DTO，不另造跨模块结构。操作占位与账本变化由事务保护；操作+幂等+outbox 一起提交；新事件+Core提供的投影变化+游标一起提交；事件唯一键含 journal_epoch；u64 按已校验十进制文本存储并按数值比较。WAL、`synchronous=FULL`；首次初始化与损坏/迁移失败区分，失败不能自动清库。
- **验收**：同一事件重复导入不重复推进；失败事务不留下半个占位/outbox；两个竞争申请只能一个成功；数据库重新打开仍保留有效借用、未决操作和幂等结果；游标不能越过缺口、倒退或把 10 排在 2 前。
- **禁止越界**：不自行决定借还成功、不发送设备动作、不更改公共 schema 或状态枚举。
- **DONE 证据**：DDL 与字段映射说明、事务/重开数据库测试、错误路径结果；注明尚未验证 RK 存储介质掉电可靠性。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-01/S1-02/summary.md和docs/evidence/stage-01/S1-02/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S1-02 SQLite仓储；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-01: docs/evidence/stage-01/S1-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-02/批次、logs/S1-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S1-02；不接着实现后续卡。
读取docs/evidence/stage-01/S1-02/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-01/S1-02/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S1-02 SQLite仓储；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-01: docs/evidence/stage-01/S1-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-02/批次、logs/S1-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S1-02，问题来源为docs/evidence/stage-01/S1-02/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S1-02 SQLite仓储；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-01: docs/evidence/stage-01/S1-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-02/批次、logs/S1-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S1-02；依据本卡真实验收与docs/evidence/stage-01/S1-02/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-01/S1-02/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S1-03，规格文件docs/stages/stage-01-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S1-03：设备协议与模拟器

前置：S1-01。下一张计划：S1-04 业务核心。

[原始卡与公共约束](stages/stage-01-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S1-03 设备协议与模拟器；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-01: docs/evidence/stage-01/S1-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-03/批次、logs/S1-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S1-03，不自动开始S1-04。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/device/
- host/simulator/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: locker_device_open（归属：implementation）
internal_ports: locker_device_get_snapshot（归属：implementation）
internal_ports: locker_device_execute_once（归属：implementation）
internal_ports: locker_device_query_command（归属：implementation）
internal_ports: locker_device_fetch_events（归属：implementation）
internal_ports: locker_device_ack_persisted（归属：implementation）
internal_ports: locker_device_resume_ready（归属：implementation）
internal_ports: locker_device_release_context（归属：implementation）
internal_ports: locker_device_send_health（归属：implementation）
internal_ports: locker_device_create（归属：implementation）
internal_ports: locker_device_close（归属：implementation）
internal_ports: locker_device_destroy（归属：implementation）
internal_ports: locker_device_time_anchor（归属：implementation）
internal_ports: locker_device_bind_sink（归属：implementation）
uart_requests: HELLO（归属：host）
uart_requests: GET_SNAPSHOT（归属：host）
uart_requests: FETCH_EVENTS（归属：host）
uart_requests: ACK_PERSISTED（归属：host）
uart_requests: RESUME_READY（归属：host）
uart_requests: DISARM（归属：host）
uart_requests: EXECUTE_UNLOCK（归属：host）
uart_requests: QUERY_COMMAND（归属：host）
uart_requests: HOST_HEALTH（归属：host）
uart_requests: TIME_ANCHOR（归属：host）
uart_requests: RELEASE_CONTEXT（归属：host）

本卡验收依据全文如下，不得缩减：
- **输入**：DevicePort、UART-v1、黄金帧、会话和重连屏障规则。
- **可改目录**：`host/device/`、`host/simulator/`、`tests/unit/device/`、`tests/contract/device/`；不改业务数据库。
- **产出**：协议编码/流式解析；DevicePort 的模拟 socket 实现；`locker-device-sim` 经 `AF_UNIX SOCK_STREAM` 提供 UART-v1 字节，另用权限 `0600` 的私有控制入口注入门/物品、读卡、断链、拒绝、丢 ACK、事件缺口与模拟重启。模拟设备使用独立 SQLite 文件保存设备日志/动作水位，不共享业务库。
- **验收**：黄金帧逐字节一致；半帧、多帧、CRC/长度错误不越界；旧 session、重复 action_no、跳号都不重复动作；模拟器重启后动作查询及事件键仍可核对；ACK 和 PULSE_STARTED 可独立控制；日志提交失败时拒绝新动作。
- **禁止越界**：不创建公共 HTTP 模拟接口，不接串口实物，不用真实 HAL，不把进程/SQLite 模拟重启结果称为 MCU/littlefs 掉电验收。
- **DONE 证据**：字节解析测试、控制入口说明、独立持久化路径及重启场景记录；明确“硬件脉冲只模拟计数”。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-01/S1-03/summary.md和docs/evidence/stage-01/S1-03/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S1-03 设备协议与模拟器；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-01: docs/evidence/stage-01/S1-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-03/批次、logs/S1-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S1-03；不接着实现后续卡。
读取docs/evidence/stage-01/S1-03/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-01/S1-03/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S1-03 设备协议与模拟器；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-01: docs/evidence/stage-01/S1-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-03/批次、logs/S1-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S1-03，问题来源为docs/evidence/stage-01/S1-03/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S1-03 设备协议与模拟器；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-01: docs/evidence/stage-01/S1-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-03/批次、logs/S1-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S1-03；依据本卡真实验收与docs/evidence/stage-01/S1-03/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-01/S1-03/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S1-04，规格文件docs/stages/stage-01-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S1-04：业务核心

前置：S1-02、S1-03。下一张计划：S1-05 HTTPS/IPC。

[原始卡与公共约束](stages/stage-01-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S1-04 业务核心；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-02: docs/evidence/stage-01/S1-02/summary.md、handoff.md、audit.md
- S1-03: docs/evidence/stage-01/S1-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-04/批次、logs/S1-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S1-04，不自动开始S1-05。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/domain/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: locker_core_submit（归属：implementation）
internal_ports: locker_core_wait_result（归属：implementation）
internal_ports: locker_core_apply_device_event（归属：implementation）
internal_ports: locker_core_apply_device_reply（归属：implementation）
internal_ports: locker_core_tick（归属：implementation）
internal_ports: locker_core_query（归属：implementation）
internal_ports: locker_local_presence_submit（归属：implementation）
internal_ports: locker_core_create（归属：implementation）
internal_ports: locker_core_start（归属：implementation）
internal_ports: locker_core_stop（归属：implementation）
internal_ports: locker_core_destroy（归属：implementation）
internal_ports: locker_core_authenticate（归属：implementation）
internal_ports: locker_local_maintenance_reconcile（归属：implementation）
internal_ports: locker_core_get_progress（归属：implementation）
public_routes: login（归属：core）
public_routes: getCurrentSession（归属：core）
public_routes: logout（归属：core）
public_routes: getDevice（归属：core）
public_routes: getSnapshot（归属：core）
public_routes: listTools（归属：core）
public_routes: getTool（归属：core）
public_routes: createOperation（归属：core）
public_routes: getOperation（归属：core）
public_routes: confirmOperation（归属：core）
public_routes: cancelOperation（归属：core）
public_routes: listLoans（归属：core）
public_routes: listEvents（归属：core）
public_routes: listReviews（归属：core）
public_routes: resolveOperation（归属：core）
public_routes: listUsers（归属：core）
public_routes: createUser（归属：core）
public_routes: updateUser（归属：core）
public_routes: listCardObservations（归属：core）
public_routes: bindCard（归属：core）
public_routes: unbindCard（归属：core）
public_routes: subscribeChanges（归属：core）

本卡验收依据全文如下，不得缩减：
- **输入**：S1-02、S1-03 已通过的端口；主规格完整状态表和默认超时。
- **可改目录**：`host/domain/`、`tests/unit/domain/`、核心相关 `tests/integration/`。
- **产出**：Core 单所有者循环、申请/取消/确认/核验、有效借用投影、实时到柜证明、outbox 调度、事件幂等与恢复屏障；对外只使用统一类型，不依赖 CivetWeb 或 SQLite 的实现类型。
- **健康基线接口**：真实Core循环完成后发布instance/counter/sample副本，locker_core_get_progress非阻塞读取，不经Core等待队列。没有supervisor/UI时保持未知；同counter重取不续命，停滞测试不能只证明Runtime还活着。locker_core_apply_supervisor_health的实现归S4-04，本阶段不空实现冒充。
- **验收**：正常借还、只开门不取物、提前确认、错误用户、两个申请竞争、15 秒证明过期、各业务超时和 UNKNOWN 输入均有确定结果；仅 ACK 不进入开门；物理变化与本人确认都满足才完成；历史读卡不开柜；重启后先核对，可能执行过的动作不重放。
- **禁止越界**：不把“有当前离位快照”代替完整开门/物品变化证据；不允许管理员无理由覆盖；不调用阻塞网络；不能自动恢复或重放旧 EXECUTE。
- **DONE 证据**：状态转移场景矩阵与实际测试；对“准备→发送→ACK→脉冲→落库”中断位置记录预期和结果。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-01/S1-04/summary.md和docs/evidence/stage-01/S1-04/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S1-04 业务核心；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-02: docs/evidence/stage-01/S1-02/summary.md、handoff.md、audit.md
- S1-03: docs/evidence/stage-01/S1-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-04/批次、logs/S1-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S1-04；不接着实现后续卡。
读取docs/evidence/stage-01/S1-04/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-01/S1-04/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S1-04 业务核心；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-02: docs/evidence/stage-01/S1-02/summary.md、handoff.md、audit.md
- S1-03: docs/evidence/stage-01/S1-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-04/批次、logs/S1-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S1-04，问题来源为docs/evidence/stage-01/S1-04/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S1-04 业务核心；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-02: docs/evidence/stage-01/S1-02/summary.md、handoff.md、audit.md
- S1-03: docs/evidence/stage-01/S1-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-04/批次、logs/S1-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S1-04；依据本卡真实验收与docs/evidence/stage-01/S1-04/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-01/S1-04/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S1-05，规格文件docs/stages/stage-01-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S1-05：HTTPS/IPC

前置：S1-04。下一张计划：S1-06 装配与初始化。

[原始卡与公共约束](stages/stage-01-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S1-05 HTTPS/IPC；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-04: docs/evidence/stage-01/S1-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-05/批次、logs/S1-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S1-05，不自动开始S1-06。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/http/
- host/ipc/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: locker_events_try_publish（归属：implementation）
internal_ports: locker_events_create（归属：implementation）
internal_ports: locker_events_destroy（归属：implementation）
internal_ports: locker_http_start（归属：implementation）
internal_ports: locker_http_stop（归属：implementation）
internal_ports: locker_http_destroy（归属：implementation）
internal_ports: locker_ipc_start（归属：implementation）
internal_ports: locker_ipc_stop（归属：implementation）
internal_ports: locker_ipc_destroy（归属：implementation）
public_routes: login（归属：implementation）
public_routes: getCurrentSession（归属：implementation）
public_routes: logout（归属：implementation）
public_routes: getDevice（归属：implementation）
public_routes: getSnapshot（归属：implementation）
public_routes: listTools（归属：implementation）
public_routes: getTool（归属：implementation）
public_routes: createOperation（归属：implementation）
public_routes: getOperation（归属：implementation）
public_routes: confirmOperation（归属：implementation）
public_routes: cancelOperation（归属：implementation）
public_routes: listLoans（归属：implementation）
public_routes: listEvents（归属：implementation）
public_routes: listReviews（归属：implementation）
public_routes: resolveOperation（归属：implementation）
public_routes: listUsers（归属：implementation）
public_routes: createUser（归属：implementation）
public_routes: updateUser（归属：implementation）
public_routes: listCardObservations（归属：implementation）
public_routes: bindCard（归属：implementation）
public_routes: unbindCard（归属：implementation）
public_routes: subscribeChanges（归属：implementation）
local_methods: login（归属：implementation）
local_methods: getCurrentSession（归属：implementation）
local_methods: logout（归属：implementation）
local_methods: getDevice（归属：implementation）
local_methods: getSnapshot（归属：implementation）
local_methods: listTools（归属：implementation）
local_methods: getTool（归属：implementation）
local_methods: createOperation（归属：implementation）
local_methods: getOperation（归属：implementation）
local_methods: confirmOperation（归属：implementation）
local_methods: cancelOperation（归属：implementation）
local_methods: listLoans（归属：implementation）
local_methods: listEvents（归属：implementation）
local_methods: listReviews（归属：implementation）
local_methods: resolveOperation（归属：implementation）
local_methods: listUsers（归属：implementation）
local_methods: createUser（归属：implementation）
local_methods: updateUser（归属：implementation）
local_methods: listCardObservations（归属：implementation）
local_methods: bindCard（归属：implementation）
local_methods: unbindCard（归属：implementation）
local_methods: presence.submit（归属：implementation）
local_methods: maintenance.reconcile（归属：implementation）

本卡验收依据全文如下，不得缩减：
- **输入**：OpenAPI、已冻结 Core 端口、认证与本机信任边界。
- **可改目录**：`host/http/`、`host/ipc/`、对应 `tests/contract/` 和接口集成测试。
- **产出**：CivetWeb HTTPS/WS；Argon2id 密码验证、会话 token 哈希存储、角色/停用检查；REST 适配；受限 `service.sock`；WS 变化提示和重查。阶段 1 无真实 UI，由正式测试客户端代表已认证本机 UI。
- **验收**：响应字段/状态码符合契约；未知/越权身份拒绝；超大 body/分页边界正确；同键同意图返回原结果、同键异正文冲突；普通 HTTPS 无到柜证明或模拟入口；IPC 同时检查 SO_PEERCRED、socket 权限及当前用户会话；慢 WS 不阻塞 Core。
- **禁止越界**：不关 TLS、不提供默认密码/公网注册、不接受 actor_id 代替鉴权、不直接 SQL、不上传传感器事件、不添加任意开锁/进程命令路由。
- **DONE 证据**：OpenAPI 契约测试、TLS 信任失败测试、权限矩阵与 IPC 负例；全部测试证书只用于测试，密钥不进入正式版本库。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-01/S1-05/summary.md和docs/evidence/stage-01/S1-05/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S1-05 HTTPS/IPC；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-04: docs/evidence/stage-01/S1-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-05/批次、logs/S1-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S1-05；不接着实现后续卡。
读取docs/evidence/stage-01/S1-05/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-01/S1-05/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S1-05 HTTPS/IPC；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-04: docs/evidence/stage-01/S1-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-05/批次、logs/S1-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S1-05，问题来源为docs/evidence/stage-01/S1-05/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S1-05 HTTPS/IPC；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-04: docs/evidence/stage-01/S1-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-05/批次、logs/S1-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S1-05；依据本卡真实验收与docs/evidence/stage-01/S1-05/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-01/S1-05/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S1-06，规格文件docs/stages/stage-01-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S1-06：装配与初始化

前置：S1-05。下一张计划：S1-07 主机集成审计。

[原始卡与公共约束](stages/stage-01-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S1-06 装配与初始化；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-05: docs/evidence/stage-01/S1-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-06/批次、logs/S1-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S1-06，不自动开始S1-07。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/runtime/
- host/cli/
- deployment/host/
- CMakeLists.txt
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **输入**：前述模块通过的测试、资源上限、既定进程边界。
- **可改目录**：`host/runtime/`、`host/cli/`、`deployment/host/`、根 CMake、README 当前状态及模块使用文档。
- **产出**：`locker-service`、`locker-admin` 和模拟器的统一启动/退出流程；首位管理员和单工具的显式本机初始化；无凭据配置样例，独立数据库路径、受限 socket 路径和模拟模式醒目标识。
- **基线装配**：Runtime每秒搬运Core不可变源样本到HOST_HEALTH；UI实例零UUID、年龄UINT32_MAX、监督UNKNOWN。真实Core停止递增后即使搬运还运行，也不能发新鲜Core年龄。共享单实例/离线维护互斥先取得再打开营业DB，退出关闭DB后释放，为S5维护排除竞态。
- **默认运行边界**：主机模拟 HTTPS 默认仅绑定 loopback；若为局域网人工测试，必须在配置显式改变绑定并使用正式证书信任流程。不会启动真实 UART、GPIO、MCU 或 UI 监督进程。
- **验收**：全新状态目录可初始化、启动、退出、重启；错误配置/端口占用/数据库错误明确退出或降级；密码从终端安全输入/受限 stdin 取得，不出现在命令行和日志；重启后先 RECONCILING；Core 队列默认 64、HTTP 工作线程 4、WS 缓冲 16 的上限生效。
- **禁止越界**：不自动建立默认管理员；不清库掩盖失败；不新增公开调试路由；不把 service 纳入自动重启或引入 shell 指令执行。
- **DONE 证据**：从空目录运行的完整可复现说明、真实输出与退出码、已知平台限制及依赖锁定清单。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-01/S1-06/summary.md和docs/evidence/stage-01/S1-06/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S1-06 装配与初始化；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-05: docs/evidence/stage-01/S1-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-06/批次、logs/S1-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S1-06；不接着实现后续卡。
读取docs/evidence/stage-01/S1-06/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-01/S1-06/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S1-06 装配与初始化；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-05: docs/evidence/stage-01/S1-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-06/批次、logs/S1-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S1-06，问题来源为docs/evidence/stage-01/S1-06/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S1-06 装配与初始化；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-05: docs/evidence/stage-01/S1-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-06/批次、logs/S1-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S1-06；依据本卡真实验收与docs/evidence/stage-01/S1-06/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-01/S1-06/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S1-07，规格文件docs/stages/stage-01-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S1-07：主机集成审计

前置：S1-06。下一张计划：S2-01 HAL与BSP。

[原始卡与公共约束](stages/stage-01-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S1-07 主机集成审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-06: docs/evidence/stage-01/S1-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-07/批次、logs/S1-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S1-07，不自动开始S2-01。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- tests/integration/
- tests/contract/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **输入**：所有卡的 DONE 证据；主规格的故障/幂等/恢复验收项。
- **可改目录**：`tests/integration/`、`tests/contract/`、正式 `tests/fixtures/`、`docs/evidence/stage-01/`；若需改业务实现退回对应卡，不通过改预期隐藏缺陷。
- **产出**：可重复的端到端测试及正式报告；测试从私有控制入口驱动模拟器，通过真实 HTTPS/IPC/UART 字节和数据库验证结果，不能只直接调用领域函数。
- **验收**：执行下节阶段门禁；报告每场景是通过、失败或未运行及原因，不用“部分通过”代替清单。运行中断后测试资源可以清理，但正式 fixture、故障证据和回归测试必须保留。
- **禁止越界**：不得把模拟日志提交视为 SPI Flash 掉电安全，不得把静态 checker 通过视为服务运行正常，不得为赶进度删去故障验收。
- **DONE 证据**：`docs/evidence/stage-01/summary.md` 包含环境、契约版本、构建/运行命令、场景结果、日志位置、缺陷/限制、临时残留自查及文档同步结论。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-01/S1-07/summary.md和docs/evidence/stage-01/S1-07/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S1-07 主机集成审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-06: docs/evidence/stage-01/S1-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-07/批次、logs/S1-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S1-07；不接着实现后续卡。
读取docs/evidence/stage-01/S1-07/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-01/S1-07/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S1-07 主机集成审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-06: docs/evidence/stage-01/S1-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-07/批次、logs/S1-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S1-07，问题来源为docs/evidence/stage-01/S1-07/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S1-07 主机集成审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-01-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-06: docs/evidence/stage-01/S1-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S1-07/批次、logs/S1-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S1-07；依据本卡真实验收与docs/evidence/stage-01/S1-07/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-01/S1-07/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡是阶段1正式门禁卡。逐项核实本阶段全部卡报告和必要实跑。
只有全部通过才生成docs/evidence/stage-01/summary.md汇总并链接每卡证据，
由唯一负责人同步本卡DONE、阶段implementation_status=DONE、gate_result=PASS及固定evidence。
关键NOT_RUN/阻塞存在不能把阶段设PASS，也不能启动下一阶段。
下一张计划卡为S2-01，规格文件docs/stages/stage-02-hal-hardware-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-01：HAL与BSP

前置：S1-07。下一张计划：S2-02 输入与SPI。

[原始卡与公共约束](stages/stage-02-hal-hardware-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-01 HAL与BSP；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-07: docs/evidence/stage-01/S1-07/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-01/批次、logs/S2-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-01，不自动开始S2-02。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- firmware/board/
- firmware/hal/
- firmware/config/
- deployment/firmware/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: mcu_board_apply_profile（归属：implementation）

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S1-07；实物板/官方原理图、未验证profile、本地HAL V1.8.5。无实物可完成配置设计，不能把下载/测量验收标通过。
- **允许目录**：firmware/board/、firmware/hal/、firmware/config/、deployment/firmware/；根固件工程由唯一集成负责人维护，不改用户原HAL目录与ZIP。
- **接口/调用链**：启动安全输出→mcu_board_apply_profile→HAL GPIO/DMA/定时器配置→四任务初始化；HAL句柄止于BSP。未验证配置只诊断、不允许真锁。
- **产物**：HAL应用工程、芯片/时钟/引脚/DMA/中断/定时器资源表、静态内存预算和工具链记录。候选VGT6/1MB、8MHz/168MHz须实物核对，不执行课程ZETx/25MHz工程。统一STM32F407xx宏、FreeRTOS异常入口、独立HAL时基；USE_RTOS=0。按组件锁定FreeRTOS/littlefs版本与许可。
- **验收**：SWD下载/断点、上电锁输出默认关闭；HAL时基无冲突；UART DMA缓冲不在CCM；64位时间处理底层计数回绕；未验证profile拒绝驱动。
- **完成证据**：编译器/工程/映射文件、烧录记录、资源冲突检查与原库未修改说明。
- **禁止越界**：不编造空闲引脚、不把one-pulse API存在视为硬截止已实现、不驱动未核对连接。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-01/summary.md和docs/evidence/stage-02/S2-01/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-01 HAL与BSP；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-07: docs/evidence/stage-01/S1-07/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-01/批次、logs/S2-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-01；不接着实现后续卡。
读取docs/evidence/stage-02/S2-01/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-01/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-01 HAL与BSP；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-07: docs/evidence/stage-01/S1-07/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-01/批次、logs/S2-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-01，问题来源为docs/evidence/stage-02/S2-01/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-01 HAL与BSP；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S1-07: docs/evidence/stage-01/S1-07/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-01/批次、logs/S2-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-01；依据本卡真实验收与docs/evidence/stage-02/S2-01/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-01/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S2-02，规格文件docs/stages/stage-02-hal-hardware-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-02：输入与SPI

前置：S2-01。下一张计划：S2-03 UART DMA。

[原始卡与公共约束](stages/stage-02-hal-hardware-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-02 输入与SPI；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-01: docs/evidence/stage-02/S2-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-02/批次、logs/S2-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-02，不自动开始S2-03。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- firmware/drivers/
- firmware/board/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: mcu_board_read_inputs（归属：implementation）
internal_ports: mcu_board_spi_transfer（归属：implementation）
internal_ports: mcu_board_set_alarm（归属：implementation）

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-01；门磁/微动开关/RC522/Flash引脚及额定电平。
- **允许目录**：firmware/drivers/、输入相关firmware/board/、tests/firmware/inputs/。
- **接口/调用链**：HAL输入采样/ADC完成→原始副本→mcu_board_read_inputs→Control去抖/observe_inputs；PeripheralStorage统一调用mcu_board_spi_transfer读取RC522，卡片只产生事实。
- **产物**：门/物品/供电有效性映射、去抖参数、RC522 UID读取、独立片选和SPI仲裁。Flash内部busy期间释放总线，先测延迟再承诺读卡响应。
- **验收**：闭门/开门、有物/无物四项实测；未配置/可诊断采样故障为UNKNOWN；两线无监督电路不能诊断断线，NO断线与未按下、NC断线与开门电气等价，按相应非占位/非闭门事实与视觉冲突处理，不假报断线诊断；UID 4/7/10字节；未授权读卡不开锁；ADC有效范围与毫伏标定。类别阈值细化留S4-02。
- **完成证据**：原始电平与规范状态对照、ADC标定、读卡和总线互斥记录。
- **禁止越界**：不按NO/NC猜电平，不用软件输入冒充传感器交付；本卡不接锁线圈、不写借还账本。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-02/summary.md和docs/evidence/stage-02/S2-02/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-02 输入与SPI；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-01: docs/evidence/stage-02/S2-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-02/批次、logs/S2-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-02；不接着实现后续卡。
读取docs/evidence/stage-02/S2-02/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-02/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-02 输入与SPI；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-01: docs/evidence/stage-02/S2-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-02/批次、logs/S2-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-02，问题来源为docs/evidence/stage-02/S2-02/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-02 输入与SPI；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-01: docs/evidence/stage-02/S2-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-02/批次、logs/S2-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-02；依据本卡真实验收与docs/evidence/stage-02/S2-02/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-02/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S2-03，规格文件docs/stages/stage-02-hal-hardware-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-03：UART DMA

前置：S2-02。下一张计划：S2-04 真实Journal。

[原始卡与公共约束](stages/stage-02-hal-hardware-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-03 UART DMA；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-02: docs/evidence/stage-02/S2-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-03/批次、logs/S2-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-03，不自动开始S2-04。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- firmware/transport/
- firmware/tasks/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
uart_requests: HELLO（归属：mcu_transport）
uart_requests: GET_SNAPSHOT（归属：mcu_transport）
uart_requests: FETCH_EVENTS（归属：mcu_transport）
uart_requests: ACK_PERSISTED（归属：mcu_transport）
uart_requests: RESUME_READY（归属：mcu_transport）
uart_requests: DISARM（归属：mcu_transport）
uart_requests: EXECUTE_UNLOCK（归属：mcu_transport）
uart_requests: QUERY_COMMAND（归属：mcu_transport）
uart_requests: HOST_HEALTH（归属：mcu_transport）
uart_requests: TIME_ANCHOR（归属：mcu_transport）
uart_requests: RELEASE_CONTEXT（归属：mcu_transport）

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-02；UART布局/黄金帧、RK串口额定IO电平。
- **允许目录**：firmware/transport/、通信相关firmware/tasks/、tests/firmware/transport/。
- **接口/调用链**：HAL circular DMA/IDLE通知→Communication流式解析→有界Control/Storage请求→响应/DEVICE_EVENT；ISR只通知，不运行Flash/printf/业务。
- **产物**：11类请求分派、旧session/CRC/长度拒绝、错误计数、RX环形区和发送背压。未完成Journal/Control的动作请求明确NOT_READY，不固定成功。
- **验收**：半帧、多帧、回绕、错误CRC、超长payload；ReceiveToIdle的Size按DMA位置处理，HT/TC/IDLE重复不重复消费；旧会话不入锁入口；2048字节设计缓冲不越界。
- **完成证据**：串口抓取、黄金帧比较、DMA回绕/重复通知和过载结果。
- **禁止越界**：不把一次回调当一帧，不在解析器驱动锁，不把USB调试通路当RK控制UART。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-03/summary.md和docs/evidence/stage-02/S2-03/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-03 UART DMA；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-02: docs/evidence/stage-02/S2-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-03/批次、logs/S2-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-03；不接着实现后续卡。
读取docs/evidence/stage-02/S2-03/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-03/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-03 UART DMA；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-02: docs/evidence/stage-02/S2-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-03/批次、logs/S2-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-03，问题来源为docs/evidence/stage-02/S2-03/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-03 UART DMA；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-02: docs/evidence/stage-02/S2-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-03/批次、logs/S2-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-03；依据本卡真实验收与docs/evidence/stage-02/S2-03/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-03/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S2-04，规格文件docs/stages/stage-02-hal-hardware-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-04：真实Journal

前置：S2-02。下一张计划：S2-05 Control与硬截止。

[原始卡与公共约束](stages/stage-02-hal-hardware-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-04 真实Journal；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-02: docs/evidence/stage-02/S2-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-04/批次、logs/S2-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-04，不自动开始S2-05。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- firmware/journal/
- firmware/drivers/flash/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: mcu_journal_append（归属：implementation）
internal_ports: mcu_journal_commit_intent（归属：implementation）
internal_ports: mcu_journal_read_page（归属：implementation）
internal_ports: mcu_journal_ack（归属：implementation）
internal_ports: mcu_journal_init（归属：implementation）
internal_ports: mcu_journal_bind_sink（归属：implementation）
internal_ports: mcu_journal_control（归属：implementation）
uart_requests: FETCH_EVENTS（归属：mcu_semantics）
uart_requests: ACK_PERSISTED（归属：mcu_semantics）

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-02；真实Flash几何、Journal声明及回执规则。与S2-03目录独立，默认单卡串行。
- **允许目录**：firmware/journal/、firmware/drivers/flash/、tests/firmware/journal/。
- **接口/调用链**：journal_init→绑定sink→append/commit_intent队列→Storage独占littlefs→sync/close提交→token回执；read_page/ack同样经Storage所有者处理。
- **产物**：epoch/boot、动作水位、未决上下文、连续事实、提交屏障、持久ACK。容量预留/拒绝底线在此实现；阶段4扩完整回收策略。首次初始化为受限现场动作，无公开API；挂载失败不自动format。
- **验收**：意图与水位同提交；失败无成功回执；重启保留已提交记录；未ACK日志不回收；挂载失败/满/连续性失效不可READY。掉电试验先不接锁。
- **完成证据**：Flash几何、持久格式/版本、掉电位置与恢复结果、提交回执及未ACK保护记录。
- **禁止越界**：不用RAM冒充持久化，不先跑真锁再补Journal，不format掩盖损坏。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-04/summary.md和docs/evidence/stage-02/S2-04/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-04 真实Journal；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-02: docs/evidence/stage-02/S2-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-04/批次、logs/S2-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-04；不接着实现后续卡。
读取docs/evidence/stage-02/S2-04/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-04/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-04 真实Journal；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-02: docs/evidence/stage-02/S2-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-04/批次、logs/S2-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-04，问题来源为docs/evidence/stage-02/S2-04/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-04 真实Journal；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-02: docs/evidence/stage-02/S2-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-04/批次、logs/S2-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-04；依据本卡真实验收与docs/evidence/stage-02/S2-04/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-04/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S2-05，规格文件docs/stages/stage-02-hal-hardware-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-05：Control与硬截止

前置：S2-03、S2-04。下一张计划：S2-06 RK串口及首用部署。

[原始卡与公共约束](stages/stage-02-hal-hardware-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-05 Control与硬截止；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-03: docs/evidence/stage-02/S2-03/summary.md、handoff.md、audit.md
- S2-04: docs/evidence/stage-02/S2-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-05/批次、logs/S2-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-05，不自动开始S2-06。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- firmware/control/
- firmware/board/
- firmware/tasks/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: mcu_control_submit_unlock（归属：implementation）
internal_ports: mcu_control_observe_inputs（归属：implementation）
internal_ports: mcu_control_on_journal_result（归属：implementation）
internal_ports: mcu_control_make_snapshot（归属：implementation）
internal_ports: mcu_control_release_context（归属：implementation）
internal_ports: mcu_board_start_bounded_pulse（归属：implementation）
internal_ports: mcu_board_force_off（归属：implementation）
internal_ports: mcu_health_report_progress（归属：implementation）
internal_ports: mcu_health_evaluate（归属：implementation）
internal_ports: mcu_control_init（归属：implementation）
internal_ports: mcu_health_init（归属：implementation）
internal_ports: mcu_control_bind_sink（归属：implementation）
uart_requests: HELLO（归属：mcu_semantics）
uart_requests: GET_SNAPSHOT（归属：mcu_semantics）
uart_requests: RESUME_READY（归属：mcu_semantics）
uart_requests: DISARM（归属：mcu_semantics）
uart_requests: EXECUTE_UNLOCK（归属：mcu_semantics）
uart_requests: QUERY_COMMAND（归属：mcu_semantics）
uart_requests: HOST_HEALTH（归属：mcu_semantics）
uart_requests: TIME_ANCHOR（归属：mcu_semantics）
uart_requests: RELEASE_CONTEXT（归属：mcu_semantics）

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-03、S2-04；锁允许脉冲、电源保护和BSP定时输出已核对。
- **允许目录**：firmware/control/、锁相关firmware/board/、firmware/tasks/、tests/firmware/control/。
- **接口/调用链**：submit_unlock→commit_intent→on_journal_result排队→Control复核session/物理版本/Core新鲜度/期限→start_bounded_pulse；故障force_off。只有Control可启动脉冲。
- **产物**：四任务装配、预执行复核、≤1000ms且不超过实物限值的硬截止、快照屏障、上下文释放。先空载/假负载测定时输出，再接有保护的真锁。
- **验收**：存储阻塞/RTOS延迟时硬件仍关断；复位关闭；准备超过2000ms或not_after截止、物理变化、日志错误均不驱动；已接纳但结果不明不退回水位；重复释放幂等。
- **完成证据**：示波器/逻辑分析波形、故障时最长脉冲、队列时序与水位；日志时间差不能替代实际通电波形。
- **禁止越界**：不用RTOS回调/Linux取消实现硬截止，不把ACCEPTED说成开门，不跳过独立试锁和保护核对。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-05/summary.md和docs/evidence/stage-02/S2-05/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-05 Control与硬截止；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-03: docs/evidence/stage-02/S2-03/summary.md、handoff.md、audit.md
- S2-04: docs/evidence/stage-02/S2-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-05/批次、logs/S2-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-05；不接着实现后续卡。
读取docs/evidence/stage-02/S2-05/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-05/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-05 Control与硬截止；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-03: docs/evidence/stage-02/S2-03/summary.md、handoff.md、audit.md
- S2-04: docs/evidence/stage-02/S2-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-05/批次、logs/S2-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-05，问题来源为docs/evidence/stage-02/S2-05/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-05 Control与硬截止；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-03: docs/evidence/stage-02/S2-03/summary.md、handoff.md、audit.md
- S2-04: docs/evidence/stage-02/S2-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-05/批次、logs/S2-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-05；依据本卡真实验收与docs/evidence/stage-02/S2-05/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-05/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S2-06，规格文件docs/stages/stage-02-hal-hardware-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-06：RK串口及首用部署

前置：S2-05。下一张计划：S2-07 整柜闭环。

[原始卡与公共约束](stages/stage-02-hal-hardware-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-06 RK串口及首用部署；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-05: docs/evidence/stage-02/S2-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-06/批次、logs/S2-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-06，不自动开始S2-07。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/device/
- host/runtime/
- host/cli/
- deployment/target/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: locker_device_open（归属：refinement）
internal_ports: locker_device_get_snapshot（归属：refinement）
internal_ports: locker_device_execute_once（归属：refinement）
internal_ports: locker_device_query_command（归属：refinement）
internal_ports: locker_device_fetch_events（归属：refinement）
internal_ports: locker_device_ack_persisted（归属：refinement）
internal_ports: locker_device_resume_ready（归属：refinement）
internal_ports: locker_device_release_context（归属：refinement）
internal_ports: locker_device_send_health（归属：refinement）
internal_ports: locker_device_create（归属：refinement）
internal_ports: locker_device_close（归属：refinement）
internal_ports: locker_device_destroy（归属：refinement）
internal_ports: locker_device_time_anchor（归属：refinement）
internal_ports: locker_device_bind_sink（归属：refinement）
uart_requests: HELLO（归属：real_link）
uart_requests: GET_SNAPSHOT（归属：real_link）
uart_requests: FETCH_EVENTS（归属：real_link）
uart_requests: ACK_PERSISTED（归属：real_link）
uart_requests: RESUME_READY（归属：real_link）
uart_requests: DISARM（归属：real_link）
uart_requests: EXECUTE_UNLOCK（归属：real_link）
uart_requests: QUERY_COMMAND（归属：real_link）
uart_requests: HOST_HEALTH（归属：real_link）
uart_requests: TIME_ANCHOR（归属：real_link）
uart_requests: RELEASE_CONTEXT（归属：real_link）

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-05；已通过的主机端、实际RK用户态ABI/设备节点/权限/网络。
- **允许目录**：host/device/、host/runtime/、host/cli/、deployment/target/及适配测试；根构建只由集成负责人调整。
- **接口/调用链**：DevicePort切换simulated=false→真实UART 115200 8N1→HELLO/屏障补取/核对/RESUME_READY；真实Core进度→locker_core_get_progress→Runtime→locker_device_send_health。UI未知使用零UUID/age未知哨兵，不虚报RUNNING。
- **产物**：目标机编译/启动配置、设备ID匹配、串口权限与恢复；HTTPS局域网地址、证书SAN及公开配对导出工具，格式见[客户端接口](architecture/client-interfaces.md)。私钥不导出。首用能力在S3前具备，阶段5固化维护。
- **验收**：产物ABI可执行；串口断接先核对、不重发旧动作；HTTP→Core→真设备可追踪；证书地址/SAN匹配，未信任拒绝；配对文件无token/密码/私钥。
- **完成证据**：目标机/二进制信息、部署命令、UART日志、LAN TLS烟测和脱敏导出样例。
- **禁止越界**：不默认systemd或ARM64 ABI，不关证书校验，不把模拟结果当实物。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-06/summary.md和docs/evidence/stage-02/S2-06/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-06 RK串口及首用部署；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-05: docs/evidence/stage-02/S2-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-06/批次、logs/S2-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-06；不接着实现后续卡。
读取docs/evidence/stage-02/S2-06/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-06/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-06 RK串口及首用部署；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-05: docs/evidence/stage-02/S2-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-06/批次、logs/S2-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-06，问题来源为docs/evidence/stage-02/S2-06/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-06 RK串口及首用部署；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-05: docs/evidence/stage-02/S2-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-06/批次、logs/S2-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-06；依据本卡真实验收与docs/evidence/stage-02/S2-06/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-06/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S2-07，规格文件docs/stages/stage-02-hal-hardware-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-07：整柜闭环

前置：S2-06。下一张计划：S2-08 硬件阶段审计。

[原始卡与公共约束](stages/stage-02-hal-hardware-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-07 整柜闭环；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-06: docs/evidence/stage-02/S2-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-07/批次、logs/S2-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-07，不自动开始S2-08。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- tests/hardware/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-06；供电/刚性固定/已验证profile、测试管理员和普通用户。
- **允许目录**：本阶段模块缺陷修复、tests/hardware/、docs/evidence/stage-02/；共享契约变更走统一流程。
- **接口/调用链**：HTTPS申请→实时卡或受限本机测试客户端presence→唯一command→真实脉冲/开门/物品变化/关门→GET operation→本人confirm；归还对称，落库终结再RELEASE_CONTEXT。
- **产物**：借还、NO_CHANGE、未确认取物待核验、尾杆无指令访问hold和本机maintenance指导。
- **验收**：竞争、只ACK、提前确认、UNKNOWN、取消遇可能执行、主机支路断电、MCU重启、ACK丢失、日志失败；恢复查账本与实物，不重复驱动。
- **完成证据**：operation/command/事件键关联、脱敏录像/照片、脉冲计数、数据库与实物对照。
- **禁止越界**：不把纸箱当防盗柜，不为演示改传感器/删故障，不扩写App/UI。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-07/summary.md和docs/evidence/stage-02/S2-07/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-07 整柜闭环；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-06: docs/evidence/stage-02/S2-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-07/批次、logs/S2-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-07；不接着实现后续卡。
读取docs/evidence/stage-02/S2-07/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-07/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-07 整柜闭环；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-06: docs/evidence/stage-02/S2-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-07/批次、logs/S2-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-07，问题来源为docs/evidence/stage-02/S2-07/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-07 整柜闭环；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-06: docs/evidence/stage-02/S2-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-07/批次、logs/S2-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-07；依据本卡真实验收与docs/evidence/stage-02/S2-07/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-07/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S2-08，规格文件docs/stages/stage-02-hal-hardware-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-08：硬件阶段审计

前置：S2-07。下一张计划：S2-09 模型资产/导出来源。

[原始卡与公共约束](stages/stage-02-hal-hardware-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-08 硬件阶段审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-07: docs/evidence/stage-02/S2-07/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-08/批次、logs/S2-08/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-08，不自动开始S2-09。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- tests/hardware/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-07及全部真实证据。
- **允许目录**：正式测试、docs/evidence/stage-02/、接线/模块README；缺陷退回所属卡。
- **接口/调用链**：对照API→Core→UART→Journal→锁→事实→事务→ACK各段证据，查旁路/重放。
- **产物**：summary、逐场景PASS/FAIL/NOT_RUN、接线/profile/工程指纹、实付成本和未验证项；测量满足schema才标VERIFIED。
- **验收**：基础各卡通过，硬截止/持久意图/真实Core进度/不重放均有证据，只解锁S2-09。基础汇总为foundation-summary.md，完整阶段summary由S2-16形成。
- **完成证据**：实际构建/静态检查/硬件结果、残留自查。课程原HAL和Objects/Listings不作为本轮证据或临时文件删除。
- **禁止越界**：不用静态checker/模拟掉电替代硬件验收，不自动跨入阶段3。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-08/summary.md和docs/evidence/stage-02/S2-08/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-08 硬件阶段审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-07: docs/evidence/stage-02/S2-07/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-08/批次、logs/S2-08/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-08；不接着实现后续卡。
读取docs/evidence/stage-02/S2-08/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-08/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-08 硬件阶段审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-07: docs/evidence/stage-02/S2-07/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-08/批次、logs/S2-08/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-08，问题来源为docs/evidence/stage-02/S2-08/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-08 硬件阶段审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02-hal-hardware-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-07: docs/evidence/stage-02/S2-07/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-08/批次、logs/S2-08/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-08；依据本卡真实验收与docs/evidence/stage-02/S2-08/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-08/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡只是2A基础子门禁。写stage-02/foundation-summary.md，不能把整个阶段2设PASS。
S2-09..16未完成时完整阶段2仍在进行；通过后只解锁S2-09。
下一张计划卡为S2-09，规格文件docs/stages/stage-02b-vision-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-09：模型资产/导出来源

前置：S2-08。下一张计划：S2-10 统一预处理/量化验证。

[原始卡与公共约束](stages/stage-02b-vision-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-09 模型资产/导出来源；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-08: docs/evidence/stage-02/S2-08/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-09/批次、logs/S2-09/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-09，不自动开始S2-10。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- deployment/vision/
- tools/vision/
- tests/vision/model/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-08；E:/equipment-vision的README、v11权重/classes/metrics、现有ONNX及导出脚本。只读原项目，不能覆盖原模型或猜测v11为YOLO。
- **允许目录**：deployment/vision/、tools/vision/的版本化导入/检查工具、tests/vision/model/、本卡证据；模型副本进入明确的本项目制品目录，不修改原项目。
- **接口/调用链**：检查checkpoint/类别与ONNX来源→生成模型资产清单→参考PyTorch与ONNX对同组实图运行→保存逐样本logits/top1及等价结果。
- **产物**：checkpoint/ONNX/类别表哈希、来源、MobileNetV3-Large/7类/224、输入输出/opset和导出recipe。默认ONNX七类不能标旧六类；权重同SHA也不能代替ONNX等价证据。
- **错误/验收**：类别顺序/输出维度/哈希不符、输出含NaN/Inf或等价失败拒绝制品；不存在RKNN不声称可上板。重新导出写新目录，禁weights_only=False自动回退或从网络下载另一骨干代替自训权重。
- **完成证据/禁区**：来源/许可、文件指纹、逐样本差异/命令/环境；未运行记NOT_RUN。不重新训练、清洗数据或改公开API。
- **给AI的单卡提示**：只做S2-09，先核对七类v11资产与参考等价，保留原项目；完成制品清单和证据即交接，不继续量化。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-09/summary.md和docs/evidence/stage-02/S2-09/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-09 模型资产/导出来源；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-08: docs/evidence/stage-02/S2-08/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-09/批次、logs/S2-09/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-09；不接着实现后续卡。
读取docs/evidence/stage-02/S2-09/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-09/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-09 模型资产/导出来源；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-08: docs/evidence/stage-02/S2-08/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-09/批次、logs/S2-09/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-09，问题来源为docs/evidence/stage-02/S2-09/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-09 模型资产/导出来源；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-08: docs/evidence/stage-02/S2-08/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-09/批次、logs/S2-09/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-09；依据本卡真实验收与docs/evidence/stage-02/S2-09/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-09/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S2-10，规格文件docs/stages/stage-02b-vision-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-10：统一预处理/量化验证

前置：S2-09。下一张计划：S2-11 相机与现场profile。

[原始卡与公共约束](stages/stage-02b-vision-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-10 统一预处理/量化验证；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-09: docs/evidence/stage-02/S2-09/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-10/批次、logs/S2-10/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-10，不自动开始S2-11。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- deployment/vision/
- tools/vision/
- tests/vision/preprocess/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-09；训练验证参考Resize短边255→CenterCrop224、RGB/ImageNet归一化；目标RK3568 SDK约束与独立标定数据规划。
- **允许目录**：tools/vision/转换/评估、deployment/vision/recipe、tests/vision/preprocess/及量化fixture、本卡证据。
- **接口/调用链**：参考像素管线→固定预处理版本/哈希→ONNX固定板端形状→RKNN转换→FP/INT8逐样本比较；几何处理先在CPU固定，归一化只做一次。
- **产物**：明确RGB、裁剪/缩放/插值/取整、输入dtype/layout、mean/std归属、输出logits/softmax规则；RKNN/runtime/driver版本与兼容记录、校准/独立验证集指纹及阈值策略草案。
- **错误/验收**：禁止把短边224或拉伸224×224称参考一致；禁止RKNN嵌入mean/std后又手动归一化；检出类别乱序/输出维数错/NaN。accuracy_analysis输入列表与标签列表分开，VM路径可解析；训练集另抽35张不作为独立柜内测试。
- **完成证据/禁区**：参考/ONNX/RKNN的样本、各类指标和量化差异；柜内实拍未具备时阈值保持未标定、REQUIRED不可启。无SDK/板端时如实BLOCKED，不造.rknn假文件、不保证量化固定掉点。
- **给AI的单卡提示**：只做S2-10，先证明同像素/同归一化，再量化评估；不接相机、不接账本。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-10/summary.md和docs/evidence/stage-02/S2-10/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-10 统一预处理/量化验证；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-09: docs/evidence/stage-02/S2-09/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-10/批次、logs/S2-10/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-10；不接着实现后续卡。
读取docs/evidence/stage-02/S2-10/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-10/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-10 统一预处理/量化验证；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-09: docs/evidence/stage-02/S2-09/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-10/批次、logs/S2-10/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-10，问题来源为docs/evidence/stage-02/S2-10/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-10 统一预处理/量化验证；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-09: docs/evidence/stage-02/S2-09/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-10/批次、logs/S2-10/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-10；依据本卡真实验收与docs/evidence/stage-02/S2-10/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-10/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S2-11，规格文件docs/stages/stage-02b-vision-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-11：相机与现场profile

前置：S2-10。下一张计划：S2-12 RKNN视觉worker。

[原始卡与公共约束](stages/stage-02b-vision-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-11 相机与现场profile；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-10: docs/evidence/stage-02/S2-10/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-11/批次、logs/S2-11/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-11，不自动开始S2-12。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/vision-worker/camera/
- config/vision/
- deployment/vision/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-10；真实相机/接口、RK设备节点/驱动、供电/线缆/机位/照明与新增预算。接口层面已定USB UVC具体型号（HBVCAM-F2170HD 75°手动调焦，商品/SKU与判据见[相机选型与成本](hardware/相机选型与成本.md)），但相机未购买、未核价、未接板，条件仍未具备。
- **允许目录**：host/vision-worker/camera/、config/vision/中新建实际相机profile（保留unverified模板）、deployment/vision/、采集测试及本卡证据；材料变更交材料真源负责人统一处理。
- **接口/调用链**：受限相机FD→V4L2配置/缓冲→固定slot=1的ROI/颜色→带帧序号、采集时刻和camera generation的受控帧；帧不进入Core。
- **产物**：真实相机profile、时钟/分辨率/像素格式/ROI/曝光/固定方式、代际变更规则；按S2-10数据规则采集有真实标签的柜内标定集与独立现场测试集。相机、线缆、必要补光/固定/运费列全增量成本（当前候选见`config/materials/selected-bom.json`的`vision_increment`），不从旧200元预算默认0元覆盖；到货须实测手动曝光/增益可写、每帧`V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC`与序号递增，否则该候选不合格。
- **错误/验收**：曝光时间戳/clock标志与MONOTONIC映射可证明；请求前缓冲不能当新帧；相机断开、重新打开、ROI变化使旧capture失效；3新帧间隔≥100ms、3000ms总截止作为设计边界实测，不用重复同一帧冒充。
- **完成证据/禁区**：型号/驱动/设备权限/预算决策、实际帧时间线、机位照片及光照样本。无相机标BLOCKED，不要求购入未确认器件，不把文件图片冒充V4L2采集。
- **给AI的单卡提示**：只做S2-11，先核对相机与成本，证明新帧/时钟/代际；完成后不启动借还业务。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-11/summary.md和docs/evidence/stage-02/S2-11/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-11 相机与现场profile；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-10: docs/evidence/stage-02/S2-10/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-11/批次、logs/S2-11/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-11；不接着实现后续卡。
读取docs/evidence/stage-02/S2-11/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-11/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-11 相机与现场profile；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-10: docs/evidence/stage-02/S2-10/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-11/批次、logs/S2-11/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-11，问题来源为docs/evidence/stage-02/S2-11/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-11 相机与现场profile；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-10: docs/evidence/stage-02/S2-10/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-11/批次、logs/S2-11/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-11；依据本卡真实验收与docs/evidence/stage-02/S2-11/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-11/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S2-12，规格文件docs/stages/stage-02b-vision-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-12：RKNN视觉worker

前置：S2-11。下一张计划：S2-13 私有视觉IPC适配。

[原始卡与公共约束](stages/stage-02b-vision-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-12 RKNN视觉worker；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-11: docs/evidence/stage-02/S2-11/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-12/批次、logs/S2-12/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-12，不自动开始S2-13。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/vision-worker/
- deployment/vision/
- config/vision/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
vision_worker_messages: HELLO（归属：consumer）
vision_worker_messages: STATUS（归属：consumer）
vision_worker_messages: CAPTURE（归属：consumer）

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-11；已验证模型bundle、相机profile、SDK/runtime/driver兼容与像素recipe。
- **允许目录**：host/vision-worker/、模块局部构建、deployment/vision/、config/vision/中仅标定策略、tests/vision/worker/和本卡证据；根构建由集成负责人接入。
- **接口/调用链**：locker-vision加载/查询tensor属性→采集新帧→统一预处理→RKNN C推理/释放输出→返回有界类别/分数/质量/时间元数据；不连接MCU。
- **产物**：相机/NPU生命周期、固定包、质量/稳定性和截止处理；退出释放帧/FD/context。复用S2-10工具，在S2-11柜内标定集对当前expected_class与empty共同确定一组threshold_ppm/margin_ppm及质量参数，再用独立现场集验证并冻结policy哈希；不宣称存在按类独立阈值表。未验证不启REQUIRED，worker不决定RETURN_OK。
- **错误/验收**：错误模型/维度/类别/设备不运行；无新帧、曝光异常、遮挡、超时返回非成功；不能softmax低值判empty；3帧像素可相同但序号/曝光必须真实不同。单请求有界、队列满显式拒绝，重启新worker generation使旧请求失效。
- **完成证据/禁区**：真实RK构建/ABI、SDK、模型/tensor信息、每帧结果、时延分布/资源与错误测试。原Python公网骨架不作生产实现，不训练模型、不自动重启service。
- **给AI的单卡提示**：只做S2-12，复用官方C API生命周期，按本项目预处理和七类标签适配；不要扩HTTP服务或锁控制。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-12/summary.md和docs/evidence/stage-02/S2-12/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-12 RKNN视觉worker；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-11: docs/evidence/stage-02/S2-11/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-12/批次、logs/S2-12/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-12；不接着实现后续卡。
读取docs/evidence/stage-02/S2-12/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-12/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-12 RKNN视觉worker；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-11: docs/evidence/stage-02/S2-11/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-12/批次、logs/S2-12/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-12，问题来源为docs/evidence/stage-02/S2-12/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-12 RKNN视觉worker；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-11: docs/evidence/stage-02/S2-11/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-12/批次、logs/S2-12/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-12；依据本卡真实验收与docs/evidence/stage-02/S2-12/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-12/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S2-13，规格文件docs/stages/stage-02b-vision-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-13：私有视觉IPC适配

前置：S2-12。下一张计划：S2-14 Core持久证据与API。

[原始卡与公共约束](stages/stage-02b-vision-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-13 私有视觉IPC适配；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-12: docs/evidence/stage-02/S2-12/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-13/批次、logs/S2-13/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-13，不自动开始S2-14。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/vision/
- host/vision-worker/
- host/runtime/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: locker_vision_create（归属：implementation）
internal_ports: locker_vision_bind_sink（归属：implementation）
internal_ports: locker_vision_open（归属：implementation）
internal_ports: locker_vision_submit（归属：implementation）
internal_ports: locker_vision_get_status（归属：implementation）
internal_ports: locker_vision_close（归属：implementation）
internal_ports: locker_vision_destroy（归属：implementation）
vision_worker_messages: HELLO（归属：implementation/producer）
vision_worker_messages: STATUS（归属：implementation/producer）
vision_worker_messages: CAPTURE（归属：implementation）

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-12；冻结私有视觉协议/DTO/82端口头与部署service UID，生命周期规则。
- **允许目录**：host/vision/、host/vision-worker/中的私有协议服务、host/runtime/装配、tests/vision/transport/和本卡证据。
- **接口/调用链**：vision create/bind_sink/open→submit→vision.sock→worker→正式测试sink验证异步结果；get_status取不可变缓存。真实Core接收函数及Runtime接线归S2-14，本卡不能用空实现或未定义函数强行链接。
- **产物**：独立版本/包络、SO_PEERCRED+权限、capture关联、请求/响应边界、背压、超时与close/destroy；格式由机器契约唯一生成，不能把vision方法塞普通service.sock。
- **错误/验收**：错误peer/未知字段/超限包/旧capture/重复不同正文/旧实例拒绝；断连与worker重启不重放旧capture，取消等待不伪称已撤回worker工作。close后无回调，停止超时不释放仍借用对象。
- **完成证据/禁区**：真实socket边界、关联/乱序/断线/背压及退出测试；无0.0.0.0:8080、任意图片路径、base64、客户端上传事实或直接UART。
- **给AI的单卡提示**：只做S2-13，只接冻结私有协议与异步端口；Core融合规则留下一卡，不在适配器写SQL。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-13/summary.md和docs/evidence/stage-02/S2-13/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-13 私有视觉IPC适配；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-12: docs/evidence/stage-02/S2-12/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-13/批次、logs/S2-13/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-13；不接着实现后续卡。
读取docs/evidence/stage-02/S2-13/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-13/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-13 私有视觉IPC适配；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-12: docs/evidence/stage-02/S2-12/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-13/批次、logs/S2-13/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-13，问题来源为docs/evidence/stage-02/S2-13/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-13 私有视觉IPC适配；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-12: docs/evidence/stage-02/S2-12/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-13/批次、logs/S2-13/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-13；依据本卡真实验收与docs/evidence/stage-02/S2-13/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-13/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S2-14，规格文件docs/stages/stage-02b-vision-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-14：Core持久证据与API

前置：S2-13。下一张计划：S2-15 视觉实物借还联调。

[原始卡与公共约束](stages/stage-02b-vision-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-14 Core持久证据与API；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-13: docs/evidence/stage-02/S2-13/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-14/批次、logs/S2-14/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-14，不自动开始S2-15。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/domain/
- host/http/
- host/ipc/
- host/storage/
- host/runtime/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: locker_core_bind_vision（归属：implementation）
internal_ports: locker_core_apply_vision_result（归属：implementation）
internal_ports: locker_core_apply_vision_status（归属：implementation）
public_routes: getVisionStatus（归属：implementation/core）
public_routes: getOperationVisionEvidence（归属：implementation/core）
local_methods: getVisionStatus（归属：implementation）
local_methods: getOperationVisionEvidence（归属：implementation）
vision_worker_messages: CAPTURE（归属：producer）

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-13；expected_class配置、三种模式/阈值策略、PRE/POST状态矩阵、仓储及新只读API机器契约。
- **允许目录**：host/domain/、host/storage/由唯一仓储负责人修改、host/http/、host/ipc/、host/runtime/中的真实视觉绑定、相关正式测试及本卡证据；共享schema变更先交契约负责人。
- **接口/调用链**：Core bind_vision→提交capture→apply_vision_result/status入队→验证operation/phase/guard/实例/hash→证据与融合/操作变化同事务→getVisionStatus/getOperationVisionEvidence一致读取。
- **产物**：PRE borrow=MATCH/return=EMPTY，POST borrow=EMPTY/return=MATCH；REQUIRED不静默退化。PRE无通过不建command/outbox，保持REQUESTED可取消；POST失败进入REVIEW_REQUIRED保占位。人工resolve保留矛盾与理由，仍受原物理规则限制。
- **错误/验收**：旧证据/MCU或service重启/相机或worker代际变更/物理版本变化拒绝；DB失败不发布成功；重复capture幂等且异内容冲突。本人/管理员之外读operation视觉证据拒绝；公开只有GET，无图片/路径/阈值写入/开锁/上传。
- **完成证据/禁区**：状态矩阵、事务失败/重启/权限/幂等/guard负例、原API回归、元数据大小。Core不阻塞NPU、不以首预测建账、不覆盖MCU item_state、不把same-class当资产防伪。
- **给AI的单卡提示**：只做S2-14，先列每种模式和PRE/POST转移，再实现Core唯一决策与持久证据；客户端不得替代融合。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-14/summary.md和docs/evidence/stage-02/S2-14/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-14 Core持久证据与API；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-13: docs/evidence/stage-02/S2-13/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-14/批次、logs/S2-14/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-14；不接着实现后续卡。
读取docs/evidence/stage-02/S2-14/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-14/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-14 Core持久证据与API；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-13: docs/evidence/stage-02/S2-13/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-14/批次、logs/S2-14/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-14，问题来源为docs/evidence/stage-02/S2-14/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-14 Core持久证据与API；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-13: docs/evidence/stage-02/S2-13/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-14/批次、logs/S2-14/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-14；依据本卡真实验收与docs/evidence/stage-02/S2-14/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-14/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S2-15，规格文件docs/stages/stage-02b-vision-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-15：视觉实物借还联调

前置：S2-14。下一张计划：S2-16 视觉与阶段2审计。

[原始卡与公共约束](stages/stage-02b-vision-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-15 视觉实物借还联调；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-14: docs/evidence/stage-02/S2-14/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-15/批次、logs/S2-15/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-15，不自动开始S2-16。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- tests/integration/vision/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-14；一格实物/相机、可验证的REQUIRED策略与工具配置，正式HTTPS/本机测试客户端。
- **允许目录**：tests/hardware/vision/、tests/integration/vision/、docs/architecture/client-interfaces.md的视觉展示说明、阶段3卡的只读接口接线说明及本卡证据。此卡不提前实现完整Flutter/LVGL。
- **接口/调用链**：借出申请/到柜→PRESENT+MATCH→一次开锁→取走关门→ABSENT+EMPTY→本人confirm；归还先ABSENT+EMPTY→开锁→放入关门→PRESENT+MATCH→确认。手机/柜屏后续只GET状态/证据，WS提示后重查。
- **产物**：万用表正常借还、剪线钳错放、相机故障/遮挡/旧帧、传感器冲突和人工resolve的真实矩阵；给S3的页面状态、按钮门禁、错误与只读API映射。上游性能数字不当本阶段实测。
- **错误/验收**：归还申请时空柜不判已还；把别类物体压住微动开关不能完成；REQUIRED故障不降级；Post故障不释放占位。确认前物理/证据冲突阻止自动完成，响应丢失原幂等键核对、不再次开锁。
- **完成证据/禁区**：操作/command/capture/帧/版本关联、脱敏波形/日志/实物记录、指标与失败样本。完整App未实现不造截图，不把SHADOW记录称拦截生效。
- **给AI的单卡提示**：只做S2-15，以正式测试客户端跑真机闭环并交给S3具体读接口/显示规则；不要把阶段3整个App开发夹进来。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-15/summary.md和docs/evidence/stage-02/S2-15/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-15 视觉实物借还联调；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-14: docs/evidence/stage-02/S2-14/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-15/批次、logs/S2-15/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-15；不接着实现后续卡。
读取docs/evidence/stage-02/S2-15/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-15/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-15 视觉实物借还联调；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-14: docs/evidence/stage-02/S2-14/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-15/批次、logs/S2-15/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-15，问题来源为docs/evidence/stage-02/S2-15/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-15 视觉实物借还联调；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-14: docs/evidence/stage-02/S2-14/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-15/批次、logs/S2-15/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-15；依据本卡真实验收与docs/evidence/stage-02/S2-15/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-15/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S2-16，规格文件docs/stages/stage-02b-vision-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S2-16：视觉与阶段2审计

前置：S2-15。下一张计划：S3-01 客户端类型与生命周期。

[原始卡与公共约束](stages/stage-02b-vision-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S2-16 视觉与阶段2审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-15: docs/evidence/stage-02/S2-15/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-16/批次、logs/S2-16/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S2-16，不自动开始S3-01。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- docs/evidence/stage-02/
- tests/contract/vision/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S2-15及S2-09—15全部真实报告；契约/模型/相机/策略/代码指纹和预算记录。
- **允许目录**：正式审计测试、docs/evidence/stage-02/、当前阶段/模块README和状态索引；缺陷退回责任卡修复。
- **接口/调用链**：核对模型来源→采集→预处理→NPU→私有协议→Core事务→MCU动作/事实→本人确认→只读API，确认没有第二账本或视觉开锁旁路。
- **产物**：2B阶段汇总、REQUIRED可用资格、逐场景PASS/FAIL/NOT_RUN、已知限制与阶段3交接。S2-16通过是阶段2完整门禁，S2-08仅为底座门禁。
- **错误/验收**：关键实物/量化/权限/持久化/错还/故障项均有真实证据；相机/预算/阈值/SDK仍未知则不通过。静态checker、同类识别或14张empty样本不代替现场结果。
- **完成证据/禁区**：统一检查/构建/正式测试、残留自查、文档同步、制品哈希和未完成项；全卡完成后才解锁S3-01。不伪造DONE、不删失败证据、不擅自commit/push。
- **给AI的单卡提示**：只审S2-16的已交付事实，按清单逐项核对，不靠报告文件存在即通过；有缺口退回对应卡，不能顺手放宽门禁。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-02/S2-16/summary.md和docs/evidence/stage-02/S2-16/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S2-16 视觉与阶段2审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-15: docs/evidence/stage-02/S2-15/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-16/批次、logs/S2-16/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S2-16；不接着实现后续卡。
读取docs/evidence/stage-02/S2-16/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-02/S2-16/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S2-16 视觉与阶段2审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-15: docs/evidence/stage-02/S2-15/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-16/批次、logs/S2-16/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S2-16，问题来源为docs/evidence/stage-02/S2-16/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S2-16 视觉与阶段2审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-02b-vision-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-15: docs/evidence/stage-02/S2-15/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S2-16/批次、logs/S2-16/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S2-16；依据本卡真实验收与docs/evidence/stage-02/S2-16/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-02/S2-16/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡是阶段2正式门禁卡。逐项核实本阶段全部卡报告和必要实跑。
只有全部通过才生成docs/evidence/stage-02/summary.md汇总并链接每卡证据，
由唯一负责人同步本卡DONE、阶段implementation_status=DONE、gate_result=PASS及固定evidence。
关键NOT_RUN/阻塞存在不能把阶段设PASS，也不能启动下一阶段。
下一张计划卡为S3-01，规格文件docs/stages/stage-03-clients-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S3-01：客户端类型与生命周期

前置：S2-16。下一张计划：S3-02 Android配对会话。

[原始卡与公共约束](stages/stage-03-clients-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S3-01 客户端类型与生命周期；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-16: docs/evidence/stage-02/S2-16/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-01/批次、logs/S3-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S3-01，不自动开始S3-02。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- app/lib/contracts/
- app/lib/data/
- app/lib/core/
- terminal-ui/include/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置**：S2-16；读完本阶段入口与客户端接口全文。
- **输入**：OpenAPI和RPC真源、目标Android系统版本、RK显示/触摸驱动与ABI记录、既有本机构建接口。仅核对已用库；按[复用依据](architecture/client-interfaces.md#10-复用与适配依据)选锁定版本，不重建完整开发环境。
- **允许目录**：`app/lib/contracts/`、`app/lib/data/`、`app/lib/core/`、`app/pubspec.yaml`及lock、App最小Android构建/权限配置；`terminal-ui/include/`中的模块私有声明和局部构建入口；对应正式类型/协议测试、模块README与本卡证据。根构建/跨端契约修改交集成负责人。
- **接口与产出**：落实U64Text、ApiResult、CallContext、MutationIntent与typed ApiCall白名单；声明PairingStore/CredentialStore/IntentStore/LockerApi/ClientController及ui_ipc/ui_controller私有头。建立最小可构建页面壳，不做业务页面。客户端DTO从契约生成或集中映射并记录生成/再生办法，不手写另一套枚举。列出依赖来源、精确版本、license、目标SDK、已知公告核对与适配决定。
- **调用链**：测试fixture → DTO严格解析 → typed operationId描述符；无真实借还副作用。用现有样例验证外部包络与RPC status/body映射。
- **异常**：超uint64、UNKNOWN/null、未知状态、响应字段非法、204无正文、旧schema/不兼容契约，均有明确只读错误结果。
- **验收**：全部24公开operationId覆盖，同名23RPC+presence/progress覆盖，WS不出现在service.sock方法里；uint64边界不经浮点；编译/静态分析在实际工具可用时运行并记录。许可/SDK选择有来源，不声称“最新版一定可用”。
- **完成证据**：类型映射表、构建输出、契约fixture结果、版本锁定和许可索引。此卡不凭空填充真机profile。
- **禁止越界**：修改服务器schema/路由、自动重试mutation、业务层重新判断借还、加入云/扫码/蓝牙、多平台依赖。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-03/S3-01/summary.md和docs/evidence/stage-03/S3-01/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S3-01 客户端类型与生命周期；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-16: docs/evidence/stage-02/S2-16/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-01/批次、logs/S3-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S3-01；不接着实现后续卡。
读取docs/evidence/stage-03/S3-01/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-03/S3-01/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S3-01 客户端类型与生命周期；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-16: docs/evidence/stage-02/S2-16/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-01/批次、logs/S3-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S3-01，问题来源为docs/evidence/stage-03/S3-01/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S3-01 客户端类型与生命周期；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S2-16: docs/evidence/stage-02/S2-16/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-01/批次、logs/S3-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S3-01；依据本卡真实验收与docs/evidence/stage-03/S3-01/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-03/S3-01/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S3-02，规格文件docs/stages/stage-03-clients-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S3-02：Android配对会话

前置：S3-01。下一张计划：S3-03 借还与幂等恢复。

[原始卡与公共约束](stages/stage-03-clients-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S3-02 Android配对会话；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-01: docs/evidence/stage-03/S3-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-02/批次、logs/S3-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S3-02，不自动开始S3-03。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- app/lib/features/pairing/
- app/lib/features/session/
- app/lib/data/security/
- app/lib/data/network/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
public_routes: login（归属：client）
public_routes: getCurrentSession（归属：client）
public_routes: logout（归属：client）
public_routes: getDevice（归属：client）
public_routes: getSnapshot（归属：client）

本卡验收依据全文如下，不得缩减：
- **前置**：S3-01；S2-06现场网络准备提供不含私钥的柜体公开配对文件、导出工具与完整指纹来源。该工具必须在本卡实现配对前可用，不能等到阶段5；缺失时提交前置卡补齐。未提供真实证书时只验测试证书，不解锁真机门槛。
- **输入**：PairingStore/CredentialStore/LockerApi规格、当前24入口、目标机证书SAN与局域网地址。先核对锁定Dart/Android版本的TLS与安全存储官方接口。
- **允许目录**：`app/lib/features/pairing/`、`app/lib/features/session/`、`app/lib/data/security/`、`app/lib/data/network/`、配套Android平台安全存储/文档选择配置、本卡正式测试及证据。依赖锁定变更须回S3-01统一复核。
- **接口与产出**：实现公开配对文件导入、完整SHA256/地址/device_id柜前核对页、专用受限SecurityContext、同origin HTTPS/WSS、Keystore凭据隔离、启动/前后台鉴权、退出/忘记配对。密码不落盘，安全存储失败不回退明文。
- **调用链**：本机导出公开材料 → Android文档选择器 → inspectPublicBundle → 柜前核对 → accept/open → login → getCurrentSession/getDevice/getSnapshot → online；恢复登录时不调用不存在的refresh-token接口。
- **异常**：证书替换/过期/SAN错误/错误device_id/不兼容契约/系统时间错误/Keystore失效/401/离线退出/用户被停用。证书变化冻结请求并重新柜前配对，不能关闭校验。模拟器必须明显显示。
- **验收**：未完成带外核对前网络无密码/token；HTTP、HTTPS、WSS的受信范围一致；错误证书与主机名均拒绝且不发凭据；3xx不携带Bearer跳转；token不进入URL、日志、备份或明文文件；退出不取消借还。Android只申请实际必要权限。
- **完成证据**：证书测试矩阵、脱敏抓包/测试服务请求计数、首配与证书变化录屏、实际Android/SDK版本、会话恢复与离线退出测试输出。
- **禁止越界**：匿名配对API、固定默认账号、全局HttpOverrides、无条件badCertificateCallback、云中转、复制私钥到App。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-03/S3-02/summary.md和docs/evidence/stage-03/S3-02/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S3-02 Android配对会话；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-01: docs/evidence/stage-03/S3-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-02/批次、logs/S3-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S3-02；不接着实现后续卡。
读取docs/evidence/stage-03/S3-02/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-03/S3-02/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S3-02 Android配对会话；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-01: docs/evidence/stage-03/S3-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-02/批次、logs/S3-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S3-02，问题来源为docs/evidence/stage-03/S3-02/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S3-02 Android配对会话；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-01: docs/evidence/stage-03/S3-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-02/批次、logs/S3-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S3-02；依据本卡真实验收与docs/evidence/stage-03/S3-02/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-03/S3-02/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S3-03，规格文件docs/stages/stage-03-clients-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S3-03：借还与幂等恢复

前置：S3-02。下一张计划：S3-04 管理与变化重查。

[原始卡与公共约束](stages/stage-03-clients-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S3-03 借还与幂等恢复；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-02: docs/evidence/stage-03/S3-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-03/批次、logs/S3-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S3-03，不自动开始S3-04。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- app/lib/features/locker/
- app/lib/features/operations/
- app/lib/data/intents/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
public_routes: listTools（归属：client）
public_routes: getTool（归属：client）
public_routes: createOperation（归属：client）
public_routes: getOperation（归属：client）
public_routes: confirmOperation（归属：client）
public_routes: cancelOperation（归属：client）
public_routes: listLoans（归属：client）
public_routes: listEvents（归属：client）
public_routes: getOperationVisionEvidence（归属：client）

本卡验收依据全文如下，不得缩减：
- **前置**：S3-02通过。
- **输入**：Operation/Tool/Snapshot/Evidence、allowed_actions、当前接口错误语义、MutationIntent与IntentStore、正式借还场景fixture。
- **允许目录**：`app/lib/features/locker/`、`app/lib/features/operations/`、`app/lib/data/intents/`、本人借用/事件展示组件、正式业务交互测试、对应README和本卡证据。
- **接口与产出**：实现refresh、beginBorrowOrReturn、confirm、cancel、reconcilePending；单mutation在途、先持久意图再发、结果待核对页。只显示权威物理/借还状态，断线缓存标过期，不排队离线借还。
- **调用链**：getSnapshot/getTool → createOperation(原tool_revision/原幂等键) → REQUESTED/提示到柜 → GET operation → WAIT_CONFIRM → confirmOperation(原operation_revision/原幂等键) → COMPLETED+GET loans；归还kind=RETURN，手机不调用presence。
- **异常**：双击、超时/RESULT_PENDING、响应丢失、App被杀、进后台、账号切换、409、传感器UNKNOWN、已动作后取消、NO_CHANGE、REVIEW_REQUIRED。已知ID先GET；未知ID仅由用户选择核对后使用原方法/原键/原正文。原用户以外不能读取或恢复该意图；超过30天不自动重发。
- **验收**：重复点击只出现一个用户意图；响应丢失后无新键/新operation；本地取消等待不发送成功取消；201不显示门已开，ACK不显示成功，倒计时归零不改变账本；只有OWNER+服务端allowed_actions显示确认，最终服务端仍鉴权。
- **完成证据**：应用事件轨迹与服务端operation/command对应表、请求数/幂等键脱敏关联、双击/杀进程/丢响应/冲突测试输出、无重复锁动作的服务与设备事实证据。
- **禁止越界**：自动重开、客户端推断成功、把ADMIN当任意用户确认、重连自动发旧POST、手机伪造柜前证明。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-03/S3-03/summary.md和docs/evidence/stage-03/S3-03/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S3-03 借还与幂等恢复；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-02: docs/evidence/stage-03/S3-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-03/批次、logs/S3-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S3-03；不接着实现后续卡。
读取docs/evidence/stage-03/S3-03/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-03/S3-03/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S3-03 借还与幂等恢复；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-02: docs/evidence/stage-03/S3-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-03/批次、logs/S3-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S3-03，问题来源为docs/evidence/stage-03/S3-03/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S3-03 借还与幂等恢复；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-02: docs/evidence/stage-03/S3-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-03/批次、logs/S3-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S3-03；依据本卡真实验收与docs/evidence/stage-03/S3-03/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-03/S3-03/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S3-04，规格文件docs/stages/stage-03-clients-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S3-04：管理与变化重查

前置：S3-03。下一张计划：S3-05 LVGL异步IPC。

[原始卡与公共约束](stages/stage-03-clients-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S3-04 管理与变化重查；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-03: docs/evidence/stage-03/S3-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-04/批次、logs/S3-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S3-04，不自动开始S3-05。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- app/lib/features/admin/
- app/lib/features/history/
- app/lib/data/changes/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
public_routes: listReviews（归属：client）
public_routes: resolveOperation（归属：client）
public_routes: listUsers（归属：client）
public_routes: createUser（归属：client）
public_routes: updateUser（归属：client）
public_routes: listCardObservations（归属：client）
public_routes: bindCard（归属：client）
public_routes: unbindCard（归属：client）
public_routes: subscribeChanges（归属：client）
public_routes: getVisionStatus（归属：client）

本卡验收依据全文如下，不得缩减：
- **前置**：S3-03通过。
- **输入**：Review/User/CardObservation/CardBinding、WS消息schema、分页和权限规则、已存在LockerApi与前后台生命周期。
- **允许目录**：`app/lib/features/admin/`、`app/lib/features/history/`、`app/lib/data/changes/`、对应controller接线、正式测试、README和本卡证据。不另换网络/TLS实现。
- **接口与产出**：实现listReviews/resolveOperation、用户启停/角色/卡绑定、本人事件/借用分页；统一一个WS订阅和一个10秒前台快照刷新循环，通知合并重查。管理员核验页显示原用户/类型/当前物理/版本及理由，不提供强制完成。createUser密码只在短期内存，持久意图只存原key与username等非敏感定位，不能照普通借还意图保存完整正文。
- **调用链**：WSS resource.changed或resync_required → dirty合并 → GET snapshot/operation/相关列表；ADMIN listReviews → GET operation/当前物理 → resolveOperation；ADMIN listCardObservations → 选择新鲜observation → bindCard。unbindCard保留查询user_revision与幂等键。
- **异常**：WS丢失/重连/溢出、消息乱序、旧GET晚到、页面销毁、后台恢复、卡观察过期/已消费/重复绑定、管理员降权/停用、物理UNKNOWN或核验revision变化。createUser丢响应先listUsers按username核对；确需重发且内存密码已丢失时要求用户重输原密码，保留原key/相同正文，冲突停止核查，不自动换key建重复账号。
- **验收**：WS永不执行命令或直接推进状态；重连与前台恢复重查，暂停后台轮询；不透明cursor原样传递、无跨页越权；旧登录generation与旧资源版本不覆盖新视图；未知物理与原用户不匹配不能核验；Device.ui_restart_count为null时显示“未知”且不自行累计；全部24入口有调用/不适用说明。专门复现createUser提交后丢响应并杀进程：无密码落盘/入日志，重启先listUsers核对，不后台重发；重输原密码只复用原key，错误重输导致IDEMPOTENCY_CONFLICT而不自动换key；账号最多一个。
- **完成证据**：WS断连恢复轨迹、两个GET反序返回测试、管理员/普通用户权限矩阵、卡观察测试、分页与后台网络计数。
- **禁止越界**：手机maintenance/清日志/任意GPIO按钮、给WS加命令、用本地列表缓存判定可借、把管理员账号写死。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-03/S3-04/summary.md和docs/evidence/stage-03/S3-04/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S3-04 管理与变化重查；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-03: docs/evidence/stage-03/S3-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-04/批次、logs/S3-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S3-04；不接着实现后续卡。
读取docs/evidence/stage-03/S3-04/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-03/S3-04/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S3-04 管理与变化重查；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-03: docs/evidence/stage-03/S3-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-04/批次、logs/S3-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S3-04，问题来源为docs/evidence/stage-03/S3-04/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S3-04 管理与变化重查；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-03: docs/evidence/stage-03/S3-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-04/批次、logs/S3-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S3-04；依据本卡真实验收与docs/evidence/stage-03/S3-04/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-03/S3-04/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S3-05，规格文件docs/stages/stage-03-clients-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S3-05：LVGL异步IPC

前置：S3-01。下一张计划：S3-06 双端真机闭环。

[原始卡与公共约束](stages/stage-03-clients-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S3-05 LVGL异步IPC；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-01: docs/evidence/stage-03/S3-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-05/批次、logs/S3-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S3-05，不自动开始S3-06。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- terminal-ui/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置**：S3-01通过；使用阶段1真实service.sock协议。supervisor可尚未实现，进度接收端测试必须标为测试替身。
- **输入**：客户端C私有声明、InternalRpcRequest/Response、23同名方法及presence.submit、含sampled_mono_ms的ProgressReport/locker_progress_t、RK显示/触摸ABI与单UI线程约束。所有源采样使用同机CLOCK_MONOTONIC。
- **允许目录**：`terminal-ui/`、对应正式UI/IPC测试、模块README和本卡证据；只使用模块局部构建入口。不得编辑host/ipc、host/domain、host/supervisor来迎合自己的实现。
- **接口与产出**：实现ui_ipc生命周期、copy提交/非阻塞取结果、受限包长/MSG_TRUNC、请求generation、内存token、LVGL所有控件单线程；界面包括登录/总览/当前操作/柜前确认/本人确认/必要管理员核验。只实现当前范围必要页面，用户/卡管理可复用同名RPC的简洁管理员页，不能另建“高级开锁”。
- **调用链**：触摸login/getSnapshot→本人getOperation→presence.submit→Core→异步结果/轮询→WAIT_CONFIRM后confirmOperation。新实例counter为0，登录页完整真实循环后commit_loop_progress产生非零/sample，不等待登录。后台只复制原sample和源age报告，接收方累计now-source_sample一次；IPC/Runtime不能重打时间或先写已累计age造成双算。
- **异常**：服务未启动、socket权限拒绝、包截断/非法JSON、响应丢失/乱序、IPC队列满、UI退出/重启、触摸卡死、显示flush停滞、监督进程未启动。UI断线不自动补发presence，重启回登录页并由原用户重查。
- **验收**：工作线程无LVGL调用；UI没有数据库/UART/GPIO权限；SOCK_SEQPACKET上下限与request_id匹配测试通过；新实例未完成一整轮UI循环时counter为0，登录页真实完整循环即可非零；卡在lv_timer_handler/视图更新时progress/sample时刻不再更新而age增长；IPC积压覆盖传输前等待，重复相同counter不刷新真实进展；源采样字段超uint64/未来/倒退拒绝。独立IPC线程活着不能报告UI继续前进。OBSERVED_RUNNING要求期望新实例非零、至少两次严格递增且源样本新鲜，只证明可交互；登录页可通过，不能因未登录或未取快照而反复重启。业务按钮仍需重新登录+fresh snapshot，不静默复用旧凭据或加匿名接口。supervisor缺席不伪造ACK/恢复成功，不单独阻断依赖真实Core健康的阶段2操作。
- **完成证据**：单线程调用审查、请求/结果队列边界与生命周期测试、SO_PEERCRED拒绝案例、实际RK触摸显示结果、UI卡死与progress采样轨迹。模拟窗口只能证明UI逻辑，不代替RK显示驱动验证。
- **禁止越界**：在UI线程等待IPC、工作线程直接更新控件、定时器自行加健康计数、共享终端永久保存密码/token、给LVGL加入CLI专属maintenance。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-03/S3-05/summary.md和docs/evidence/stage-03/S3-05/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S3-05 LVGL异步IPC；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-01: docs/evidence/stage-03/S3-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-05/批次、logs/S3-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S3-05；不接着实现后续卡。
读取docs/evidence/stage-03/S3-05/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-03/S3-05/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S3-05 LVGL异步IPC；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-01: docs/evidence/stage-03/S3-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-05/批次、logs/S3-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S3-05，问题来源为docs/evidence/stage-03/S3-05/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S3-05 LVGL异步IPC；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-01: docs/evidence/stage-03/S3-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-05/批次、logs/S3-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S3-05；依据本卡真实验收与docs/evidence/stage-03/S3-05/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-03/S3-05/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S3-06，规格文件docs/stages/stage-03-clients-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S3-06：双端真机闭环

前置：S3-04、S3-05。下一张计划：S3-07 客户端阶段审计。

[原始卡与公共约束](stages/stage-03-clients-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S3-06 双端真机闭环；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-04: docs/evidence/stage-03/S3-04/summary.md、handoff.md、audit.md
- S3-05: docs/evidence/stage-03/S3-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-06/批次、logs/S3-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S3-06，不自动开始S3-07。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- tests/integration/clients/
- app/
- terminal-ui/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置**：S3-04与S3-05通过，S2-16实物底座仍有效；硬件profile或安装改变须先重验受影响的阶段2门槛。
- **输入**：同一契约版本的服务/固件/App/UI、设备身份与证书、已绑定本人/管理员测试账号、正式验收场景；账号/密码不进入证据仓库。
- **允许目录**：`tests/integration/clients/`、App/UI跨端接线与配置、脱敏正式fixture、模块README和本卡证据。发现Core/UART问题提交最小复现给原模块卡修复，不在客户端补另一套状态机。
- **接口与产出**：固定一个工具、一个合法有效借用；明确标注手机申请→本人到柜→取物关门→本人确认→归还的完整链。编写可复现流程和各段ID关联，触摸与手机显示同一Core结果。
- **调用链**：先用模拟服务跑正式客户端故障fixture，再在局域网真机跑HTTPS/IPC → Core/SQLite → UART → MCU持久化/硬脉冲 → 门/物品事实 → 手机/柜屏重查。simulated=true和实物结果分栏记录。
- **异常**：手机网断、柜屏重启、刷他人卡/旧卡事件、两端同时申请、创建响应丢失、物品没变、未确认超时、门未关、服务/MCU重启、管理核验。独立自动恢复留阶段4，不临时加服务自启来绕过核对。
- **验收**：手机申请不能单独打开柜；本人到柜后仅一条command、物理完成后才可确认；两个端版本与账本一致，读到另一个用户的数据被服务阻止；所有不确定结果有可解释待核对出口；总预算和材料范围不因客户端增加新硬件。
- **完成证据**：真实Android/RK/MCU版本与构建指纹、脱敏ID关联表、双端录像/截图、数据库审计及设备事件索引、每项PASS/FAIL/NOT_RUN矩阵。没有真机证据不写“完整项目可用”。
- **禁止越界**：为了演示写死成功、隐藏simulated、用App动画代替物理证据、改公共契约规避错误、偷偷加入云/新采购。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-03/S3-06/summary.md和docs/evidence/stage-03/S3-06/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S3-06 双端真机闭环；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-04: docs/evidence/stage-03/S3-04/summary.md、handoff.md、audit.md
- S3-05: docs/evidence/stage-03/S3-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-06/批次、logs/S3-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S3-06；不接着实现后续卡。
读取docs/evidence/stage-03/S3-06/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-03/S3-06/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S3-06 双端真机闭环；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-04: docs/evidence/stage-03/S3-04/summary.md、handoff.md、audit.md
- S3-05: docs/evidence/stage-03/S3-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-06/批次、logs/S3-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S3-06，问题来源为docs/evidence/stage-03/S3-06/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S3-06 双端真机闭环；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-04: docs/evidence/stage-03/S3-04/summary.md、handoff.md、audit.md
- S3-05: docs/evidence/stage-03/S3-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-06/批次、logs/S3-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S3-06；依据本卡真实验收与docs/evidence/stage-03/S3-06/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-03/S3-06/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S3-07，规格文件docs/stages/stage-03-clients-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S3-07：客户端阶段审计

前置：S3-06。下一张计划：S4-01 日志容量与回收。

[原始卡与公共约束](stages/stage-03-clients-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S3-07 客户端阶段审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-06: docs/evidence/stage-03/S3-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-07/批次、logs/S3-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S3-07，不自动开始S4-01。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- tests/contract/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置**：S3-06交付；审查前六卡全部实际结果，不凭其他AI结论复述通过。
- **输入**：前六卡证据、当前输入契约SHA、修改文件/依赖清单、正式测试入口及阶段0禁止事项。
- **允许目录**：正式审计测试、`docs/evidence/stage-03/`和阶段报告。产品缺陷退回责任卡修复并重验受影响项；审计卡不扩大功能或放宽断言。
- **接口与调用链核对**：24公开入口/23同名RPC+presence/progress逐项匹配；TLS带外信任、凭据、幂等持久意图、WS重查、LVGL所有权与真实progress/sample时刻；验证Runtime/IPC只搬运不可变采样、同counter报告不刷新进展。管理员核验只走现有规则；无客户端→锁的旁路。
- **异常复核**：至少独立复现一次证书替换拒绝、原键响应丢失核对、跨用户隔离、旧响应晚到、UI登录页完整循环即可产生非零且业务按钮仍受登录/快照限制、旧实例/单次非零不能判恢复、UI循环卡死而通信线程仍活、presence断线不自动重放。注明测试环境与真机/模拟差异。
- **验收**：契约检查与实际客户端构建/测试通过；相关README/接口/状态文档等于当前事实；无本轮debug print/一次性mock/硬写账号/真实密钥残留；正式诊断日志和fixture保留。存在关键FAIL或NOT_RUN不开放阶段4实物门槛，普通静态检查不能替代。
- **完成证据**：审计报告、精确命令/实际输出索引、未完成项/风险/责任卡、允许进入阶段4或阻断结论。当前设计阶段不预填PASS，不编造专家审核已通过。
- **禁止越界**：将“页面能点击”写成实物成功、只以自测截图解锁阶段、隐藏证据缺口、自动commit/push。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-03/S3-07/summary.md和docs/evidence/stage-03/S3-07/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S3-07 客户端阶段审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-06: docs/evidence/stage-03/S3-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-07/批次、logs/S3-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S3-07；不接着实现后续卡。
读取docs/evidence/stage-03/S3-07/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-03/S3-07/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S3-07 客户端阶段审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-06: docs/evidence/stage-03/S3-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-07/批次、logs/S3-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S3-07，问题来源为docs/evidence/stage-03/S3-07/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S3-07 客户端阶段审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-03-clients-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-06: docs/evidence/stage-03/S3-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S3-07/批次、logs/S3-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S3-07；依据本卡真实验收与docs/evidence/stage-03/S3-07/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-03/S3-07/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡是阶段3正式门禁卡。逐项核实本阶段全部卡报告和必要实跑。
只有全部通过才生成docs/evidence/stage-03/summary.md汇总并链接每卡证据，
由唯一负责人同步本卡DONE、阶段implementation_status=DONE、gate_result=PASS及固定evidence。
关键NOT_RUN/阻塞存在不能把阶段设PASS，也不能启动下一阶段。
下一张计划卡为S4-01，规格文件docs/stages/stage-04-monitoring-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S4-01：日志容量与回收

前置：S3-07、S2-08。下一张计划：S4-02 供电关联。

[原始卡与公共约束](stages/stage-04-monitoring-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S4-01 日志容量与回收；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-07: docs/evidence/stage-03/S3-07/summary.md、handoff.md、audit.md
- S2-08: docs/evidence/stage-02/S2-08/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-01/批次、logs/S4-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S4-01，不自动开始S4-02。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- firmware/journal/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: mcu_journal_append（归属：refinement）
internal_ports: mcu_journal_commit_intent（归属：refinement）
internal_ports: mcu_journal_read_page（归属：refinement）
internal_ports: mcu_journal_ack（归属：refinement）
internal_ports: mcu_journal_init（归属：refinement）
internal_ports: mcu_journal_bind_sink（归属：refinement）
internal_ports: mcu_journal_control（归属：refinement）

本卡验收依据全文如下，不得缩减：
- **前置/输入**：上表前置；阶段 2 实测 Flash 几何、文件格式/检查点、共享 SPI 延迟、Journal sink；UART FETCH/ACK 布局和阶段 1 事件/游标事务。
- **允许目录**：`firmware/journal/`、该模块私有测试、`tests/integration/monitoring/`、`docs/evidence/stage-04/S4-01/`；模块 README。不能改普通 API、线布局或根构建。
- **接口/调用链**：`mcu_journal_read_page(journal,after,limit,result)` 最多 4 条；`mcu_journal_ack(journal,cursor)` 在 Storage 独占执行。`repo_import_events` 提交 → `device_ack_persisted` → ACK_PERSISTED → 持久 ACK → 逐段安全回收。私有容量/回收 helper 签名按接口文档第 3 节。
- **产物**：含元数据/检查点预留的容量表、字节与文件数上限、已 ACK 完整段回收、不可回收未决引用、满容量拒绝策略；有界诊断日志轮转。
- **验收**：未 ACK 不删；ACK 倒退/越界/跨缺口拒绝；ACK 丢失多保留；回收前/中/后掉电不回退动作水位；空间不足时新 EXECUTE 被 LOG_UNAVAILABLE 拒绝。满容量期间丢失事实可见，event_seq 连续不能掩盖 dropped_events/连续性失效。
- **完成证据**：实际容量/预留依据、填满到边界前后文件索引、持久游标与保留记录、回收故障注入日志；区分主机文件后端测试和真实 Flash 结果。
- **禁止越界**：新建另一套 Journal、按时间删除未 ACK 记录、mount失败format、回收未决/去重检查点、删 SQLite/WAL 来“释放空间”、报告未经测试的保留天数。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-04/S4-01/summary.md和docs/evidence/stage-04/S4-01/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S4-01 日志容量与回收；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-07: docs/evidence/stage-03/S3-07/summary.md、handoff.md、audit.md
- S2-08: docs/evidence/stage-02/S2-08/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-01/批次、logs/S4-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S4-01；不接着实现后续卡。
读取docs/evidence/stage-04/S4-01/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-04/S4-01/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S4-01 日志容量与回收；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-07: docs/evidence/stage-03/S3-07/summary.md、handoff.md、audit.md
- S2-08: docs/evidence/stage-02/S2-08/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-01/批次、logs/S4-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S4-01，问题来源为docs/evidence/stage-04/S4-01/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S4-01 日志容量与回收；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S3-07: docs/evidence/stage-03/S3-07/summary.md、handoff.md、audit.md
- S2-08: docs/evidence/stage-02/S2-08/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-01/批次、logs/S4-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S4-01；依据本卡真实验收与docs/evidence/stage-04/S4-01/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-04/S4-01/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S4-02，规格文件docs/stages/stage-04-monitoring-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S4-02：供电关联

前置：S4-01。下一张计划：S4-03 MCU进度与IWDG。

[原始卡与公共约束](stages/stage-04-monitoring-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S4-02 供电关联；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-01: docs/evidence/stage-04/S4-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-02/批次、logs/S4-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S4-02，不自动开始S4-03。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- firmware/control/
- firmware/drivers/adc/
- firmware/config/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: mcu_control_observe_inputs（归属：refinement）

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S4-01；阶段 2 ADC 校准、分压/额定输入与 supply_valid；Control 单写物理版本规则；原始单调时间与 TIME_ANCHOR。
- **允许目录**：`firmware/control/`、`firmware/drivers/adc/`、`firmware/config/` 的已核验参数、相应模块测试、`docs/evidence/stage-04/S4-02/`；参数调整不得占用锁截止定时器。
- **接口/调用链**：`mcu_board_read_inputs(locker_board_inputs_t *result)` → `mcu_control_observe_inputs(control,inputs)` → `mcu_journal_append(journal,event,token)` → SUPPLY_CHANGED 事件 → DEVICE_EVENT/FETCH_EVENTS → Core 唯一事实事务。
- **产物**：供电类别编号映射、实际阈值/滞回/持续时间、样本过期判据、UNKNOWN传播、同boot单调时间关联说明；只解释观察不自动诊断根因。
- **验收**：正常噪声不刷屏/不连续涨物理版本；越界/过期为未知；有效0mV不当未知；电压异常关闭新动作资格；恢复不清日志缺口/未知操作。突断 MCU 电源不得声称掉电前最后事件必落盘或断电期间可记录。
- **完成证据**：标定与真实输入对照、类别转换与event_seq/physical_revision时间线、ADC无效/过期场景、供电与主机失联时间质量记录；无实测不能标VERIFIED。
- **禁止越界**：凭12V标签猜阈值、更新公开schema/添加云端分析、以到达时间改原始mono_ms、把“供电变化同时出现”写成已证明原因、增加UPS或新传感器。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-04/S4-02/summary.md和docs/evidence/stage-04/S4-02/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S4-02 供电关联；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-01: docs/evidence/stage-04/S4-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-02/批次、logs/S4-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S4-02；不接着实现后续卡。
读取docs/evidence/stage-04/S4-02/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-04/S4-02/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S4-02 供电关联；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-01: docs/evidence/stage-04/S4-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-02/批次、logs/S4-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S4-02，问题来源为docs/evidence/stage-04/S4-02/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S4-02 供电关联；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-01: docs/evidence/stage-04/S4-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-02/批次、logs/S4-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S4-02；依据本卡真实验收与docs/evidence/stage-04/S4-02/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-04/S4-02/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S4-03，规格文件docs/stages/stage-04-monitoring-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S4-03：MCU进度与IWDG

前置：S4-02。下一张计划：S4-04 监督观察及健康搬运。

[原始卡与公共约束](stages/stage-04-monitoring-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S4-03 MCU进度与IWDG；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-02: docs/evidence/stage-04/S4-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-03/批次、logs/S4-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S4-03，不自动开始S4-04。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- firmware/health/
- firmware/tasks/
- firmware/hal/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: mcu_health_report_progress（归属：refinement）
internal_ports: mcu_health_evaluate（归属：refinement）
internal_ports: mcu_health_init（归属：refinement）

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S4-02；Control/Communication/PeripheralStorage/Health 实际周期，最坏擦写延迟与阶段 2 硬截止证据；本地 HAL V1.8.5 IWDG。
- **允许目录**：`firmware/health/`、`firmware/tasks/` 的进度点、`firmware/hal/` 的私有IWDG适配、模块测试、`docs/evidence/stage-04/S4-03/`；不修改原HAL库。
- **接口/调用链**：`mcu_health_report_progress(health,task_id,progress)` 报告已完成循环 → `mcu_health_evaluate(health,monotonic_ms)` 检查进度期限 → Health 私有HAL刷新。退化事实用既有HEALTH_CHANGED/JOURNAL_FAULT，经Control/Storage有界路径记录；Health不能直接抢占littlefs或锁。
- **产物**：统一4任务私有ID表、64位原子副本、任务期限及测量依据、唯一IWDG刷新点、复位原因记录、状态变化去抖。主机离线与任务卡死分开。
- **验收**：冻结任一关键任务即停止有效进展，Health撤销喂狗；Health自身卡住由IWDG超时；重复progress/中断Tick不能保持健康；空闲串口/空日志队列正常推进。主机断线或可处理日志满不能使MCU无故复位；锁截止不随冻结延长。
- **完成证据**：各任务冻结条件、IWDG配置/LSI误差与实际复位窗口、复位后安全输出与boot记录、对照“仅失联不复位”数据；调试冻结状态单列。
- **禁止越界**：ISR/定时器直接喂狗、一个线程替所有任务加计数、因Flash忙无限延长期限、自动主机复位、把IWDG当成锁脉冲硬截止。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-04/S4-03/summary.md和docs/evidence/stage-04/S4-03/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S4-03 MCU进度与IWDG；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-02: docs/evidence/stage-04/S4-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-03/批次、logs/S4-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S4-03；不接着实现后续卡。
读取docs/evidence/stage-04/S4-03/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-04/S4-03/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S4-03 MCU进度与IWDG；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-02: docs/evidence/stage-04/S4-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-03/批次、logs/S4-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S4-03，问题来源为docs/evidence/stage-04/S4-03/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S4-03 MCU进度与IWDG；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-02: docs/evidence/stage-04/S4-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-03/批次、logs/S4-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S4-03；依据本卡真实验收与docs/evidence/stage-04/S4-03/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-04/S4-03/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S4-04，规格文件docs/stages/stage-04-monitoring-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S4-04：监督观察及健康搬运

前置：S4-03。下一张计划：S4-05 固定UI恢复。

[原始卡与公共约束](stages/stage-04-monitoring-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S4-04 监督观察及健康搬运；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-03: docs/evidence/stage-04/S4-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-04/批次、logs/S4-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S4-04，不自动开始S4-05。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/runtime/
- terminal-ui/
- host/supervisor/
- host/domain/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: locker_supervisor_report_progress（归属：implementation）
internal_ports: locker_supervisor_create（归属：implementation）
internal_ports: locker_supervisor_destroy（归属：implementation）
internal_ports: locker_supervisor_get_health（归属：implementation）
internal_ports: locker_core_apply_supervisor_health（归属：implementation）
local_methods: progress.report（归属：implementation）
local_methods: health.get（归属：implementation）

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S4-03；阶段3已有UI真实进度生产与非阻塞快照接线；0.2.1 health.get、SupervisorHealthResponse、C端口、Core观察入口及样例检查通过。源或接线缺失时回契约负责人，不实现越界通道，不标本卡完成。
- **允许目录**：`host/runtime/`、`terminal-ui/`的进度/IPC、`host/supervisor/`观察缓存、`host/domain/`的core_apply_supervisor_health观察入口和投影、相应测试及`docs/evidence/stage-04/S4-04/`；不重写借还状态机，共享定义只消费生成契约。
- **接口/调用链**：Core实际回路→get_progress→Runtime→progress.report；UI报告同源不可变样本；supervisor_report_progress接收。service→health.get→supervisor_get_health→core_apply_supervisor_health入队→Core唯一更新Device投影。HOST_HEALTH的Core始终直取get_progress，UI取监督源样本，各累计源年龄一次→device_send_health；监督缺席不覆盖直接Core健康。
- **产物**：已有真实进度生产点的接线、原始sampled_mono_ms/年龄搬运、未知与溢出处理、5秒停滞判据；可独立启动的supervisor观察模式、socket权限/实例登记/查询及生命周期，恢复功能保持禁用。此卡不等待S4-05的子进程恢复才能验收。
- **验收**：观察模式可启动、停止、查询，任意恢复请求明确拒绝；阻塞业务Core而HTTP线程仍活时应判Core停滞；阻塞LVGL而IPC线程仍活时应判UI停滞；重复值不刷新进展，旧实例晚到不复活；在socket队列延迟后收到的旧progress仍过期。未知年龄映射UINT32_MAX，缺席的UI/supervisor只告警；新动作主机健康前置仍取真实Core新鲜度。UI等待服务期间循环继续推进；启动宽限不授予READY。
- **完成证据**：生产点代码索引、冻结/正常两组进度时间线、supervisor接收样本与HOST_HEALTH字节解码、年龄上界计算、0.2.1真源指纹与入口映射。
- **禁止越界**：新增定时器假进度、supervisor共享UART、service直读监督私有文件、把ACK当整体健康、未取得UI样本时填0伪装健康、放宽证书或peer检查。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-04/S4-04/summary.md和docs/evidence/stage-04/S4-04/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S4-04 监督观察及健康搬运；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-03: docs/evidence/stage-04/S4-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-04/批次、logs/S4-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S4-04；不接着实现后续卡。
读取docs/evidence/stage-04/S4-04/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-04/S4-04/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S4-04 监督观察及健康搬运；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-03: docs/evidence/stage-04/S4-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-04/批次、logs/S4-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S4-04，问题来源为docs/evidence/stage-04/S4-04/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S4-04 监督观察及健康搬运；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-03: docs/evidence/stage-04/S4-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-04/批次、logs/S4-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S4-04；依据本卡真实验收与docs/evidence/stage-04/S4-04/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-04/S4-04/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S4-05，规格文件docs/stages/stage-04-monitoring-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S4-05：固定UI恢复

前置：S4-04。下一张计划：S4-06 监护与业务接线。

[原始卡与公共约束](stages/stage-04-monitoring-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S4-05 固定UI恢复；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-04: docs/evidence/stage-04/S4-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-05/批次、logs/S4-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S4-05，不自动开始S4-06。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/supervisor/
- deployment/host/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
internal_ports: locker_supervisor_request_ui_restart（归属：implementation）
local_methods: ui.restart.request（归属：implementation）

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S4-04；目标机init/固定UI程序位置与UID；`locker_supervisor_config_t`、ProgressReport、RestartRequest、SupervisorRpcAck；状态与检查点规则。
- **允许目录**：`host/supervisor/`、`deployment/host/` 的监督配置、监督模块测试、`docs/evidence/stage-04/S4-05/`；不修改业务数据库schema。
- **接口/调用链**：`locker_supervisor_create(config,out)` → 监督循环；私有 `ui.restart.request` → `locker_supervisor_request_ui_restart(supervisor,request)` → incident/额度持久化 → 确认旧UI退出 → 固定exec新UI → 登录页可交互+真实完整循环两次新鲜递增 → OBSERVED_RUNNING；登录/重连后业务按钮另由fresh getSnapshot门禁。停止循环及工人后destroy。
- **产物**：在S4-04观察模式上启用固定UI子进程监督；固定程序/参数/环境白名单、受控子进程身份、10分钟最多3次/60秒冷却、30秒UI启动宽限、独立恢复检查点、同incident回查、检查点损坏的LIMITED状态。
- **验收**：伪造peer/实例/任意target拒绝；同incident不重复拉起；并发故障不生成两份UI；监督器自身重启仍限次；检查点写失败不拉起；冷却和限次有效；进程创建成功不能当恢复。新UI清旧token回登录页，progress从0开始，登录页可交互且完整真实UI循环完成即可非零；期望新instance至少两次非零严格增加且源样本新鲜，30秒启动宽限内判OBSERVED_RUNNING，无需用户登录。该状态不代表业务同步，登录/重连后须fresh getSnapshot才开业务按钮；不新增匿名snapshot。service不健康或无人登录时不反复恢复UI；新UI不重放presence/确认。
- **完成证据**：受控PID/进程身份、恢复前后的instance/进度、限次窗口与掉电/重启检查点、错误peer和重复incident测试、UI实际恢复后的界面与当前业务状态。
- **禁止越界**：自动重启service、调用shell、按手机路径exec、删除监督检查点绕过限次、自动Linux断电/硬复位、UI重启时发开锁/重建借还操作。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-04/S4-05/summary.md和docs/evidence/stage-04/S4-05/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S4-05 固定UI恢复；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-04: docs/evidence/stage-04/S4-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-05/批次、logs/S4-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S4-05；不接着实现后续卡。
读取docs/evidence/stage-04/S4-05/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-04/S4-05/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S4-05 固定UI恢复；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-04: docs/evidence/stage-04/S4-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-05/批次、logs/S4-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S4-05，问题来源为docs/evidence/stage-04/S4-05/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S4-05 固定UI恢复；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-04: docs/evidence/stage-04/S4-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-05/批次、logs/S4-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S4-05；依据本卡真实验收与docs/evidence/stage-04/S4-05/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-04/S4-05/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S4-06，规格文件docs/stages/stage-04-monitoring-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S4-06：监护与业务接线

前置：S4-05。下一张计划：S4-07 故障矩阵。

[原始卡与公共约束](stages/stage-04-monitoring-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S4-06 监护与业务接线；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-05: docs/evidence/stage-04/S4-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-06/批次、logs/S4-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S4-06，不自动开始S4-07。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/domain/
- host/device/
- host/runtime/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S4-05；既有event键/连续游标、LIVE/REPLAY区分、UI_RESTART_REQUEST事实、私有maintenance.reconcile与管理员resolve规则。
- **允许目录**：`host/domain/`、`host/device/`、`host/runtime/` 的既有端口接线、相关正式测试、`docs/evidence/stage-04/S4-06/`；仓储变更须交唯一Storage所有者评估，不另开写连接。
- **接口/调用链**：Device sink → `locker_core_apply_device_event(core,event)` → `locker_repo_import_events(repo,batch,projection_change,cursor)` → `device_ack_persisted`。仅当前LIVE/新鲜UI恢复事实可映射持久incident → supervisor请求；REPLAY只补记录。人工重启service先HELLO/屏障/FETCH/落库/ACK/核对/RESUME_READY。
- **产物**：健康原因到公开Device投影、ui_restart_count确切来源、UUIDv5完整事件键到incident确定性映射、恢复中业务保留、日志缺口和未决动作核验路径；App沿用既有GET/WS重查。
- **验收**：同事实重复导入只落一次；历史UI恢复请求不重启UI、不生成柜前证明、不触锁；旧session晚到被拒；DB事务失败不ACK；服务重启不重发EXECUTE；UI恢复不改账本。尾杆访问仍hold，维护不清损坏/连续性故障。
- **完成证据**：同一故障LIVE/REPLAY对照、数据库事件/游标/incident索引、设备发送计数、未决操作保持和人工核验审计、公开查询的权限与敏感字段检查。
- **禁止越界**：以健康恢复完成借还、以新session清日志故障、自动放弃占位/未决操作、历史补传触发任何动作、公开HTTP增加重启/清日志/强制READY接口。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-04/S4-06/summary.md和docs/evidence/stage-04/S4-06/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S4-06 监护与业务接线；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-05: docs/evidence/stage-04/S4-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-06/批次、logs/S4-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S4-06；不接着实现后续卡。
读取docs/evidence/stage-04/S4-06/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-04/S4-06/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S4-06 监护与业务接线；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-05: docs/evidence/stage-04/S4-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-06/批次、logs/S4-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S4-06，问题来源为docs/evidence/stage-04/S4-06/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S4-06 监护与业务接线；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-05: docs/evidence/stage-04/S4-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-06/批次、logs/S4-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S4-06；依据本卡真实验收与docs/evidence/stage-04/S4-06/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-04/S4-06/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S4-07，规格文件docs/stages/stage-04-monitoring-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S4-07：故障矩阵

前置：S4-06。下一张计划：S4-08 监护阶段审计。

[原始卡与公共约束](stages/stage-04-monitoring-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S4-07 故障矩阵；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-06: docs/evidence/stage-04/S4-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-07/批次、logs/S4-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S4-07，不自动开始S4-08。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- tests/integration/monitoring/
- tests/fixtures/monitoring/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S4-06；S2-08已核准接线/保护/硬截止；以上各卡的版本/配置/正式测试；不购置预算外器件。
- **允许目录**：`tests/integration/monitoring/`、`tests/fixtures/monitoring/`、`docs/evidence/stage-04/S4-07/`；测试注入入口保持私有且生产禁用，不开放HTTPS。
- **接口/调用链**：仅通过前卡公开/内部既有端口和受控测试适配器注入；正式设备走真实UART/Flash，测试替身结果单列。
- **产物**：故障矩阵覆盖：Core卡住、UI卡住/UI崩溃、监督器重启、MCU任一关键任务冻结、主机失电、串口拔断、Flash满/故障/回收中断、ADC未知/波动、历史补传、ACK丢失、同时发生故障。
- **验收**：每场景同时判定锁脉冲上界、账本/操作、日志连续性、恢复对象/次数、状态可见性；冻结任务仍满足阶段2硬截止。不能写事件时明确丢失/未知，不能以无记录认定无故障；主机恢复后先核对，历史不动作。
- **完成证据**：逐场景步骤/时间/版本/真实或模拟标识、原始串口与事实记录、文件/DB检查点、脉冲波形和复位窗口、期望/实际/差异。失败保留原始诊断，不能删日志重测后仅报成功。
- **禁止越界**：短接电源等破坏性注入、未经S2核准真锁测试、放宽阈值掩盖卡死、模拟结果冒充实物、以自动重启service令测试通过。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-04/S4-07/summary.md和docs/evidence/stage-04/S4-07/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S4-07 故障矩阵；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-06: docs/evidence/stage-04/S4-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-07/批次、logs/S4-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S4-07；不接着实现后续卡。
读取docs/evidence/stage-04/S4-07/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-04/S4-07/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S4-07 故障矩阵；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-06: docs/evidence/stage-04/S4-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-07/批次、logs/S4-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S4-07，问题来源为docs/evidence/stage-04/S4-07/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S4-07 故障矩阵；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-06: docs/evidence/stage-04/S4-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-07/批次、logs/S4-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S4-07；依据本卡真实验收与docs/evidence/stage-04/S4-07/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-04/S4-07/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S4-08，规格文件docs/stages/stage-04-monitoring-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S4-08：监护阶段审计

前置：S4-07。下一张计划：S5-01 制品与平台清单。

[原始卡与公共约束](stages/stage-04-monitoring-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S4-08 监护阶段审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-07: docs/evidence/stage-04/S4-07/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-08/批次、logs/S4-08/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S4-08，不自动开始S5-01。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- docs/evidence/stage-04/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S4-07完成；S4-01..07证据和0.2.1监督观察接口检查；当前契约/生成器及阶段2安全证据。
- **允许目录**：`docs/evidence/stage-04/`、阶段/模块README和本阶段设计说明；审计发现实现问题回所属卡处理，审计卡不顺手扩功能。
- **接口/调用链**：逐条追踪“原始输入→生产进度/事实→持久化→观察搬运→恢复/拒绝→公开投影”；对照统一C/JSON/UART真源，不以单端能编译当三端一致。
- **产物**：通过/未通过/未测项表、架构偏移检查、接口变更清单、容量与任务期限测量结论、后续阶段前置清单。
- **验收**：契约检查通过；正式测试和实物矩阵有证据；无假进度、历史动作、自动service/主机复位、未ACK回收、格式化掩盖故障、直接锁入口。所有未决缺口有owner与阻塞范围，未通过项不能标DONE。
- **完成证据**：`docs/evidence/stage-04/summary.md`、每卡证据索引、契约/固件/主机/UI版本和指纹、预算变化（应无新增器件）、临时残留检查。保留正式日志/fixture/测试，不修改用户原HAL包。
- **禁止越界**：声称未执行测试通过、把生成声明当实现、删失败证据、自动commit/push、把下一阶段整项开发夹入审计卡。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-04/S4-08/summary.md和docs/evidence/stage-04/S4-08/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S4-08 监护阶段审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-07: docs/evidence/stage-04/S4-07/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-08/批次、logs/S4-08/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S4-08；不接着实现后续卡。
读取docs/evidence/stage-04/S4-08/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-04/S4-08/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S4-08 监护阶段审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-07: docs/evidence/stage-04/S4-07/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-08/批次、logs/S4-08/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S4-08，问题来源为docs/evidence/stage-04/S4-08/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S4-08 监护阶段审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-04-monitoring-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-07: docs/evidence/stage-04/S4-07/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S4-08/批次、logs/S4-08/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S4-08；依据本卡真实验收与docs/evidence/stage-04/S4-08/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-04/S4-08/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡是阶段4正式门禁卡。逐项核实本阶段全部卡报告和必要实跑。
只有全部通过才生成docs/evidence/stage-04/summary.md汇总并链接每卡证据，
由唯一负责人同步本卡DONE、阶段implementation_status=DONE、gate_result=PASS及固定evidence。
关键NOT_RUN/阻塞存在不能把阶段设PASS，也不能启动下一阶段。
下一张计划卡为S5-01，规格文件docs/stages/stage-05-delivery-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S5-01：制品与平台清单

前置：S4-08。下一张计划：S5-02 启动权限配置。

[原始卡与公共约束](stages/stage-05-delivery-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S5-01 制品与平台清单；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-08: docs/evidence/stage-04/S4-08/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-01/批次、logs/S5-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S5-01，不自动开始S5-02。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- deployment/releases/
- deployment/target/
- host/cli/
- tests/maintenance/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S4-08；已运行的RK程序、HAL工程/固件、Android APK与依赖锁定记录。
- **视觉制品**：包含locker-vision二进制、RKNN包/类别表/管线、实际相机profile和策略哈希、对应SDK/runtime许可；不能只打包service而遗漏视觉依赖或混用原PT作为板端模型。
- **允许目录**：deployment/releases/、deployment/target/、host/cli/中仅verify-release/release_verify、对应tests/maintenance/、docs/evidence/stage-05/及构建说明；不重写业务。
- **接口/调用链**：固定输入源码/依赖→各端真实构建→制品哈希/ABI/schema/契约清单→release检查。制品校验只读，不启动设备动作。
- **产物**：release-manifest、许可证清单、RK ABI/动态库、固件目标/Flash布局/profile、APK SDK/包名/签名身份；可复现命令。APK签名密钥留本机受保护位置，不进制品包或Git。
- **验收**：干净构建得到可装配产物；哈希匹配；模拟与实物配置可辨；缺依赖/错误芯片/ABI/契约拒绝发布。
- **完成证据**：环境、命令/退出码、哈希、许可证和实际运行关联；字节不一致说明原因，不伪称可重复二进制。
- **禁止越界**：不浮动拉master、不将用户课程编译目录当正式固件、不带运行DB/密钥/测试账号到发布包。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-05/S5-01/summary.md和docs/evidence/stage-05/S5-01/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S5-01 制品与平台清单；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-08: docs/evidence/stage-04/S4-08/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-01/批次、logs/S5-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S5-01；不接着实现后续卡。
读取docs/evidence/stage-05/S5-01/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-05/S5-01/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S5-01 制品与平台清单；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-08: docs/evidence/stage-04/S4-08/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-01/批次、logs/S5-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S5-01，问题来源为docs/evidence/stage-05/S5-01/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S5-01 制品与平台清单；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S4-08: docs/evidence/stage-04/S4-08/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-01/批次、logs/S5-01/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S5-01；依据本卡真实验收与docs/evidence/stage-05/S5-01/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-05/S5-01/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S5-02，规格文件docs/stages/stage-05-delivery-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S5-02：启动权限配置

前置：S5-01。下一张计划：S5-03 离线维护备份。

[原始卡与公共约束](stages/stage-05-delivery-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S5-02 启动权限配置；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-01: docs/evidence/stage-05/S5-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-02/批次、logs/S5-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S5-02，不自动开始S5-03。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- deployment/target/
- host/runtime/
- host/supervisor/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S5-01；实际init、串口/屏幕/触摸/网络节点、固定程序路径和本机维护权限。
- **视觉启动/恢复**：init可按依赖启动locker-vision一次，使用独立worker UID、固定模型/profile及vision.sock权限；不配置自动重启循环。缺相机/模型的REQUIRED服务只诊断/只读并阻止新业务，不假READY；首版仍只自动恢复UI，视觉故障人工恢复。
- **允许目录**：deployment/target/、host/runtime/配置解析、host/supervisor/启动适配、对应测试/说明。
- **接口/调用链**：init启动service一次→Core RECONCILING→设备核对；supervisor启动并管理固定UI；UI登录/GET快照；手机导入本地公开材料→柜前核对→HTTPS/WSS。
- **产物**：独立身份、目录/socket权限、串口独占、日志轮转、配置及启动停止。复核S1-06共享单实例/离线维护锁：固定受保护锁路径，在打开营业DB前取得OS互斥，关闭DB/退出后才释放，锁忙拒启，维护工具用同一锁。已有init不默认替换systemd；service禁自动重启，UI只交supervisor。证书/地址变化重新配对，SAN覆盖实际地址。
- **验收**：冷启动、进程失败/退出、socket被占结果明确；离线工具持锁时service拒启，service持锁时维护拒绝，不只检查PID；UID/peer隔离、旧证书拒绝、公开导出及时间错误处理有效，不关TLS验证。
- **完成证据**：实际开机日志/权限/进程树、正常登录页可交互、service未自动恢复、证书轮换测试。
- **禁止越界**：不以root运行全部组件省权限，不加手机重启/任意shell接口，不把进程已启动称READY。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-05/S5-02/summary.md和docs/evidence/stage-05/S5-02/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S5-02 启动权限配置；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-01: docs/evidence/stage-05/S5-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-02/批次、logs/S5-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S5-02；不接着实现后续卡。
读取docs/evidence/stage-05/S5-02/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-05/S5-02/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S5-02 启动权限配置；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-01: docs/evidence/stage-05/S5-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-02/批次、logs/S5-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S5-02，问题来源为docs/evidence/stage-05/S5-02/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S5-02 启动权限配置；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-01: docs/evidence/stage-05/S5-01/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-02/批次、logs/S5-02/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S5-02；依据本卡真实验收与docs/evidence/stage-05/S5-02/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-05/S5-02/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S5-03，规格文件docs/stages/stage-05-delivery-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S5-03：离线维护备份

前置：S5-02。下一张计划：S5-04 更新与程序回退。

[原始卡与公共约束](stages/stage-05-delivery-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S5-03 离线维护备份；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-02: docs/evidence/stage-05/S5-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-03/批次、logs/S5-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S5-03，不自动开始S5-04。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- host/cli/
- deployment/maintenance/
- tests/maintenance/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S5-02；原有resolve、maintenance.reconcile规则及已存在真实数据。
- **允许目录**：host/cli/维护工具、deployment/maintenance/、tests/maintenance/和维护手册。
- **接口/调用链**：管理员先处理未决操作/实物→受控停service且锁关闭→持有离线维护互斥→sqlite3_backup生成独立副本→校验/清单；恢复只到隔离目录做对照，不直接覆盖营业数据。
- **产物**：离线备份/检查工具、备份包含范围/访问权限/保留策略；现场尾杆访问先保留事实，有未决操作走resolve，无未决且账实一致走私有maintenance.reconcile。首版不设计远程密码重置。
- **验收**：营业服务存在时离线写/备份命令拒绝；WAL中已有提交也被正确保留；失败备份不替换旧有效副本；副本完整性与业务版本可核验；敏感数据和访问方式有明确说明。
- **完成证据**：停机/互斥、备份结果/哈希、WAL场景与隔离检查、损坏拒绝行为。
- **禁止越界**：不裸复制.db遗漏WAL，不删WAL“修复”，不把旧备份一键恢复后自动READY，不在运行中CLI直写业务库。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-05/S5-03/summary.md和docs/evidence/stage-05/S5-03/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S5-03 离线维护备份；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-02: docs/evidence/stage-05/S5-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-03/批次、logs/S5-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S5-03；不接着实现后续卡。
读取docs/evidence/stage-05/S5-03/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-05/S5-03/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S5-03 离线维护备份；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-02: docs/evidence/stage-05/S5-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-03/批次、logs/S5-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S5-03，问题来源为docs/evidence/stage-05/S5-03/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S5-03 离线维护备份；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-02: docs/evidence/stage-05/S5-02/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-03/批次、logs/S5-03/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S5-03；依据本卡真实验收与docs/evidence/stage-05/S5-03/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-05/S5-03/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S5-04，规格文件docs/stages/stage-05-delivery-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S5-04：更新与程序回退

前置：S5-03。下一张计划：S5-05 现场验收。

[原始卡与公共约束](stages/stage-05-delivery-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S5-04 更新与程序回退；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-03: docs/evidence/stage-05/S5-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-04/批次、logs/S5-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S5-04，不自动开始S5-05。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- deployment/releases/
- deployment/maintenance/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S5-03；新旧release-manifest、存储schema及固件Journal兼容声明。
- **允许目录**：deployment/releases/、deployment/maintenance/、相关CLI检查/正式测试；业务变更另开所属卡。
- **接口/调用链**：核对当前账实/停止接单→受控停机→验证包/备份→兼容判断→切程序与配置→人工启动service→新session/日志屏障/补取/核对→再开放。程序失败只回退兼容程序/配置。
- **产物**：升级前后步骤、失败停留点、版本兼容检查与人工恢复规则。首版不含OTA/双分区bootloader/自动Flash迁移；不兼容schema或Journal更新停止并另做迁移设计。
- **验收**：包损坏/错误板型/不兼容版本不安装；程序启动失败可保留全部业务数据回退；没有旧outbox重发；固件更新保留外部Journal/epoch/水位，不全片抹除掩盖格式问题。
- **完成证据**：错误包、兼容回退、启动失败与重新核对记录，DB/Journal/监督额度未倒退证明。
- **禁止越界**：不回滚旧DB/MCU日志/恢复额度，不自动恢复旧数据营业，不在用户取物中切版本。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-05/S5-04/summary.md和docs/evidence/stage-05/S5-04/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S5-04 更新与程序回退；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-03: docs/evidence/stage-05/S5-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-04/批次、logs/S5-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S5-04；不接着实现后续卡。
读取docs/evidence/stage-05/S5-04/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-05/S5-04/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S5-04 更新与程序回退；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-03: docs/evidence/stage-05/S5-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-04/批次、logs/S5-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S5-04，问题来源为docs/evidence/stage-05/S5-04/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S5-04 更新与程序回退；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-03: docs/evidence/stage-05/S5-03/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-04/批次、logs/S5-04/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S5-04；依据本卡真实验收与docs/evidence/stage-05/S5-04/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-05/S5-04/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S5-05，规格文件docs/stages/stage-05-delivery-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S5-05：现场验收

前置：S5-04。下一张计划：S5-06 文档与演示。

[原始卡与公共约束](stages/stage-05-delivery-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S5-05 现场验收；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-04: docs/evidence/stage-05/S5-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-05/批次、logs/S5-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S5-05，不自动开始S5-06。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- tests/acceptance/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S5-04；全套真实柜体/电源/手机/网络及正式验收矩阵。
- **允许目录**：tests/acceptance/、docs/evidence/stage-05/；缺陷退回对应模块任务卡修复并补回归。
- **接口/调用链**：从Android/触摸端发起真实操作，贯穿API/IPC/Core/UART/HAL/传感器；再注入网络/进程/电源/存储异常，逐层比对证据。
- **产物**：完整借还、冲突/越权/重复、离线重连、现场尾杆维护、主机掉电、MCU重启、UI卡死/限次、日志满/DB满、更新和证书变化的结果；至少24小时真实连续观察，记录实际负载和环境。
- **验收**：没有无授权/重复驱动；所有结果可对照实物与账本；UI登录页恢复不依赖用户登录；有缺口/UNKNOWN不能伪装正常。实测响应/最长脉冲/资源/误报，不预填性能数字。
- **完成证据**：逐项PASS/FAIL/NOT_RUN、脱敏日志/波形/录像、运行起止与中断；24小时未完成就不标通过。
- **禁止越界**：不删失败样本，不把测试固定等待或模拟录像当连续真机运行，不以工业门禁可靠性作宣传。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-05/S5-05/summary.md和docs/evidence/stage-05/S5-05/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S5-05 现场验收；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-04: docs/evidence/stage-05/S5-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-05/批次、logs/S5-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S5-05；不接着实现后续卡。
读取docs/evidence/stage-05/S5-05/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-05/S5-05/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S5-05 现场验收；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-04: docs/evidence/stage-05/S5-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-05/批次、logs/S5-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S5-05，问题来源为docs/evidence/stage-05/S5-05/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S5-05 现场验收；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-04: docs/evidence/stage-05/S5-04/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-05/批次、logs/S5-05/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S5-05；依据本卡真实验收与docs/evidence/stage-05/S5-05/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-05/S5-05/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S5-06，规格文件docs/stages/stage-05-delivery-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S5-06：文档与演示

前置：S5-05。下一张计划：S5-07 最终交付审计。

[原始卡与公共约束](stages/stage-05-delivery-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S5-06 文档与演示；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-05: docs/evidence/stage-05/S5-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-06/批次、logs/S5-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S5-06，不自动开始S5-07。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- README.md
- docs/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S5-05；当前源码/契约/实付材料/接线/真实已知限制。
- **允许目录**：README、docs/运行与维护.md、docs/evidence/stage-05/、各模块README、演示说明；接口变化统一回真源。
- **接口/调用链**：文档逐项对应发布版本与实际入口；演示从登录/申请/到柜/实物/确认/归还，到一个可恢复故障，不绕过接口。
- **产物**：快速开始、物理接线、API/IPC/串口导航、维护备份/更新、已知限制、真实成本与演示脚本；区分已有/待验证/范围外。简历描述只引用实测能力。
- **验收**：另一AI/人工按入口能定位一张卡、一个接口、一个错误场景；默认阅读路径没有过期规则；演示账号/凭据现场配置，无固定密码；数据不泄露个人卡UID/账户信息。
- **完成证据**：文档核对清单、干净阅读演练与脱敏演示记录。
- **禁止越界**：不把设计数量当实现能力，不用夸大指标替代测量，不重开材料比价大任务。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-05/S5-06/summary.md和docs/evidence/stage-05/S5-06/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S5-06 文档与演示；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-05: docs/evidence/stage-05/S5-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-06/批次、logs/S5-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S5-06；不接着实现后续卡。
读取docs/evidence/stage-05/S5-06/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-05/S5-06/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S5-06 文档与演示；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-05: docs/evidence/stage-05/S5-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-06/批次、logs/S5-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S5-06，问题来源为docs/evidence/stage-05/S5-06/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S5-06 文档与演示；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-05: docs/evidence/stage-05/S5-05/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-06/批次、logs/S5-06/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S5-06；依据本卡真实验收与docs/evidence/stage-05/S5-06/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-05/S5-06/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。
下一张计划卡为S5-07，规格文件docs/stages/stage-05-delivery-task-cards.md。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。
运行适用检查，报告结果与残留自查，不commit/push。
````


### S5-07：最终交付审计

前置：S5-06。下一张计划：无后续卡：S5-07完成最终交付。

[原始卡与公共约束](stages/stage-05-delivery-task-cards.md) / [总手册](AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。

#### 开发指令

````text
项目根目录：%REPO%。
本卡：S5-07 最终交付审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-06: docs/evidence/stage-05/S5-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-07/批次、logs/S5-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

本次只开发S5-07，不自动开始其他任务。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
- tests/acceptance/
- docs/evidence/stage-05/
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。

本卡验收依据全文如下，不得缩减：
- **前置/输入**：S5-06和全部阶段门禁、缺陷/限制清单。
- **允许目录**：正式审计/验收测试、docs/evidence/stage-05/、状态索引；功能缺陷归所属模块。
- **接口/调用链**：检查每个公开/内部入口有实现归属、测试和真实调用证据；核对阶段依赖无跳过、状态与报告一致。
- **产物**：最终交付summary、制品/证据索引、未完成项及可接受限制、维护责任、残留清单与真实状态。
- **验收**：关键功能/异常/物理截止/持久化/权限/备份回退均有证据；无关键NOT_RUN；正式测试/fixture保留，临时mock/调试路由/写死成功清理后再运行相应检查。
- **完成证据**：实际构建/测试、静态契约与阶段图检查、全部门禁、制品哈希、文档同步、残留自查。
- **禁止越界**：不把审计文档存在当通过，不擅自commit/push/发布、不将未完工作标完成。

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入docs/evidence/stage-05/S5-07/summary.md和docs/evidence/stage-05/S5-07/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
````

#### 独立审计指令

````text
项目根目录：%REPO%。
本卡：S5-07 最终交付审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-06: docs/evidence/stage-05/S5-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-07/批次、logs/S5-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你作为独立审计AI，只审S5-07；不接着实现后续卡。
读取docs/evidence/stage-05/S5-07/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写docs/evidence/stage-05/S5-07/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
````

#### 审计整改指令

````text
项目根目录：%REPO%。
本卡：S5-07 最终交付审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-06: docs/evidence/stage-05/S5-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-07/批次、logs/S5-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是原开发AI，继续整改S5-07，问题来源为docs/evidence/stage-05/S5-07/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
````

#### 收尾与下一卡交接指令

````text
项目根目录：%REPO%。
本卡：S5-07 最终交付审计；契约基线：0.2.1。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读docs/stages/stage-05-delivery-task-cards.md中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
- S5-06: docs/evidence/stage-05/S5-06/summary.md、handoff.md、audit.md
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/S5-07/批次、logs/S5-07/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。

你是本轮唯一集成负责人，收尾S5-07；依据本卡真实验收与docs/evidence/stage-05/S5-07/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report=docs/evidence/stage-05/S5-07/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
本卡是阶段5正式门禁卡。逐项核实本阶段全部卡报告和必要实跑。
只有全部通过才生成docs/evidence/stage-05/summary.md汇总并链接每卡证据，
由唯一负责人同步本卡DONE、阶段implementation_status=DONE、gate_result=PASS及固定evidence。
关键NOT_RUN/阻塞存在不能把阶段设PASS，也不能启动下一阶段。
已到最后S5-07，无下一张卡；最终报告说明交付、限制与维护方式。
recommended_next保留S5-07作为最后里程碑，不填机器索引不存在的新卡，不能自动开新项目。
运行适用检查，报告结果与残留自查，不commit/push。
````

