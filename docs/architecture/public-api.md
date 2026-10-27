# 公共 API 速查

生成的阶段 0 评审稿；真源是 `contracts/contract-source.json`，不要手改。

基地址：`https://<终端地址>:8443/api/v1`。App 与本机 UI 调用相同领域核心；本机存在证明不注册到公共 HTTP。

## 路由清单

| 方法与路径 | 权限 | 请求模型 | 成功结果 | 幂等 | 首实现卡 |
|---|---|---|---|---|---|
| `POST /sessions` | 无需登录 | LoginRequest | 200 Session | 查询/接口自身语义 | S1-05 |
| `GET /sessions/current` | USER/ADMIN | 无正文 | 200 User | 查询/接口自身语义 | S1-05 |
| `DELETE /sessions/current` | 无需登录 | 无正文 | 204 无正文 | 查询/接口自身语义 | S1-05 |
| `GET /device` | USER/ADMIN | 无正文 | 200 Device | 查询/接口自身语义 | S1-05 |
| `GET /snapshot` | USER/ADMIN | 无正文 | 200 Snapshot | 查询/接口自身语义 | S1-05 |
| `GET /tools` | USER/ADMIN | 无正文 | 200 ToolList | 查询/接口自身语义 | S1-05 |
| `GET /tools/{tool_id}` | USER/ADMIN | 无正文 | 200 Tool | 查询/接口自身语义 | S1-05 |
| `POST /operations` | USER/ADMIN | CreateOperationRequest | 201 Operation | 必须同键重试 | S1-05 |
| `GET /operations/{operation_id}` | OWNER/ADMIN | 无正文 | 200 Operation | 查询/接口自身语义 | S1-05 |
| `POST /operations/{operation_id}/confirm` | OWNER | RevisionRequest | 200 Operation | 必须同键重试 | S1-05 |
| `POST /operations/{operation_id}/cancel` | OWNER | CancelRequest | 200 Operation | 必须同键重试 | S1-05 |
| `GET /loans` | USER/ADMIN | 无正文 | 200 LoanPage | 查询/接口自身语义 | S1-05 |
| `GET /events` | USER/ADMIN | 无正文 | 200 EventPage | 查询/接口自身语义 | S1-05 |
| `GET /reviews` | ADMIN | 无正文 | 200 ReviewList | 查询/接口自身语义 | S1-05 |
| `POST /operations/{operation_id}/resolve` | ADMIN | ResolveRequest | 200 Operation | 必须同键重试 | S1-05 |
| `GET /users` | ADMIN | 无正文 | 200 UserList | 查询/接口自身语义 | S1-05 |
| `POST /users` | ADMIN | CreateUserRequest | 201 User | 必须同键重试 | S1-05 |
| `PATCH /users/{user_id}` | ADMIN | PatchUserRequest | 200 User | 必须同键重试 | S1-05 |
| `GET /card-observations` | ADMIN | 无正文 | 200 CardObservationList | 查询/接口自身语义 | S1-05 |
| `PUT /users/{user_id}/card` | ADMIN | BindCardRequest | 200 CardBinding | 必须同键重试 | S1-05 |
| `DELETE /users/{user_id}/card` | ADMIN | 无正文 | 200 CardBinding | 必须同键重试 | S1-05 |
| `GET /ws` | USER/ADMIN | 无正文 | 101 WS 提示 | 查询/接口自身语义 | S1-05 |
| `GET /vision/status` | USER/ADMIN | 无正文 | 200 VisionStatus | 查询/接口自身语义 | S2-14 |
| `GET /operations/{operation_id}/vision` | OWNER/ADMIN | 无正文 | 200 VisionEvidencePage | 查询/接口自身语义 | S2-14 |

OWNER 是操作原用户，不是客户端自报身份；ADMIN 只能在明确标注的接口核验，不能伪装本人确认。

## 通用输入与响应

