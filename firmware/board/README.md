# 板级资源与安全边界

全局profile保持UNVERIFIED，apply_profile和start_bounded_pulse始终NOT_READY，PB6不配置为输出。S201是非执行器安全目标；S202独立启用经核对的只读外围，资格仅适用于输入映射，不使全局profile或真锁获得授权。

| 资源 | 用途与真实状态 |
|---|---|
| TIM6 | 独立HAL 1ms时基；两时钟约1000 tick/s、64位回绕已有板上证据 |
| SysTick/SVC/PendSV | FreeRTOS专用 |
| PB6 / TIM4_CH1候选 | PB6保持输入/低；低电流负载稳态默认关闭通过；硬截止、GPIO高电平裕量、复位瞬态和真锁留S2-05 |
| SPI1 / PA4–PA7 | Flash Mode0、软件CS PA4；实读JEDEC EF4018；S202仅9F/05，S204真实块设备/挂载/提交后掉电见Journal报告 |
| SPI2 / PB12–PB15、PE3 | RC522 Mode0、软件CS PB12、复位PE3；模块版本0x82，配置回读与两张4字节卡通过，7/10仅协议回归 |
| PE0、PE1 | 门磁/微动，内部上拉并保留外部10k；闭门/有物实测低有效；开路两线输入不能诊断断线 |
| PC0 / ADC1_IN10 | 分压、100nF、基准和实物标定未完成，ADC资格false；用户授权后移，基础门禁前补证 |
| USART2 / PD5、PD6候选 | S2-03实现AF7/115200 8N1、DMA1 Stream5 RX/Stream6 TX（Channel4），IRQ逻辑5；默认构建关闭，--enable-uart HSE目标经ADuM与RK UART4真实收发/回绕/背压通过 |
| PA9/PA10 或 PB10候选 | 用户日志线PB10，固件未启用USART3 |

SPI1/2各一把互斥锁，无ISR调用；片选先预置高再改输出。HSE目标SPI1/2为656.25/328.125kHz，HSI目标均125kHz。GPIO、句柄与FreeRTOS原语止于BSP。mcu_board_read_inputs复制带时间的映射原始副本，100ms失鲜则UNKNOWN/NOT_READY；Control仍负责业务去抖与事件版本。详见[驱动说明](../drivers/README.md)。

主SRAM 128KiB，CCM 64KiB；scatter仅用主SRAM，后续DMA不得放CCM。S201 ROM HSI/HSE为5560/7140 B、RAM 17816 B，四个休眠任务栈各256words。S202 ROM为12496/13904 B、RAM 20296 B、16KiB FreeRTOS heap，四任务栈各768words；Control角色运行输入采样，Storage角色运行只读外围，Communication/Health休眠，不报告假健康。S202尚无UART；S203增加Communication独占协议/TX队列，2048B RX环与2048B稳定副本仅在UART编译检查分支分配，4槽TX每槽524B。S203未绑定Journal/业务Control，动作请求NOT_READY；S204独立Journal实现见[journal/README](../journal/README.md)，产品四任务装配与Control归S2-05。Communication栈1024words、优先级2，其余仍768words/优先级1，16KiB堆；需上板测实际栈高水位。

[S2-01证据](../../docs/evidence/stage-02/S2-01/summary.md)、[S2-02证据](../../docs/evidence/stage-02/S2-02/summary.md)区分软件/上板/后移项目。未核对资源不能据候选表直接启用。完整硬件截止证据须来自实际波形，不能用HAL one-pulse API存在或日志时间差替代。
