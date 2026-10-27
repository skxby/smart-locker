# 视觉接口、具体地址与接线规格

状态：0.2.1设计，尚无视觉服务；相机已定USB UVC候选但未购买、未接板，见[相机选型与成本](../hardware/相机选型与成本.md)。业务语义见[视觉架构](vision-architecture.md)，实施从[阶段2B](../stages/stage-02b-vision-task-cards.md)逐卡进行。训练项目只读，其示例HTTP服务不是正式接口。

## 1. 地址与调用方

| 调用方 | 地址/入口 | 鉴权与结果 |
|---|---|---|
| Android | `GET https://<RK局域网地址>:8443/api/v1/vision/status` | Bearer USER/ADMIN，返回VisionStatusResponse |
| Android | `GET https://<RK局域网地址>:8443/api/v1/operations/<operation_id>/vision?limit=20` | 仅原用户/ADMIN，返回VisionEvidencePageResponse；他人操作404 |
| LVGL | service.sock method=getVisionStatus/getOperationVisionEvidence | 原本机RPC包络/peer/当前用户会话，不能连相机 |
| Core | locker_vision_submit | 提交已持久化的VisionCaptureRequest，QUEUED不代表有图像/结果 |
| host/vision | `/run/locker/vision.sock` | AF_UNIX SOCK_SEQPACKET，独立协议；只有固定service peer |
| locker-vision | 本机配置的相机节点（USB UVC，按`/dev/v4l/by-id`固定，不依赖枚举顺序）与RKNN模型文件 | 不接受网络URL/图片路径/手机上传；无TCP监听 |

两条公开GET由S2-14实现；原22入口在阶段1实现，S1不编造视觉结果。新增GET没有采集副作用，刷新页面不会调用NPU。WS沿用resource.changed(operation/device)提示后重查，**不新增vision资源枚举**；视觉证据提交使操作资源版本变化。手机不发送expected_class、model路径、capture请求或开锁命令。

## 2. 跨模块函数与具体DTO

签名唯一来源为[生成C头](../../contracts/include/locker_contracts.h)：

```c
locker_status_t locker_vision_create(const locker_vision_config_t *config,
                                    locker_vision_t **out);
locker_status_t locker_vision_bind_sink(locker_vision_t *vision,
                                       const locker_vision_sink_t *sink);
locker_status_t locker_vision_open(locker_vision_t *vision);
locker_status_t locker_vision_submit(locker_vision_t *vision,
                                    const locker_vision_request_t *request);
locker_status_t locker_vision_get_status(locker_vision_t *vision,
                                        locker_vision_status_t *result);
locker_status_t locker_vision_close(locker_vision_t *vision, uint32_t timeout_ms);
void locker_vision_destroy(locker_vision_t *vision);
locker_status_t locker_core_bind_vision(locker_core_t *core, locker_vision_t *vision);
locker_status_t locker_core_apply_vision_result(locker_core_t *core,
                                              const locker_vision_result_t *result);
locker_status_t locker_core_apply_vision_status(locker_core_t *core,
                                              const locker_vision_status_t *status);
```

| DTO | 固定字段与所有权 |
|---|---|
| locker_vision_config_t | socket_path、worker_uid、queue_capacity=4、Runtime同一host_instance_id、clock；路径来自本机部署 |
| locker_vision_policy_t | length+4097字节JSON容器，内容严格符合VisionPolicy；由CoreConfig传入，Core复制并固定到操作 |
| locker_vision_request_t | length+8193字节JSON容器，对应VisionCaptureRequest，不是任意JSON |
| locker_vision_result_t | length+16385字节JSON容器，对应VisionCaptureResult；received_mono_ms由适配器收包时填写，worker不能指定 |
| locker_vision_status_t | length+4097字节JSON容器，对应VisionWorkerStatus；received_mono_ms同上，不在读取缓存时刷新 |
| locker_vision_sink_t | user+result/status回调；参数仅借用到回调返回，接收方复制入Core队列 |

有界JSON类型只在RK使用，不能实例化到MCU队列/小栈。Core/适配器使用有上限的池或拥有明确生命周期的消息，不传解析树/请求栈地址。核心队列满时停止新采集/新动作，不能把丢结果当通过。

生命周期：repo/device/publisher及vision_create→core_create（复制策略）→core_bind_vision→vision_bind_sink→core_start→vision_open→HTTP/IPC接单。基础DISABLED模式不要求启动worker，状态必须明确DISABLED。REQUIRED即使worker失败也可提供只读查询/诊断，但不开放新借还动作。

退出：停止外部接单/新采集→vision_close关闭IO并等回调→device_close→Core收敛/停止→销毁所有适配器与Core→关闭仓储。close/stop超时不destroy。worker的RKNN调用只在独立进程，卡住不能阻塞Core；首版视觉进程人工恢复，不改变只自动恢复UI的既定范围。

## 3. vision.sock 私有协议

