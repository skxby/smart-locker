# 内部接口合同 v1

视觉新增端口的参数/DTO、sink、装配/退出及持久化见[视觉接口](vision-interfaces.md)。下文无视觉的装配用于DISABLED_FOR_FOUNDATION；REQUIRED在Core启动前绑定视觉适配器。NPU不进入Core线程，视觉协议不复用UI方法表。

当前跨端契约版本0.2.1：70个原生C端口、28个业务/监督RPC方法。按[接口实施归属](traceability.md)逐卡实现；下列声明不代表已有代码。

状态：阶段 0 设计声明，无业务实现。上位决策见 `docs/stages/stage-00-architecture.md`；字节字段以 `contracts/uart-layouts.json` 为真源，枚举和 C 声明由根合同统一生成。本文件定义逻辑端口及所有权，不允许实现 AI 通过另加直连路径绕过它们。

## 1. 公共类型与调用约定

- `operation_id/command_id/session_id/instance_id` 是不同含义的 UUID；设备事实键为 `device_id+journal_epoch+boot_id+event_seq`。不得互相代用。
- `operation_revision` 是单操作版本，`snapshot_revision` 是业务一致快照版本；MCU `physical_revision` 是同一次 boot 中的去抖物理状态版本。版本不能跨含义比较。
- 内部同步返回 `QUEUED/REJECTED` 仅说明消息已复制进队列或被拒绝。`COMMITTED` 表示 SQLite/littlefs 提交确认；`ACCEPTED` 表示 MCU 已持久化动作意图；物理结果由事件另行表示。
- Core 输入队列默认 64；ISR 不执行日志/数据库操作；队列满返回 `QUEUE_FULL`，不丢弃关键消息继续营业。消息按值复制，或使用固定池索引显式移交所有权，不提交回调栈地址。
- 所有超时使用单调时钟。调用超时不撤回已排队/已提交/已驱动操作；查询结果或核验，不能自动再发新 command。
- C 结构体只用于同进程调用，不直接映射线路；网络回调、UART 与文件系统模块均不向其他模块泄露可变内部指针。
- 对外 request_id 可以来自不可信的 X-Request-ID，仅用于排障。Core 每次提交生成随机 submission_token，wait_result 只按该内部 token 寻址，不能把两个用户相同 request_id 的结果混在一起。principal 包含 session_id，执行命令时重新检查当前会话与用户状态。

## 1.1 生命周期与跨 AI 数据合同

所有函数声明由 `contracts/include/locker_contracts.h` 生成。只有模块 context 保持 opaque；仓储跨模块 DTO 公开为有界 JSON 容器，字段由 `contracts/internal-data.schema.json` 固定。

| 接口 | 生命周期或完成规则 |
|---|---|
| `locker_repo_open/query/close` | 数据库路径/schema 与显式首次初始化；query 按集合分页，不暴露 SQLite 连接 |
| `locker_core_create/start/stop/destroy` | create 借用 repo/device/publisher；start 建唯一线程；stop 收敛在途写入，超时禁止 destroy；不关闭借用依赖 |
| `locker_core_authenticate` | 鉴权经 Core 查会话；命令执行时再次验证，HTTP线程不直接查库 |
| `locker_device_create/open/close/destroy` | create 未打开对象；open建会话；close停止IO/回调不重放动作；线程退出后销毁 |
| `locker_events_create/destroy` | 有界提示发布器；Core停止发布、全部WS关闭后释放 |
| `locker_supervisor_create/destroy` | 固定UI目标与独立限次检查点，不接任意命令 |
| `mcu_control_init/journal_init/health_init` | 返回模块自有静态context；硬件未核对或日志失败仅诊断，不READY、不自动format |

装配顺序：repo_open → device_create → events_create → core_create → device_bind_sink绑定Core接收口 → core_start → device_open → http_start/ipc_start。停机先http_stop/ipc_stop并等待工作线程结束，再device_close停止回调，core_stop收敛，然后destroy HTTP/IPC/Core/device/publisher，最后repo_close。未成功停止的线程不得释放对象。构造期由初始化线程独占资源，start后仓储与领域状态交给Core线程；停止后只允许Runtime关闭，不进行新的CRUD。

