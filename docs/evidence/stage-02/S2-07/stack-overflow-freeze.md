# S2-07 整机静默：Storage 任务栈溢出

日期：2026-10-30｜契约 0.2.1｜结论：**已定位并修复**。

## 现象

导入修好后，服务先正常工作一段（`device_events` 4→8），随后进入稳定复现的抖动：

```
dev open=1 hello=0 lost=0 rd=1 wr=1 streak=0 pend=1
device session not established; reopening
device reopen ok
```

主机侧表现：HELLO 发出去没有配对回执，`pend=1` 一直挂着；不是断链，也不是工作线程死亡；
新增的 `sink rejected` 诊断一行都没有，所以不是 `device_sink_result` 锁存路径。
一次 ST-LINK 复位能短暂恢复，说明是**设备侧**停摆。

## 定位（SWD 直读 MCU 内部状态）

按 `locker_s205.map` 取地址，直接读 RAM：

| 观测 | 值 | 含义 |
|---|---|---|
| `xTickCount` | 8563 → 8563 | 系统 tick 不再推进 |
| `s205_diag[0]`（Control 每轮 ++） | 7780 → 7780 | 控制任务不再运行 |
| `xSchedulerRunning` / `uxCurrentNumberOfTasks` | 1 / 5 | 调度器曾启动，5 个任务都在 |
| `CFSR` / `HFSR` | 0 / 0 | 不是 HardFault |
| `PRIMASK` / `BASEPRI` | 1 / 0x50 | 中断被全局屏蔽 |
| `PC` | `0x080001e6` | 位于 `env_assert_failed()` 的死循环（`main.c:21`） |

`PC` 落在 `env_assert_failed`，`IPSR=14`（PendSV），说明断言是在**上下文切换**里被调用的
——这正是 FreeRTOS 的栈溢出检查路径（`configCHECK_FOR_STACK_OVERFLOW 2`）。

再读当前 TCB（`pxCurrentTCB`）：

| 字段 | 值 |
|---|---|
| 任务名 | `Storage` |
| `pxStack` | `0x20002308` |
| 保存的 SP | `0x200022ec`（已越过栈底 0x1C） |

即 Storage 任务（当时 1024 words = 4 KB）在 FETCH/ACK 路径上栈溢出。

## 为什么以前没暴露

导入被 `supply_mv` 缺陷挡住时，宿主从不真正 FETCH 事件，这条深路径（4 条事件解析成
`locker_event_batch_t` + 回复 union，再进 littlefs）从未执行过。导入修好后它立刻被跑到并溢出。

## 修复

| 位置 | 改动 |
|---|---|
| `firmware/config/FreeRTOSConfig.h` | `configTOTAL_HEAP_SIZE` 16 KB → 24 KB（4 个任务栈 4+4+6+1 KB + TCB 需要） |
| `firmware/tasks/assembly.c` | Storage 栈 1024 → 1536 words（6 KB） |
| `firmware/app/main.c` | 新增 `s201_stack_overflow_task`，把溢出任务名前 4 字节留在 RAM 供 SWD 读取（原来参数被丢弃） |

镜像：ROM 56 040 B、RAM 84 624 B；`locker_s205.hex` hex SHA `03d02233…`。

## 验证

| 项 | 结果 |
|---|---|
| 烧录后自检 | 5 任务、tick 593→994、PB6 低（`product-restore.json`） |
| 运行健康 | `xTickCount` 86 359→87 562、`s205_diag[0]` 84 918→86 121、`s201_fault=0`、溢出任务=0 |
| 会话稳定 | 60 s 内 `device session not established` 计数 **0**，`hello=1 pend=0` |
| 事件导入 | `device_events` 8→11 |

## 顺带修掉的一个自伤

`journal-maintenance.py` 原先把 `xTickCount`/`uxCurrentNumberOfTasks` 硬编码为
`0x20000088`/`0x2000008C`。在 `main.c` 增加一个全局变量后 `.bss` 整体后移，这两个地址指向了
无关字节，于是健康板子被判成 “tick not advancing (scheduler dead)”，烧录被误报失败。
现在符号从 `locker_s205.map` 解析，随镜像自动更新。

## 仍未解决

`device_state` 仍停在旧 epoch `a75ce458…`、`continuity_ok=false`、`door=OPEN`、`item=ABSENT`。
设备侧新 epoch `6fad5fcd…` 已 HEALTHY/continuity=1。推进需要管理员 `reconcile`，
而它要求 `door=CLOSED` 且无在借时 `item=PRESENT`，属于现场动作，本轮未做。
