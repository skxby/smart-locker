# UART v1：字节布局与动作恢复合同

状态：阶段 0 契约，没有固件实现或真机结果。上位决策为 [阶段 0 架构](../stages/stage-00-architecture.md)。字段机器真源为 [uart-layouts.json](../../contracts/uart-layouts.json)，枚举值来自 [contract-source.json](../../contracts/contract-source.json)，不能由不同端各维护一份数值表。

## 1. 连接、头部与编码

115200、8N1，二进制流。UART DMA 块、Linux read 边界与帧边界无关系。多字节整数均 little-endian，UUID 为文本 UUID 去掉连字符后的顺序对应的 16 个原始字节，不使用 GUID 内存布局。C struct 禁止直接 memcpy 上线。

| 帧偏移 | 字段 | 字节 | 规则 |
|---:|---|---:|---|
| 0 | magic | 2 | 固定十六进制 4c 4b，即 ASCII LK |
| 2 | version | 1 | 固定 1 |
| 3 | type | 1 | 请求 1..11；响应为请求 type OR 0x80；异步 DEVICE_EVENT=0x40 |
| 4 | payload_len | 2 | 0..512，实际还须等于该消息布局长度 |
| 6 | request_no | 4 | 请求使用非零关联号，响应原样回送；异步事件为 0 |
| 10 | payload | payload_len | 按本文件与 JSON 编解码 |
| 10+payload_len | crc16 | 2 | 校验值 little-endian，帧总长=12+payload_len，最大524 |

CRC-16/CCITT-FALSE：poly=0x1021、init=0xffff、refin=false、refout=false、xorout=0。覆盖从 offset 2 的 version 到 payload 最后一个字节，**不含 magic 和 CRC 自身**。ASCII 123456789 的算法黄金值为 0x29b1，在线路上两个 CRC 字节为 b1 29；该算法向量不是完整协议帧。

CRC 错误、超大长度、结构不足丢弃并重新寻找 magic，不能按对端长度无界分配内存。版本或消息类型已识别、CRC 正确但字段非法时返回 BAD_VERSION/UNSUPPORTED/BAD_PAYLOAD。CRC 不承担身份认证；机内 UART 不防拆柜总线注入。

请求号只用于请求/响应匹配，不承担动作幂等；当前 session+request_no+response_type 三者都匹配才应用响应。不得复用仍未完成的请求号，接近 u32 回绕前应完成查询并建立新会话；旧响应不能改变当前状态。

### 通用前缀

- 每条请求 payload 开始均为 session_id:uuid16。HELLO 此字段全0，body 提供 new_session_id；其余请求必须是当前 session。
- 每条响应先 session_id:uuid16，再 result:u16，总18字节；result=0 才跟随本文件列出的成功 body。非0响应严格只有18字节，不携带成功 body。
- HELLO 成功的响应 session 是 new_session_id；其他响应回送该请求 session。拒绝旧会话时回送旧请求 session，仅用于关联错误，不表示该会话仍有效。
- DEVICE_EVENT 不使用 result 前缀，单独布局为 session16+journal_epoch16+record96，共128字节 payload。
- 不存在 flags 头字段；事件/快照的观测 flags 是各自 payload 字段。

## 2. 公共值、空值与状态

| 类型/值 | 定义 |
|---|---|
| UUID 全0 | 仅表示缺少关联、HELLO 请求前缀，或未初始化的 epoch；真实 operation/command/session/epoch 不得为0 |
| cursor | boot_id:u64 后接 event_seq:u64，共16字节；(0,0) 为开始/空日志哨兵，禁止仅其中一个为0 |
| boot_id/event_seq | 健康日志均从1开始；仅在同 device_id+journal_epoch 中有意义 |
| physical_revision | 当前 boot 中从1开始；去抖门/物品状态或供电类别改变时递增，不因ADC噪声每次测量递增；CAS 必须同时绑定 epoch+boot |
| action_no | 当前 session 中从1严格递增；action_watermark=0 表示该会话尚无接纳动作 |
| supply_mv | 0xffffffff 表示未知且 SUPPLY_VALID 位必须清0；有效0mV就是0，不能混作未知 |
| observed_age_ms | 0xffffffff 表示进度观测未知；UI 实例全0时 UI age 必须未知 |
| card_uid_len | 0/4/7/10；非卡事件为0，card_uid 剩余字节必须填0 |
| reserved | 必须0；未定义 flags 位必须0；布尔字段只能0/1 |
| detail | 事件类型相关 u64；不适用为0，计数未知可为u64最大值，不按任意指针解释 |

