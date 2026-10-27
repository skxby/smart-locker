# 阶段 2B：v11 视觉核验接入

状态：契约0.2.1设计待执行，S2-09—16共8张卡。原S2-01—08为阶段2A底座，原阶段3—5卡ID不变；整体共45张实施卡。当前没有执行模型转换、购买/接入相机或实现视觉进程。

入口为S2-08通过；出口S2-16通过后才进入S3-01。阶段2A明确使用DISABLED_FOR_FOUNDATION，不将无视觉基线说成最终产品；本阶段先SHADOW验证，REQUIRED完整验收后才过门禁。[视觉架构](../architecture/vision-architecture.md)定义业务语义，[阶段导航](README.md)规定一次只执行一张卡。

```text
S2-08 → S2-09 资产/导出等价 → S2-10 预处理/量化/独立测试
      → S2-11 相机/机位/预算 → S2-12 RKNN worker
      → S2-13 私有适配 → S2-14 Core融合/只读API
      → S2-15 真机闭环/客户端接线 → S2-16 审计 → S3-01
```

每卡报告固定为docs/evidence/stage-02/卡ID/summary.md；本阶段门禁证据另行汇总，不能把S2-08报告当S2-16已过。所有卡引用同一契约/模型包/预处理版本。相机未购买、型号与预算未知，不能借用模拟帧标实物采集通过。

## S2-09：模型资产、来源与 ONNX 等价

- **前置/输入**：S2-08；E:/equipment-vision的README、v11权重/classes/metrics、现有ONNX及导出脚本。只读原项目，不能覆盖原模型或猜测v11为YOLO。
- **允许目录**：deployment/vision/、tools/vision/的版本化导入/检查工具、tests/vision/model/、本卡证据；模型副本进入明确的本项目制品目录，不修改原项目。
- **接口/调用链**：检查checkpoint/类别与ONNX来源→生成模型资产清单→参考PyTorch与ONNX对同组实图运行→保存逐样本logits/top1及等价结果。
- **产物**：checkpoint/ONNX/类别表哈希、来源、MobileNetV3-Large/7类/224、输入输出/opset和导出recipe。默认ONNX七类不能标旧六类；权重同SHA也不能代替ONNX等价证据。
- **错误/验收**：类别顺序/输出维度/哈希不符、输出含NaN/Inf或等价失败拒绝制品；不存在RKNN不声称可上板。重新导出写新目录，禁weights_only=False自动回退或从网络下载另一骨干代替自训权重。
- **完成证据/禁区**：来源/许可、文件指纹、逐样本差异/命令/环境；未运行记NOT_RUN。不重新训练、清洗数据或改公开API。
- **给AI的单卡提示**：只做S2-09，先核对七类v11资产与参考等价，保留原项目；完成制品清单和证据即交接，不继续量化。

## S2-10：统一预处理、量化与独立测试

- **前置/输入**：S2-09；训练验证参考Resize短边255→CenterCrop224、RGB/ImageNet归一化；目标RK3568 SDK约束与独立标定数据规划。
- **允许目录**：tools/vision/转换/评估、deployment/vision/recipe、tests/vision/preprocess/及量化fixture、本卡证据。
- **接口/调用链**：参考像素管线→固定预处理版本/哈希→ONNX固定板端形状→RKNN转换→FP/INT8逐样本比较；几何处理先在CPU固定，归一化只做一次。
- **产物**：明确RGB、裁剪/缩放/插值/取整、输入dtype/layout、mean/std归属、输出logits/softmax规则；RKNN/runtime/driver版本与兼容记录、校准/独立验证集指纹及阈值策略草案。
- **错误/验收**：禁止把短边224或拉伸224×224称参考一致；禁止RKNN嵌入mean/std后又手动归一化；检出类别乱序/输出维数错/NaN。accuracy_analysis输入列表与标签列表分开，VM路径可解析；训练集另抽35张不作为独立柜内测试。
- **完成证据/禁区**：参考/ONNX/RKNN的样本、各类指标和量化差异；柜内实拍未具备时阈值保持未标定、REQUIRED不可启。无SDK/板端时如实BLOCKED，不造.rknn假文件、不保证量化固定掉点。
- **给AI的单卡提示**：只做S2-10，先证明同像素/同归一化，再量化评估；不接相机、不接账本。

## S2-11：相机、固定机位与预算门槛

