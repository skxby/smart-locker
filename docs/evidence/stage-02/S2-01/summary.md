# S2-01 HAL/BSP 开发者验证

| 字段 | 当前事实 |
|---|---|
| 卡号 / 契约 | S2-01 / 0.2.1；S1-07 DONE、阶段一 PASS |
| 状态 | DONE；S2-02待开始；阶段二门禁NOT_RUN |
| 入口 | `python -B scripts/build_firmware.py --target s201 [--clock hse8_168]`；ARMCC5.05u1 / MDK-Lite5.14 |
| 范围 | 安全应用、BSP拒绝边界、TIM6时基、64位毫秒、四个休眠任务、板级资源表与独立Keil工程 |
| 构建 | 两时钟均exit 0，254依赖哈希匹配；最新HSI/HSE ROM 5560/7140 B，RAM 17816 B |
| SWD / 时钟 | HSI16与HSE8/168烧录回读、TIM6硬件断点、约1000 tick/s、64位回绕均通过；见原始JSON与历史测量记录 |
| 默认关闭 | MCU失电/MOS供12V：PWM=0V、OUT差分29mV；双电源上电：PWM=0V、OUT差分0.2mV；真锁拆离、OUT跨10kΩ |
| 拒绝驱动 | 2026-10-29 HSE镜像上板，s201_safety_checks=15、fault=0；空profile、未验证profile、伪称verified的profile及pulse均返回NOT_READY；PB6始终输入、ODR bit6=0 |
| Flash保护 | 新镜像逐字节回读、第0扇区外原备份逐字节核对PASS；[原始证据](board-safety-rejection.json) |
| 主机测试 | VMware Ubuntu：新增BSP测试Debug 1/1、ASan/UBSan 1/1；既有主机2872个输入文件哈希与Windows一致。首轮全量35/36，中断矩阵时序失败；复验全量36/36，日志均保留；偶发风险列debt.md |
| 设计检查 | check_contracts / check_stage_plan / check_vision_contracts 各exit 0，仅设计一致性 |
| 未实现 | profile仍UNVERIFIED；传感器/SPI/ADC留S2-02，UART DMA留S2-03，真锁与复位瞬态/硬截止波形留S2-05 |
| 残留 | 已清理旧s201-board.o、误命名locker_env三产物及本轮传输包；保留正式测试、诊断入口、原始日志与照片 |
| 下一卡 | 本地提交后进入S2-02；用户授权做到S2-03开始之前，不开发S2-03 |
| 耗时 / 轮数 | 历史耗时未精确计时；本轮19:35至19:47约12分钟；新增诊断一轮、主机全量回归两轮 |

MOS输入问题及先前探针失败、测量更正、缺100nF等经过保留在[历史记录](measurement-history.md)，不作为当前执行入口。3.3V台面注入经1k/10k分压为约3.0V不能导通，直连约3.3V可导通；这不能替代GPIO实际驱动裕量或硬截止验收。PC0未接，用户确认门磁、微动、RC522已接线，尚未功能验收。

本轮SSH最初在握手前关闭，用户重启ssh服务后恢复；首个测试发送命令有CRLF导致bash解析失败，未运行测试，改用LF输入后执行成功。运行证据位于vm-validation/，没有以失败命令计作PASS。HAL依赖254文件哈希未变，用户原HAL未修改。新增主机BSP测试仅验证安全软件边界，不代替上板证据。
