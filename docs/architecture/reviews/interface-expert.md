# 软件与接口专家评审：一个柜格的借还契约

日期：2026-10-20。依据为现有 README、项目计划、架构专家评审及文末官方资料。本文件是阶段 0 的建议输入，不是已实现功能；最终由主规格统一冻结命名、字段和错误码，开发 AI 不同时把评审建议当作另一套规格。

## 1. 三条所有权规则

1. `locker-service` 的业务线程是借还状态、借用账本、SQLite 写入和业务事件序列的唯一写者。Android、LVGL、HTTP 工作线程与 UART 线程只能提交命令或事实，不能各自完成借还。
2. STM32 控制任务拥有锁 GPIO 与物理状态；存储任务拥有 SPI/littlefs；通信任务解析/发送，健康任务观察进度。接收到命令、日志已提交、锁脉冲启动、门打开、工具取走、借出完成分别是不同事实。
3. supervisor 只恢复固定进程，不控制锁、不写借还数据库。恢复应用、恢复服务和重发开锁没有隐含关联。

业务资源至少分开建模：`Tool/Custody`（可借、谁持有）、`Operation`（一次借/还）、`PhysicalSnapshot`（门/物品的观测）、`DeviceHealth`（通信/日志/监护）。断线时不能把工具自动改成可借；未知不能用 false 代替。

## 2. 标识、版本与幂等

| 字段 | 创建者与范围 | 语义与持久化规则 |
|---|---|---|
| `request_id` | HTTP 客户端，每次网络请求 | 排障关联；不承担幂等，不是业务操作 ID |
| `Idempotency-Key` | App/UI，每个有副作用的用户意图 | UUID；同一用户+方法+规范化路径下，同键同正文返回原结果；同键不同正文返回冲突。传输重试必须复用，不能每次点击生成新键 |
| `operation_id` | RK，UUID | 一次借出或归还；从申请至完成/核验闭环保持不变；一柜格最多一个非终结操作 |
| `command_id` | RK，UUID | 一次锁脉冲的意图；首版一个 operation 最多一个 command；查询/记录不能生成新的开锁 ID |
| `device_id` | 板级配置 | MCU 芯片 UID 的 24 位小写十六进制文本；不当密码使用 |
| `boot_id` | MCU，持久化 u64 启动计数 | 每次启动先可靠递增再开放业务；计数损坏或无法提交时禁新开锁 |
| `event_seq` | MCU，在 boot 内递增 u64 | 唯一设备事实键为 `(device_id, boot_id, event_seq)`；重复导入不得再次推进业务 |
| `session_id` | RK，每次串口握手，UUID | 绑定一次连接与双方实例；重连作废旧会话，旧会话开锁帧无条件拒绝 |
| `action_no` | RK，在 session 内从 1 递增 u64 | 单柜同一时间一条动作；MCU 持久化已接受水位与对应 command。低于水位的动作不执行，水位相同只返回既有状态，跳号拒绝 |
| `revision` | RK，SQLite 全局提交版本 u64 | 业务快照变化后递增；API JSON 以十进制字符串表达 u64，避免跨语言整数精度差异 |
| `host_instance_id/ui_instance_id` | 各进程每次启动，UUID | 监护会话标识，与 operation、串口 session 均不同 |

对业务命令的幂等记录与状态变更同一 SQLite 事务提交。保留期首版固定 30 天；已存在 operation 的动作仍受状态机约束，不依赖响应缓存防止重复开锁。提交动作意图和 outbox 也必须在同一事务内完成。事务提交成功之前不能返回已接受。

网络或串口超时表示结果未知，不表示取消成功。首版**不自动重试有副作用的串口 EXECUTE**；发送一次后通过 QUERY_COMMAND 和事件核对。MCU 的水位仍是必要的第二道防重复措施。重新建会话后只查询/补取旧动作，不把它迁移到新会话重发。

对有限存储的处理：可回收历史事件明细，但当前会话动作水位、未决动作、未获 RK 落库确认的事实不能回收。过期动作查不到明细返回 `UNKNOWN_RESULT`，绝不视作“未执行，可重试”。这个设计只保证不自动重复驱动，不宣称物理动作 exactly once。

## 3. 借还状态机与人机确认

### 3.1 前置条件

