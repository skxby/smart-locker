# v11视觉接入的独立静态设计审查

日期：2026-10-22。审查范围：契约0.2.0、阶段2B视觉八卡及与既有借还柜架构的调用边界。本报告不代表模型推理、量化或真实柜体已验收。

## 1. 结论

未发现阻止按单卡顺序实施的剩余设计阻塞。五个开发阶段和原37张卡ID保持不变，阶段2增加S2-09—16后共45卡；S2-08只解锁视觉增量，S2-16才是阶段2完整门禁，S3-01依赖S2-16。

目前45卡均NOT_STARTED，active_task=null。相机尚未购买、预算未定，没有RKNN制品或柜内实测结果；本报告不改变这些状态。

## 2. 审查输入与方法

- 交叉阅读模型源README、部署示例、类别/指标记录，以及主代理保存的权重/ONNX只读核对记录；独立重验七个源文件哈希。没有修改E:/equipment-vision，也没有运行训练或模型转换。
- 阅读[视觉架构](../vision-architecture.md)、[接口与具体地址](../vision-interfaces.md)、[阶段2B任务卡](../../stages/stage-02b-vision-task-cards.md)、阶段2A及总阶段索引、部署和客户端接口。
- 核对根契约/vision-source、生成C声明/OpenAPI、内部仓储集合及RPC映射；从PRE_ACCESS请求追踪到私有worker、结果入Core、证据事务，再追踪POST_CLOSE和本人确认。
- 核对前序卡能否独立编译/验收、后序依赖是否闭合、相机和未验证模板是否如实保留。

## 3. 已核实的闭合点

| 主题 | 当前统一规则 |
|---|---|
| 模型身份 | v11是MobileNetV3-Large七分类，包含empty；默认best.pt与v11同SHA，已有ONNX为七输出，不能误认YOLOv11或旧六类。ONNX数值等价和RKNN仍待实施。 |
| 输入与输出 | 参考短边255再CenterCrop224，固定RGB/插值/取整；RKNN部署选原始uint8 NHWC及SDK单次归一化，不能叠加示例手动归一化。七类顺序与模型包固定，分数不是正确性概率。 |
| 所有权 | locker-vision独占相机/NPU，host/vision只适配，Core独占账本/策略/融合；预测不覆盖MCU item_state，视觉不连接UART或驱动锁。 |
| 时机 | 借出前MATCH、借出关门后EMPTY；归还前本就应EMPTY，放入关门后才检查MATCH。expected_class来自工具配置，不由首次预测建立账本。 |
| 失效处理 | REQUIRED不静默降为SHADOW。PRE失败不建command；POST失败/冲突转REVIEW_REQUIRED并保持占位。人工resolve保留视觉矛盾与理由，仍受原物理规则限制。 |
| 关联与新鲜度 | capture绑定操作/阶段/MCU guard/服务、worker、相机代际及包/管线/profile/策略哈希；Core前后复核实际物理事实，worker仅回显guard。三帧须真实新序号/时刻，允许静态场景像素相同。 |
| 确认窗口 | 2000ms约束首次接纳逐帧结果，不强迫用户两秒点击。已提交POST证据有accepted_mono_ms，在120秒WAIT_CONFIRM且guard/实例/代际/策略持续有效、确认物理快照新鲜时可引用；变化/健康未知转核验。 |
| 私有协议 | vision.sock与service/supervisor RPC分开，受peer/实例校验，无公网classify/path入口。未接纳坏包关闭连接，既有状态映射明确，不临时发未定义Error JSON或重拍。 |
| 生命周期 | adapter构造/绑定/打开/提交/缓存查询/关闭/销毁及Core入口完整；停止超时不销毁依赖，回调按借用/复制规则处理，Core不等待NPU。 |
| 单卡可执行性 | S2-13先用正式测试sink验证适配器，不依赖下一卡尚无的Core实现；S2-14再实现Core入口及Runtime真实绑定。worker目录统一host/vision-worker，适配器统一host/vision。 |
| 仓储和查询 | 三个视觉集合由Core事务写入；证据key为operation_id+'/'+evidence_id，复用受控exact/after_key查询定位本操作历史，遇其他前缀停止，公开cursor绑定操作/版本，HTTP不直接SQL。 |
| 客户端 | 新增仅两条受权限限制的GET，查询无采集副作用；WS提示后重查，无图片、路径、阈值写入、视觉上传或远程开锁。 |
| 标定与模板 | 一柜一工具用expected_class与empty共同验证的一组阈值/间隔，不宣称已有每类阈值表。S2-11/12新建真实profile/bundle；unverified模板保留，不为过检查改成VERIFIED。 |

## 4. 本轮实际静态检查

独立执行以下命令，结果均PASS：

```text
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py --verify-local-assets
```

实际范围：

- 根契约：24公开入口、80内部C端口、28业务/监督RPC、11UART请求；C声明仅语法解析，没有实际C编译/链接。
- 阶段图：5阶段、45卡，接口实施归属覆盖完整，独立视觉worker的HELLO/STATUS/CAPTURE三类消息单列，实施DONE为0。
- 视觉设计样例：8个schema正反例，5个关联/时效/重帧/分数语义负例，2个跨操作历史页负例；相同像素但不同真实新帧的fixture按规则可接纳。
- 七个模型源文件哈希重验通过；camera_owned=false、rknn_available=false、model_inference_tested=false、hardware_tested=false。

这些是契约、索引和正式fixture一致性检查，不能证明生产Core已实现这些判断，也不代替相机时间戳、NPU时延、模型精度或电锁测试。

## 5. 仍需实施卡证明的内容

真实相机/预算、V4L2采集时钟与缓冲新鲜度、ROI与颜色处理、ONNX数值等价、RKNN量化/SDK兼容、现场阈值、独立柜内测试集、错误器材放行率、失联与事务失败行为，均需对应卡留下真实证据。

分类器只识别类别，不能证明同类具体资产、数量、真伪或完整性；高softmax与三帧相关结果不等于可靠认证。README的准确率、empty少量样本全对及示例耗时均不作为现场保证。

后续AI按单卡模板实施，缺设备或环境如实BLOCKED/NOT_RUN，不绕过S2-16门禁，也不因本报告把任何实现卡标DONE。

本任务仅新增本报告，未改模型、契约或实施状态，没有创建临时业务mock/测试数据库/凭据，未commit/push。