所有枚举以根合同 wire_enums 为准：door_state、item_state、operation_kind、command_state、device_event_type、wire_result、wire_device_mode、wire_journal_state、wire_supervisor_state。设备只报告 RECONCILING/READY/DEGRADED；OFFLINE 是主机观察投影，不是 MCU 在线返回的模式。

observation_flags 按位编号：bit0 SUPPLY_VALID、bit1 PHYSICAL_STABLE、bit2 HISTORY_INCOMPLETE、bit3 JOURNAL_DEGRADED、bit4 PULSE_ACTIVE，其余保留。HISTORY_INCOMPLETE 是连续性失效锁存；即便 event_seq 连续也不能认为无丢失。dropped_events 为当前 epoch 的累计关键事件丢失数，无法确定时用u64最大值。RESUME_READY 不能清该锁存；本版没有远程忽略丢失接口，须走现场显式日志维护与业务核验。

capabilities：bit0 门/物品、bit1 持久日志、bit2 读卡、bit3 供电ADC、bit4 合格限时脉冲。缺少必要能力不得由主机当作正常。固件版本是3个u8数值，v1不传固定字符串，不存在UTF-8/NUL终止问题。

## 3. 逐消息 body 布局

下面偏移均相对于 body 起点，**不含**请求16字节/响应18字节通用前缀。长度校验由固定布局和 FETCH 的 event_count 决定，不是 TLV，不允许尾随任意字段。

### 1. HELLO

请求 type=0x01，成功响应 type=0x81。请求 payload=49 字节，成功响应 payload=71 字节。

请求 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `new_session_id` | uuid | 16 | 按本文通用规则 |
| 16 | `host_instance_id` | uuid | 16 | 按本文通用规则 |
| 32 | `requested_version` | u8 | 1 | 固定 1 |

成功响应 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `device_uid` | bytes12 | 12 | 按本文通用规则 |
| 12 | `journal_epoch` | uuid | 16 | 按本文通用规则 |
| 28 | `boot_id` | u64 | 8 | 按本文通用规则 |
| 36 | `firmware_major` | u8 | 1 | 按本文通用规则 |
| 37 | `firmware_minor` | u8 | 1 | 按本文通用规则 |
| 38 | `firmware_patch` | u8 | 1 | 按本文通用规则 |
| 39 | `capabilities` | u32 | 4 | 位表 capabilities |
| 43 | `device_mode` | u8 | 1 | 枚举 wire_device_mode |
| 44 | `journal_state` | u8 | 1 | 枚举 wire_journal_state |
| 45 | `action_watermark` | u64 | 8 | 按本文通用规则 |

### 2. GET_SNAPSHOT

请求 type=0x02，成功响应 type=0x82。请求 payload=16 字节，成功响应 payload=189 字节。

请求 body：

无 body 字段；payload 只有通用前缀。