- **前置/输入**：S2-10；真实相机/接口、RK设备节点/驱动、供电/线缆/机位/照明与新增预算。接口层面已定USB UVC具体型号（HBVCAM-F2170HD 75°手动调焦，商品/SKU与判据见[相机选型与成本](../hardware/相机选型与成本.md)），但相机未购买、未核价、未接板，条件仍未具备。
- **允许目录**：host/vision-worker/camera/、config/vision/中新建实际相机profile（保留unverified模板）、deployment/vision/、采集测试及本卡证据；材料变更交材料真源负责人统一处理。
- **接口/调用链**：受限相机FD→V4L2配置/缓冲→固定slot=1的ROI/颜色→带帧序号、采集时刻和camera generation的受控帧；帧不进入Core。
- **产物**：真实相机profile、时钟/分辨率/像素格式/ROI/曝光/固定方式、代际变更规则；按S2-10数据规则采集有真实标签的柜内标定集与独立现场测试集。相机、线缆、必要补光/固定/运费列全增量成本（当前候选见`config/materials/selected-bom.json`的`vision_increment`），不从旧200元预算默认0元覆盖；到货须实测手动曝光/增益可写、每帧`V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC`与序号递增，否则该候选不合格。
- **错误/验收**：曝光时间戳/clock标志与MONOTONIC映射可证明；请求前缓冲不能当新帧；相机断开、重新打开、ROI变化使旧capture失效；3新帧间隔≥100ms、3000ms总截止作为设计边界实测，不用重复同一帧冒充。
- **完成证据/禁区**：型号/驱动/设备权限/预算决策、实际帧时间线、机位照片及光照样本。无相机标BLOCKED，不要求购入未确认器件，不把文件图片冒充V4L2采集。
- **给AI的单卡提示**：只做S2-11，先核对相机与成本，证明新帧/时钟/代际；完成后不启动借还业务。

## S2-12：独立 RKNN C 视觉进程

- **前置/输入**：S2-11；已验证模型bundle、相机profile、SDK/runtime/driver兼容与像素recipe。
- **允许目录**：host/vision-worker/、模块局部构建、deployment/vision/、config/vision/中仅标定策略、tests/vision/worker/和本卡证据；根构建由集成负责人接入。
- **接口/调用链**：locker-vision加载/查询tensor属性→采集新帧→统一预处理→RKNN C推理/释放输出→返回有界类别/分数/质量/时间元数据；不连接MCU。
- **产物**：相机/NPU生命周期、固定包、质量/稳定性和截止处理；退出释放帧/FD/context。复用S2-10工具，在S2-11柜内标定集对当前expected_class与empty共同确定一组threshold_ppm/margin_ppm及质量参数，再用独立现场集验证并冻结policy哈希；不宣称存在按类独立阈值表。未验证不启REQUIRED，worker不决定RETURN_OK。
- **错误/验收**：错误模型/维度/类别/设备不运行；无新帧、曝光异常、遮挡、超时返回非成功；不能softmax低值判empty；3帧像素可相同但序号/曝光必须真实不同。单请求有界、队列满显式拒绝，重启新worker generation使旧请求失效。
- **完成证据/禁区**：真实RK构建/ABI、SDK、模型/tensor信息、每帧结果、时延分布/资源与错误测试。原Python公网骨架不作生产实现，不训练模型、不自动重启service。
- **给AI的单卡提示**：只做S2-12，复用官方C API生命周期，按本项目预处理和七类标签适配；不要扩HTTP服务或锁控制。

## S2-13：vision.sock 与主机异步适配

- **前置/输入**：S2-12；冻结私有视觉协议/DTO/82端口头与部署service UID，生命周期规则。
- **允许目录**：host/vision/、host/vision-worker/中的私有协议服务、host/runtime/装配、tests/vision/transport/和本卡证据。
- **接口/调用链**：vision create/bind_sink/open→submit→vision.sock→worker→正式测试sink验证异步结果；get_status取不可变缓存。真实Core接收函数及Runtime接线归S2-14，本卡不能用空实现或未定义函数强行链接。
- **产物**：独立版本/包络、SO_PEERCRED+权限、capture关联、请求/响应边界、背压、超时与close/destroy；格式由机器契约唯一生成，不能把vision方法塞普通service.sock。
- **错误/验收**：错误peer/未知字段/超限包/旧capture/重复不同正文/旧实例拒绝；断连与worker重启不重放旧capture，取消等待不伪称已撤回worker工作。close后无回调，停止超时不释放仍借用对象。
- **完成证据/禁区**：真实socket边界、关联/乱序/断线/背压及退出测试；无0.0.0.0:8080、任意图片路径、base64、客户端上传事实或直接UART。
- **给AI的单卡提示**：只做S2-13，只接冻结私有协议与异步端口；Core融合规则留下一卡，不在适配器写SQL。