每个包一个严格UTF-8 JSON对象，不重复键/BOM/尾随NUL。请求≤8192字节，响应≤16384字节，status主体≤4096字节；recvmsg发现MSG_TRUNC则拒绝整包。最多1个在途采集、适配队列4；worker执行采集期间状态可返回BUSY，不能并发使用同一RKNN context。

| 消息 | 请求/响应schema | 作用 |
|---|---|---|
| HELLO | VisionControlRequest(kind=HELLO)→VisionControlResponse | service实例绑定；返回worker/camera generation、实际包/管线/profile哈希 |
| STATUS | VisionControlRequest(kind=STATUS)→VisionControlResponse | 只读当前观测，不采集、不增加进度、不刷新旧帧 |
| CAPTURE | VisionCaptureRequest→VisionCaptureResult | 仅执行当前已绑定实例的一次有界采集；不修改账本 |

本机worker端口由服务UID、SO_PEERCRED、PID启动身份及HELLO共同绑定；UI/supervisor/其他peer拒绝。HELLO中service_instance_id不能自证身份；本机受限权限和实际进程身份是前置。服务重启生成新实例、关闭旧连接，旧实例不能继续提交CAPTURE。客户端反向校验worker peer UID，返回实例变化使在途采集失效。

拒绝规则固定：格式/超限/未绑定/旧实例/同ID异正文/并发CAPTURE等未接纳请求关闭该连接，不另发未定义Error JSON；适配器映射既有INVALID/BUSY/IO_ERROR并标受影响在途结果未知。适配器队列满在本地返回BUSY，不发送。只有已接纳的合法capture回当前五种result.status；不能自行加BUSY/RETURN_OK等线上值。连接失败不触发自动重拍或动作。

请求字段包括capture_id、operation_id、PRE_ACCESS/POST_CLOSE、slot=1/camera=slot-1、service/预期worker/相机代际、MCU guard、issued/deadline、4种哈希和frame_count=3。worker不接收“预期成功类别”，只输出固定七类分数；expected_class由Core比较。CAPTURE收到重复ID只返回已保存的当前结果/明确拒绝，不重拍；跨连接/重启不得重放。

结果回显请求关联、实际worker/camera代际、guard、模型/管线/profile/策略哈希；status为OK/CAMERA_ERROR/MODEL_ERROR/TIMEOUT/INVALID_FRAME。OK须有3帧，每帧含真实采集序号/单调时刻、输入像素哈希、质量、七维scores_ppm、inference_ms。分数是0..1000000整数，避免跨端浮点/NaN歧义；原始logits只在测试证据，生产不传大数组或图片。七分数总和允许舍入误差±7，排序/最大值在Core复核；不能把模型闭集置信度解释成正确概率。

新帧要求序号严格增加、时间至少间隔100ms，三帧都在请求后、截止前；每帧到Core处理时年龄≤2000ms。像素相同的真实新帧允许同哈希，禁止拿哈希不同/相同证明采集时间；使用已核验V4L2时钟/序号。相机重开、ROI/分辨率/模型变化生成新代际，不能拼接结果。示例见[机器样例](../../contracts/examples/vision-examples.json)。

融合算法固定为逐帧核验：三帧均VALID、各七分数有效、argmax相同，且每帧top1≥同一threshold_ppm、top1-top2≥margin_ppm才有稳定类别；confidence_ppm取三个top1最小值，不平均或相乘。稳定empty为EMPTY、固定工具类为MATCH、其余为MISMATCH；物理冲突优先CONFLICT，来源/时间失效为STALE。任一质量/类间隔/稳定性不足为UNCERTAIN，公开predicted_class/confidence为null，原七分数仍留内部capture；未标定阈值不产生可授权MATCH。SHADOW只记录，不影响原动作。

## 4. Core关联、事务和公开投影

三类受控仓储集合：

| 集合/key | 存储 | 原子性 |
|---|---|---|
| vision_operations / operation_id | 固定策略、前后capture/evidence引用、当前截止/实例、人工核验原因 | 与创建操作一起提交；不能后改策略绕过门禁 |
| vision_captures / capture_id | 原请求、可null的原结果、状态、evidence引用 | 提交REQUESTED后才submit；同capture不同正文拒绝 |
| vision_evidence / operation_id+'/'+evidence_id | 类别/置信度/融合结论/guard/哈希/帧键/时间/实例、人工标记 | 与capture终结及操作/投影/版本一起提交 |

证据key使用规范小写UUID文本拼接，73字节。读取pre/post引用用operation_id+'/'+evidence_id exact查询；历史列表从operation_id+'/'作为after_key，按既有字节序分页，仅接受同前缀且value.operation_id匹配，遇其他前缀立即结束，不全扫历史。公开cursor由Core绑定operation_id+snapshot_revision；跨操作/旧快照cursor拒绝或要求重查。HTTP不接触SQL或仓储，也不能由客户端提供after_key。

PRE通过：先验当前关联/物理guard/剩余柜前证明时效，再在事务中记录证据及动作意图/outbox；事务成功后沿用发送检查点和execute_once。视觉与数据库不是同一事务，执行前仍做原物理与期限复核。数据库失败不发动作。