成功响应 body（SNAPSHOT）：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `journal_epoch` | uuid | 16 | 按本文通用规则 |
| 16 | `boot_id` | u64 | 8 | 按本文通用规则 |
| 24 | `sampled_mono_ms` | u64 | 8 | 按本文通用规则 |
| 32 | `physical_revision` | u64 | 8 | 按本文通用规则 |
| 40 | `door` | u8 | 1 | 枚举 door_state |
| 41 | `item` | u8 | 1 | 枚举 item_state |
| 42 | `supply_mv` | u32 | 4 | 按本文通用规则 |
| 46 | `flags` | u16 | 2 | 位表 observation_flags |
| 48 | `dropped_events` | u64 | 8 | cumulative since journal epoch, unknown=u64 max; HISTORY_INCOMPLETE remains latched even with contiguous event_seq |
| 56 | `first_available` | cursor | 16 | 按本文通用规则 |
| 72 | `last_committed` | cursor | 16 | 按本文通用规则 |
| 88 | `ack_persisted` | cursor | 16 | 按本文通用规则 |
| 104 | `active_operation_id` | uuid | 16 | 按本文通用规则 |
| 120 | `active_command_id` | uuid | 16 | 按本文通用规则 |
| 136 | `active_command_state` | u8 | 1 | 枚举 command_state |
| 137 | `active_session_id` | uuid | 16 | 按本文通用规则 |
| 153 | `active_action_no` | u64 | 8 | 按本文通用规则 |
| 161 | `action_watermark` | u64 | 8 | 按本文通用规则 |
| 169 | `device_mode` | u8 | 1 | 枚举 wire_device_mode |
| 170 | `journal_state` | u8 | 1 | 枚举 wire_journal_state |

### 3. FETCH_EVENTS

请求 type=0x03，成功响应 type=0x83。请求 payload=49 字节，成功响应 payload最大=469 字节。

请求 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `journal_epoch` | uuid | 16 | 按本文通用规则 |
| 16 | `after` | cursor | 16 | 按本文通用规则 |
| 32 | `max_count` | u8 | 1 | 范围 1..4 |

成功响应 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `journal_epoch` | uuid | 16 | 按本文通用规则 |
| 16 | `first_available` | cursor | 16 | 按本文通用规则 |
| 32 | `last_committed` | cursor | 16 | 按本文通用规则 |
| 48 | `next_after` | cursor | 16 | 按本文通用规则 |
| 64 | `event_count` | u8 | 1 | 范围 0..4 |
| 65 | `has_more` | u8 | 1 | 范围 0..1 |
| 66 | `gap_before_page` | u8 | 1 | 范围 0..1 |
| 67 | `events` | event96_array | 0..384 | event_count 条，每条96字节，最多4条 |

### 4. ACK_PERSISTED

请求 type=0x04，成功响应 type=0x84。请求 payload=48 字节，成功响应 payload=34 字节。

请求 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `journal_epoch` | uuid | 16 | 按本文通用规则 |
| 16 | `through` | cursor | 16 | 按本文通用规则 |

成功响应 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `ack_persisted` | cursor | 16 | 按本文通用规则 |

### 5. RESUME_READY

请求 type=0x05，成功响应 type=0x85。请求 payload=64 字节，成功响应 payload=27 字节。

请求 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `journal_epoch` | uuid | 16 | 按本文通用规则 |
| 16 | `expected_boot_id` | u64 | 8 | 按本文通用规则 |
| 24 | `expected_physical_revision` | u64 | 8 | 按本文通用规则 |
| 32 | `persisted_through` | cursor | 16 | 按本文通用规则 |

成功响应 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `physical_revision` | u64 | 8 | 按本文通用规则 |
| 8 | `device_mode` | u8 | 1 | 枚举 wire_device_mode |

### 6. DISARM

请求 type=0x06，成功响应 type=0x86。请求 payload=18 字节，成功响应 payload=27 字节。

请求 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `reason` | u16 | 2 | 枚举 wire_result |

成功响应 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `device_mode` | u8 | 1 | 枚举 wire_device_mode |
| 1 | `physical_revision` | u64 | 8 | 按本文通用规则 |

### 7. EXECUTE_UNLOCK

请求 type=0x07，成功响应 type=0x87。请求 payload=99 字节，成功响应 payload=93 字节。