## S2-14：Core 持久化融合与只读 API

- **前置/输入**：S2-13；expected_class配置、三种模式/阈值策略、PRE/POST状态矩阵、仓储及新只读API机器契约。
- **允许目录**：host/domain/、host/storage/由唯一仓储负责人修改、host/http/、host/ipc/、host/runtime/中的真实视觉绑定、相关正式测试及本卡证据；共享schema变更先交契约负责人。
- **接口/调用链**：Core bind_vision→提交capture→apply_vision_result/status入队→验证operation/phase/guard/实例/hash→证据与融合/操作变化同事务→getVisionStatus/getOperationVisionEvidence一致读取。
- **产物**：PRE borrow=MATCH/return=EMPTY，POST borrow=EMPTY/return=MATCH；REQUIRED不静默退化。PRE无通过不建command/outbox，保持REQUESTED可取消；POST失败进入REVIEW_REQUIRED保占位。人工resolve保留矛盾与理由，仍受原物理规则限制。
- **错误/验收**：旧证据/MCU或service重启/相机或worker代际变更/物理版本变化拒绝；DB失败不发布成功；重复capture幂等且异内容冲突。本人/管理员之外读operation视觉证据拒绝；公开只有GET，无图片/路径/阈值写入/开锁/上传。
- **完成证据/禁区**：状态矩阵、事务失败/重启/权限/幂等/guard负例、原API回归、元数据大小。Core不阻塞NPU、不以首预测建账、不覆盖MCU item_state、不把same-class当资产防伪。
- **给AI的单卡提示**：只做S2-14，先列每种模式和PRE/POST转移，再实现Core唯一决策与持久证据；客户端不得替代融合。

## S2-15：真实借还与客户端接线规格

- **前置/输入**：S2-14；一格实物/相机、可验证的REQUIRED策略与工具配置，正式HTTPS/本机测试客户端。
- **允许目录**：tests/hardware/vision/、tests/integration/vision/、docs/architecture/client-interfaces.md的视觉展示说明、阶段3卡的只读接口接线说明及本卡证据。此卡不提前实现完整Flutter/LVGL。
- **接口/调用链**：借出申请/到柜→PRESENT+MATCH→一次开锁→取走关门→ABSENT+EMPTY→本人confirm；归还先ABSENT+EMPTY→开锁→放入关门→PRESENT+MATCH→确认。手机/柜屏后续只GET状态/证据，WS提示后重查。
- **产物**：万用表正常借还、剪线钳错放、相机故障/遮挡/旧帧、传感器冲突和人工resolve的真实矩阵；给S3的页面状态、按钮门禁、错误与只读API映射。上游性能数字不当本阶段实测。
- **错误/验收**：归还申请时空柜不判已还；把别类物体压住微动开关不能完成；REQUIRED故障不降级；Post故障不释放占位。确认前物理/证据冲突阻止自动完成，响应丢失原幂等键核对、不再次开锁。
- **完成证据/禁区**：操作/command/capture/帧/版本关联、脱敏波形/日志/实物记录、指标与失败样本。完整App未实现不造截图，不把SHADOW记录称拦截生效。
- **给AI的单卡提示**：只做S2-15，以正式测试客户端跑真机闭环并交给S3具体读接口/显示规则；不要把阶段3整个App开发夹进来。

## S2-16：视觉阶段审计与门禁

- **前置/输入**：S2-15及S2-09—15全部真实报告；契约/模型/相机/策略/代码指纹和预算记录。
- **允许目录**：正式审计测试、docs/evidence/stage-02/、当前阶段/模块README和状态索引；缺陷退回责任卡修复。
- **接口/调用链**：核对模型来源→采集→预处理→NPU→私有协议→Core事务→MCU动作/事实→本人确认→只读API，确认没有第二账本或视觉开锁旁路。
- **产物**：2B阶段汇总、REQUIRED可用资格、逐场景PASS/FAIL/NOT_RUN、已知限制与阶段3交接。S2-16通过是阶段2完整门禁，S2-08仅为底座门禁。
- **错误/验收**：关键实物/量化/权限/持久化/错还/故障项均有真实证据；相机/预算/阈值/SDK仍未知则不通过。静态checker、同类识别或14张empty样本不代替现场结果。
- **完成证据/禁区**：统一检查/构建/正式测试、残留自查、文档同步、制品哈希和未完成项；全卡完成后才解锁S3-01。不伪造DONE、不删失败证据、不擅自commit/push。
- **给AI的单卡提示**：只审S2-16的已交付事实，按清单逐项核对，不靠报告文件存在即通过；有缺口退回对应卡，不能顺手放宽门禁。