Runtime每次service启动生成一个非零host_instance_id，传给locker_core_config_t.instance_id与locker_device_config_t.host_instance_id；Core进度/HELLO/HOST_HEALTH/监督SERVICE绑定使用同一实例，不让各模块分别生成。首次真实Core循环前get_progress返回NOT_READY/未知，不能给0计数补一个新采样假装有进展；新进程生成新实例，旧报告不继承。

`locker_device_bind_sink` 的user、reply/event回调在open前绑定；回调参数只在调用期间借用，接收者复制入Core队列。sink的Core上下文活到device_close等待所有回调结束。回调背压时不静默丢事件，不ACK未落库数据，暂停新动作和健康授权并要求重新核对，不能无限积压。

`locker_http_start/stop/destroy` 和 `locker_ipc_start/stop/destroy` 是Runtime装配入口。两者借用Core；stop超时不能销毁。Network模块内部WS连接表和发送helper由同一所有者维护，不另扩跨模块入口。

MCU初始化journal与control后，在启动存储任务前调用 `mcu_journal_bind_sink` 绑定静态Control回执接收者；回执按token复制入Control，绝不在存储回调直接驱动锁。队列满或回执不能交付要锁存故障、拒绝新动作；没有回执就不能驱动。具体任务创建/ISR通知为阶段2同一固件所有者的本地实现细节。

仓储采用受控文档集合，`locker_change_t` 符合 RepositoryChange；`locker_recovery_t` 符合 RepositoryRecoveryPage。BEGIN IMMEDIATE 内核对 snapshot_revision 和记录存在/不存在条件，提交文档变化与新版本。slot_guards的key=1保护占位；用户名、UID索引同事务更新。不得向仓储输入SQL，集合与行schema必须匹配。不使用跨模块JSON哈希比较分支，避免两端序列化差异。

`locker_core_query` 是给HTTP/IPC使用的有界同步包装器：内部排队到Core并以私有submission_token等待最多1000ms；不能在调用线程直接读仓储或可变业务对象，Core线程自身不能调用这个等待包装器。超时查询可重新发起，没有物理副作用。

幂等request_hash由Core统一生成：用户+方法+规范化路径作为作用域，正文与查询参数按固定ASCII字段名排序、无额外空白、UTF-8保留原字符串、不进行Unicode归一化或浮点转换后SHA-256。Repo只存储/比较Core提供的哈希，不自行再次序列化计算。

HTTP operationId 与 Core命令名对应在 internal-data-source.json 的 command_bindings；context工厂是已冻结接口，本地static helper不属于跨AI合同。

## 1.2 本机 RPC 的唯一包络

service.sock 与 supervisor.sock 均使用 AF_UNIX SOCK_SEQPACKET。一个包恰好包含一个 UTF-8 JSON 对象，无 BOM、尾随 NUL、数组批处理或重复键。请求最多 **8192 字节**，响应最多 **65535 字节**，均按完整序列化包的 UTF-8 字节数计算，包含包络。机器定义是 contracts/internal-data-source.json 中 InternalRpcRequest、InternalRpcResponse、SupervisorRpcAck 及 internal_rpc.method_bindings。

请求所有字段均须出现，不适用时按合同使用 null 或空对象，不允许额外字段：

| 字段 | 定义 |
|---|---|
| v | 整数1 |
| request_id | 小写 UUID，响应关联及排障；不是 Core 私有 submission_token，也不承担幂等 |
| method | 公开operationId（排除subscribeChanges），或presence.submit、maintenance.reconcile、progress.report、health.get、ui.restart.request |
| session_token | 普通业务为当前用户 token；login、监督方法必须 null；logout 可 null 并保留重复退出语义 |
| path_ids | 公开路由中的具名 UUID 参数对象，例如 operation_id；无路径参数为{}，禁止加入未声明参数 |
| query | 与公开查询参数同名同类型，例如 limit 为整数、user_revision 为u64字符串；无查询参数为{} |
| idempotency_key | 对应公开路由要求幂等时为 UUID；presence.submit 也要求；查询/登录/监督为 null，监督恢复以 incident_id 去重 |
| params | 对应公开 input_schema 或本机 request_schema；无请求体必须 null，不能把 query/path 参数塞入这里 |

适配器按 method 查固定映射，生成与 HTTPS 相同的 Core command/query。source、principal、actor_id、submission_token 不属于包络；来源与 peer 身份由 socket 及 SO_PEERCRED 构造，再由 Core 根据 token 重新核对用户/权限。本机 presence 还须 owner 匹配；监督方法校验实例与对端 UID 绑定，不依赖用户 token。

