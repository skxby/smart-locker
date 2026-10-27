# 整体分阶段架构与接口静态审查

日期：2026-10-20。范围：契约0.1.2与阶段1—5的37张开发卡。本报告是独立的静态设计复核，未构建业务程序、运行客户端或验证真实硬件。

## 1. 结论

本轮复核未发现阻止按单卡顺序启动开发的剩余设计阻塞。当前规格可以作为后续AI实施的统一交接入口；下一张可执行卡为S1-01。

这个结论只覆盖阶段依赖、职责、接口数据来源与调用链在设计上的可接通性。37张卡仍全部NOT_STARTED，active_task=null，各阶段运行门禁仍NOT_RUN；没有任何一张实施卡因本报告而获得DONE或实物验收PASS。

## 2. 审查输入与方法

实际阅读并交叉核对：

- [阶段导航与单卡规则](../../stages/README.md)、[交接模板](../../stages/task-handoff-template.md)、阶段0主规格及阶段1—5全部任务卡。
- [API地址和调用链](../api-links.md)、[分阶段调用链](../stage-call-chains.md)、[内部接口](../internal-interfaces.md)、[UART规格](../uart-v1.md)、[模块所有权](../module-ownership.md)。
- [客户端接口](../client-interfaces.md)、[监护接口](../monitoring-interfaces.md)、[部署维护接口](../deployment-interfaces.md)及[接口追踪表](../traceability.md)。
- 根契约、内部数据真源、生成C头与OpenAPI；阶段和接口机器索引、两项静态检查器及当前设计验证汇总。

从使用者请求向下追踪到Core、仓储、设备端口、MCU Control与Journal，再反向追踪物理事实、持久水位和状态投影。对后序卡需要的前置能力，核对其首次实现卡、允许目录和验收条件；不以接口声明存在推断实现已经存在。

## 3. 已核实的设计闭合点

| 主题 | 当前统一设计及复核结果 |
|---|---|
| 分阶段开发 | 阶段1主机模拟7卡、阶段2实物8卡、阶段3客户端7卡、阶段4监护8卡、阶段5交付7卡；单卡有输入、目录、接口、错误、证据与禁止越界项。根构建、DDL和契约不由多AI各自维护。 |
| 机器门禁 | 任务依赖无环，前置卡须DONE；跨阶段开工须前阶段门禁PASS。门禁卡DONE必须对应PASS；卡报告与阶段证据路径固定，不能以任意现存文件冒充证据。静态检查不修改实施状态。 |
| API/内部端口归属 | 22个公开入口、70个C端口、11类UART请求、26个私有RPC均有实现/验证卡；公开API、同名本机方法和MCU消息取同一真源。 |
| 单写者与动作边界 | Core独占账本写入，Control独占锁启动；客户端/网络线程/监督进程没有SQL或GPIO旁路。申请、接纳、脉冲、开门和借还完成分别解释。 |
| 健康基线与阶段依赖 | 阶段1已有真实Core循环副本和非阻塞get_progress，阶段2可据此发送HOST_HEALTH。UI/supervisor未知保留哨兵且不反向阻断阶段2；Core停滞不能由搬运线程假进度掩盖。 |
| 实例与年龄来源 | Runtime同一启动UUID供Core/SERVICE/HELLO/HOST_HEALTH使用。真实循环同时产生计数和源采样时间；搬运不刷新源时间，同counter不续命；源年龄与now减源采样时刻只累计一次。 |
| 监督观察接口 | health.get及C只读端口明确返回观察副本；Runtime经Core入队更新公开投影。Core健康始终直接取本机get_progress，监督缺席不抹去Core事实；未知恢复次数为null。 |
| 监督生命周期 | S4-04先交付可独立启动/停止/查询的观察模式，recovery_enabled明确禁用恢复；S4-05再启用固定UI子进程恢复，避免先验收尚不存在的监督程序。 |
| UI恢复与业务同步 | 新UI清token回登录页；两次新鲜且严格递增的真实循环可证明页面恢复。OBSERVED_RUNNING不要求无人值守时先登录；借还按钮仍须本人登录及新鲜权威快照。 |
| 手机接通条件 | 首用配对导出、真实RK串口与LAN证书准备放S2-06，早于S3。Android导入公开证书并柜前核对，HTTPS/WSS共用信任与固定origin；Bearer不入URL，证书/SAN错误拒绝连接。 |
| 幂等与凭据 | 响应丢失保留原键/原意图，已知操作先查询，不自动重发EXECUTE或presence。login/createUser密码不落持久意图，用户管理的含密码创建有独立核对路径。 |
| 物理与持久化前置 | 真实Journal、动作水位/提交回执和硬脉冲截止在阶段2先验收，再进入完整借还及后续监护。ACK不是物理动作证据；REPLAY不产生到柜证明或恢复动作。 |
| 维护与回退 | Runtime在营业DB打开前持有同一单实例/离线互斥，工具消费同一锁；备份使用SQLite机制并校验独立副本。程序回退不回退DB、MCU Journal/动作水位或监督额度；旧备份仅隔离检查。 |
| 制品检查归属 | verify-release首次实现已放S5-01，并允许对应host/cli和正式维护测试；S5-04复用该能力。S5-02明确复核共享互斥的生命周期与双向竞争拒绝。 |

## 4. 本轮实际检查

使用工作区已配置Python执行以下检查，实际结果均为PASS：

```text
python scripts/check_contracts.py
python scripts/check_stage_plan.py
```

契约检查结果：0.1.2，公开入口22、内部C端口70、私有RPC26、UART请求11；C声明仅语法解析，hardware_profile=UNVERIFIED，business_or_hardware_tested=false。

阶段检查结果：5阶段、37卡，公开/C/UART/RPC归属全覆盖，active_task=null，implementation_cards_done=0，runtime_or_hardware_tested=false。

另以仅内存修改的计划负例独立复核：前阶段gate_result=FAIL时尝试令S2-01进入IN_PROGRESS，检查器明确以`previous stage gate not PASS: S2-01`拒绝。未写入伪造报告或改变实际状态索引。主代理保存的其他阶段负例证据见阶段0验证汇总；本报告不把它们说成业务运行测试。

## 5. 实施与实物仍需证明的内容

- 尚无C服务、HAL应用固件、LVGL或Flutter功能实现；未验证实际C编译/链接、进程退出、TLS/WS连接、Android安全存储或真实权限隔离。
- 硬件profile、具体引脚/电平/ADC/Flash几何、RK ABI与init、锁脉冲最长通电时间、真实Journal掉电恢复、IWDG和24小时现场运行均须由对应卡留证；模拟器、schema检查和本报告均不能替代。
- 实际库/SDK版本、许可清单和目标系统适配在实施卡锁定。本轮审查未下载或修改用户HAL原包，也未对候选采购报价重新结算。
- `docs/项目计划.md`被Word占用，当前入口已明确其产品讨论背景属性。正式执行使用阶段导航和机器索引，不能沿用背景文档中的旧开发顺序。

## 6. 交接纪律

执行AI从S1-01开始，每次只接一张卡，按真实前置、允许目录和验收产出报告。环境或设备缺失写BLOCKED/NOT_RUN；发现共享接口矛盾回契约负责人统一修正，不创建第二套字段或降低验收。

本次审计仅新增本报告，未改产品实现、契约或实施状态；未创建临时业务mock、凭据或测试数据库，未commit/push。专家报告只保留审查证据，当前规则仍以主规格、机器真源及阶段入口为准。