- 已登录请求使用 Authorization: Bearer；会话不放 URL。路径 UUID 为小写标准文本。
- 有副作用的业务请求需 Idempotency-Key UUID；同用户/方法/路径/键绑定规范化正文和查询参数，与业务事务一起保存 30 天。
- operation_revision、tool_revision、user_revision 分别比较对应资源版本；snapshot_revision 是全局快照版本，不能混用。
- JSON 的 u64 是十进制字符串并检查完整 uint64 上界，禁止经浮点。未知供电/物理状态显式为 null/UNKNOWN。
- 成功：`{data, snapshot_revision, request_id}`；错误：`{error:{code,message,retryable,operation_id},request_id}`。
- Core 排队不直接返回成功；最多等 1000ms，结果未到返回 RESULT_PENDING。超时不会撤销命令，客户端复用原幂等键。
- HTTP 201 只表示申请提交；ACCEPTED ACK 只表示 MCU 动作意图提交；门打开和借还成功分别依事实判定。
- POST 创建资源返回 Location；列表分页默认 20、最大 100，游标不透明，不允许客户端构造 SQL。

## 关键模型字段

| 模型 | 字段/限制 |
|---|---|
| CreateOperationRequest | tool_id、kind、tool_revision；没有 actor_id、通电时长、成功状态 |
| Operation | id、tool_id、kind、actor_id、state、operation_revision、command_id、reason_code、allowed_actions、evidence、时间 |
| Evidence | 完整历史、开门见证、物品变化见证、当前物理快照、事件键、人工核验标记 |
| ResolveRequest | operation_revision、physical_revision、resolution、至少5字节原因；原用户与类型不可换 |
| PhysicalSnapshot | device_id、journal_epoch、boot、physical_revision、门/物品/供电、观察年龄、连续性与dropped_events、持久游标 |
| CardObservation | 短期 observation_id、脱敏UID、年龄、是否已消费；绑定只引用观测ID |

完整字段、必填项和长度以 [OpenAPI](../../contracts/openapi.json) 为准。

## 错误码

| code | HTTP | retryable |
|---|---:|---|
| VALIDATION_ERROR | 400 | false |
| UNAUTHENTICATED | 401 | false |
| FORBIDDEN | 403 | false |
| NOT_FOUND | 404 | false |
| SLOT_BUSY | 409 | false |
| STATE_CONFLICT | 409 | false |
| REVISION_CONFLICT | 409 | false |
| IDEMPOTENCY_CONFLICT | 409 | false |
| CARD_ALREADY_BOUND | 409 | false |
| PHYSICAL_PRECONDITION_FAILED | 422 | false |
| RATE_LIMITED | 429 | true |
| DEVICE_UNAVAILABLE | 503 | true |
| RECOVERY_REQUIRED | 503 | false |
| STORAGE_UNAVAILABLE | 503 | false |
| QUEUE_FULL | 503 | true |
| RESULT_PENDING | 503 | true |
| VISION_UNAVAILABLE | 503 | true |
| VISION_CONFLICT | 409 | false |

retryable 只允许规范中的查询/复用原幂等键，绝不表示可再次发送开锁。

## WS 与私有入口

WS 只推 resource.changed 与 resync_required，客户端重查权威快照；无动作命令、无可靠历史回放。
建立 WS 后先处理 resync，再 GET snapshot；重连、队列溢出和服务实例变化一律重查。前台10秒刷新兜底。

私有 IPC 方法不属于上述网络路由：

- `presence.submit`：trusted UI peer UID + current user session + operation owner。
- `progress.report`：trusted registered service/UI instance。
- `ui.restart.request`：trusted service peer; UI fixed target; rate limit。
- `maintenance.reconcile`：trusted local admin CLI peer + current ADMIN session + verified physical baseline。
- `health.get`：trusted registered service peer instance; supervisor socket only; read-only。

API 样例与16项故障走查在 [examples](../../contracts/examples/acceptance-scenarios.json)；这里只定义合同，不声明已运行。
