# 接口实施归属与调用链追踪

生成的设计索引；修改config/development/interface-map.json后运行scripts/build_stage_docs.py。

契约0.2.1。所有卡目前以阶段状态索引为准；表格只分配职责，不宣称实现或测试通过。
完整函数签名见[C契约](../../contracts/include/locker_contracts.h)，请求/响应见[OpenAPI](../../contracts/openapi.json)。
[阶段入口](../stages/README.md) / [分阶段调用链](stage-call-chains.md) / [机器映射](../../config/development/interface-map.json)。

## 1. 公开 API

全部地址相对https://<终端局域网地址>:8443/api/v1；WS为同origin的wss。

| operationId | 路径 | 适配/核心 | 手机消费 | 验证卡 | 调用链 |
|---|---|---|---|---|---|
| `login` | `POST /sessions` | S1-05 / S1-04 | S3-02 | S1-07, S3-07 | F3 |
| `getCurrentSession` | `GET /sessions/current` | S1-05 / S1-04 | S3-02 | S1-07, S3-07 | F3 |
| `logout` | `DELETE /sessions/current` | S1-05 / S1-04 | S3-02 | S1-07, S3-07 | F3 |
| `getDevice` | `GET /device` | S1-05 / S1-04 | S3-02 | S1-07, S3-07 | F3 |
| `getSnapshot` | `GET /snapshot` | S1-05 / S1-04 | S3-02 | S1-07, S3-07 | F3 |
| `listTools` | `GET /tools` | S1-05 / S1-04 | S3-03 | S1-07, S3-07 | F3 |
| `getTool` | `GET /tools/{tool_id}` | S1-05 / S1-04 | S3-03 | S1-07, S3-07 | F3 |
| `createOperation` | `POST /operations` | S1-05 / S1-04 | S3-03 | S1-07, S3-07 | F1 |
| `getOperation` | `GET /operations/{operation_id}` | S1-05 / S1-04 | S3-03 | S1-07, S3-07 | F1 |
| `confirmOperation` | `POST /operations/{operation_id}/confirm` | S1-05 / S1-04 | S3-03 | S1-07, S3-07 | F1 |
| `cancelOperation` | `POST /operations/{operation_id}/cancel` | S1-05 / S1-04 | S3-03 | S1-07, S3-07 | F1 |
| `listLoans` | `GET /loans` | S1-05 / S1-04 | S3-03 | S1-07, S3-07 | F3 |
| `listEvents` | `GET /events` | S1-05 / S1-04 | S3-03 | S1-07, S3-07 | F3 |
| `listReviews` | `GET /reviews` | S1-05 / S1-04 | S3-04 | S1-07, S3-07 | F3 |
| `resolveOperation` | `POST /operations/{operation_id}/resolve` | S1-05 / S1-04 | S3-04 | S1-07, S3-07 | F3 |
| `listUsers` | `GET /users` | S1-05 / S1-04 | S3-04 | S1-07, S3-07 | F3 |
| `createUser` | `POST /users` | S1-05 / S1-04 | S3-04 | S1-07, S3-07 | F3 |
| `updateUser` | `PATCH /users/{user_id}` | S1-05 / S1-04 | S3-04 | S1-07, S3-07 | F3 |
| `listCardObservations` | `GET /card-observations` | S1-05 / S1-04 | S3-04 | S1-07, S3-07 | F3 |
| `bindCard` | `PUT /users/{user_id}/card` | S1-05 / S1-04 | S3-04 | S1-07, S3-07 | F3 |
| `unbindCard` | `DELETE /users/{user_id}/card` | S1-05 / S1-04 | S3-04 | S1-07, S3-07 | F3 |
| `subscribeChanges` | `GET /ws` | S1-05 / S1-04 | S3-04 | S1-07, S3-07 | F3 |
| `getVisionStatus` | `GET /vision/status` | S2-14 / S2-14 | S3-04 | S2-16, S3-07 | F6 |
| `getOperationVisionEvidence` | `GET /operations/{operation_id}/vision` | S2-14 / S2-14 | S3-03 | S2-16, S3-07 | F6 |

LVGL使用23个同名业务RPC（不含WS）及私有presence；显示范围由服务端权限决定。视觉新增入口在S2-14实现，S1不编造视觉结果。

## 2. 跨模块 C 端口

初次实现与后续扩展分别列出；后续卡复用初次实现，不另造同名兼容层。