服务socket只接受公开业务方法、presence.submit和maintenance.reconcile，各按peer/当前会话/权限核验；监督socket只接受progress.report、health.get和ui.restart.request。health.get/恢复请求只准已绑定service peer。非法跨socket方法拒绝，不通过另一个进程代为执行。

响应固定为 {v:1,request_id,status,body}，无额外字段。status 保留公开 HTTP 成功/错误语义；body 为原公开 *Response 或 Error 对象，嵌套 request_id 与包络一致。204 的 body 为 null。调用方按原请求 method 校验具体响应 schema，不能仅因满足其他资源 schema 就接受。

监督成功 body 为以下 ack，不能用空对象替代：

- progress.report：200，{kind:"PROGRESS_RECORDED",target,instance_id,progress}，仅表示该观测被接受。
- ui.restart.request：202，{kind:"UI_RESTART_ACCEPTED",incident_id,state}，state 为 QUEUED/STARTED/OBSERVED_RUNNING/FAILED。相同 incident_id 重试取得既有状态，不能再创建恢复；限次/冷却拒绝按 Error 返回，接受不代表 UI 已恢复。

- health.get：200，SupervisorHealthResponse，含监督实例、外层采样时间、可null的service/UI源进度、监督状态、可null的恢复累计/窗口次数/冷却/预算资格。params、token、幂等键均null，path/query为{}；不创建恢复、不更新进度。具体字段见内部schema。

请求/响应大小分别检查。recvmsg 必须检查 MSG_TRUNC，禁止执行截断请求或解析部分响应；没有可信 request_id 的坏包关闭连接。响应按完整包预算分页，减少返回条目并保留连续游标，不截断 JSON 或单条记录。不可分页查询超限以有界503 QUEUE_FULL错误返回；已提交命令的响应发送失败不撤销或重做动作，使用原幂等键取得原结果。

LVGL 使用异步 IPC 适配器/工作线程，主循环只提交意图与消费结果，不阻塞等待连接、查询或结果。IPC 工作线程不直接操作 LVGL 对象。响应通过 request_id 回到原请求，Core 的随机 submission_token 只在 service 进程内等待使用。重连先取快照，不重放未完成 presence；查询可重试，有副作用意图保留原幂等键。token、密码和原始敏感 params 禁止出现在日志。

SOCK_SEQPACKET 保留包边界，不保证默认缓冲已足够；启动时验证实际收发缓冲与包上限。设备验收包含8192字节请求边界、65535字节响应边界、超限/截断、跨 socket 方法、伪造source字段、未知方法，以及 UI 等待期间进度继续推进。


## 2. RK 领域与持久化端口

| 逻辑接口 | 调用方 → 唯一所有者 | 输入与完成结果 |
|---|---|---|
| `locker_core_submit` | HTTP/本机 IPC 适配器 → Core 线程 | 输入 principal、来源、request_id、幂等键、规范化命令及 operation_revision。排队后由 Core 回送提交结果。来源由已验证通道构造，客户端 JSON 不能自报 LOCAL |
| `locker_core_wait_result` | HTTP/本机 IPC 适配器 → Core 结果通道 | 输入提交回执及有界等待期限，取得匹配的已提交/明确拒绝结果；等待超时不取消命令，调用者保留幂等键重查 |
| `locker_core_apply_device_event` | 设备适配器 → Core 线程 | 输入已校验设备事实、epoch/boot/seq、LIVE 或 REPLAY 传输来源。按事件键幂等导入；缺口先补齐，不按到达顺序猜历史 |
| `locker_core_apply_device_reply` | 设备适配器 → Core 线程 | 输入解码响应、原请求关联、session 与发送检查点；拒收旧会话响应，ACK只推进动作状态，不能代替门/物品事实 |
| `locker_core_tick` | 定时调度 → Core 线程 | 输入当前单调时间，推进申请/开门/关门/确认超时；不调用 sleep 等待条件 |
| `locker_core_query` | HTTP/IPC → Core 线程 | 输入 principal 与查询范围，返回不可变一致快照。使用副本，不能把 Core 内存交给网络线程写入 |
| `locker_repo_commit_change` | Core 线程 → SQLite 适配器 | 一个事务提交 operation、loan 变更、审计、幂等记录、outbox 和 snapshot_revision；失败全部回滚，无动作发送 |
| `locker_repo_import_events` | Core 线程 → SQLite 适配器 | 一个事务导入一批唯一事实、业务投影变化及连续补取游标。事务成功才允许 ACK_PERSISTED；重复事实不再次推进业务 |
| `locker_repo_load_recovery` | Core 启动 → SQLite 适配器 | 恢复未完成 operation、loan、幂等记录、outbox、事件游标和待核验项；不自动发送恢复出来的旧动作 |

