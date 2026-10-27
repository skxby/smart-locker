# v11视觉集成设计交付记录

日期：2026-10-22。契约0.2.0，状态为设计评审稿。5个开发阶段、45张实施卡，全部NOT_STARTED，active_task=null；下一实现入口仍S1-01。

## 本轮范围

仔细读取E:/equipment-vision的README、部署指南、训练/验证/导出/推理/校准脚本、v11类别/指标；使用weights_only=True只读加载checkpoint元数据，使用ONNX解析器只读检查图形状/opset。未训练、导出/转换、下载库、安装SDK、运行模型推理或接入硬件，原训练工程和权重未改动。

## 模型事实

- v11为训练迭代号，架构MobileNetV3-Large、224输入、七分类；不是YOLOv11。
- 固定顺序为empty、heat_gun、multimeter、oscilloscope、power_supply、soldering_iron、wire_cutter；checkpoint末层7×1280。
- 默认equipment_cls/best.pt与v11权重SHA相同；已有ONNX是opset18、动态batch、输入batch×3×224×224/输出batch×7。没有RKNN，不虚报数值等价或板端可用。
- checkpoint/metrics最佳验证准确率0.9241706161，211张；README另记91.9%。这些来源值不是本轮重测，不作为柜内准确率。empty 14/14及30—60ms同样不作现场保证。
- 原示例存在三种几何预处理、归一化可能重复、empty误解为已还、归还拍摄时机错误及公网任意图片路径接口；本项目正式规格明确替代这些集成建议，原文仍作为只读研究输入。

资产指纹见[v11-source-audit.json](../../../config/vision/v11-source-audit.json)，包含7个实际源文件的大小/SHA。相机用户明确尚未购买，型号/驱动/补光/固定/运费和预算均未知，原200元预算不宣称覆盖视觉增量。

## 当前执行入口

- [整体阶段计划](../../stages/README.md)：保留原37卡ID，阶段2A原S2-01—08，新增[2B八卡](../../stages/stage-02b-vision-task-cards.md)S2-09—16；S3-01改依赖S2-16。
- [视觉架构](../../architecture/vision-architecture.md)、[字段/函数/地址](../../architecture/vision-interfaces.md)、[AI逐卡接线说明](../../architecture/vision-ai-handoff.md)。
- [API总调用链](../../architecture/api-links.md)、[分阶段链](../../architecture/stage-call-chains.md)、[接口实施归属](../../architecture/traceability.md)。
- 机器真源vision-source.json与根契约/内部仓储；派生OpenAPI、C声明、vision.schema与正式正反样例。

24个公开入口（新增两条视觉只读GET）、80个内部C端口、28个业务/监督RPC；vision.sock的HELLO/STATUS/CAPTURE为独立3消息，不塞进手机或MCU协议。11类UART v1保持不变。

## 冻结的设计规则

Core唯一持账/融合，Control唯一启动锁，视觉worker不接UART。前置借出PRESENT+MATCH/归还ABSENT+EMPTY，后置借出ABSENT+EMPTY/归还PRESENT+MATCH，仍需原连续物理事实和本人确认；NO_CHANGE回原基线核对。视觉与开关矛盾保留双方证据、转核验，不覆盖item_state。

摄像/推理异步，源实例/代际/guard/4种哈希/帧序号/时钟绑定，策略及capture先落库；证据与投影/操作同事务。2秒用于首次接纳帧的新鲜度；已提交证据可在同guard的120秒确认窗口内使用，变化/失联不复用。只有实拍标定/模型/相机/SDK均有证据才REQUIRED，不自动降级。

预处理固定短边255→中心224，部署raw RGB uint8/NHWC交SDK做一次mean/std；校准同几何。不能用直接224拉伸、两次归一化、训练集内35张或14张empty样本代替柜内独立验证。

## 验证范围

实际设计检查结果分别保存在本目录contracts-validation.json、stage-plan-validation.json和vision-validation.json。检查生成漂移、引用/样例/黄金帧、阶段依赖/分单元卡标题、全接口归属、视觉schema与关联负例、真实源文件哈希。check_vision_contracts只校验设计fixture，不能直接用于生产开锁授权，也没有执行分类/NPU。

[独立静态审查](../../architecture/reviews/vision-design-audit.md)结论为未发现阻止按单卡实施的剩余设计阻塞。它不替代后续C构建、HTTP/IPC运行、相机采集、RKNN量化或实物借还验收。

## 未完成与收尾

没有任何产品实现或实物验收；相机、RKNN、柜内独立测试/阈值、实际SDK兼容和延迟均未验证。未验证模板保留，未来另建实际已验证profile/bundle，不改模板冒充通过。预算未结算；没有修改BOM、原HAL库、训练项目或原模型。

当前README/AGENTS/主规格/专题接口/阶段卡/机器索引/派生产物已同步。旧产品讨论计划及前期0.1.2证据保留为背景，不作为当前执行顺序。正式fixture、生成器、模板、检查器与证据保留，不含临时业务mock/调试路由/固定成功API；未commit/push。

## 后续变更（同日，不属于本轮验证）

本记录完成后，按用户要求做了相机接口选型：[相机选型与成本](../../hardware/相机选型与成本.md)定为USB UVC免驱定焦候选CAM-UVC-01（未购买/未核价/未接板）。`selected-bom.json`只新增独立段`vision_increment`（已选定HBVCAM-F2170HD 75°手动调焦，页面价合计50.56元、上限55元、不计入200元；补光走用户已有充电头），原`items`与197.94元总额未改；`deployment.unverified.json`只增`camera.candidate`，`owned`仍为false、`camera_cost`仍为null。三项设计检查复跑仍PASS，本节不改变上面任何一条PASS或未验证结论，也不代表相机/采集/REQUIRED已可用。
