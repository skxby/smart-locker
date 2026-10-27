# 后续 AI 如何逐步接入 v11

这是设计交接说明，没有开始实现任何卡。默认从S1-01按[阶段计划](../stages/README.md)推进；模型已经训练好，不表示RK服务、STM32固件或相机链路已存在。

## 1. 先分清三个目录

| 位置 | 用途 | 操作规则 |
|---|---|---|
| E:/equipment-vision | 原训练工程、v11权重、数据与已有ONNX | 本轮只读；不要执行里面的公网HTTP示例，不覆盖原权重 |
| %REPO%/contracts、config/vision | 0.2.1字段/函数/worker协议与资产核对 | 真源由契约负责人维护；生成文件不手改 |
| 后续本项目host/vision-worker/、host/vision/、tools/vision/ | RK worker、业务适配、离线转换工具 | 对应卡获准后创建真实实现；当前没有这些运行程序 |

## 2. 一张卡交给一个执行 AI

先完成S1-01..07主机模拟闭环，再S2-01..08 HAL与一格实物，然后依次执行[视觉八张卡](../stages/stage-02b-vision-task-cards.md)。给AI以下提示，只替换卡ID：

```text
只执行S2-09（后续每次替换为当前一张卡），不要继续下一卡。
根目录%REPO%；先读AGENTS、README、docs/stages/README及stage-plan。
读取当前卡、vision-architecture.md、vision-interfaces.md和契约0.2.1。
从config/vision/v11-source-audit.json核对v11输入，E:/equipment-vision保持原样。
前置未通过则如实报告，不用模拟/假文件解除硬件门禁。
先报本卡接口/路径/验收；完成后写卡报告、检查/同步文档和状态并交接。
```

| 卡 | AI需要接通什么 | 本卡完成后交给下一卡什么 |
|---|---|---|
| S2-09 | v11 PT→可追溯ONNX，已有ONNX先验证 | 七类顺序、哈希、数值等价证据 |
| S2-10 | 参考像素→固定几何/单一归一化→RKNN评估工具 | pipeline/SDK与可重复转换/对比方法 |
| S2-11 | 真实相机→可证明新鲜的3帧→固定ROI | 预算/驱动/profile/柜内标定与独立集；接口已定UVC候选，仍未购相机 |
| S2-12 | 三帧→RKNN推理→七分数/质量元数据 | 独立C worker、模型生命周期、经现场验证的策略 |
| S2-13 | Core侧适配器→私有vision.sock→异步结果sink | 有界请求/超时/重启/回调测试，无账本写入 |
| S2-14 | 结果→Core关联/融合/事务→两条只读GET | 持久证据、原状态机视觉门槛与权限测试 |
| S2-15 | 正式测试客户端→真实借还/错放/旧照片/断相机 | 必要实物证据和S3消费接口说明 |
| S2-16 | 审查所有视觉卡/原物理门槛 | 阶段2完整PASS才进入S3手机/LVGL |

相机接口已定为USB UVC定焦候选（见[相机选型与成本](../hardware/相机选型与成本.md)），但未购买/未测量/未接板，S2-11等实物门槛不能假通过；先做允许的离线任务不等于可开启REQUIRED。

## 3. 具体函数与 API 如何相连

```text
用户POST /operations（沿用原幂等）
→ Core固定VisionPolicy并持久化REQUESTED
→ 实时卡/可信presence
→ Core持久化VisionCaptureRequest(PRE_ACCESS)
→ locker_vision_submit → vision.sock CAPTURE → locker-vision
→ sink.result → locker_core_apply_vision_result（只排队）
→ Core核对guard/实例/代际/时间/分数 → 事务存证据与动作意图
→ 原DevicePort/UART/HAL开锁 → 原传感器完整事实
→ POST_CLOSE同链采集 → 正确才WAIT_CONFIRM → 本人confirm
手机GET /operations/<id>/vision只查证据，不触发拍照或开锁
```

错误入口也要接：无相机/超时、错类、低质量、旧capture/旧worker、同ID异正文、物理版本变化、DB失败、Core队列满。不能只让“万用表识别成功”的演示路径通过。

## 4. 当前可运行的设计检查

```text
python -B scripts/build_contracts.py
python -B scripts/build_stage_docs.py
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
```

它们生成/检查设计，不能拍照、转换RKNN、启动服务或下载APK。将来各卡必须补真实构建和运行证据，不能复制本轮静态PASS当自己的DONE。

相机费用已列为独立增量（已选定HBVCAM-F2170HD 75°手动调焦，页面价合计50.56元、上限55元，见`selected-bom.json`的`vision_increment`），到货型号/驱动/曝光回读/补光和固定仍须在S2-11实测核对；不默认从原200元余量覆盖，不为视觉删除原保护件。分类提升的是错放类别的核验能力，不能宣称资产防伪或同类实物身份认证。
