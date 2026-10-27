# 契约入口

状态：v0.2.1设计评审稿。24个公开入口、82个内部C声明、11类UART请求、28个业务/监督RPC。当前没有柜锁、借还服务或App实现；STM32固定用户HAL路线。

## 真源与生成物

- [contract-source.json](contract-source.json)：公开 API、业务枚举、错误、消息编号、常量、内部端口。
- [uart-layouts.json](uart-layouts.json)：串口头部、负载、固定事件记录与位标志的字节真源；枚举数值引用 contract-source。
- [internal-data-source.json](internal-data-source.json)：仓储事务/分页/恢复与command_bindings真源；[内部schema](internal-data.schema.json)为派生产物。
- [硬件profile schema](hardware-profile.schema.json)与[未验证候选](../config/hardware/skystar-f407vgt6-pro.unverified.json)：仅供BSP部署，不注册到公开API，不因材料NO/NC翻转业务枚举。
- [openapi.json](openapi.json)：生成的 OpenAPI 3.1.1。
- [locker_contracts.h](include/locker_contracts.h)：生成的 C 类型/原生解码 DTO/端口声明。结构体可能有 padding，不能裸 memcpy 上线。
- [API 样例](examples/api-examples.json)、[串口黄金帧](examples/uart-golden.json)、[验收场景](examples/acceptance-scenarios.json)。
- [API链接与调用链](../docs/architecture/api-links.md)：地址、私有路径与内部函数对应，供开发AI先读。
- [分阶段入口](../docs/stages/README.md)及[接口归属](../docs/architecture/traceability.md)：45张卡、各接口的初次实现/后续扩展/验收位置。

修改真源后运行：

```text
python scripts/build_contracts.py
python scripts/check_contracts.py
python scripts/build_stage_docs.py
python scripts/check_stage_plan.py
python scripts/check_vision_contracts.py
```

命令在项目根目录执行。检查器采用标准库验证所使用的 schema 子集、引用、生成物漂移、正反样例、帧边界/CRC、消息/字段长度及本地文档链接；它不是完整的 OpenAPI 标准验证器，也不证明 HTTP 或固件可运行。

## C 端口调用约定

公共类型中的 host-only JSON DTO 用于解耦 HTTP 库与业务核心，不在 MCU 队列中实例化。`arguments_json` 只承载 OpenAPI 已验证请求正文，不含客户端自报的 principal/source；身份由可信适配器附加。

Core请求路径：submit按值复制 → QUEUED receipt → HTTP/IPC凭Core生成的submission_token调用wait_result，最多1000ms → Core事务提交后回填结果。X-Request-ID只排障，不是等待键。超时不取消，返回RESULT_PENDING并复用原幂等键。Core自己不得等待；查询也通过一致结果副本，不由网络线程读可变领域对象。

设备返回有两类入口：`locker_core_apply_device_reply` 是已解码请求响应；`locker_core_apply_device_event` 是物理事件。帧到达、排队、日志提交和动作执行不是同一结果。

监督观察通过受限supervisor.sock的health.get读取；Runtime通过locker_core_apply_supervisor_health入Core队列，不能直接修改公开Device投影。locker_core_get_progress只读真实回路已发布副本，不阻塞Core队列或代增进度。ProgressReport保留源sampled_mono_ms；原生ProgressTarget、McuTaskId按根契约固定顺序。Device.ui_restart_count为未知时是null。

只有模块context保持opaque；工厂、停止和销毁已定义。仓储聚合/恢复DTO为带长度的有界JSON容器，字段受internal-data.schema.json约束，仓储/Core/Runtime不各自发明数据模型。S1-01实现既定生命周期后，仓储和模拟器可以并行。本地static helper不属于跨AI合同。

C原生DTO的本地布局映射由生成器维护，因此生成器也是设计真源。当前检查不等于对全部C字段和OpenAPI语义作形式化证明；wire原生DTO由uart-layouts生成，所有结构体禁止裸发。

数值型 C enum 在解码后必须按明确映射转换，尤其 API DeviceMode 包含 OFFLINE，而线协议模式不包含 OFFLINE，两者禁止直接强转。所有保留字节为 0；u64 在 JSON 为十进制字符串，不能经 double。

## 尚未固定的硬件参数

引脚、UART 电平、Flash 具体几何、锁实际允许脉冲、ADC 标定、RK 用户态 ABI 不在机器真源中伪填默认值。它们只影响 BSP/部署配置，真机阶段核对前不得驱动实际柜锁。

## 视觉证据合同

[vision-source.json](vision-source.json)定义模型类顺序、策略、worker请求/结果与公开证据；[vision.schema.json](vision.schema.json)及[视觉样例](examples/vision-examples.json)由同一生成器派生。vision.sock仅本机service使用，3消息独立于28个业务/监督RPC及11类UART。新增两条GET首实现卡S2-14，阶段1只交付原22入口，不造空实现。

[视觉接口说明](../docs/architecture/vision-interfaces.md)固定82端口中的10个视觉端口、原始/公开DTO、事务/guard与具体URL；[AI接线](../docs/architecture/vision-ai-handoff.md)按S2-09—16逐卡执行。相机/预算/RKNN/阈值未验证，不开启REQUIRED。