请求 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `journal_epoch` | uuid | 16 | 按本文通用规则 |
| 16 | `expected_boot_id` | u64 | 8 | 按本文通用规则 |
| 24 | `expected_physical_revision` | u64 | 8 | 按本文通用规则 |
| 32 | `action_no` | u64 | 8 | 按本文通用规则 |
| 40 | `not_after_mcu_ms` | u64 | 8 | expected_boot monotonic deadline; derived conservatively from 15s presence proof; MCU rechecks before drive |
| 48 | `operation_id` | uuid | 16 | 按本文通用规则 |
| 64 | `command_id` | uuid | 16 | 按本文通用规则 |
| 80 | `slot_id` | u16 | 2 | 固定 1 |
| 82 | `kind` | u8 | 1 | 枚举 operation_kind |

成功响应 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `operation_id` | uuid | 16 | 按本文通用规则 |
| 16 | `command_id` | uuid | 16 | 按本文通用规则 |
| 32 | `command_state` | u8 | 1 | 枚举 command_state |
| 33 | `action_no` | u64 | 8 | 按本文通用规则 |
| 41 | `command_session_id` | uuid | 16 | 按本文通用规则 |
| 57 | `command_boot_id` | u64 | 8 | 按本文通用规则 |
| 65 | `physical_revision` | u64 | 8 | 按本文通用规则 |
| 73 | `reason` | u16 | 2 | 枚举 wire_result |

### 8. QUERY_COMMAND

请求 type=0x08，成功响应 type=0x88。请求 payload=48 字节，成功响应 payload=93 字节。

请求 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `journal_epoch` | uuid | 16 | 按本文通用规则 |
| 16 | `command_id` | uuid | 16 | 按本文通用规则 |

成功响应 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `operation_id` | uuid | 16 | 按本文通用规则 |
| 16 | `command_id` | uuid | 16 | 按本文通用规则 |
| 32 | `command_state` | u8 | 1 | 枚举 command_state |
| 33 | `action_no` | u64 | 8 | 按本文通用规则 |
| 41 | `command_session_id` | uuid | 16 | 按本文通用规则 |
| 57 | `command_boot_id` | u64 | 8 | 按本文通用规则 |
| 65 | `physical_revision` | u64 | 8 | 按本文通用规则 |
| 73 | `reason` | u16 | 2 | 枚举 wire_result |

### 9. HOST_HEALTH

请求 type=0x09，成功响应 type=0x89。请求 payload=73 字节，成功响应 payload=27 字节。

请求 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `host_instance_id` | uuid | 16 | 按本文通用规则 |
| 16 | `ui_instance_id` | uuid | 16 | 按本文通用规则 |
| 32 | `core_progress` | u64 | 8 | 按本文通用规则 |
| 40 | `ui_progress` | u64 | 8 | 按本文通用规则 |
| 48 | `core_observed_age_ms` | u32 | 4 | 按本文通用规则 |
| 52 | `ui_observed_age_ms` | u32 | 4 | 按本文通用规则 |
| 56 | `supervisor_state` | u8 | 1 | 枚举 wire_supervisor_state |

成功响应 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `received_mono_ms` | u64 | 8 | 按本文通用规则 |
| 8 | `device_mode` | u8 | 1 | 枚举 wire_device_mode |

### 10. TIME_ANCHOR

请求 type=0x0a，成功响应 type=0x8a。请求 payload=36 字节，成功响应 payload=46 字节。

请求 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `anchor_id` | u32 | 4 | 按本文通用规则 |
| 4 | `host_utc_ms` | u64 | 8 | 按本文通用规则 |
| 12 | `host_monotonic_ms` | u64 | 8 | 按本文通用规则 |

成功响应 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `anchor_id` | u32 | 4 | 按本文通用规则 |
| 4 | `boot_id` | u64 | 8 | 按本文通用规则 |
| 12 | `mcu_receive_mono_ms` | u64 | 8 | 按本文通用规则 |
| 20 | `mcu_send_mono_ms` | u64 | 8 | 按本文通用规则 |

### 11. RELEASE_CONTEXT

请求 type=0x0b，成功响应 type=0x8b。请求 payload=80 字节，成功响应 payload=27 字节。

