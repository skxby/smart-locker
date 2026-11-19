# S2-07 对账已通过，但 MCU 停在 DEGRADED

日期：2026-10-30 19:30｜契约 0.2.1｜状态：**未解决，需固件侧处理**。

## 事实链

1. 第 5 个 epoch `6fad5fcd-7242-4c6b-8a9f-4540d4cfd976` 建立后，设备侧 HEALTHY、干净关闭，
   宿主导入 29 条事件、`continuity_ok=true`、`door=CLOSED`、`item=PRESENT`。
2. `reconcile 6fad5fcd… 2 41 25 <key> "bench reconcile after journal epoch provision"`
   返回 **200**，说明 Core 的全部前置（管理员/无活动操作/物理新鲜/关门/物品在位/
   `journal_state==HEALTHY`/`cursor.continuity_ok`）都通过了。
3. 但返回体里的 `mode` 变成 **`DEGRADED`**，`device` 复核仍是 `DEGRADED`；
   `boot_id` 仍为 2（没有重启），`door=CLOSED`、`item=PRESENT`、`observed_age` 11 ms。

## 判读

`mode` 是 **MCU 上报的值**（`control.c` 的 `c->mode`），不是宿主推导：

```c
if(kind==RECOVER) {
    c->recovered=true;
    if(!healthy(c)) c->mode=2;   /* DEGRADED */
}
static bool healthy(const mcu_control_t *c)
{ return c->recovered && !c->failed && c->state.continuity_ok && !c->state.dropped_events &&
    c->state.journal_state==LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY; }
```

对账前该设备报到宿主的 `journal_state` 还是 HEALTHY（否则对账会被 503 拦下），
对账后同一个 boot 内变成 DEGRADED，说明是**对账流程本身触发了 MCU 侧的
`mcu_control_fault()`**（`c->failed` 一旦置位只有复位能清），
而不是日志真的损坏。

最可疑的是对账成功后宿主紧接着发的 RESUME_READY/BARRIER：设备对这类请求
返回非 OK 时，`control.c:286-294` 会无条件 `mcu_control_fault(c)` 并把
`failed` 永久置位。这与之前"出队队列满即 force_off 且无恢复路径"是同一类问题：
**一次可恢复的拒绝被升级成整机永久降级**。

## 为什么没有继续修

- 该修复在固件控制状态机里，需要改 `healthy()/fault()` 的锁存策略并重新上板验证；
- 任何 MCU 复位都会把当前 epoch 锁成 DEGRADED（本卡已重建 5 次），
  修完必须连 epoch 一起重建，属于一个完整回合；
- 本轮已到 19:35，台架保持原状（服务在跑、客户端已退出、12V 关闭、未发任何脉冲）。

## 下一步建议

1. 固件：`mcu_control_fault()` 不应因为一次可恢复的拒绝（CONFLICT/NOT_READY）而永久
   置 `failed`；至少区分「动作性故障」与「一次性协议拒绝」，后者应允许下一次 RECOVER 自愈。
2. 同时给 `control.c` 的 fault 路径留诊断（把触发 kind/status 写进一个全局，供 SWD 读）。
3. 修好后重建 epoch：bench 镜像 provision → 干净 shutdown → 产品镜像 → 导入 → reconcile，
   然后才能开始借还矩阵。
