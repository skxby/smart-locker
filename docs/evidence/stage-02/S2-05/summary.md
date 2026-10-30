# S2-05 Control 与锁硬件截止

日期：2026-10-30｜契约：0.2.1｜结论：**实现与验证完成**。
主机回归 Debug/ASan/TSan 各 13/13 通过，空载硬件波形五组通过；真锁负载、MOS、12V 回路与断电冷启动未验证。

## 入口与命令

| 项 | 命令 | 结果 |
|---|---|---|
| 产品固件 | `python -B scripts/build_firmware_gnu.py --target s205 --clock hse8_168 --toolchain-root <xpack>` | exit 0，46 单元 |
| 空载镜像 | 同上 `--target s205-cutoff` | exit 0，39 单元 |
| 主机回归 | `docs/evidence/stage-02/S2-05/run-vm-final.sh`（VMware Ubuntu，gcc 15.2.0 / cmake 4.2.3） | 9 个步骤 exit 0 |
| 设计检查 | `check_contracts` / `check_stage_plan` / `check_vision_contracts` / `check_feasibility` | 四项 PASS |
| 上板 | `docs/evidence/stage-02/S2-05/flash-and-trigger.py`（pyOCD 0.46.0 + Keil DFP 2.13.0 FLM） | 见下 |

## 构建与内存

| 镜像 | ROM | RAM | SHA-256 |
|---|---|---|---|
| s205（产品） | 56012 B | 76432 B | `b34063aa779848e964ce387e606ac832da69d3383f9ca04dd96606658d953c49` |
| s205-cutoff（空载） | 10100 B | 19448 B | `29dc255f2f9c89de2851193c3a8828521db82778901223119fc2963ceb9760df` |

ARMCC 仍受 MDK-Lite 32 KiB 限制，沿用 xPack GNU 15.2.1（与 S2-04 同一合法工具链）。
空载镜像按 11:29 最终源码重建后 SHA 与原烧录镜像一致，说明同期 `peripherals.c/h` 改动未进入该链接结果。

## 主机回归

| 配置 | 结果 | 日志 |
|---|---|---|
| Debug | 13/13 passed | `vm-debug-ctest.log` |
| ASan+UBSan | 13/13 passed | `vm-asan-ctest.log` |
| TSan | 13/13 passed | `vm-tsan-ctest.log` |

覆盖用例：`control_persistent_guards`、`lock_hal_0/1`、`assembly_concurrent_mailboxes`、
journal 事务与掉电、journal 单写者并发、Flash 命令与故障、transport、communication×2、
inputs/UID、SPI 互斥、board 安全。

`test_control` 实际覆盖：提交→commit_intent→复核→start_bounded_pulse 全链；
重复释放幂等；物理版本变化、快照新鲜度（2000 ms）、`not_after` 截止、日志错误均不驱动；
快照屏障；健康未验证时拒绝；卡读取只产生事实。

源码指纹：`run-vm-final.sh` 首步对 419 个文件逐一比对 SHA-256（`vm-source-hashes.log`）。

## 上板波形（空载：PB6 仅接探头，锁/MOS/12V/PB6-MOS 线全断）

详表见 [waveform-summary.md](waveform-summary.md)，要点：

| 组 | 命令 | 冻结条件 | 实测 |
|---|---|---|---|
| 1 | 1000 ms | 无 | 3.3 V、1.00 s（正脉宽读数）、落沿干净、回零保持 |
| 2 | 50 ms | 关中断后自旋 | 仍 2.5 格（50 ms）结束，读回 `phase=2` |
| 3 | 50 ms | 挂起调度器后自旋 | 仍 2.5 格结束，读回 `phase=3` |
| 4 | 1000 ms | 50 ms 时 `board_lock_off()` | 截断在 2.5 格，MODER 收回 0x1280，不复位 |
| 5 | 1000 ms | 50 ms 时 `NVIC_SystemReset()` | 截断 + 恢复后 0 V；随后 count 归 0、MODER 0x1280 |

第 2、3 组的 `phase` 读回发生在脉冲结束约 0.9 s 后仍为 2/3，证明该窗口内 CPU 确实无法运行软件，
脉冲仍按设定宽度自行结束——关断来自 TIM4 one-pulse 硬件，不依赖中断、RTOS 调度或存储完成。

复位瞬态：100 µs/格 下降沿观测（`photo-reset-edge-100us.jpg`）显示落沿陡直、之后 0.7 ms 内稳定在 0 V 附近，
波动约 70 mV，无中间平台、无回升、无振铃。PB6→GND 的 10 kΩ 下拉**已实装**（用户确认）。
此结论仅限 100 µs/格分辨率与空载条件。

## 验收对照

| 卡验收项 | 结果 | 依据 |
|---|---|---|
| 存储阻塞/RTOS 延迟时硬件仍关断 | PASS | 第 2、3 组波形 + `phase` 读回 |
| 复位关闭 | PASS | 第 5 组（两次复现 count 归零、MODER 回复位默认） |
| 准备超过 2000 ms 或 not_after 截止不驱动 | PASS | `control_persistent_guards`（主机） |
| 物理变化不驱动 | PASS | 同上（physical_revision 失效分支） |
| 日志错误不驱动 | PASS | 同上（committed 返回错误分支） |
| 已接纳但结果不明不退回水位 | PASS | journal 事务用例 + control 复核用例 |
| 重复释放幂等 | PASS | `success_and_duplicate` |
| ≤1000 ms 且不超实物限值 | PASS（空载） | `BOARD_LOCK_MAX_MS=1000`、`board_lock_start` 拒绝 >1000 与 0 |
| 完成证据：示波器波形、故障最长脉冲、队列时序与水位 | PASS | 本页与 waveform-summary.md |

## 未实现 / 未覆盖

- 真锁线圈、MOS 驱动、12 V 回路、保险与保护器件：**未接、未测**。
- 断电（拔电）冷启动与掉电瞬态：未测；只测了 `NVIC_SystemReset`。
- 连续多次脉冲的温升、抖动累积：未测。
- RK 真实串口与整柜借还闭环属 S2-06/S2-07。
- 真机 UART 链路上的 2000 ms/`not_after` 拒绝只在主机用例覆盖，未在实板链路复现。

## 跨目录改动说明

本卡允许目录外新增 57 行最小改动，用于把 HELLO 元数据交给 Control 所有者：
`firmware/transport/transport.{c,h}`（新增 `backend_hello` 标志与回退路径）、
`firmware/board/board.c`、`firmware/board/peripherals.h`、`firmware/app/main.c`、
`tests/firmware/transport/test_transport.c`。旧诊断目标保持内建未初始化 HELLO，行为不变。

## 新增与修改文件

新增：`firmware/control/{control.c,control.h,cutoff_validation.c}`、
`firmware/tasks/{assembly.c,assembly.h,health.c}`、`firmware/board/{lock_output.c,lock_output.h}`、
`tests/firmware/control/*`、`docs/evidence/stage-02/S2-05/*`。
