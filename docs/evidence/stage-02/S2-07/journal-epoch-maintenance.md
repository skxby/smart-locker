# S2-07 Journal 新 epoch 维护记录

日期：2026-10-30｜契约 0.2.1｜结论：**设备侧成功，宿主侧仍阻断业务**。

本记录对应 S2-04 债务「诊断epoch故障锁存：先取回/归档未ACK诊断事实，完成业务核验，
再显式维护新epoch；不得静默format」。用户在本轮明确授权“先归档再新建”。

## 为什么必须新建 epoch

`firmware/journal/journal.c:193` 在挂载时若发现上次 `boot_open` 未清，就把
`continuity_ok=false`、`dropped_events=UINT64_MAX`、`journal_state=DEGRADED` 写入检查点，
注释写明这是 “epoch-wide latch that survives every later clean mount/session”。
全代码中只有 `mcu_journal_provision()`（`journal.c:167`）能把 `continuity_ok` 置回 true。

宿主侧两条准入都要求它，因此旧 epoch 下**连对账都不可用**：

| 位置 | 条件 |
|---|---|
| `domain_commands.c:184` physical_ready_for_action | `device_mode==READY && continuity_ok && journal_state==HEALTHY && door==CLOSED` |
| `domain_commands.c:2099` reconcile 分支 | 同上，另加 `item` 与 `loan_open` 一致、快照新鲜 |

## 归档（擦除前完成）

| 项 | 值 |
|---|---|
| 旧 epoch | `9628099c-8c85-4af6-973a-2c4b8322461f` |
| 旧 epoch 状态 | DEGRADED；continuity=false；boot 12；动作水位 1；事件 30 条 |
| 旧 epoch 游标 | first_available=(1,1)、last_committed=(12,1)、ack_persisted=(1,3) |
| dropped_events | `18446744073709551615`（未知哨兵，非“零丢失”） |
| NOR 全片备份 | 16 777 216 B，SHA-256 `8e6f74208db338d002f14a7362781d5edf04961571934ea958301cd840876b54` |
| 备份文件 | `build/hardware-S2-07/journal/nor-before-epoch2.bin`（不入库） |
| 读取方式 | 板级命令 1 逐 8 KiB 读满，2048 次，730 s |

Journal 在**外部 W25Q128（16 MiB，JEDEC 0xef4018）**上，不在 MCU 内部 Flash；
S2-04 的 16 MiB 备份同样是这颗 NOR。30 条未 ACK 事实的字节级证据在备份镜像中，
逐条解码未做（本轮只解码到检查点级状态），如需可离线从镜像提取。

## 执行步骤与结果

| 步 | 动作 | 证据 | 结果 |
|---|---|---|---|
| 0 | 只读普查 | board-survey.json | 内部整片 `8c0c26d1…` 与此前恢复记录一致 |
| 1 | 烧 S2-04 验收镜像（3 扇区，34 924 B） | bench-program.json | PASS，JEDEC 0xef4018，pc 0x8000188 |
| 2 | 读满 16 MiB NOR | nor-backup.json | PASS，SHA 见上 |
| 3 | 挂载旧 epoch | cmd-mount-01.json | DEGRADED，30 事件 |
| 4 | recover | cmd-recover-01.json | 同上，未新增事实 |
| 5 | 干净 shutdown | cmd-shutdown-01.json | OK，boot_open 清零 |
| 6 | provision 新 epoch | cmd-provision-01.json | OK |
| 7 | 挂载校验 | cmd-mount-02.json | boot 1、1 事件、HEALTHY、continuity=1、水位 0 |
| 8 | 干净 shutdown | cmd-shutdown-02.json | OK |
| 9 | 恢复产品镜像（4 扇区，56 012 B） | product-restore.json | PASS，5 任务、tick 递增、PB6 低 |

新 epoch：`a054d7fd-8190-40d1-b497-4d7e869ce0cd`。
产品镜像 `locker_s205.hex`，hex SHA-256 `9b603283…`，与恢复前同一镜像。

## 工具

`journal-maintenance.py`（本目录）：板级命令 1=读NOR、2=provision、3=挂载、9=关机、
10=读页、11=recover，符号取自 `locker_s204.map`。脚本拒绝覆盖同名证据；
`--backup-nor` 与 provision 分两次运行，保证先归档。

