# S2-07 交接

| 字段 | 内容 |
|---|---|
| 卡号/契约 | S2-07 / 0.2.1 |
| 状态 | BLOCKED；active_task=null，recommended_next=S2-07 |
| 本轮完成 | 账户基线重建；Journal 新 epoch 维护（5 次）；修复宿主导入死锁（`supply_mv` 哨兵）；修复整机静默（Storage 栈溢出）；宿主导入诊断 + 工具符号修正 |
| 新账本 | `/userdata/locker/state-s207`（admin 1 / tool A1 1 / operations 0）；旧库原样保留 |
| 当前 epoch | 设备侧 `6fad5fcd-7242-4c6b-8a9f-4540d4cfd976`（HEALTHY、continuity=1、干净关闭）；宿主记录仍停在旧 epoch |
| 板上固件 | `locker_s205.hex` hex `03d02233…`（ROM 56 040 B、RAM 84 624 B），5 任务、PB6 低 |
| 宿主二进制 | `locker-service` `63c3cda6…`（含 fetch/sink/reconcile 诊断）；客户端 `6d882820…` |
| 导入状态 | `device_events` 8→11、`import_cursors=1`，TRACE 无 fetch 失败 |
| 稳定性 | 60 s 内 0 次 `session not established`；`xTickCount`/`s205_diag[0]` 持续增长；`s201_fault=0` |
| 验证 | VM Debug 37/37、ASan+UBSan 37/37（含固件改动后重跑）；客户端 12/12；四项设计检查 PASS |
| 当前接线 | 12V 关闭、真锁未驱动、door=OPEN、item=ABSENT、PC0 未接；本轮未发脉冲 |
| **阻断** | 对账已 200 通过，但 MCU 随后自报 DEGRADED（疑似 reconcile 后续 RESUME/BARRIER 触发 fault 永久锁存）；业务仍不可用 |
| 失败路径 | `provision` 在挂载中返回 BUSY(4)；非干净关闭会把 epoch 锁存 DEGRADED；硬编码符号曾误报“调度器死” |
| 未验项 | 借还/NO_CHANGE/竞争/只ACK/取消/断电/重启/ACK丢失矩阵、真锁带载、PC0、profile、逐条事实解码 |
| 度量 | 16:49–19:35 约 166 分钟；VM 构建 7 轮、上板 6 轮、登录 5 次、epoch 重建 5 次 |
| 下一步 | 关门+工具就位 → 管理员登录 → `reconcile` → `RUNNING` → 借还矩阵；不自动进入 S2-08 |

入口：`tests/hardware/README.md`；`journal-maintenance.py`（板级维护）；
`host-import-defect.md`、`stack-overflow-freeze.md`（本轮两个根因）；`journal-epoch-maintenance.md`（epoch 链）。
未完成卡不 commit、不 push。