`locker_repo_import_events` 还接收可选 projection_change，投影由 Core 计算；仓储在同一事务导入新事实、投影变化与连续水位，不能自行判断借还成功。重复事实不能再次改变投影。

### 同一业务入口

Android HTTPS、本机 LVGL 和实时读卡结果均进入 `locker_core_submit`。公开的创建/确认/取消/核验命令不包含原始 GPIO、任意锁通电时长或可写物理状态。

`PRESENCE_SUBMIT` 仅由两种适配器构造：SO_PEERCRED 和文件权限验证通过的 AF_UNIX 本机 UI；或当前 READY 串口会话中实时、未消费且新鲜的读卡事件。Core 再检查用户绑定、operation owner 和 15 秒时效。历史补传与重复读卡只能补记录，不能产生存在证明、开锁或 UI 恢复请求。

`locker_core_submit` 的 QUEUED 不能直接被 HTTP 转换成业务成功。网络适配器等待 Core 的已提交结果，再返回资源；排队前失败可以返回明确未接受的 503。排队后客户端断开或等待结果超时，客户端使用原幂等键查询/重试原意图；不得生成新 operation 或声称取消成功。

### 事务与副作用边界

只有 `locker_repo_commit_change` 成功后，Core 才把该次进程实例中新创建的 outbox 项交给设备适配器。写串口前先持久化“已尝试发送”检查点；即使只写了部分字节也算结果未知，不自动补发 EXECUTE。ACK 丢失只查询 command。服务重启恢复出的可能已发送意图进入核验，不恢复发送队列。

本人确认需要 operation_revision 匹配且 WAIT_CONFIRM 条件仍成立；读取新鲜 MCU 快照不代表锁定物理世界。Core 按该快照的 boot、physical_revision 及后续事件检查，任何已知冲突先核验。终结后再发生的未授权物理变化产生新的异常，不能无痕修改已完成历史。

## 3. RK 设备端口：真 UART 与模拟器共用

| 逻辑接口 | 输入 | 异步结果/失败 |
|---|---|---|
| `locker_device_open` | 固定设备配置、host_instance、新 session | 握手身份/能力/epoch；进入 RECONCILING。相同 session 的 HELLO 重试不能清水位 |
| `locker_device_get_snapshot` | 当前 session | Control 捕获并经过日志提交屏障的快照；拿不到屏障时返回不可核对，不伪造健康快照 |
| `locker_device_execute_once` | session、action_no、operation、command、期望 epoch/boot/physical_revision、not_after_mcu_ms | 一次有副作用发送；期限保守映射到该 boot 的 MCU 单调时钟。无法保证到柜证明仍新鲜时禁发。ACCEPTED、PULSE_STARTED、门打开分开，不把超时解释成未执行 |
| `locker_device_query_command` | 当前 session、目标 epoch、command_id | 查询已知状态或 UNKNOWN_RESULT；查询本身不执行、不生成新 command |
| `locker_device_fetch_events` | epoch、after_cursor、数量≤4 | 连续已提交记录、边界与缺口信息；跨 boot 按持久日志顺序推进 |
| `locker_device_ack_persisted` | epoch、RK 已落库连续游标 | MCU 记录确认水位；不倒退、不跨缺口、不越已提交记录 |
| `locker_device_resume_ready` | epoch、boot、期望 physical_revision、RK 连续落库游标 | 在 Control 中 CAS 核验：物理版本没变、已补齐屏障、无未决上下文/日志故障才 READY |
| `locker_device_release_context` | epoch、boot、operation、command、期望 physical_revision | RK 完成/核验已落库后释放物理关联。闭门、无脉冲、上下文匹配；重复清空幂等。不开锁、不改账本 |
| `locker_device_send_health` | host/UI 实例、真实循环进度与观察年龄 | 每秒传输，不能由通信线程代增。5 秒失联禁新动作，仍记录与喂 MCU 看门狗 |
| `locker_device_time_anchor` | request_no、同一注入时钟 | 每2秒发TIME_ANCHOR；reply含请求最早发起和收到的主机单调时间，以及MCU收/发单调时间 |