| 函数 | 所有者 | 初次实现 | 后续扩展 | 首轮验证 | 调用链 |
|---|---|---|---|---|---|
| `locker_core_submit` | RK Core | S1-04 | — | S1-07 | F1 |
| `locker_core_wait_result` | RK Core completion port | S1-04 | — | S1-07 | F1 |
| `locker_core_apply_device_event` | RK Core | S1-04 | — | S1-07 | F1 |
| `locker_core_apply_device_reply` | RK Core | S1-04 | — | S1-07 | F1 |
| `locker_core_tick` | RK Core | S1-04 | — | S1-07 | F1 |
| `locker_core_query` | RK Core | S1-04 | — | S1-07 | F1 |
| `locker_repo_commit_change` | RK Storage | S1-02 | — | S1-07 | F1 |
| `locker_repo_import_events` | RK Storage | S1-02 | — | S1-07 | F1 |
| `locker_repo_load_recovery` | RK Storage | S1-02 | — | S1-07 | F1 |
| `locker_device_open` | RK Device | S1-03 | S2-06 | S1-07 | F1 |
| `locker_device_get_snapshot` | RK Device | S1-03 | S2-06 | S1-07 | F1 |
| `locker_device_execute_once` | RK Device | S1-03 | S2-06 | S1-07 | F1 |
| `locker_device_query_command` | RK Device | S1-03 | S2-06 | S1-07 | F1 |
| `locker_device_fetch_events` | RK Device | S1-03 | S2-06 | S1-07 | F1 |
| `locker_device_ack_persisted` | RK Device | S1-03 | S2-06 | S1-07 | F1 |
| `locker_device_resume_ready` | RK Device | S1-03 | S2-06 | S1-07 | F1 |
| `locker_device_release_context` | RK Device | S1-03 | S2-06 | S1-07 | F1 |
| `locker_device_send_health` | RK Device | S1-03 | S2-06 | S1-07 | F1 |
| `locker_events_try_publish` | RK Events | S1-05 | — | S1-07 | F1 |
| `locker_local_presence_submit` | RK Local IPC | S1-04 | — | S1-07 | F1 |
| `locker_supervisor_report_progress` | Supervisor | S4-04 | — | S4-08 | F4 |
| `locker_supervisor_request_ui_restart` | Supervisor | S4-05 | — | S4-08 | F4 |
| `mcu_control_submit_unlock` | MCU Control | S2-05 | — | S2-08 | F2 |
| `mcu_control_observe_inputs` | MCU Control | S2-05 | S4-02 | S2-08 | F2 |
| `mcu_control_on_journal_result` | MCU Control | S2-05 | — | S2-08 | F2 |
| `mcu_control_make_snapshot` | MCU Control | S2-05 | — | S2-08 | F2 |
| `mcu_control_release_context` | MCU Control | S2-05 | — | S2-08 | F2 |
| `mcu_journal_append` | MCU Journal | S2-04 | S4-01 | S2-08 | F2 |
| `mcu_journal_commit_intent` | MCU Journal | S2-04 | S4-01 | S2-08 | F2 |
| `mcu_journal_read_page` | MCU Journal | S2-04 | S4-01 | S2-08 | F2 |
| `mcu_journal_ack` | MCU Journal | S2-04 | S4-01 | S2-08 | F2 |
| `mcu_board_read_inputs` | MCU BSP | S2-02 | — | S2-08 | F2 |
| `mcu_board_start_bounded_pulse` | MCU BSP | S2-05 | — | S2-08 | F2 |
| `mcu_board_force_off` | MCU BSP | S2-05 | — | S2-08 | F2 |
| `mcu_board_spi_transfer` | MCU BSP | S2-02 | — | S2-08 | F2 |
| `mcu_health_report_progress` | MCU Health | S2-05 | S4-03 | S2-08 | F2 |
| `mcu_health_evaluate` | MCU Health | S2-05 | S4-03 | S2-08 | F2 |
| `locker_repo_open` | RK Storage | S1-02 | — | S1-07 | F1 |
| `locker_repo_query` | RK Storage | S1-02 | — | S1-07 | F1 |
| `locker_repo_close` | RK Storage | S1-02 | — | S1-07 | F1 |
| `locker_core_create` | RK Runtime/Core | S1-04 | — | S1-07 | F1 |
| `locker_core_start` | RK Core | S1-04 | — | S1-07 | F1 |
| `locker_core_stop` | RK Core | S1-04 | — | S1-07 | F1 |
| `locker_core_destroy` | RK Core | S1-04 | — | S1-07 | F1 |
| `locker_core_authenticate` | RK Auth/Core | S1-04 | — | S1-07 | F1 |
| `locker_device_create` | RK Device | S1-03 | S2-06 | S1-07 | F1 |
| `locker_device_close` | RK Device | S1-03 | S2-06 | S1-07 | F1 |
| `locker_device_destroy` | RK Device | S1-03 | S2-06 | S1-07 | F1 |
| `locker_device_time_anchor` | RK Device | S1-03 | S2-06 | S1-07 | F1 |
| `locker_events_create` | RK Events | S1-05 | — | S1-07 | F1 |
| `locker_events_destroy` | RK Events | S1-05 | — | S1-07 | F1 |
| `locker_supervisor_create` | Supervisor | S4-04 | — | S4-08 | F4 |
| `locker_supervisor_destroy` | Supervisor | S4-04 | — | S4-08 | F4 |
| `mcu_control_init` | MCU Control | S2-05 | — | S2-08 | F2 |
| `mcu_journal_init` | MCU Journal | S2-04 | S4-01 | S2-08 | F2 |
| `mcu_health_init` | MCU Health | S2-05 | S4-03 | S2-08 | F2 |
| `locker_device_bind_sink` | RK Device | S1-03 | S2-06 | S1-07 | F1 |
| `locker_http_start` | RK HTTP | S1-05 | — | S1-07 | F1 |
| `locker_http_stop` | RK HTTP | S1-05 | — | S1-07 | F1 |
| `locker_http_destroy` | RK HTTP | S1-05 | — | S1-07 | F1 |
| `locker_ipc_start` | RK IPC | S1-05 | — | S1-07 | F1 |
| `locker_ipc_stop` | RK IPC | S1-05 | — | S1-07 | F1 |
| `locker_ipc_destroy` | RK IPC | S1-05 | — | S1-07 | F1 |
| `mcu_journal_bind_sink` | MCU Journal | S2-04 | S4-01 | S2-08 | F2 |
| `mcu_board_apply_profile` | MCU HAL BSP | S2-01 | — | S2-08 | F2 |
| `mcu_board_set_alarm` | MCU HAL BSP | S2-02 | — | S2-08 | F2 |
| `locker_local_maintenance_reconcile` | RK Local Admin | S1-04 | — | S1-07 | F1 |
| `locker_supervisor_get_health` | Supervisor | S4-04 | — | S4-08 | F4 |
| `locker_core_apply_supervisor_health` | RK Core | S4-04 | — | S4-08 | F4 |
| `locker_core_get_progress` | RK Core | S1-04 | — | S1-07 | F4 |
| `locker_vision_create` | RK Vision | S2-13 | — | S2-16 | F6 |
| `locker_vision_bind_sink` | RK Vision | S2-13 | — | S2-16 | F6 |
| `locker_vision_open` | RK Vision | S2-13 | — | S2-16 | F6 |
| `locker_vision_submit` | RK Vision | S2-13 | — | S2-16 | F6 |
| `locker_vision_get_status` | RK Vision | S2-13 | — | S2-16 | F6 |
| `locker_vision_close` | RK Vision | S2-13 | — | S2-16 | F6 |
| `locker_vision_destroy` | RK Vision | S2-13 | — | S2-16 | F6 |
| `locker_core_bind_vision` | RK Core | S2-14 | — | S2-16 | F6 |
| `locker_core_apply_vision_result` | RK Core | S2-14 | — | S2-16 | F6 |
| `locker_core_apply_vision_status` | RK Core | S2-14 | — | S2-16 | F6 |
| `mcu_control_bind_sink` | MCU Control/Communication | S2-05 | — | S2-08 | F2 |
| `mcu_journal_control` | MCU Journal | S2-04 | S4-01 | S2-08 | F2 |