- 借出：工具账本可借、当前闭门且物品在位、设备 READY、无有效操作。
- 归还：工具借用人是当前用户、当前闭门且物品离位、设备 READY、无有效操作。
- 申请成功即占用该柜格。手机申请只产生 `REQUESTED`，不直接开锁。
- 到柜证明采用两种入口：绑定卡片被 MCU 读到；或借用者在本机触摸屏登录后点击到柜确认。本机确认只接受来自受限 Unix socket 的 UI 通道，公共 HTTPS 不提供可伪造 `source=local` 的字段。
- 卡片事件只在该用户已有 REQUESTED 操作时匹配；未绑定卡、其他人的卡、无申请刷卡都不开锁。UID 只是可复制的原型凭据，不能宣称安全身份认证。
- 到柜证明只绑定一个 operation，有效 15 秒；RK 消费证明并检查物理快照新鲜度后提交一次动作。卡片事件只有来自当前 READY 会话、实时到达且未被消费时可以生成证明；历史补取、重复事件和旧会话事件只补事实，绝不生成开锁或恢复命令。首版按“发起/取消/确认/核验”分别留审计记录。

### 3.2 最小操作状态

| 状态 | 进入条件 | 确定出口 |
|---|---|---|
| `REQUESTED` | 申请及基线快照已提交 | 到柜证明有效→ACCESS_PENDING；取消/120 秒无到柜确认→CANCELLED |
| `ACCESS_PENDING` | command/outbox 意图已提交；等待 MCU 明确结果 | ACK 仅保持本状态；PULSE_STARTED→ACCESS_ACTIVE；已知拒绝且无动作→FAILED；结果未知→REVIEW_REQUIRED |
| `ACCESS_ACTIVE` | 已有 PULSE_STARTED 事实；分别跟踪门开过、物品变化 | 门已开过再关、目标物品变化成立→WAIT_CONFIRM；完整开关门且物品未变→NO_CHANGE；冲突/超时→REVIEW_REQUIRED |
| `WAIT_CONFIRM` | 门已关闭，借出为在位→离位，归还为离位→在位，事件无缺口 | 本人确认且新鲜快照仍符合→COMPLETED；120 秒未确认/物理状态再变化→REVIEW_REQUIRED |
| `REVIEW_REQUIRED` | 结果未知、状态冲突、已执行后取消、日志缺口或中断 | 保持占位，管理员带原因核验后→COMPLETED 或 NO_CHANGE |
| `COMPLETED` | 物理证据和本人确认同事务核对通过，或管理员有依据核验 | 终结；借出创建有效借用，归还关闭该借用 |
| `CANCELLED` | REQUESTED 阶段取消或过期，尚未提交开锁意图 | 终结，账本不变 |
| `NO_CHANGE` | 有明确证据确认未发生借还，或核验恢复到基线 | 终结，账本不变 |
| `FAILED` | MCU 明确拒绝且证据表明未启动驱动 | 终结，账本不变；若底层健康异常仍禁止新操作 |

`ACK_ACCEPTED` 只表示动作已持久化接纳，不进入“门已打开”。没有锁舌反馈，事件名称只能是 `PULSE_STARTED/PULSE_FINISHED/DOOR_OPENED/DOOR_CLOSED`，不提供 `LOCK_OPEN_SUCCESS`。

默认业务超时：锁驱动后 30 秒未见开门、开门后 120 秒未见关门、等待本人确认 120 秒，均进入 REVIEW_REQUIRED。定时以单调时钟计算。锁脉冲时间不是这些业务超时，必须根据锁规格在硬件阶段确定，且有 MCU 本地硬上限。

本人确认只允许 WAIT_CONFIRM；提早确认返回 `STATE_CONFLICT`，不能预先存一个会在未来自动完成的确认。基线与目标仅靠当前传感器状态不够，还必须看到同一操作时间窗内开门和物品变化事实。门未打开而物品变化、物品未知、门传感器未知均须核验。

取消语义：REQUESTED 可直接取消；从 ACCESS_PENDING 开始取消只能请求终止业务并进入 REVIEW_REQUIRED，不能声称已撤回 MCU 动作。MCU 已接纳的锁脉冲只按本地短时限结束，不能依赖取消包关断。开门不取物然后正常关门为 NO_CHANGE；取物后未确认为 REVIEW_REQUIRED，不自动释放柜格。