TIME_ANCHOR 仅在RTT≤200ms、年龄≤3000ms且boot一致时用于动作期限/读卡时效。设主机发起/收到为h0/h3，MCU收到/发送为m1/m2，则MCU−host偏移保守区间为 `[m2−h3,m1−h0]`，统一整数毫秒并扩展测量误差。用最坏边界估计事件年龄；大于2秒或无法验证就拒绝柜前证明。EXECUTE绑定not_after_mcu_ms，MCU在意图提交前后都验证，UART积压不会刷新旧证明期限。

设备适配器拥有串口 fd、解析缓冲和有界发送队列，不直接修改借还状态。模拟器实现完全相同端口并显式返回 simulated=true；模拟故障注入只存在测试/模拟入口，不通过公共产品 API 暴露。

DISARM 是连接内部控制，关闭新动作资格，不撤销已持久化接纳的脉冲，也不清活动上下文；脉冲仍由 MCU 本地硬截止保证。超时关闭会话后，旧 session 的动作帧不得执行。

## 4. 事件提示与进程监督

| 逻辑接口 | 所有者 | 约束 |
|---|---|---|
| `locker_events_try_publish` | 网络适配器 | Core 提交后只发布 resource.changed 或 resync_required；每连接 16 条有界缓冲，慢客户端断开，不阻塞 Core |
| `locker_supervisor_report_progress` | supervisor | service/UI 通过受限 supervisor.sock 登记实例并报告真实循环进度；进度来源、观察年龄和未知值必须保留 |
| `locker_supervisor_request_ui_restart` | supervisor | 只接受固定 UI 目标、request_id、来源事件键和理由；同一请求幂等。10 分钟最多 3 次、60 秒冷却，UI 启动宽限 30 秒 |
| `locker_supervisor_get_health` | supervisor | 只读一致观察；service跨进程通过health.get取得，不传模块ctx指针 |
| `locker_core_get_progress` | Core→Runtime | 非阻塞读取真实循环发布的不可变源样本，不排队/IO/自增，阶段1即实现 |
| `locker_core_apply_supervisor_health` | Runtime→Core队列 | 按值复制健康观察；Core独占更新公开投影，不由Runtime直写 |

supervisor 不提供业务服务自动重启、主机硬复位或任意 exec 接口；service 死锁时 MCU 记录失联，人工处理。主机启动宽限 120 秒只影响误报策略，不能使已失联主机仍有开锁资格。请求恢复与恢复完成分别记录，恢复必须看到新 UI 实例和真实进度。

ProgressTarget固定SERVICE=0/UI=1；McuTaskId固定CONTROL=0/COMMUNICATION=1/PERIPHERAL_STORAGE=2/HEALTH=3。源ProgressReport/locker_progress_t含sampled_mono_ms，取实际完成回路的CLOCK_MONOTONIC，observed_age_ms表示该采样时已存在的年龄，通常为0。后续副本保留源时间和源年龄；消费者在同boot用饱和加(source_age, now-source_sample)计算有效年龄，不能搬运后又重置时间或将已累计年龄重复相加。未来/乱序/回退样本拒绝；同counter重报不更新最后严格进展依据。

SupervisorHealth外层sampled_mono_ms表示只读副本形成时刻；service/ui内层保留最后严格递增的原样本时间和原年龄。Runtime组装HOST_HEALTH只对内层按当前now累计一次；外层只用于缓存过期，不再次加内层已覆盖区间。未知线上零UUID/UINT32_MAX age，未知恢复数公开为null。supervisor不存在的阶段1/2仍传真实Core基线；UI未知不单独阻止新动作，Core停滞则不能靠健康搬运线程维持资格。

OBSERVED_RUNNING只证明预期新UI登录页可交互且至少两次新鲜真实进展，不证明业务已同步。UI重启清token；业务按钮另需登录后的新鲜快照。正常无人登录不构成反复重启理由。完整生命周期/限次/incident规则见[监护接口](monitoring-interfaces.md)。

## 5. MCU 控制端口

