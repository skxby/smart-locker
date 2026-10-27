# 全架构、接口、API与材料可实施性验证

日期2026-10-22；当前契约0.2.1。结论为有条件可实施的桌面功能样机，不是已完成产品。45卡全部未开始，0个业务/硬件验收PASS。

## 独立审查与修正对应

- [硬件审计](../../architecture/reviews/feasibility-hardware-2026-10-22.md)：两线开关断线不可辨已纠正规格；相机MONOTONIC非曝光起点已明确SOE/EOF及延迟上界；预算余量改为1.61元。锁扣、UART电平、真实硬截止、实际相机仍待实测。
- [API审计](../../architecture/reviews/feasibility-api-2026-10-22.md)：原0.2.0审查发现的异步回传、Journal管理与健康TTL缺口由主代理统一修正。报告保留发现时快照，不将原报告当修正后的运行证明。
- 新增mcu_control_bind_sink与mcu_journal_control，共82原生端口；请求上下文按值保留session/request_no/type/time。Journal管理RECOVER/BARRIER/FINALIZE/RELEASE回control_result，已提交事实经event_committed传真实事件键；回执不驱动GPIO。
- 视觉STATUS轮询1000ms、回复期限1000ms、缓存TTL3000ms；源采样不刷新，worker IO不能堵在NPU。总API时序与REQUESTED转移补PRE/POST门槛。

## 实际执行

四项检查的真实JSON输出存同目录：check_contracts.json、check_stage_plan.json、check_vision_contracts.json、check_feasibility.json。可重新运行scripts下对应.py。新增可实施性检查只回归类型/时序设计和曝光负例，不是生产授权逻辑。C头仅pycparser语法解析；当前未发现可用C编译器，没有业务源码可启动API，未做编译链接/HTTP/固件/NPU运行测试。

## 当前能证明什么

24公开入口、82内部C端口、28业务监督RPC、3视觉消息、11UART请求有规格和阶段归属；生成一致性、样例/字段引用、异步完成声明、阶段图及旧曝光/未知曝光界限负例检查通过。产品实际能否达到时延、精度、掉电持久化和权限隔离，仍需对应卡真实运行。

## 采购与实物待决项

基础候选198.39，视觉候选50.56，合计248.95元；另加板49.90后298.85元。金额不是本轮重新核价或成交价。材料已选链接但仍有待SKU/规格核对项，未擅自修改采购真源；不含锁扣、复合电容行、充电头总负载、RK板型/IO、相机controls/时钟和曝光缓冲界限均有实际影响。不能仅凭淘宝“免驱/PWM/3.3V”文字解除门禁。

本轮只改设计/契约/回归检查和文档，不实现产品；保留原HAL、模型、BOM及正式证据，不删除用户临时锁文件或第三方编译产物，未commit/push。