## 3. UART 请求

每类请求都区分主机模拟、MCU解析、MCU语义和真实线路；11类消息不是11个随意重试的动作。

| 消息 | 主机协议 | MCU解析 | MCU语义 | 真实线路 | 验证 | 链 |
|---|---|---|---|---|---|---|
| `HELLO` | S1-03 | S2-03 | S2-05 | S2-06 | S1-07, S2-08 | F2 |
| `GET_SNAPSHOT` | S1-03 | S2-03 | S2-05 | S2-06 | S1-07, S2-08 | F2 |
| `FETCH_EVENTS` | S1-03 | S2-03 | S2-04 | S2-06 | S1-07, S2-08 | F2 |
| `ACK_PERSISTED` | S1-03 | S2-03 | S2-04 | S2-06 | S1-07, S2-08 | F2 |
| `RESUME_READY` | S1-03 | S2-03 | S2-05 | S2-06 | S1-07, S2-08 | F2 |
| `DISARM` | S1-03 | S2-03 | S2-05 | S2-06 | S1-07, S2-08 | F2 |
| `EXECUTE_UNLOCK` | S1-03 | S2-03 | S2-05 | S2-06 | S1-07, S2-08 | F2 |
| `QUERY_COMMAND` | S1-03 | S2-03 | S2-05 | S2-06 | S1-07, S2-08 | F2 |
| `HOST_HEALTH` | S1-03 | S2-03 | S2-05 | S2-06 | S1-07, S2-08 | F4 |
| `TIME_ANCHOR` | S1-03 | S2-03 | S2-05 | S2-06 | S1-07, S2-08 | F2 |
| `RELEASE_CONTEXT` | S1-03 | S2-03 | S2-05 | S2-06 | S1-07, S2-08 | F2 |