REQUIRED视觉不可用时，Core将工具可用投影置UNAVAILABLE并关闭新动作allowed_actions，保留已有借用/占位；不能因MCU当前READY就认为整体可借。MCU物理模式仍是原事实，不为视觉故障伪造串口OFFLINE或覆盖item_state。健康恢复只恢复资格检查，不自动完成旧操作。

POST通过：原开门/物品变化/关门事实完整且同guard，三帧结论符合目标后，事务进入WAIT_CONFIRM。采集中保持ACCESS_ACTIVE并显示检查中；超时/冲突转REVIEW_REQUIRED。不能先进入WAIT_CONFIRM给按钮，再异步发现放错。

2000ms只用于Core首次接纳采集结果的逐帧新鲜度检查，不是用户必须2秒内点击确认。POST证据提交后记录accepted_mono_ms，在原120秒WAIT_CONFIRM窗口内，且同operation/MCU guard/服务与相机代际/冻结模型策略始终有效时可用于确认；确认再核对新鲜物理快照。期间开门/物品变化、重启/重连、worker或相机代际变化/健康未知使证据失效并转核验，不每2秒循环重拍直到凑成功。原120秒超时规则保留，Core不在HTTP请求等待NPU。

恢复加载上述三个集合；未决采集标失效，已产生动作的操作按原规则核验，无动作且REQUESTED可按当前用户/证明重新申请采集。原结果永远保留为历史，不在新service/MCU会话自动复用。历史证据查询不触发采集。

VisionEvidence公开投影不含逐帧原scores、图像或本机路径；保留预测/融合结论、来源哈希/时间/guard和人工标记。HTTP/IPC只读Core一致投影。管理员resolve保留原矛盾和原因，不能改原预测为MATCH；相机损坏需先处理，不能通过resolve自动重新开放REQUIRED新业务。

## 5. 模型包与预处理约定

选中源权重SHA256及类表见[资产核对](../../config/vision/v11-source-audit.json)。现有ONNX为7输出，但数值等价未验证；没有RKNN。未来包必须固定checkpoint/ONNX/RKNN/类表/参考版本/量化SDK与runtime/driver/校准及独立测试集指纹。

参考几何为PIL RGB→短边int(224×1.14)=255→CenterCrop224，保持torchvision版本的插值/抗锯齿/整数取整规则。部署先按同一机位ROI得到RGB后做相同几何，预处理recipe和ROI分别纳入pipeline/profile哈希。ONNX输入为归一化NCHW float32；RKNN部署选择CPU产生未归一化RGB uint8 NHWC，SDK嵌入mean=[123.675,116.28,103.53]、std=[58.395,57.12,57.375]，rknn_inputs_set按已验证属性设置type/fmt/pass_through，不在CPU再减均值。输出明确是logits还是已softmax，选定当前logits只做一次稳定softmax。

校准图片必须先走同一几何，不能让工具自动拉伸原图。动态batch ONNX不能假定RKNN直接支持；锁定batch=1导出或按匹配SDK指定输入尺寸后，逐样本证明等价。不能简单降opset后省略校验。现有calibration.txt与Windows带标签val_list分别转换为VM可解析的图像输入/标签清单，不直接传同一格式给accuracy_analysis。

阈值由校准集确定，在未参与选择的柜内独立集报告各类错误、错误器材被放行率、empty误判、拒识/人工率；低光/遮挡/同类替换/多件等场景要说明分类能力边界。3帧相关，不能相乘置信度宣称可靠性。原README 91.9%、checkpoint 92.417%、empty 14/14和30—60ms都不是现场门禁指标。

哈希规则：类表按文件字节、checkpoint/ONNX/RKNN按制品字节；pipeline/profile按规范JSON（键排序、UTF-8、无空白、无NaN）计算；policy_sha256计算时剔除policy_sha256自身。bundle清单自身哈希同样不包含其自指哈希字段。模型、类别、管线、ROI、策略变更均版本化，营业中不热切。

## 状态时效与曝光时间资格

STATUS每1000ms一次，HELLO/STATUS回复期限1000ms，状态样本TTL=3000ms，按同Linux boot下now-sampled_mono_ms判断；received_mono_ms由适配器记录，不能用重读缓存续期。未来时刻/断连/超时/过期发Core未知或DEGRADED，REQUIRED关闭新动作并使受影响WAIT_CONFIRM转核验。worker的控制IO不得阻塞等待NPU，采集中可报BUSY但仍响应STATUS；实际卡死仍受3000ms采集截止约束，不能靠控制线程存活假报完成。

captured_mono_ms为曝光开始保守下界，不能将MONOTONIC的EOF时间直接当曝光起点。SOE直接使用；EOF/驱动时间需实际profile记录到曝光起点的端到端上界B及验证依据，取timestamp-B，并验证晚于请求且到Core仍新鲜。未知B不启REQUIRED；原timestamp/flags/B保留在受限采集日志，profile哈希绑定该转换。手动曝光值/流重开/序号递增本身不证明内部缓冲界限。
