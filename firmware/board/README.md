# S2-01 板级资源与安全边界

目标芯片为候选天空星 STM32F407VGT6（LQFP100、1 MiB Flash）。当前板级 profile 为 `UNVERIFIED`，BSP 拒绝所有动作；`board.c` 只提供关闭、UNKNOWN 输入和 NOT_READY 返回，不配置任何外接 GPIO 输出。它是产品安全边界的初始实现，完整S2-01证据见单卡报告。

| 资源 | 候选用途 | 占用/实测状态 |
|---|---|---|
| TIM6 | HAL 1 ms 时基 | HSI16、HSE8/168 产品安全目标均已上板约1000 tick/s；64位回绕注入通过 |
| SysTick、SVC、PendSV | FreeRTOS | 环境镜像已验证；不可转给 HAL |
| TIM4_CH1 / PB6 | 锁驱动候选 | 极性、外接下拉和硬件截止未实测，BSP 禁止输出 |
| USART2 / PD5、PD6 | RK 隔离 UART 候选 | IO 电平、隔离板排针、DMA Stream/Channel 与通信未实测 |
| SPI1 / PA4–PA7 | 板载 Flash 候选 | JEDEC 型号和几何未实测 |
| SPI2 / PB12–PB15、PE3 | RC522 候选 | 接线和实物读卡未实测 |
| PE0、PE1 | 门磁、物品输入候选 | NO/NC 与原始电平未实测 |
| PC0 / ADC1_IN10 | 供电采样候选 | 分压、基准、保护和标定未实测 |
| PA9/PA10 或 PB10 | 诊断串口候选 | 用户接线 PB10；USART3 配置尚未验证，不自动启用 |

候选孔位和电路见[引脚接线表](../../docs/hardware/引脚确定与接线表.md)。上述资源尚未据此写入已验证 profile；DMA/中断优先级要随 S2-03 的实际通道和 FreeRTOS FromISR 约束核定。TIM4 的 HAL one-pulse API 存在不等于锁脉冲硬截止电路已通过，硬截止留 S2-05 实测。

主 SRAM 为 128 KiB（0x20000000 起），CCM 为 64 KiB；当前 scatter 仅向主 SRAM 放数据，DMA 缓冲不能放入 CCM。S2-01 安全应用使用 16 KiB FreeRTOS heap（其中四个休眠任务的栈各 256 words），ARMCC 链接 map 报 HSI16 ROM 5560 B、HSE8/168 ROM 7140 B、两者 RAM 均17816 B，低于 1 MiB Flash / 128 KiB 主 SRAM。此数字不含后续 UART DMA 环、队列和日志缓存；S2-03 将 2048 B 环放入主 SRAM 并重新核算，不得放入 CCM。四个任务只占位休眠，不汇报健康进度，也不处理业务请求。

S2-01已完成：双电源低电流负载稳态默认关闭，以及板上未验证profile拒绝驱动诊断通过；s201_safety_checks=15、PB6输入/低。SWD、HSE/时基及回绕证据见[单卡报告](../../docs/evidence/stage-02/S2-01/summary.md)。profile仍UNVERIFIED，复位瞬态和硬截止波形留S2-05；本卡不授权真锁。