请求 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `journal_epoch` | uuid | 16 | 按本文通用规则 |
| 16 | `expected_boot_id` | u64 | 8 | 按本文通用规则 |
| 24 | `expected_physical_revision` | u64 | 8 | 按本文通用规则 |
| 32 | `operation_id` | uuid | 16 | 按本文通用规则 |
| 48 | `command_id` | uuid | 16 | 按本文通用规则 |

成功响应 body：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `already_released` | u8 | 1 | 范围 0..1 |
| 1 | `physical_revision` | u64 | 8 | 按本文通用规则 |

## 4. 固定设备事件记录（96字节）

每个 event96 的字段偏移：

| body 偏移 | 字段 | 类型 | 字节数 | 约束 |
|---:|---|---|---:|---|
| 0 | `boot_id` | u64 | 8 | 按本文通用规则 |
| 8 | `event_seq` | u64 | 8 | 按本文通用规则 |
| 16 | `mono_ms` | u64 | 8 | 按本文通用规则 |
| 24 | `event_type` | u16 | 2 | 枚举 device_event_type |
| 26 | `reason` | u16 | 2 | 枚举 wire_result；wire_result or 0; type-specific detail carries narrower reason |
| 28 | `operation_id` | uuid | 16 | 按本文通用规则 |
| 44 | `command_id` | uuid | 16 | 按本文通用规则 |
| 60 | `door` | u8 | 1 | 枚举 door_state |
| 61 | `item` | u8 | 1 | 枚举 item_state |
| 62 | `flags` | u16 | 2 | 位表 observation_flags |
| 64 | `supply_mv` | u32 | 4 | 按本文通用规则 |
| 68 | `physical_revision` | u64 | 8 | 按本文通用规则 |
| 76 | `detail` | u64 | 8 | 按本文通用规则 |
| 84 | `card_uid_len` | u8 | 1 | 允许 0/4/7/10 |
| 85 | `card_uid` | bytes10 | 10 | first card_uid_len bytes valid, remaining bytes zero |
| 95 | `reserved` | u8 | 1 | 固定 0 |

记录唯一键是 device_id+journal_epoch+boot_id+event_seq。device_id/epoch 由已验证握手及外层提供；原始 mono_ms 永远保留，补取到达时间不能当故障发生时间。用户/工具 UUID 不在 MCU 事件中重新创造；业务关联通过 operation/command 决定。

| 事件 | 字段解释 |
|---|---|
| BOOT | detail 为 BSP 复位原因位值；新 boot 的首条提交记录 |
| COMMAND_ACCEPTED | operation/command 非0，detail 为 action_no；只证明意图提交 |
| PULSE_STARTED/PULSE_FINISHED | detail 分别为计划脉冲ms/实际可测脉冲ms；无实测值用未知哨兵；不表示门打开 |
| DOOR_OPENED/DOOR_CLOSED | door 反映对应去抖状态，detail=0 |
| ITEM_REMOVED/ITEM_PRESENT | item 为对应去抖状态；不识别工具身份 |
| CARD_OBSERVED | card_uid_len 及 card_uid 有效；detail=0；历史导入不能生成柜前证明 |
| SUPPLY_CHANGED | supply_mv+有效位提供测量，detail 为配置中的供电类别编号；不推断完整故障根因 |
| HEALTH_CHANGED/JOURNAL_FAULT | reason 为 wire_result 粗分类，detail 为对应诊断位或驱动错误值；原始值保留供维护解释，不由手机据此自动控制锁 |
| CONTEXT_RELEASED | operation/command 标明被释放上下文；detail=0 |
| CONTINUITY_LOST | HISTORY_INCOMPLETE=1，detail 为丢失计数或未知；新开锁资格关闭 |
| UI_RESTART_REQUEST | 当前真实健康观察提出的请求，detail 为原因；历史补传不得重新执行恢复 |

DEVICE_EVENT 只在有当前 session 时实时发送；无会话仍提交本地日志。实时发送成功不代表 RK 已落库，只有 ACK_PERSISTED 可以推进可回收水位。发送队列满不丢日志；关键日志队列丢失则锁存连续性故障。

## 5. 每条消息的执行语义

### HELLO 与快照屏障