RK 服务重启或 MCU 重启：所有 REQUESTED 在确认没有动作意图时取消；所有可能已驱动的未完成操作先转 REVIEW_REQUIRED，补取与快照核对后由管理员处理。UI 重启不改变业务状态。终结状态与有效借用在 SQLite 恢复后保留。

### 3.3 核验边界

管理员 `resolve` 必须包含 `resolution`、非空原因及快照 revision。允许 `COMPLETE_BORROW/COMPLETE_RETURN/NO_CHANGE`，目标必须与原 operation 类型和原借用人匹配，不能借此把工具无痕转给第三人。要求新鲜快照、门关闭、目标物品状态明确；历史证据缺失可以通过现场检查解释，但原始缺口与人工处理标识永久保留。传感器仍未知时先维修，首版不提供绕过门状态的强制成功。

## 4. 公共 HTTPS/WS 资源

公共端口只接受 TLS。会话认证是 `Authorization: Bearer <token>`；服务端决定用户/角色，不接收客户端指定 actor_id。局域网不等于免鉴权。TLS 配对和账号初始化由安全规格另行冻结，接口 AI 不得用 accept-all-certificate 替代。

### 4.1 资源表

| 方法与路径 | 权限 | 输入/结果 |
|---|---|---|
| `GET /api/v1/device` | 已登录 | 设备身份、版本、能力、是否模拟、健康，不返回证书私钥/配置密码 |
| `POST /api/v1/sessions` | 未登录 | username/password→token、过期时间、user；限速，禁止记录密码/token |
| `DELETE /api/v1/sessions/current` | 已登录 | 撤销当前会话；重复退出仍返回无会话 |
| `GET /api/v1/snapshot` | 已登录 | 一致快照 revision、工具、本人操作/借用、设备健康；管理员可见全部当前状态 |
| `GET /api/v1/tools`、`GET /api/v1/tools/{id}` | 已登录 | 首版一件工具，不硬编码客户端只有一个 ID |
| `POST /api/v1/operations` | 用户/管理员 | `{tool_id, kind:BORROW|RETURN}`；201 + operation；返回 Location |
| `GET /api/v1/operations/{id}` | 本人/管理员 | 状态、证据摘要、允许的下一步，不返回其他用户敏感信息 |
| `POST /api/v1/operations/{id}/confirm` | 本人 | `operation_revision`；只在 WAIT_CONFIRM 完成本人确认 |
| `POST /api/v1/operations/{id}/cancel` | 本人 | `operation_revision,reason`；返回最终或待核验状态 |
| `GET /api/v1/loans?cursor=&limit=` | 本人/管理员 | 按权限分页历史；limit 默认 20，最大 100，稳定游标排序 |
| `GET /api/v1/events?operation_id=&cursor=&limit=` | 本人/管理员 | 本人业务时间线；管理员可见底层/设备事件及证据缺口 |
| `GET /api/v1/reviews` | 管理员 | 待核验列表 |
| `POST /api/v1/operations/{id}/resolve` | 管理员 | `operation_revision,resolution,reason`；返回被审计的新状态 |
| `GET/POST /api/v1/users` | 管理员 | 用户列表/创建；不回传 password_hash |
| `PATCH /api/v1/users/{id}` | 管理员 | enabled、role、显示名；停用不会抹去未归还记录；禁止停用最后一个管理员 |
| `PUT/DELETE /api/v1/users/{id}/card` | 管理员 | 绑定/解绑已采集 UID；UID 唯一；不能无授权认领别人的卡 |
| `GET /api/v1/ws` | 已登录 | 认证后只推变化提示；无远程开锁、GPIO 或 shell 命令 |

凡有业务副作用的 POST/PATCH 均要求幂等键；认证登录例外。PUT/DELETE 自身也必须保持相同效果，不因重复调用制造新绑定。开发调试 API 不能出现在生产公共路由中。

本机 UI 经 `/run/locker/service.sock` 调用相同路由与领域命令；Unix socket 权限限定 UI 和 service 用户，并由 `SO_PEERCRED` 核验连接进程 UID，仍需登录获得当前借用者身份。仅本机增加 `POST /local/v1/operations/{id}/presence`。HTTP 监听器没有此路由。读卡事件由串口适配器走相同 `PRESENT_LOCAL` 命令分派，并由服务端匹配用户。