| 逻辑接口 | 调用方 → 所有者 | 结果与边界 |
|---|---|---|
| `mcu_control_submit_unlock` | 通信 → Control | 接受经过会话校验的动作意图，检查 READY、slot=1、物理基线、expected physical_revision、action 水位、活动上下文及存储健康；不直接驱动 |
| `mcu_control_observe_inputs` | 周期调度/BSP → Control | 去抖门、物品和供电类别；对稳定状态变化生成事件并递增 physical_revision；UNKNOWN 明确传播 |
| `mcu_control_on_journal_result` | Storage → Control | 关联 token 的持久化回执。只有意图提交成功且执行前条件再次有效，才允许一次锁脉冲；失败禁新动作 |
| `mcu_control_make_snapshot` | 通信 → Control | 捕获 boot、physical_revision、物理状态与活动上下文，建立日志屏障 token，待此前事件已提交才发布快照 |
| `mcu_control_release_context` | 通信 → Control | 对 epoch/boot/physical_revision 做 CAS；闭门且无脉冲，且关联 ID 匹配才提交释放检查点；释放持久化后返回成功 |

`mcu_control_submit_unlock` 的排队成功不等于 ACCEPTED。Control 将 command+session+action_no+operation+expected physical_revision+not_after_mcu_ms 交给 `mcu_journal_commit_intent`。该检查点成功提交后才能返回 ACCEPTED，之后启动脉冲；如果在提交期间门/物品/供电状态或版本改变，报告已接纳但未驱动/条件变化，不驱动、不回退 action 水位、要求核验。

执行前再验证与意图绑定的 physical_revision、当前 session、READY、主机心跳新鲜度及到柜证明期限，避免验证与执行之间失效。动作准备本地上限 PREPARE_DEADLINE_MS=2000；实际截止取接收时间+2000 和 not_after_mcu_ms 中更早者。接收和持久化回执后都检查期限，过期不驱动且已接纳 action_no 不退回。只有“未驱动”终结记录可靠提交后才可报告 REJECTED，否则 UNKNOWN_RESULT/核验。Control 在同一任务迭代内核对最新去抖输入与状态后调用板级脉冲；该方法不证明机械状态在随后永远不变，因此异常物理变化仍须记录。

锁输出上电默认关闭；首版脉冲上限 1000ms，实物规格更短时取更小值。`force_off` 是安全关断，不依赖 Linux 取消包；硬件截止与任务执行结果分别验证。没有硬截止适配器不得通过真锁验收。

## 6. MCU 日志、硬件和健康端口

| 逻辑接口 | 唯一所有者 | 明确语义 |
|---|---|---|
| `mcu_journal_append` | PeripheralStorage | 异步追加定长事实；QUEUE_OK 与已提交分开；生成 boot/seq 时序与提交边界一致 |
| `mcu_journal_commit_intent` | PeripheralStorage | 原子提交动作意图、动作水位与活动上下文检查点；成功回执前不驱动；重复同水位/command 返回既有状态 |
| `mcu_journal_read_page` | PeripheralStorage | 只返回已提交事件，最多 4 条；报告 first_available/last_committed、next cursor 和缺口 |
| `mcu_journal_ack` | PeripheralStorage | 确认 RK 持久化水位；仅回收已确认且不影响去重/未决动作的明细；不重置 action 水位 |
| `mcu_board_read_inputs` | BSP | 门/物品/供电原始值及有效性；未知值不能默认正常 |
| `mcu_board_start_bounded_pulse` | BSP，只有 Control 可调用 | pulse_ms 已通过实际硬件配置核准且≤1000；启动硬件截止；失败不伪造 PULSE_STARTED |
| `mcu_board_force_off` | BSP，Control/安全故障路径 | 立即将驱动设安全状态，可幂等调用，不改借还账本 |
| `mcu_board_spi_transfer` | BSP，PeripheralStorage 调用 | 有界一次事务与独立片选；Flash 内部忙轮询不长期占用总线；不由 ISR 调用文件系统 |
| `mcu_health_report_progress` | 各任务 → Health | 报告各自真实工作循环进度及观测时间，不由中断伪造任务进展 |
| `mcu_health_evaluate` | Health | 综合关键任务、日志、主机/UI 观察，决定告警/恢复请求/喂狗资格；Linux 断线不阻止 MCU 继续观察和记录 |

Flash 满、挂载失败、意图提交失败、关键队列丢失或去重检查点损坏均禁止新开锁。不得自动格式化恢复营业。未经落库确认的事件和未决动作不得回收；若没有可回收空间，报告 LOG_UNAVAILABLE 并停止新操作。

