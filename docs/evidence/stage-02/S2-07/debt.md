# S2-07 后续门禁 / 非阻塞限制

| 项目 | 位置 / 影响 / 后续处理 |
|---|---|
| ~~HELLO 不被应答~~ **已修** | 根因是 Storage 任务栈溢出触发 `env_assert_failed()` 中断自锁（不是队列溢出）。已加栈/堆并由 SWD 实测稳定 60 s 无重开，见 stack-overflow-freeze.md |
| **对账后 MCU 仍 DEGRADED（阻断）** | `reconcile` 已返回 200（全部前置通过），但同一 boot 内 MCU 把 `mode` 置为 DEGRADED，业务仍不可用。判读为对账后续 RESUME/BARRIER 触发 `mcu_control_fault()` 永久锁存 `failed`。见 mcu-degraded-after-reconcile.md |
| 非干净关闭即降级 | 任何未经 `mcu_journal_shutdown` 的复位都会把该 epoch 锁存为 DEGRADED，恢复只能新建 epoch（本轮已重建 5 次）。S4-01 定回收/维护策略前，每次掉电都要走一次维护流程，操作成本需在 S2-08 说明 |
| 真锁带载未验 | 本轮只做 Journal 维护与固件修复，12V 关闭、未发脉冲。S2-05 硬截止是空载 PB6 证据 |
| PC0 供电采样 | 未接、未标定，`supply_valid=false`；事件因此带 `0xffffffff` 哨兵，不得靠改配置解除真锁资格 |
| profile 仍 UNVERIFIED | 缺带载默认关闭、供电/保护实测与截止波形 |
| Journal 逐条事实未解码 | 首个 epoch 的 30 条未 ACK 事实已随 16 MiB 镜像归档；后三个 epoch 只做检查点级归档，未逐条提取。需要时离线从镜像解析 |
| 旧账本保留 | `/userdata/locker/state` 原样保留（7 条登录记录、0 业务操作）；新账本在 `state-s207` |
| 台架机械固定 | 锁体/锁扣、门磁、微动仍未刚性固定；影响重复性，不影响本次电气结论 |
| 客户端证据文件名 | 证据文件独占创建，中断重跑必须换 `--evidence` 文件名，否则在输入密码前失败 |
| 部署二进制回退 | 板上留 `locker-service-prev`、`locker-service-s207-old` 两份历史二进制，S2-08 交接前清理或归档 |
