# 宿主事件导入死锁：根因与修复

日期：2026-10-30｜契约 0.2.1｜结论：**已定位并修复，设备与宿主两侧都改过**。

## 现象

新 epoch 建立后宿主一直停在 `RECONCILING`：设备日志 HEALTHY、快照新鲜、链路 `open=1 hello=1 lost=0`，
但 `business.db` 里没有 `device_events`，`continuity_ok` 恒 false。
而 `reconcile` 要求 `continuity_ok`，于是对账与借还全部不可用。

## 定位过程

1. 先用 `LOCKER_DEVICE_TRACE=1` 打开已有诊断，确认链路正常、请求在收发，排除断链。
2. 在 `domain_device.c` 给三处静默返回加一次性诊断（预算 24 行）：
   `request_fetch` 被拒、`apply_fetch` 收到设备错误、`domain_import_page` 返回非 OK。
3. 上板后得到 `fetch import_status=3`（INVALID），说明不是设备拒答，而是**仓储拒绝整页**。
   若没有这行，现象只是“连着但什么都不发生”。
4. 再打印页边界：`n=4 has_more=0 gap=0 fa=1/1 lc=2/3 na=2/3 ev0=1/1 evN=2/3`——
   边界完全自洽，所以拒绝发生在**逐条事件文档**上。

## 根因（固件侧）

`host/storage/src/repository.c:879`：

```c
bool supply_valid = (event->flags & 0x0001u) != 0u;
if (supply_valid) { ... }
else if (event->supply_mv != UINT32_MAX) {
    /* uart-v1.md:42：未知必须用 0xffffffff 且 SUPPLY_VALID=0，不能混用 0。 */
    return NULL;      /* → 整页 INVALID */
}
```

`firmware/control/control.c` 生成的观测事件正确使用了 `UINT32_MAX`（`control.c:103/120/253`），
但 Journal 自己造的内部事实是零初始化的：

| 事件 | 位置 | 修复前 supply_mv |
|---|---|---|
| BOOT | `journal.c:203` `boot={0}` | 0 |
| COMMAND_ACCEPTED | `journal.c:329` `e={0}` | 0 |
| CONTEXT_RELEASED 等 | 同一 `e` | 0 |

PC0 未接、`supply_valid=false` 时，这些事实带上 `supply_mv=0`，宿主按契约拒收 →
整页 INVALID → 不落库 → 无连续性 → RECONCILING 死锁。
模拟器事件不走这条路径，所以 S1 主机门禁与 S1-07 全绿的情况下从未暴露。

## 修复

**固件**（`firmware/journal/journal.c` `add_event`）：在唯一 append 入口统一规范化，
而不是修补每个构造点——

```c
if((e.flags&0x0001u)==0u) e.supply_mv=UINT32_MAX;
```

**宿主**（`host/domain/src/domain_device.c`）：保留三处诊断输出（默认关闭，
`LOCKER_DEVICE_TRACE` 打开后前 24 行），并在 `imported==INVALID` 时打印页边界。
行为未变，只是不再静默。

## 验证

| 项 | 结果 |
|---|---|
| 固件重建 | s205 `hex sha256=8715b5ab…`（56 024 B）；s204 bench `8d6b6b95…`（34 936 B） |
| 上板 | 5 任务、tick 递增、PB6 低；Journal 新 epoch `a75ce458-18bc-4351-a0b0-66ae44dfa27d` |
| 宿主导入 | `device_events=4`、`import_cursors=1`，TRACE 无 fetch 失败 |
| 连续性 | `continuity_ok=true`，`durable_cursor=boot 2/seq 3`，`dropped_events=0` |
| 回归 | VM Debug 37/37、ASan+UBSan 37/37（vm-debug-ctest.log、vm-asan-ctest.log） |

## 过程中被纠正的两处

1. `journal_state` 枚举曾按 `0=HEALTHY` 误读，实际是
   `0=UNINITIALIZED,1=HEALTHY,2=DEGRADED,3=FAULT,4=FULL`；相关证据保留 raw 原值并加说明。
2. 第一次“修好固件”后仍失败：因为该 epoch 里的 `(1,1)` BOOT 是**旧 bench 镜像**写的，
   仍带 `supply_mv=0`。旧事实不可能被后续页洗白，只能重建 epoch——所以 bench 镜像也必须带修复。

## 仍未解决

### 第二个缺陷：HELLO 不被应答，会话反复撕裂

导入修好后出现过一段可用状态（`device_events` 4→8、`continuity_ok=true`），随后服务进入稳定复现的抖动：

```
dev open=1 hello=0 lost=0 rd=1 wr=1 streak=0 pend=1 tx=0
device session not established; reopening
device reopen ok
```

判读依据：

- `hello=0` 且 `pend=1`：HELLO 已发出、有一笔未完成请求，但**没有配对回执被消费**；
- `lost=0`、`rd=1`、`wr=1`：不是断链，也不是工作线程死亡；
- 新增的 `sink rejected` 诊断**一行都没有**，所以不是 `device_sink_result` 锁存路径；
- 一轮 ST-LINK 复位后能短暂恢复（事件从 4 涨到 8），说明设备侧状态机卡住而不是主机逻辑错。

指向固件侧：主机短暂离开时 `assembly.c` 的出队队列（8 槽）会溢出，
`event()/reply()/post()` 随即 `failed=true` + `mcu_board_force_off()` 且**没有恢复路径**，
之后 `assembly_ready()` 恒 false，HELLO 不再被正常应答，只有复位能清。
本轮未改此路径，留作下一步。

### 其它

- 对账与借还矩阵尚未执行：`reconcile` 要求 `item` 与无在借状态一致（当前微动报 ABSENT，需要实物工具就位）。
- `host/runtime/main.c` 的重开循环本轮未改；上面这条一旦修好，抖动应随之消失。
