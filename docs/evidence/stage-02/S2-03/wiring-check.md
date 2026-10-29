# S2-03 接线与实物通信记录

用户2026-10-29确认ADuM接线和近端去耦完成，RK侧为UART4且方向正确。V2–G2实测3.3V；V1–G1首测5V，因MCU3.3V TX不满足5V侧0.7VDD输入高阈值，已断电改接STM32侧3V3并由用户确认。修改后精确复测数值未单列提供；实际双向串口已完成本卡功能验收，不据此宣称整板profile或隔离耐压资格。

依据为用户提供粤嵌RK3568底板V10原理图第8页J703/UART4。以下为逻辑针号，不指定面对实物的左右顺序：

| RK J703 | ADuM1201 RK侧功能 |
|---|---|
| 1 GND | G2 / 芯片5脚GND2 |
| 4 3V3 | V2 / 芯片8脚VDD2 |
| 3 TX | VIA输入 / 芯片7脚 |
| 2 RX | VOB输出 / 芯片6脚 |

STM32侧PD5/TX接VIB（芯片3脚）、PD6/RX接VOA（芯片2脚），V1/G1各取MCU3V3/地。商品排针不得套芯片脚序，1200方向不同；接线需断电，不跨接G1/G2。用户方向确认加实际双向UART成功是本卡证据，不补造照片/逐点电阻测量。

[ADI ADuM1200/1201 Rev.L](https://www.analog.com/media/en/technical-documentation/data-sheets/adum1200_1201.pdf)第28页PCB Layout规定10–100nF去耦及引线总长≤20mm。项目每侧优先100nF陶瓷，分别V1–G1、V2–G2靠近芯片；合格板载去耦可复用。10/22/47nF在手册范围内，长杜邦线末端电容不替代近端去耦。

RK实查ttyS4=fe680000.serial，GPIO3_B1/B2为uart4m1-xfer且无占用；驱动采用中断模式（不是MCU DMA），未改设备树。SecureCRT释放COM7后用SerialPort API/1.5Mbps读RK控制台，业务字节由RK Python/termios经UART4/115200送到ADuM与STM32。未把USB控制台当业务UART。

`uart-real-01.json`记录全部原始TX/RX和11项PASS；`board-after-uart-01.json`核MCU消费11103字节与RK完全一致、CRC/超长/旧session计数各1、TX满10与缺失10相符、HAL错误/RX覆盖0、UID一致和PB6输入/低。真实Journal、锁、硬截止、冷启动及整机闭环未执行，profile仍UNVERIFIED。
