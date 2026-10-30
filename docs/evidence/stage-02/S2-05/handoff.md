# S2-05 交接

| 字段 | 内容 |
|---|---|
| 卡号 | S2-05 Control 与锁硬件截止 |
| 契约版本 | 0.2.1 |
| 状态 | DONE（实现、主机回归、空载硬件波形通过；真锁未接） |
| 日期 | 2026-10-30 |

## 入口与命令

| 用途 | 命令 | 退出码 |
|---|---|---|
| 产品固件构建 | `python -B scripts/build_firmware_gnu.py --target s205 --clock hse8_168 --toolchain-root C:/Users/%USER%/Documents/locker-hardware-tools/xpack-arm-none-eabi-gcc-15.2.1-1.1` | 0 |
| 空载镜像构建 | 同上 `--target s205-cutoff` | 0 |
| 主机回归（VM） | `ssh locker-vm "bash ~/locker-workspace/S2-05/run-vm-final.sh"` | 0（9 步全 0） |
| 设计检查 | `python -B scripts/check_{contracts,stage_plan,vision_contracts,feasibility}.py` | 全 0，四项 PASS |
| 上板烧录/脉冲 | `python -B docs/evidence/stage-02/S2-05/flash-and-trigger.py --identify/--program/--trigger` | 0 |

## 关键结果

- 主机回归：Debug、ASan+UBSan、TSan 各 **13/13** 通过。
- 空载波形：1000 ms 正常路径、关中断 50 ms、挂起调度器 50 ms、软件提前关断、强制复位五组均符合预期。
- 硬截止证据：第 2/3 组脉冲结束 0.9 s 后读回 `phase` 仍为 2/3，证明软件全程无法运行时脉冲仍按时结束。
- 复位瞬态：100 µs/格下未见中间电平或回升；PB6 侧 10 kΩ 下拉已实装（用户确认）。

## 失败路径与保留记录

| 事件 | 处理 |
|---|---|
| 首次 `--program` 在 FLM `init(ERASE)` 触发 `_flash_algo_debug_check` 断言，第 0 扇区未被擦除（前后 SHA 均为 `19ac7d9a…`），旧固件继续运行 | 未产生损坏；随后诊断确认 `init(ERASE)` 可正常完成，改用 `--verify-only` 校验镜像并启动 |
| 复位后首次读回出现 `MODER=0x280`、`phase=5` 未清 | 判定为读回落在复位过程中；数秒后复读确认 count=0、progress 重新计数，作为"真复位"的正向证据保留 |
| 探头上电初期通道衰减仍为 1X、时基 1 µs/格 | 现场纠正为 10X / 500 ms/格后才采到有效波形；过程记录在会话中，未写入证据表 |

## 未实现项（随卡交接）

- 真锁线圈、MOS、12 V 回路、保险与保护器件未接入，未测。
- 断电冷启动、掉电瞬态未测（仅 `NVIC_SystemReset`）。
- 连续脉冲温升与抖动累积未测。
- 实板 UART 链路上的 2000 ms/`not_after` 拒绝只有主机用例证据。
- 板卡当前运行的是空载测试镜像（`s205-cutoff`），不是产品镜像；再次上电前需按需重刷 `s205`。

## 改动文件清单

新增：`firmware/control/control.c`(442)、`control.h`、`cutoff_validation.c`(85)、
`firmware/tasks/assembly.c`(274)、`assembly.h`、`health.c`(37)、
`firmware/board/lock_output.c`(66)、`lock_output.h`、`tests/firmware/control/*`、
`docs/evidence/stage-02/S2-05/*`。

修改（共享最小改动 57 行，HELLO 元数据交给 Control）：`firmware/transport/transport.c`、
`firmware/transport/transport.h`、`firmware/board/board.c`、`firmware/board/peripherals.{c,h}`、
`firmware/app/main.c`、`tests/firmware/transport/test_transport.c`。

## 度量

| 项 | 值 |
|---|---|
| 本卡实际耗时 | 约 80 分钟（10:14 起代码收尾，11:35 收尾；含硬件波形调试） |
| 开发者验证轮数 | 主机 1 轮（3 配置）；上板 6 次脉冲、5 组判据、2 次复位 |
| 整改回合 | 0（硬件配置纠正属操作层，不计整改） |

## 下一卡

S2-06 RK 串口及首用部署（依赖 S2-05）。前置建议：先把板子刷回 `s205` 产品镜像并确认 PB6 保持低，
再接 ADuM 隔离板与 RK UART4。