一次误标已更正：`cmd-mount-01.json`/`cmd-recover-01.json` 的 `journal_state` 初版按错误
枚举映射写成 FULL，实际 raw[29]=2 对应 DEGRADED；两份文件保留 raw 原值并加
`label_correction` 说明，脚本映射已修正为
`0=UNINITIALIZED,1=HEALTHY,2=DEGRADED,3=FAULT,4=FULL`。

## 宿主侧结果：仍阻断

新 epoch 已被宿主采用：

| 字段 | 值 |
|---|---|
| journal_epoch | `a054d7fd-8190-40d1-b497-4d7e869ce0cd` |
| boot_id | 2 |
| door | CLOSED |
| dropped_events | 0 |
| observed_age_ms | 120（新鲜） |
| mode | **RECONCILING** |
| continuity_ok | **false** |

但宿主**一条设备事件都没有导入**：`business.db` 只有 device_state/sessions/tools/
username_index/users 五个集合，没有 events，也没有 import_cursors。
干净重启后重复观察仍是零导入（rk-import-check-02.log）。

后果是死锁：`reconcile` 要求 `continuity_ok`，而宿主连续性只能由成功导入建立
（`repository.c:1714` 首次建游标：`!gap && first_new==1/1`）。设备侧日志是 HEALTHY、
first_available=(1,1)，本该满足。

## 第二个缺陷：多次 MCU 复位后重连不收敛

烧录期间 MCU 被反复复位，旧服务进程进入无限循环（rk-service-log-after-flash-01.log）：

```
device session not established; reopening
device reopen ok
```

`host/runtime/main.c:115` 的分支每次关闭重开，但 `hello_complete` 始终不成立，
`observed_age_ms` 一路涨到 920 s。同一配置下**重启服务立刻恢复**
（`dev open=1 hello=1 lost=0 rd=1 wr=1 streak=0`，rk-service-trace-01.log）。
即：重开路径不能自愈，必须重启进程。

## 下一步（阻断项）

按优先级：

1. 定位宿主导入路径为何不落库：`domain_device.c:275 request_fetch` 在
   `locker_device_fetch_events` 返回非 OK 时**静默返回**；
   `apply_fetch` 在 `page->result!=0` 时也**静默返回**；
   `domain_import_page` 在 `cursor.device_id` 为空时返回 NOT_READY，同样静默。
   三处都应把状态暴露到 `health_reasons` 或 TRACE，否则现场只能看到空集合。
2. 修复 `main.c` 重开路径：`hello_complete` 长期不成立时应重建会话状态，而不是无限重开。
3. 上述修复后重跑：导入 → continuity → `reconcile` → `RUNNING` → 借还矩阵。

本轮未执行任何借还、presence 或脉冲；12V 保持关闭，真锁未驱动。

## 后续：epoch 重建链与最终可用状态

上面的结论（宿主零导入）在当晚进一步定位为固件缺 `supply_mv` 哨兵，
详见 [host-import-defect.md](host-import-defect.md)。因为坏格式的旧事实不能被后续页洗白，
epoch 一共重建过三次，最终可用的在第四个：

| epoch | 用途 | 结果 |
|---|---|---|
| `9628099c-8c85-4af6-973a-2c4b8322461f` | S2-04 遗留 | DEGRADED、30 事件，已 16 MiB 全片归档 |
| `a054d7fd-8190-40d1-b497-4d7e869ce0cd` | 第一次重建 | 宿主零导入；事件为旧固件格式 |
| `99eca8a8-1a9e-4adb-acd2-8eddd6803e79` | 装上宿主诊断后重建 | 定位到 `import_status=3`；BOOT(1,1) 仍为旧 bench 固件所写 |
| `a75ce458-18bc-4351-a0b0-66ae44dfa27d` | 固件修复后重建 | **可用**：导入 4 条事实、`import_cursors=1`、`continuity_ok=true`、游标 2/3 |

第一个 epoch 做了完整 16 MiB 归档；后三个 epoch 只含 BOOT/健康类内部事实、无任何业务动作，
归档形式为解码后的检查点状态（`cmd-mount-0*.json`、`cmd-recover-0*.json`），未再逐次做 12 分钟全片读取，
此处如实标注，不声称等价于全片镜像。

`provision` 必须在 `SHUTDOWN` 之后执行：`journal.c` 在 `journal!=NULL` 时返回 BUSY(4)。
本目录 `cmd-provision-03.json` 即该误序的失败记录，`cmd-provision-04.json` 为重试成功。