### 4.2 固定最小响应形态

成功返回 `{data,snapshot_revision,request_id}`。operation 至少包含：`id,tool_id,kind,actor_id,state,operation_revision,command_id|null,reason_code|null,created_at,updated_at,allowed_actions,evidence`。证据包含 `door_open_seen,item_transition_seen,door,item,observed_at,source_event_keys,history_complete`；`observed_at` 来自同步映射，可信度另列，不能伪造离线期间精确墙钟。

错误返回 `{error:{code,message,retryable,operation_id|null},request_id}`；message 面向人，业务依据 code，不解析中文消息。客户端带旧 operation_revision 时返回 409 `REVISION_CONFLICT`，先重查；operation_revision 是该 operation 版本，snapshot_revision 是全局快照版本，不互相代替。

| HTTP | code 类别 | 含义 |
|---|---|---|
| 400 | `VALIDATION_ERROR` | 字段缺失、枚举/长度不合法 |
| 401/403 | `UNAUTHENTICATED/FORBIDDEN` | 无会话/无权限，不透露别人的操作细节 |
| 404 | `NOT_FOUND` | 资源不存在或当前用户不可见 |
| 409 | `SLOT_BUSY/STATE_CONFLICT/REVISION_CONFLICT/IDEMPOTENCY_CONFLICT` | 当前状态冲突；不能通过自动换幂等键绕过 |
| 422 | `PHYSICAL_PRECONDITION_FAILED` | 闭门/物品/凭据条件不满足，提供可理解的条件说明 |
| 429 | `RATE_LIMITED` | 限速，按 Retry-After 重试查询，不重放动作 |
| 503 | `DEVICE_UNAVAILABLE/RECOVERY_REQUIRED/STORAGE_UNAVAILABLE/QUEUE_FULL` | 尚未接受新操作；如已提交副作用意图，必须返回关联 operation 并允许查结果 |

取消、确认或核验返回的是业务资源，不把 200 当成“已经借出”。201 表示 operation 申请已落库；串口动作是异步事实。不要用 HTTP 504 抹掉已经提交的 operation 身份。

### 4.3 WebSocket 最小化

WS 仅发 `{type:"resource.changed",resource,id,snapshot_revision}` 或 `{type:"resync_required"}`。连接成功先发 resync_required；客户端建立连接后再 GET snapshot，处理中途提示并按 revision 重查，防止“先取快照、后订阅”漏掉变化。WS 不承担持久事件回放；原始历史走 HTTP 分页。重连、队列溢出、服务实例变化一律重查快照；离线禁止动作，缓存必须标记过期。

WS 缓冲有界，慢客户端断开，业务线程不等待网络写。每 10 秒查询一次快照作为提示丢失的保底，不用心跳线程为真实业务伪造进度。

## 5. C 内部端口与异步语义

下表是逻辑接口签名，不要求现在编译或写实现。类型由统一契约生成/定义，避免三个 AI 维护三套 ID、枚举和错误码。

| 端口 | 唯一所有者 | 接口与完成语义 |
|---|---|---|
| Domain | 业务线程 | `submit(principal, request_key, command) -> receipt`；`apply_device_event(event)`；`tick(monotonic_now)`；`query(scope) -> immutable_snapshot`。receipt 为接受/拒绝，异步状态从查询/订阅取得 |
| Repository | 业务线程 | `begin/commit/rollback`；`load_operation`；`save_operation_and_outbox_and_idempotency`；`import_device_event_if_absent`；`commit_cursor`。不暴露 SQLite 连接给 UI/回调 |
| DevicePort | 设备适配器 | `connect`；`get_snapshot`；`execute_unlock_once`；`query_command`；`fetch_events`；`ack_persisted`；`send_health`；`set_time_anchor`。模拟器实现同一端口并报告 simulated=true |
| EventPublisher | 网络适配器 | `try_publish(change)`；满队列断开慢客户端，不阻塞领域线程 |
| Clock/ID | 平台适配器 | `monotonic_ms/wall_time/new_uuid`；业务超时不依赖墙钟跳变 |
| SupervisorPort | supervisor | 固定 `register_instance/report_progress/request_ui_restart/query_restart_status`；受限 Unix socket；无任意程序路径和 shell |
| ControlPort | MCU 控制任务 | `submit_unlock`；`observe_inputs`；`on_journal_commit`；`on_pulse_finished`；`make_snapshot`。只有此模块驱动锁 |
| JournalPort | MCU 存储任务 | `append_async(record,token)`；`commit_result(token,status)`；`fetch_range(cursor,max)`；`ack_persisted(cursor)`。排队成功不等于持久化成功 |
| BoardPort | BSP | `read_door/read_item/read_supply`；`start_bounded_pulse/force_output_off`；有限步 `spi_transfer`；`monotonic_ms/reset_cause`。状态可为 UNKNOWN |
| HealthPort | MCU 健康任务 | 各任务真实进度与主机/UI 观察；关键进度均有效才喂狗；请求恢复只是事件，不直接执行 Linux 命令 |