## 7. AI 交接的检查重点

### 材料更换后的BSP与维护边界

- `mcu_board_apply_profile` 只接受本地编译/部署的已验证profile，将HAL句柄与引脚隐藏在BSP内；GPIO极性、MOS有效电平、ADC量程与定时器未确定时不READY。`mcu_board_set_alarm`只控制告警，不驱动锁、不改账本。
- 库基线为用户提供的HAL V1.8.5，原包不改；HAL的USE_RTOS保持0。FreeRTOS与HAL时基/异常入口的集成是阶段2任务，不改变公共C或UART v1。
- `locker_local_maintenance_reconcile` 对应私有maintenance.reconcile，只接受允许的本机管理员peer和当前ADMIN会话，比较epoch/boot/physical_revision/snapshot_revision，并保留原因。
- 该维护仅在无有效操作、当前闭门/物品与现有借用账本相符、日志健康时解除允许解除的UNCOMMANDED_ACCESS等hold，重新做CAS放行；不发锁脉冲，不自动格式化，不清未决操作/损坏记录/历史缺口。硬件故障必须先维修并留证。
- 维护不是普通用户确认的别名，不出现在公共HTTP路由；具体GPIO映射也不通过手机写入。

1. 单元测试使用端口替身，不绕过 Core 写 SQLite 或直接调用 GPIO。
2. 事务失败后设备端口调用次数必须为 0；EXECUTE 响应丢失后只能看到 QUERY_COMMAND，不能看到第二次 EXECUTE。
3. 异步 token 不重用，回执过期/乱序不能唤醒另一个动作；收到旧 session 回执不恢复新动作资格。
4. 补传、重复事件与历史卡片不会生成柜前证明；WS 丢失通过 GET 重查恢复。
5. 新 session 先快照屏障、补日志、核对、CAS 放行；有未决上下文先核验并释放，不直接开始下一次借还。
6. 本阶段头文件仅声明；板型、引脚、Flash 几何、锁允许脉冲和 ADC 标定由后续硬件核对决定。

## MCU异步结果的冻结接线

Control启动前绑定mcu_control_sink_t.reply。submit_unlock/make_snapshot/release_context带mcu_request_context_t，复制原session/request_no/type及接收单调时间，持久回执或屏障完成后才按原关联发响应。Control→Communication只排发送队列，不等待UART；回调参数借用且必须复制。原native响应DTO的host时间字段在MCU为0，不作为线协议字段。Journal另以event_committed回调交付已提交且已分配完整事件键的事实；committed仍只回token/status给Control。固件装配owner持有静态bus上下文，两个sink在创建任务前绑定，不跨模块用“最近request_no”全局变量接线。具体签名以0.2.1生成头为准。

## Journal管理操作与回传

mcu_journal_control采用同一Storage有界队列，操作编号RECOVER=0、BARRIER=1、FINALIZE=2、RELEASE=3；request包含token/barrier_after_token、epoch/session/operation/command/boot/action与terminal_state，不适用字段置0。请求/状态都是具体原生结构，禁止跨任务读取opaque Journal内部指针。

RECOVER返回已提交epoch/boot、动作水位、首尾/ACK游标、活动意图/结果、日志状态/连续性与丢失计数；journal_init成功创建本次启动检查点后才返回，失败不READY。BARRIER由Control在所有此前观察事件append之后排入同一FIFO，barrier_after_token标识最后观察提交；Storage确认该边界持久后返回一致状态，Control才组合冻结物理快照并回原request_no。未提交/失败不能造快照。

FINALIZE仅持久化该活动command已明确未驱动/完成等终结状态，不能反悔已接纳水位；无法确认则UNKNOWN_RESULT。RELEASE只在Control已验证闭门/无脉冲/原guard与关联后提交清空活动检查点，保留水位和日志。管理回执control_result(token,status,state)仅借用state，复制入Control队列再调用on_journal_result；普通committed回执的state参数传NULL。失败不发成功响应/不清活动状态。token在全部回执收敛前不重用，不按单个全局变量关联。

S2-04实现这四操作与断电/失败测试，S2-05绑定sink与Control处理，S2-08验证恢复元数据、屏障和持久释放后才响应。Journal/Control/Communication上下文存活到任务全部停止，不靠将同名JSON/结构裸发串口实现。