HELLO 先检查请求中的 new_session_id/host_instance_id 非0，requested_version=1。新 session 使 MCU 进入 RECONCILING，撤销旧动作资格，保留旧活动上下文和未决意图；同一 new_session_id+host_instance_id 的 HELLO 重试幂等，不能重置 action 水位。MCU 重启后旧 session 无效，主机必须使用新的随机 session 重新握手。

未初始化日志的 HELLO 可以返回全0 epoch/boot=0、UNINITIALIZED 和 DEGRADED，便于诊断；不能据此开锁。GET_SNAPSHOT 必须从 Control 同时捕获去抖物理状态、physical_revision、活动上下文及该时刻事件边界，并等待边界内关键事实全部可靠提交，然后把该捕获边界放入 last_committed；不能用稍后推进的最新日志尾替代，从而让旧物理状态看似对应较新的事实。快照后的变化由后续事件和 RESUME_READY 的 CAS 检出。无法完成屏障返回 LOG_UNAVAILABLE/BUSY，不能提供被标成可恢复的健康快照。

### FETCH_EVENTS 与 ACK_PERSISTED

after 是“该条之后”，(0,0) 从现有最早记录开始。max_count=1..4，响应 event_count 决定后续记录长度，记录按持久日志顺序，boot/event_seq 字典序递增。event_count=0 时 next_after 仍是本次 after；有记录时 next_after 是最后一条记录；has_more=1 表示该响应 last_committed 之前仍有更多记录。

after 已被回收或范围存在已知缺口，仍以 OK 返回 gap_before_page=1、first_available 和可用记录；**这不证明完整历史**，关联业务进入核验。epoch 不同返回 JOURNAL_CHANGED。after 超过 last_committed 或游标结构非法返回 BAD_PAYLOAD，不把未来游标当成已同步。

RK 每页将记录、投影及游标同一事务落库后才能 ACK。MCU 检查 through 不倒退、不越 last_committed、无未承认缺口；重复同水位幂等。ACK 只能回收已确认且不影响未决动作/去重检查点的明细；空日志 ACK(0,0) 是无副作用操作，不清故障锁存。已确认水位不代表文件必须立即擦除。

### RESUME_READY / DISARM

RESUME_READY 在 Control 中检查 epoch、boot、expected_physical_revision、persisted_through。persisted_through 必须覆盖该次可核对屏障，且不大于本地 ACK_PERSISTED；没有活动/未决上下文、日志健康且连续、输入明确、主机健康新鲜才 READY。任一 CAS 变化返回 PHYSICAL_CHANGED，不能用旧快照覆盖新状态；日志/boot变化要求重做握手或补取。

DISARM 关闭新动作入口，转 RECONCILING 或保持 DEGRADED；不清活动上下文，不把已接纳动作描述为已取消。正在输出的脉冲由硬件截止保障安全，不靠Linux后续包。再次 DISARM 幂等。

### EXECUTE_UNLOCK / QUERY_COMMAND

EXECUTE 的 slot_id 只能1；kind 的 BORROW 要求闭门/物品在位，RETURN 要求闭门/物品离位。operation/command 非0；epoch、boot、session、physical_revision、READY、无活动上下文、主机心跳新鲜及存储健康必须同时符合。

not_after_mcu_ms 属于 expected_boot_id 的 MCU 单调时间域。RK 使用新鲜同步锚点和有界误差，把柜前证明剩余15秒保守映射成期限；无法确认新鲜度就不发送。MCU 接收时拒绝已过期或大于当前接收时刻+15000ms 的期限，防止把无限期命令排进队列。实际准备截止取 not_after_mcu_ms 与接收时刻+PREPARE_DEADLINE_MS(2000) 的较早值。

MCU 持久化意图、session/action 水位和活动关联后才返回 ACCEPTED。持久化回执后、驱动前**再次**检查 session、READY、主机新鲜、physical_revision 和准备/证明截止。条件变化或过期不驱动，已接纳 action_no 仍消耗，不能回退以便重试。只有可靠提交“未驱动”终结检查点后才报告 REJECTED；无法确认则 UNKNOWN_RESULT/核验。PREPARE_EXPIRED 在诊断中保留，粗分类为 BAD_PRECONDITION/NOT_READY，不新增 wire result 常量。