有界消息按值复制，不能把 HTTP 回调栈指针送到异步队列。较大帧采用固定缓冲池索引，所有权从生产者移给消费者，消费后归还；队列满返回明确错误，不无限等待。业务线程不持有 SPI 锁、不等待开门；存储任务不写借还账本。

## 6. 串口契约建议

### 6.1 帧与版本

115200 8N1；所有多字节整数 little-endian，UUID 在线路上为 16 字节，不对 C struct 做裸 memcpy。建议定长头 `magic:u16(0x4C4B),version:u8(1),type:u8,payload_len:u16,request_no:u32`，其后 payload 与 CRC16/CCITT-FALSE（poly 0x1021/init 0xFFFF/refin=false/refout=false/xorout=0）；CRC 覆盖 version 至 payload 末尾；测试向量 ASCII `123456789`→0x29B1。最大 payload 512 字节。

request_no 只关联一次请求/响应，不承担动作幂等。控制消息包含 session_id；事件携带 device_id/boot_id/event_seq，允许查询历史启动段。解析器在长度非法/CRC 错误时重新找帧头；不分配由攻击性长度决定的内存；不假设一次 UART DMA 或 read 就是一帧。

### 6.2 消息与职责

| 类型 | 方向 | 最小字段与结果 |
|---|---|---|
| `HELLO/HELLO_REPLY` | RK↔MCU | supported_version、host_instance、new_session；reply 为设备/启动/能力/固件版本/日志边界/健康；协商失败禁动作 |
| `GET_SNAPSHOT/SNAPSHOT` | RK↔MCU | 门/物品/供电状态、活动 command、动作水位、控制任务观察时刻、已提交事件边界。快照须等此前必要事件提交，提交失败返回不可核对 |
| `FETCH_EVENTS/EVENT_BATCH` | RK↔MCU | after_cursor 与 limit≤8；返回连续范围、first_available、last_committed、has_more、gap；跨 boot 通过日志游标明确转段 |
| `ACK_PERSISTED` | RK→MCU | RK 已将事件与游标同事务提交的连续水位；不能确认尚未落库或有缺口的数据 |
| `RESUME_READY` | RK→MCU | 指定已核对快照边界、operation/command 摘要；前置条件仍有效才开放新动作，否则要求再次核对 |
| `EXECUTE_UNLOCK/COMMAND_STATUS` | RK↔MCU | session、action_no、command_id、operation_id、slot、kind；MCU 校验当前模式、物理基线、日志能力和水位，提交意图后才 ACK_ACCEPTED；脉冲参数来自本地已核准配置，不让手机任意设时长 |
| `QUERY_COMMAND/COMMAND_STATUS` | RK↔MCU | 按 command_id 查 ACCEPTED、PULSE_STARTED、PULSE_FINISHED、REJECTED、UNKNOWN_RESULT；返回状态不触发执行 |
| `DEVICE_EVENT` | MCU→RK | boot、seq、mono_ms、type、operation_id/command_id 可空、物理观测与原因；实时推送失败仍保留可补取日志 |
| `HOST_HEALTH` | RK→MCU | host/ui 实例、真实循环进度、观察年龄、supervisor 状态；每秒发送，不能中间层代增 |
| `TIME_ANCHOR` | RK→MCU | host UTC、MCU 单调时间关联请求与往返估计；事件原始单调时间永不覆盖 |

命令状态拒绝码最少为 `BAD_VERSION/BAD_SESSION/NOT_READY/BAD_PRECONDITION/REPLAY_BLOCKED/ACTION_GAP/BUSY/LOG_UNAVAILABLE/UNKNOWN_RESULT`。已持久化接纳后不得再以“从未接受”回答。串口 CRC 防误码，不是防恶意端口接入的认证。

