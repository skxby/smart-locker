# S2-02 输入、RC522与SPI

构建：`python -B scripts/build_firmware.py --target s202 --clock hse8_168`；独立Keil工程由`python -B scripts/generate_keil_s201.py --target s202`生成。S201目标保留安全BSP，不启用外设。

## 软件与所有权

- `inputs.c`执行原始电平到契约枚举的映射；未核准极性、采样错误、无标定或ADC越界均产生UNKNOWN/供电无效。`input_qualification.h`保存逐项实测资格，不将全局硬件profile标成VERIFIED。
- PE0/PE1每10ms采样，启用内部上拉，并保留接线表的外部10k上拉。闭门/有物的低有效极性已由两种真实状态核对；上拉使触点断开时有确定高电平。两线触点不能诊断断线；不能把普通高电平标成断线故障。
- `mcu_board_read_inputs`返回带时间的映射原始副本，超过100ms未更新则NOT_READY并清为UNKNOWN。复制使用FreeRTOS临界区。30ms稳定判定由可复用纯函数提供，诊断显示去抖结果；后续Control仍是业务去抖、physical_revision与事件的唯一所有者，不在BSP递增版本或生成业务事件。
- 供电换算使用64位整数和四舍五入：ADC码×实测参考毫伏×(上臂+下臂)/(4095×下臂)。ADC轨到轨码无效，范围可由标定收紧；电压类别阈值留S4-02。PC0未接/未标定时ADC保持关闭、supply_valid=false，不捏造12V。
- 用户授权PC0实物标定后移，不阻塞本卡软件交付，实物补证仍须在S2-08基础门禁前完成。`--check-adc-build`仅将ADC分支编译到独立的`build/firmware-s202-adc-compile-only/`；不能用它冒充标定或作为当前烧录入口。
- PeripheralStorage是RC522操作的唯一调用者。驱动只返回UID事实，覆盖ISO14443A三级级联4/7/10字节，检查BCC、SAK CRC和级联一致性；碰撞、超时、错误或截断清空结果。多卡冲突拒绝，不尝试猜选UID。读取不调用Control、Journal、账本或锁入口。
- SPI1为板载Flash（PA4–7），SPI2为RC522（PB12–15、PE3复位）。各总线独立FreeRTOS互斥锁；取锁不等待，争用返回BUSY；ISR/调度器启动前拒绝。片选先预置高再改输出，所有HAL传输返回路径均拉高片选并释放锁。单次长度1–512字节，timeout_us接收1000–50000，HAL超时向下取整为毫秒，实际截止粒度为HAL tick并可能有调度延迟；不作为锁硬截止。
- HSE目标SPI1/2分别为656.25/328.125kHz；HSI目标均125kHz，Mode0、8bit、MSB。超时/总线事务计数和最大耗时以毫秒诊断，不宣称微秒实测精度。
- Flash只读JEDEC(9F)和STATUS(05)，不写使能、不擦写、不挂载；每条命令后即释放总线，不在busy状态持锁等待。真实擦写busy与读卡延迟组合试验须由S2-04补证。本卡不把读到WIP=0写成busy试验PASS。

## 板上诊断

`s202_diag[40]`通过SWD只读，不提供网络/串口模拟入口。0为发布序列（奇数写入、偶数完成），读者须前后序列相同。

| 索引 | 含义 |
|---|---|
| 1/2 | 门磁/微动原始电平 |
| 3/4 | 映射后的door/item枚举；未知=0 |
| 5/6/7 | supply_valid、ADC原始码、供电毫伏 |
| 8/9 | 64位采样时间的低/高32位 |
| 10/11 | RC522初始化状态、版本寄存器 |
| 12/13/14–23 | 当前读卡状态、UID长度、10字节UID容器；失败清空 |
| 24/25 | Flash JEDEC三字节、状态字节（传输错为FFFFFFFF） |
| 26/27/28/29 | SPI1/SPI2事务数、争用数、HAL错误数 |
| 30/31 | 最大单次读卡毫秒、fault |
| 32/33 | 30ms去抖诊断door/item |
| 34/35/36 | SPI1/SPI2最大传输毫秒、成功读卡次数 |
| 37/38/39 | 最后一次RC522交换的IRQ、ErrorReg，以及FIFO字节数<<16 /有效位数<<8 /命令首字节 |

采集入口在`docs/evidence/stage-02/S2-02/read-diagnostics.py`；物理标签由操作者描述，不向MCU注入电平。慢速SWD无法在10ms采样周期内连续读完40个字，因此脚本在核对运行镜像后短暂停核读取，并在finally恢复运行；不写内存、不复位、不烧录，采样扰动不用于宣称实时截止。全局profile维持UNVERIFIED，PB6不配置，脉冲入口始终拒绝；复位瞬态与真锁硬截止留S2-05。

测试入口为`cmake -S tests/firmware/inputs -B <源码树外构建目录>`和CTest；只在VMware Ubuntu运行Linux检查。`hal-shim/`是正式主机测试fixture，运行实际BSP传输函数，以pthread互斥替代RTOS原语并注入HAL超时，检查同总线并发互斥、错误释放、ISR拒绝和长度边界。它不编进固件、不代替传感器/读卡实测；主机TSan不能证明目标板FreeRTOS调度行为。

## 调研与复用决定（2026-10-29）

| 来源 | 许可、维护与风险 | 决定与适配成本 |
|---|---|---|
| [ST HAL](https://github.com/STMicroelectronics/stm32f4xx-hal-driver) | BSD-3-Clause；官方维护；HAL轮询的毫秒超时不能充当硬截止 | 复用已锁定本地V1.8.5及FreeRTOS互斥，不下载新HAL、不改原库 |
| [miguelbalboa/rfid](https://github.com/miguelbalboa/rfid)及[src/MFRC522.cpp](https://github.com/miguelbalboa/rfid/blob/master/src/MFRC522.cpp) | Unlicense/公有领域；README声明功能冻结、偶尔维护；Arduino/C++整库适配较重，UID不能独立作为身份安全证明 | 借鉴寄存器初始化和级联选择流程，以有界C代码适配既有BSP端口；不引入写卡、Crypto1认证或Arduino运行时 |
| [Arduino_MFRC522v2](https://github.com/OSSLibraries/Arduino_MFRC522v2) | LGPL-2.1；README说明偶尔维护；接口拆分更灵活，仍依赖Arduino；静态固件集成需额外处理许可和C++层 | 参考驱动/传输分离思路，不复制其代码、不新增依赖 |

官方[MFRC522手册](https://www.nxp.com/docs/en/data-sheet/MFRC522.pdf)为寄存器协议参考；本轮网络获取超时，初始化/级联细节交叉核对上述公开实现。成熟库的读卡能力不等于本板模块和接线已通过，实物结果只取本卡证据。

实际模块版本为0x82。兼容分支参考[上游原始报告#59](https://github.com/OSSLibraries/Arduino_MFRC522v2/issues/59)，只接受这一明确版本，不宽放全部未知版本；本板配置寄存器写回核对和两张4字节UID读卡通过，仍不据此声称芯片为NXP正品。SELECT后发送HLTA、下一轮WUPA，支持卡片保持在场时重复观察；失败清空UID，不保留上次成功值。