同 session、同 action_no、同 command 和相同参数的重复，只返回已有状态；同水位不同参数、低水位或被回收明细的旧动作不得驱动。action_no 跳号返回 ACTION_GAP；低水位重放返回 REPLAY_BLOCKED。MCU 不能依赖无限保留 UUID 记录来防重放。

主机发送 EXECUTE 一次后不自动重发。QUERY_COMMAND 使用当前会话查询历史 command，允许其原 command_session 不同于当前 session；已知记录按成功 body 回状态，完全查不到或结果丢失返回 UNKNOWN_RESULT，不能解释成未执行。command_state=UNKNOWN_RESULT 表示已知意图但执行结局不确定；这与没有记录的 result=UNKNOWN_RESULT 都要求核验。

### HOST_HEALTH / TIME_ANCHOR

HOST_HEALTH 的实例必须与已握手主机一致。core/ui_progress 必须来自真实事件循环，core/ui_observed_age_ms 由中间传输保留；未知观察不可替换成新鲜进度。每秒心跳，5秒失联停止新开锁，仍保持 MCU 自身记录、观察和有条件喂狗。UI 掉线可以请求固定UI恢复，不自动重启 service 或主机。

TIME_ANCHOR 是时间关联，不直接改事件 mono_ms。RK 用本地发送/收到时的单调时间、MCU receive/send 时间估计往返边界，保存 boot、UTC 和误差。消息里的 host_monotonic_ms 仅在同 host_instance 中有意义，不与 MCU 时间直接相减。证明期限按误差保守缩短；时钟跳变或boot变更使旧锚点失效。MCU不根据UTC时间判断开锁期限。

### RELEASE_CONTEXT

RK 已完成或核验操作并提交账本后才调用 RELEASE_CONTEXT。MCU 核验 epoch/boot/physical_revision、闭门、无脉冲及活动 operation/command 匹配，提交清空检查点后再回复 already_released=0。当前无活动上下文且条件安全时可幂等回复 already_released=1；存在其他活动上下文时拒绝，不替别人释放。

该消息只释放 MCU 关联，不完成业务、不重放锁动作。它不清动作水位、已提交事件、HISTORY_INCOMPLETE 或 dropped_events；CAS失败先重新查询，不能无限按旧版本重试。

## 6. 错误、重连和验收

wire_result 数值固定：OK0、BAD_VERSION1、BAD_SESSION2、NOT_READY3、BAD_PRECONDITION4、REPLAY_BLOCKED5、ACTION_GAP6、BUSY7、LOG_UNAVAILABLE8、UNKNOWN_RESULT9、BAD_PAYLOAD10、CURSOR_GAP11、PHYSICAL_CHANGED12、JOURNAL_CHANGED13、UNSUPPORTED14。当前 FETCH 有可用数据的缺口使用 OK+gap_before_page；CURSOR_GAP 留给无法给出可解析连续页的拒绝情形。不能把粗粒度错误写成“锁已坏/主机已死机”。

重连必须 HELLO→GET_SNAPSHOT→FETCH 至屏障→RK 落库→ACK_PERSISTED→核对操作/释放已处理上下文→RESUME_READY。快照后新增变化继续导入，CAS失败重新取屏障，不用旧状态放行。MCU/RK重启后可能已执行的操作先待核验，旧指令不能迁移到新session。

阶段0校验：头部/CRC黄金值、96字节事件、所有payload≤512、GET_SNAPSHOT/EXECUTE/QUERY双向布局与类型统一。实现阶段验证拆帧/粘帧/长度错误/CRC错误/重复ACK/旧session/意图提交前后重启/证明过期/准备超时/事件缺口/空日志/日志重新初始化epoch变化。真锁脉冲和硬截止必须现场测量，本文件没有实测数字。

