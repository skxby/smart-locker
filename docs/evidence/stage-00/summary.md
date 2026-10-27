# 整体分阶段设计交付与验证

日期：2026-10-20。契约0.1.2，设计评审稿；当前仍属于架构与接口交接，阶段1—5实施均未开始。

## 交付入口

- [阶段导航](../../stages/README.md)：5个开发阶段、37张卡、单卡执行与证据门禁。
- [总体架构](../../stages/stage-00-architecture.md)、[API地址/调用链](../../architecture/api-links.md)、[分阶段接线](../../architecture/stage-call-chains.md)。
- [接口追踪表](../../architecture/traceability.md)：22个公开入口、70个内部C端口、11类UART请求、26个私有RPC全部有实施/验收卡。
- [客户端接口](../../architecture/client-interfaces.md)、[监护接口](../../architecture/monitoring-interfaces.md)、[部署维护接口](../../architecture/deployment-interfaces.md)。
- [机器阶段索引](../../../config/development/stage-plan.json)和[单卡提示词/报告模板](../../stages/task-handoff-template.md)。37卡NOT_STARTED，active_task=null，下一个实现入口S1-01。

## 静态验证

实际执行入口：

```text
python -B scripts/build_contracts.py
python -B scripts/build_stage_docs.py
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
```

实际运行使用Codex附带Python。结果保存在本目录validation.json、stage-plan-validation.json与stage-plan-negative-validation.json。

契约检查包含生成物漂移、公开/内部引用、正反DTO/RPC样例、API-Core映射、RPC根方法集、危险公开路径、UART字段/CRC、C声明语法、硬件profile负例、材料文档同步与本地链接。阶段检查包含37卡唯一性/依赖无环/前阶段门禁、卡标题/索引、各接口归属全覆盖、DONE证据路径、生成追踪表和真实状态。

阶段检查负例覆盖：依赖循环、漏端口归属、无证据DONE、前置未过开工、前阶段FAIL仍开工。全部应拒绝，不能因计划JSON能解析就接受。

C声明由pycparser做语法解析，未作Keil/GCC编译/链接；schema检查器仅支持项目使用子集，不是完整OpenAPI验证器。静态PASS不证明业务服务、App、固件或真实硬件可用。

## 专家评审

架构、客户端接口与监护专家意见已整合。[独立静态审查报告](../../architecture/reviews/phased-design-audit.md)结论为未发现阻止按单卡顺序启动的剩余设计阻塞；复核范围含阶段依赖、健康通道、凭据、UI恢复及备份回退。报告只作设计证据，执行仍以主规格和机器真源为准，不声称业务/真机验收通过。

关键约定已经统一：

- Core/SERVICE/HELLO/HOST_HEALTH使用Runtime同一个启动实例；真实进度通过不可变副本搬运，源采样时间/源年龄不刷新或双算。
- Core健康始终取直接源；supervisor/UI未知只告警，不反向阻止前序阶段。监督观察经Core入队更新公开投影，未知恢复数为null。
- UI恢复只证明新实例登录页可交互，业务按钮另需登录后快照；不为恢复保存旧token或增加匿名snapshot。
- login/createUser密码不进入持久意图；丢响应先核对原用户意图，不换幂等键重建。
- 真实Journal/硬截止在阶段2先交付；备份使用受控停机/共享互斥/SQLite备份，程序回退不回退DB/Journal/动作水位/监督额度。

## 未实现与保留项

- 没有业务C服务、HAL应用固件、LVGL或Flutter实现；没有运行CMake/业务测试/真机验收，37卡没有一张标DONE。
- 硬件profile仍UNVERIFIED，实际引脚/电平/定时器/ADC/Flash与RK ABI待阶段2核对。
- 用户HAL V1.8.5目录与ZIP未修改，未下载或配置新库；原包Objects/Listings保留，不冒充本轮构建证据。
- 成本197.94元仍是既有候选报价/补件预留，板卡另算；本轮没有重新购物比价或结算。
- docs/项目计划.md被用户Word占用，未强制覆盖/关闭/移动。README与AGENTS已明确它是产品讨论背景，旧开发顺序不作为执行依据；默认执行入口统一到docs/stages/README.md。

## 收尾

README、AGENTS、主架构、契约入口、专题接口、阶段卡与机器索引已同步。正式生成物/证据/模板保留；本轮没有临时业务实现、debug路由或写死成功mock。已清理本代理生成的旧build_contracts.pyc，未删除用户Word锁文件、原库编译输出或正式研究证据。未commit或push。