实时事件可能重复，RK 以唯一键落库；当前观察快照不能被较老事件覆盖。业务有序推进依赖已补齐的连续序列，不按串口到达顺序猜事件先后。缺口触发补取；first_available 已越过缺口表示证据已丢，相关操作进入核验。

### 6.3 重连放行

断线后先关闭新动作入口，建立新 session，取得带提交边界的快照，补到该边界，核对未完成 operation，再 RESUME_READY。期间后来的事件继续导入，不用旧快照覆盖新状态。快照边界之后发生门/物品变化时，MCU 必须在 RESUME_READY 检查当前输入和活动状态；变化则拒绝放行重新核对。

## 7. 样例时序与必须走查的失败点

### 正常借出

1. App 使用幂等键 K 申请 BORROW；RK 创建 O 与基线，返回 REQUESTED。
2. 借用者到柜刷卡；RK 验证卡、operation、时效与闭门/在位，事务写 C/outbox，进入 ACCESS_PENDING。
3. UART 发送 EXECUTE(O,C,session,action_no) 一次。MCU 提交 command 意图及水位，再 ACK_ACCEPTED；此时 App 仍只显示“请求已接受，等待开门”。
4. MCU 启动脉冲并记录 PULSE_STARTED；随后门开、物品离位、门关事件依次进入日志。RK 去重导入，进入 WAIT_CONFIRM。
5. 本人确认，RK 核对新鲜闭门/离位、完整事件序列，事务创建 loan 并完成 O。WS 只提示查询新版快照。

### 响应丢失与重复

- 第 1 步 HTTP 响应丢失：同 K 重试，返回同 O，不新增操作。
- 第 3 步 ACK 丢失：查询 C，不生成 C2，不自动再发送执行。即使重复执行帧到达，MCU 水位检查不重复脉冲。
- 门关事件先于缺失的门开事件抵达 RK：先缓存/补取缺口，不能依当前闭门直接宣告完成。
- RK 收到事件落库前崩溃：MCU 未获 ACK_PERSISTED，恢复后重复补取；SQLite 唯一键阻止重复业务影响。

### 重启与人工核验

- MCU 意图已提交、驱动前重启：恢复报告动作结果未知，不能因未找到 PULSE_STARTED 就再次执行。
- 实际已经取物、App 退出或超时：工具不变成可借，操作待核验；原借用人仍可查询，管理员有原因处理。
- RK 在关门后重启：旧会话无效；补取日志、核对实物，可能驱动过的未完操作不自动执行旧命令。
- Flash 满/写失败/已确认水位不连续：拒新开锁，仍保证脉冲关断和持续输入观察；不可自动格式化日志继续营业。

## 8. 阶段 0 应交付给后续 AI 的最小合同

建议主规格冻结：状态表与理由码、public OpenAPI、WS 提示 schema、线协议字段表及黄金帧、C 端口头文件合同、上述时序的 Given/When/Then 验收卡。只做声明和契约校验，不提前实现业务。

检查重点：所有 operation 资源同一词表；HTTP/UART/C 的 UUID 与 u64 映射一致；错误是否可能抹去已提交意图；取消是否谎称撤回物理动作；模拟器是否能注入重复、乱序、断线和重启。锁脉冲、传感器极性和 Flash 几何参数暂未实物确认，不能让接口 AI 自行写死“常见值”。

## 9. 官方来源与采用依据

- [RFC 9110：HTTP 语义](https://www.rfc-editor.org/rfc/rfc9110.html)：POST 不能仅凭方法名假定可安全重试；本文另外定义业务幂等键和查询结果流程，不宣称它们是 HTTP 默认行为。
- [SQLite Atomic Commit](https://www.sqlite.org/atomiccommit.html)：数据库事务提供数据库内原子性；物理柜锁不在同一原子事务中，必须留意图并核对动作结果。
- [FreeRTOS Queues](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/02-Queues-mutexes-and-semaphores/01-Queues)：队列按值复制，适用于任务间和中断到任务交接；大缓冲仍需项目自己明确所有权和长度。

官方资料用于依据，不构成真机性能、安全性或故障恢复已经通过的证明。