## 4. 私有 RPC

SO_PEERCRED/部署权限与当前用户会话各按方法验证；这些不是手机URL。

| method | socket | 实现 | 验证 | 调用链 |
|---|---|---|---|---|
| `login` | service.sock | S1-05 | S1-07 | F3 |
| `getCurrentSession` | service.sock | S1-05 | S1-07 | F3 |
| `logout` | service.sock | S1-05 | S1-07 | F3 |
| `getDevice` | service.sock | S1-05 | S1-07 | F3 |
| `getSnapshot` | service.sock | S1-05 | S1-07 | F3 |
| `listTools` | service.sock | S1-05 | S1-07 | F3 |
| `getTool` | service.sock | S1-05 | S1-07 | F3 |
| `createOperation` | service.sock | S1-05 | S1-07 | F3 |
| `getOperation` | service.sock | S1-05 | S1-07 | F3 |
| `confirmOperation` | service.sock | S1-05 | S1-07 | F3 |
| `cancelOperation` | service.sock | S1-05 | S1-07 | F3 |
| `listLoans` | service.sock | S1-05 | S1-07 | F3 |
| `listEvents` | service.sock | S1-05 | S1-07 | F3 |
| `listReviews` | service.sock | S1-05 | S1-07 | F3 |
| `resolveOperation` | service.sock | S1-05 | S1-07 | F3 |
| `listUsers` | service.sock | S1-05 | S1-07 | F3 |
| `createUser` | service.sock | S1-05 | S1-07 | F3 |
| `updateUser` | service.sock | S1-05 | S1-07 | F3 |
| `listCardObservations` | service.sock | S1-05 | S1-07 | F3 |
| `bindCard` | service.sock | S1-05 | S1-07 | F3 |
| `unbindCard` | service.sock | S1-05 | S1-07 | F3 |
| `presence.submit` | service.sock | S1-05 | S1-07 | F3 |
| `progress.report` | supervisor.sock | S4-04 | S4-08 | F4 |
| `ui.restart.request` | supervisor.sock | S4-05 | S4-08 | F4 |
| `maintenance.reconcile` | service.sock | S1-05 | S1-07 | F3 |
| `health.get` | supervisor.sock | S4-04 | S4-08 | F4 |
| `getVisionStatus` | service.sock | S2-14 | S2-16, S3-07 | F6 |
| `getOperationVisionEvidence` | service.sock | S2-14 | S2-16, S3-07 | F6 |

## 5. 部署接口

F5只使用受限本机工具/离线文件，不增加公开API。pair-export在S2-06可用；备份/制品/更新在S5逐卡实现，详见[部署维护接口](deployment-interfaces.md)。

## 6. 视觉worker私有协议

vision.sock不是service.sock的UI方法，也不是MCU串口。只允许本机service连接，无TCP classify/path入口。

| 消息 | 适配实现 | 生产者 | worker消费者 | 验证 |
|---|---|---|---|---|
| `HELLO` | S2-13 | S2-13 | S2-12 | S2-16 |
| `STATUS` | S2-13 | S2-13 | S2-12 | S2-16 |
| `CAPTURE` | S2-13 | S2-14 | S2-12 | S2-16 |

详细时序及融合规则见[视觉架构](vision-architecture.md)和[视觉接口](vision-interfaces.md)。
