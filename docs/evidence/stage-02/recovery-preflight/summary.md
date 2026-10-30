# 链路恢复预研（S2-07 恢复项前置）

日期：2026-10-30
范围：`host/device`、`host/runtime`、`tests/contract/device`
不改变通信契约，不新增对外接口。目的：拔掉 UART 后主机能自己发现、自己重开、自己重新握手，不需要人工重启服务。

## 结论

拔线 → 主机检测 → 关闭重开 → 重试建链 → 恢复收发，全链路成立，且**没有任何动作被重放**。

| 判据 | 结果 |
|---|---|
| 检测 | 断线约 5 s 后 `lost=1`（连续 3 次请求超时） |
| 恢复动作 | `device link lost; closing before reopen` → `device reopen ok` |
| 会话重建 | 线仍断时反复 `device session not established; reopening`；插回后 `hello 0→1`，日志停止刷新 |
| MCU 侧 | `hello` 4→7（每次成功建链 +1）、`health` 恢复 1 次/秒、`tick` 1000 Hz |
| 不重放 | `execute_unlock` 全程 0 |
| 主机回归 | 36/36（含新增用例） |

## 三次尝试与两次失败（保留，因为它们决定了最终设计）

### 第 1 次：只重开，不验证会话

日志出现 `device link lost` 和 `device reopen ok`，但之后 MCU 的 `health` 永久冻结。

根因：`locker_device_open()` 把 HELLO 入队即返回，不等应答。重开落在“线还没插回”的窗口时，HELLO 丢失，`hello_complete` 永远为 false，此后每个请求都被 `!opened || !hello_complete` 判 `NOT_READY`，一个字节也发不出去——设备看起来已恢复，实际永久静默。

修法：新增 `locker_device_session_ready()`，守护线程要求重开后 3 s 内建链，否则关闭重试（退避 1→2→4→8 s）。

### 第 2 次：判断顺序错误

诊断输出：`lost=1 streak=63 rd=1 wr=1 pend=5`，但**没有任何恢复动作**。

根因：守护循环里先判 `session_ready` 再判 `link_lost`。断链时 `opened` 和 `hello_complete` 都还是 true（没人拆会话），于是每次都提前 `continue`。

修法：把 `link_lost` 判定提到 `session_ready` 之前。

### 第 3 次：通过

```
device link lost; closing before reopen (lost=1 rd=1 wr=1)
device reopen ok
device session not established; reopening
device reopen ok
...
hello=0 → hello=1, pend=0
```

## 顺带修掉的两处可靠性问题

1. **超时检查的时钟不可靠**：读线程原来只在“读回 0 字节”时做 `pending_expire`。RX 悬空会持续产生噪声字节，这个分支可能永远进不去。改为每轮都检查。
2. **写线程死亡不被察觉**：`transport_write` 失败后写线程 `break` 退出，但 `link_lost` 不会被置位，此后所有请求只入队不出队，外观与“健康”无法区分。现在写线程退出即置 `link_lost`，且守护线程额外检查 `reader_alive`/`writer_alive`。

## 诊断开关

`LOCKER_DEVICE_TRACE=1` 时守护线程每秒打印一行：

```
dev open=1 hello=1 lost=0 rd=1 wr=1 streak=0 pend=0 tx=0
```

默认关闭，不影响生产日志。这次能定位到“顺序错误”，靠的就是 `streak=63` 与“无动作”同时出现这一对照。

## MCU 侧读数（恢复后实测）

```
t0 hello=7 health=2368 execute=0 tick=3255824
t3 hello=7 health=2371 execute=0 tick=3258827
```

## 明确边界

- 拔的是 ADuM 的 MCU 侧信号针（V0A/V1B 之一），不是 GND/VDD。
- 断链判定依赖**连续请求超时**：如果某个时刻没有任何在途请求（例如整机空闲且不发健康帧），拔线不会被发现。当前 `host_health` 每秒一次，这个前提成立。
- 重开是“关掉再打开同一个 tty”，不是重新枚举设备；USB 串口换口/掉电重枚举不在本预研范围。
