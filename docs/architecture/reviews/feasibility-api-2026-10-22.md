# 架构与接口独立可实施性审计

日期：2026-10-22。范围：契约0.2.0、生成C头、UART、内部接口、视觉协议、阶段调用链及2A/2B卡。本文是审查输入，不替代机器真源；根负责人应集中修正源文件、生成物和实施归属。本轮没有修改原模型、实施业务功能或更改任务状态。

## 审计结论

RK3568上的独立视觉进程、唯一Core账本、STM32 HAL物理控制、一格固定工具的分工可以实现。现阶段不能给出“完整接口已经可直接接通”或“整柜已验证可用”的结论。以下两项P1需要契约负责人处理；硬件定时输出、相机、RKNN及柜内分类阈值仍需对应实物卡验证。

## 发现与修正要求

### P1：MCU异步响应没有完成通道，持久化管理操作边界未冻结

- `contracts/include/locker_contracts.h`声明`mcu_control_make_snapshot(control, request_no)`，语义是排队捕获并等待日志屏障后异步返回；没有输出DTO、Control完成sink或绑定方法。
- 唯一MCU sink是Journal→Control的`committed(token,status)`，不能向Communication传递快照内容、UART result或原session/request_no。同样`submit_unlock`返回QUEUED后，持久化成功的ACCEPTED/明确未驱动结果也没有冻结的回传路径。`release_context`同样需持久化后响应。
- Journal公开端口仅append、commit_intent、read_page、ack和init/bind；快照屏障、持久上下文释放、明确未驱动终结及启动恢复元数据的跨模块操作尚未声明。Control依赖opaque journal context，不能凭头文件合法访问这些状态。若统一为同一固件所有者内部实现，也应明确这些是同一模块内部边界，不能继续以独立卡让不同AI猜测。
- 影响：S2-03、S2-04、S2-05无法仅凭冻结接口独立完成装配；静态声明解析不会发现未声明的回传通道。
- 建议：统一Control请求/完成包络及启动前sink，明确session/request_no/type、按值复制、回调背压和旧会话规则；为Journal恢复/屏障/终结/释放固定管理端口或固定操作枚举。不得以动态拼接额外同义API规避归属。
- 验证要求：跨模块编译/链接的最小装配；真实屏障延迟期间输入变化；旧session回执、满队列、持久化失败、释放ACK丢失均有可关联响应且不驱动第二次。

### P1：视觉持续健康的时间规则缺失

- `VisionWorkerStatus`包含`sampled_mono_ms`，适配器有`received_mono_ms`；规格要求WAIT_CONFIRM期间worker/相机代际持续有效，健康未知使证据失效。
- 当前常量和视觉接口没有规定STATUS轮询周期、控制响应超时、健康样本TTL，也未规定缓存READY超期后何时必须通知Core。不同AI可能分别实现“收到过READY即可确认”或不同过期时限。
- 影响：worker卡死但socket未立即断开时，120秒确认窗口内可能使用过期健康；反向也可能实现成每2秒废弃已提交证据，破坏既定确认体验。
- 建议：冻结STATUS周期、控制超时、健康TTL及有效年龄计算；原sample和received不能在读取缓存时刷新。Core tick必须令超期READY转未知并使待确认守卫失效；状态恢复不能自动完成原操作。数值由根负责人集中决策后写入真源及卡。
- 验证要求：worker无断连卡死、晚到STATUS、未来/乱序样本、缓存读取、相机重开、120秒窗口中的短暂失联与健康恢复。

### P2：总API借出时序未体现最终REQUIRED视觉门槛

- `docs/architecture/api-links.md`第2节主时序从柜前证明直接写ACCESS_PENDING/outbox并开锁，从关门事实直接给WAIT_CONFIRM；第5节及独立F6又要求PRE/POST视觉。
- 阶段0状态表`REQUESTED + 有效柜前证明 → ACCESS_PENDING`也没有写REQUIRED的PRE通过条件。
- 建议：总入口主链直接画最终REQUIRED行为；基础模式明确标适用范围。表中条件应包含冻结模式对应的PRE通过，POST通过才WAIT_CONFIRM。不能依赖后续AI自行跳到另一文档补足关键门禁。

### P2：柜前证明的2秒接纳与15秒有效期语义应统一

- 阶段0第183—184行明确：实时卡观察仅在端到端年龄≤2秒时首次接纳，证明从原MCU事件起有效15秒。这套规则合理。
- 内部接口TIME_ANCHOR段使用“大于2秒…拒绝柜前证明”，没有注明仅首次接纳，可能被实现成视觉采集后再次使用证明也受2秒限制，与3帧异步采集及15秒TTL混淆。
- 建议：直接统一为“卡事件首次接纳≤PHYSICAL_FRESH_MS；已接纳证明期限为原事件产生时刻+PRESENCE_TTL_MS；开锁前按剩余15秒期限保守映射，不重置”。本机presence也固定起始时刻。

## 已检查且未发现内在矛盾的设计

- 3帧间隔至少100ms，采集总截止3000ms，同时要求每帧首次到Core时年龄≤2000ms：可同时成立，但超过最老帧年龄即拒绝；3000ms不是保证结果一定可接纳。必须留板端耗时证据，不能靠文档断言性能。
- 2000ms照片新鲜度仅首次接纳；同guard的已提交证据可在120秒本人确认窗口内引用。这两项不矛盾，持续健康时间规则须补足。
- 视觉历史按operation_id前缀分页、全局snapshot_revision绑定cursor、遇非当前前缀结束的规则可实施；要在仓储有序索引上实现，不让HTTP扫描数据库。
- HAL硬件限时输出可作为锁硬截止方案，但必须把定时器输出真正接到驱动控制，软件定时器/中断中关GPIO不能满足本合同。当前未验证profile足以阻止误称已具备，不足以证明实物已经满足。
- 正常退出先停止接单，再关闭视觉/设备回调、收敛Core，失败不销毁对象的生命周期边界明确。worker阻塞在独立进程不阻塞Core；首版人工恢复也已明确。

## 本轮独立检查结果及盲点

使用本地Python分别运行三个正式checker，全部退出0：

| 检查 | 结果 |
|---|---|
| check_contracts.py | PASS；24公开入口、80内部端口、28私有RPC、11 UART请求，payload最大469；C只parse未compile/link |
| check_stage_plan.py | PASS；5阶段45卡，DONE=0，active_task=null |
| check_vision_contracts.py | PASS；8 schema样例、5关联语义负例、2跨操作页负例；未运行推理、相机或RKNN |

checker验证枚举、schema、生成一致性、已编写样例和归属映射；不证明每个异步请求都有声明完备的完成通路，也不模拟健康定时器、真实任务竞争或硬件脉冲。因此PASS与上述P1发现并不矛盾。

未完成：C工具链编译/链接、实际SQLite事务实现、HAL工程烧录、UART双端通信、示波器硬截止、断电持久性、V4L2真实新帧、RKNN转换/耗时/独立柜内误放行率。材料商品链接选定不能替代这些验证。此报告不构成阶段PASS或硬件可用声明。
