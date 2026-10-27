# STM32F407VG 的 CubeMX 配置参考

日期：2026-10-26。对象：天空星 STM32F407VGT6 高配候选板，CubeMX 芯片选择 `STM32F407VGTx / LQFP100`。本文是操作与配置参考，不是已生成的 `.ioc`，没有操作用户的 CubeMX、下载固件或完成硬件验收。具体菜单可能随 CubeMX 和固件包版本变化。

## 生成边界与版本

CubeMX 生成 MCU 外设初始化、HAL配置和工程骨架；不会自动生成 RC522 协议、W25Q128 块设备、littlefs适配、借还业务或安全锁驱动。项目已有[独立环境工程](../../firmware/README.md)、HAL 1.8.5 与锁定的 FreeRTOS 11.2.0/littlefs，正式集成继续复用这些依赖。

本次配置参考先保存并生成到项目外独立目录，例如 `E:/stm32-cubemx-reference/locker_f407_ref/`，避免覆盖 `firmware/keil`、`firmware/third_party`、`STM32F4xx_HAL_Code` 或现有中断文件。创建/生成需在CubeMX执行，本文不声称该目录已建立。

`STM32Cube FW_F4` 整包版本与 `STM32F4xx HAL` 驱动版本不是同一个编号。生成前在 Project Manager 查看所选固件包；不要只因版本数字不同就更新原HAL。核其HAL头中的版本宏与依赖锁，若不匹配，保留独立参考工程并记录版本差异，不能把新包库覆盖进产品目录。当前已有HAL源码，目的应是配置初始化，而不是重新下载整个HAL。

## 1. System Core

| 菜单 | 设置 | 说明 |
|---|---|---|
| SYS / Debug | Serial Wire | 保留PA13 SWDIO、PA14 SWCLK，避免下载口丢失 |
| SYS / Timebase Source | TIM6 | HAL 1ms时间基准；SysTick/SVC/PendSV归现有FreeRTOS |
| RCC / HSE | Crystal/Ceramic Resonator | 仅针对确认板载8MHz晶振的候选方案，不选外部时钟Bypass |
| RCC / LSE | 暂不启用 | RTC需求及晶振到板后单独验证，当前不增加RTC模块 |
| NVIC / Priority Group | 4 bits pre-emption，0 bits subpriority | 与F407的4位中断优先级及FreeRTOS配置一致 |

SYS选TIM6后让CubeMX生成HAL时基文件，不再把TIM6分配给锁或其他任务。不必为此另外在TIM6界面设第二套基础定时器。独立参考工程的时基文件与现有 `firmware/env/hal_timebase_tim6.c` 不能一起链接。

## 2. Clock Configuration：HSE8到168MHz候选

先核板上晶振为8MHz；未核实或启动失败时用既有 Env_hsi16 做环境诊断，不照搬168MHz方案的分频参数到16MHz时钟。

| 项 | 值 |
|---|---|
| HSE input | 8 MHz |
| PLL source | HSE |
| PLLM / PLLN / PLLP / PLLQ | 8 / 336 / 2 / 7 |
| System Clock Mux | PLLCLK |
| SYSCLK / HCLK | 168 MHz / 168 MHz |
| AHB Prescaler | /1 |
| APB1 Prescaler / PCLK1 | /4 / 42 MHz |
| APB2 Prescaler / PCLK2 | /2 / 84 MHz |
| APB1 timer clock | 84 MHz（本分频条件下） |
| APB2 timer clock | 168 MHz（本分频条件下） |
| PLL48CLK | 48 MHz；不表示已启用USB |

对应现有 Env_hse8_168 使用电压Scale1、Flash latency 5；由CubeMX按供电条件生成并核对，不手改成原课程库中的25MHz/F429配置。时钟树不能有未解决的超频/冲突标记。

## 3. 外设与GPIO分配

引脚为[接线候选表](../hardware/引脚确定与接线表.md)的原理图分配，未实物验证。点击芯片图上的指定GPIO可选择功能；外设自动分配到其他脚时须改回下表，不能接线一套、生成一套。

| 外设/用途 | GPIO | CubeMX功能/初值 | 可选用户标签 |
|---|---|---|---|
| Flash SCK/MISO/MOSI | PA5/PA6/PA7 | SPI1对应信号 | 保留外设默认名 |
| Flash CS | PA4 | GPIO_Output，High，Push-Pull，No pull | FLASH_CS |
| RC522 SCK/MISO/MOSI | PB13/PB14/PB15 | SPI2对应信号 | 保留外设默认名 |
| RC522 CS | PB12 | GPIO_Output，High，Push-Pull，No pull | RFID_CS |
| RC522 reset | PE3 | GPIO_Output，Low，Push-Pull，No pull | RFID_RST |
| RK链路 TX/RX | PD5/PD6 | USART2_TX / USART2_RX | 保留外设默认名 |
| 调试 TX/RX | PA9/PA10 | USART1_TX / USART1_RX | 保留外设默认名 |
| 12V分压采样 | PC0 | ADC1_IN10，Analog，No pull | SUPPLY_ADC |
| 门磁 | PE0 | GPIO_EXTI0，双边沿，No pull，外接10k上拉 | DOOR_IN |
| 工具微动 | PE1 | GPIO_EXTI1，双边沿，No pull，外接10k上拉 | ITEM_IN |
| 带驱动蜂鸣器 | PE2 | GPIO_Output，Low，Push-Pull，No pull | BUZZER_OUT |
| 锁控制预留 | PB6 | 本参考先GPIO_Output，Low，Push-Pull，No pull | LOCK_CTRL |

普通输出速度先Low；SPI复用脚以CubeMX生成的适合速度为起点，不把所有GPIO无差别设Very High。PE0/PE1已有外接上拉，不能同时设置Pull-down。没有焊外部上拉时可在脱离功率负载的单项验证中使用内部Pull-up，并记录与正式接线的区别。

PB6/PE2的Low默认态仅针对“高电平有效”候选；模块极性没核之前，MOS/锁保持物理断开。PE3保持Low意味着RC522仍处于复位，后续驱动须释放并按时序初始化，不能以生成工程已读卡解释。

### SPI1和SPI2

均选 Full-Duplex Master、8 bits、MSB First、Software NSS、CPOL Low、CPHA 1 Edge（Mode0候选）、CRC Disabled。PA4/PB12是软件GPIO片选，不选Hardware NSS；SPI1优先保持PA5/6/7，避免自动映射到其他脚。

初期Prescaler可各取32：SPI1约2.625MHz、SPI2约1.3125MHz（基于上述时钟树）。先不用SPI DMA，减少DMA资源争用；后续根据器件规格、线长和实测调整。每次访问前后由设备驱动控制各自CS，CubeMX不会生成这一事务逻辑。

### USART1与USART2

均选 Asynchronous、115200、8 bits、None parity、1 stop bit、TX/RX、None flow control、Oversampling16。USART2特别指定PD5/PD6，不用默认PA2/PA3；USART1的PA9/PA10用于日志，不接RK控制线。

USART2 DMA Settings：

| 请求 | Stream / Channel | Direction | Mode | 配置 |
|---|---|---|---|---|
| USART2_RX | DMA1 Stream5 / Channel4 | Peripheral to Memory | Circular | Peripheral不递增、Memory递增、两侧Byte，FIFO先Disable |
| USART2_TX | DMA1 Stream6 / Channel4 | Memory to Peripheral | Normal | Peripheral不递增、Memory递增、两侧Byte，FIFO先Disable |

打开USART2 global interrupt、DMA1 Stream5/6中断。CubeMX只配置DMA，不会自动启动ReceiveToIdle，也不会建立环形缓冲解析/溢出处理/发送队列；后续采用现有HAL的 `HAL_UARTEx_ReceiveToIdle_DMA` 与RxEvent路径。接收缓冲放DMA可访问SRAM，不放CCM。USART1暂不加DMA，按实际日志实现决定是否启IRQ。

### ADC1

选PC0的IN10，12bit、Right alignment、单通道Rank1、软件触发、Scan Disable、Continuous Disable、DMA Continuous Requests Disable，采样时间先480 cycles。ADC Prescaler选PCLK2/4，上述时钟下ADC时钟21MHz。由后续任务决定采样频率，不在CubeMX中生成持续高速采样。

100k/22k源阻抗约18kΩ，长采样时间是初期候选，仍需标定。PC0只能接分压中点，不能接12V；VREF+配置和MCU掉电注入问题见[接线验证](../hardware/接线验证操作与记录.md)。配置生成不代表这些电气门槛通过。

## 4. 锁TIM4与看门狗的分步边界

本次参考用PB6 GPIO安全占位，暂不启TIM4动作。后续切PB6为TIM4_CH1 AF2，实现并验证硬件单脉冲/期限到达自动回到关断态，不能用连续PWM、任务delay或定时器中断后软件拉低代替。时钟、PSC/ARR/CCR、触发、输出极性/空闲态和掉电行为由锁规格及正式阶段2卡确定，不预填未经确认的脉宽。TIM6始终留给HAL tick。

IWDG本轮暂不启用，避免空工程没有刷新路径即复位；硬件/调度稳定后按正式监护卡接入。RTC、SDIO/TF、USB CDC、ETH、CAN、I2C等当前无接入要求的外设先不启；Type-C作5V供电不需要启USB Device，现有日志使用CH340。

## 5. FreeRTOS与中断

Middleware / FREERTOS 本独立参考先Disabled：保留项目已锁定的FreeRTOS 11.2.0、RVDS/ARM_CM4F、heap_4和现有配置。不要直接把CubeMX固件包自带FreeRTOS/CMSIS包装混入当前工程，或再生成同名SysTick/SVC/PendSV实现。

需要向现有内核FromISR交接的USART2、DMA1 Stream5/6、EXTI0/1候选均取Preemption Priority=6、Subpriority=0，满足本工程逻辑阈值5的约束。数值越小优先级越高，不能把“6”写成“比5更高”。HAL TIM6时基保持生成器/现有时基的独立优先级与归属；集成时核 `HAL_InitTick` 和TIM6_DAC只有一份实现。图形界面未启FreeRTOS时NVIC页不会替你校验全部RTOS约束。

## 6. Project Manager与Generate Code

1. Project Name：`locker_f407_ref`；Project Location使用独立英文路径；Toolchain/IDE选MDK-ARM，不选STM32CubeIDE。
2. 核对Cube固件包版本和HAL驱动版本。原HAL保持不改，不能依靠勾选“Use latest available version”自动更新产品依赖。
3. Code Generator选择 `Copy only the necessary library files`；勾 `Generate peripheral initialization as a pair of .c/.h files per peripheral`、`Keep User Code when re-generating`、`Backup previously generated files when re-generating`。初期取消自动删除未再次生成的文件，减少误删手写文件风险。
4. Advanced Settings中相关外设Driver均为HAL，不切LL，不加SPL。`USE_RTOS`保持0；这不是启用FreeRTOS的开关。
5. 选择Generate Code。若提示工具链兼容问题，按本机Keil/ARMCC5实际版本核项目设置，不自动升级库或假定新CubeMX输出必然适配旧MDK。

“Copy all used libraries”可复制所使用HAL库的更完整内容，但不意味着所有外设都已经初始化。选择necessary可得到本参考所需文件；本项目完整HAL源码已在本地，无需为得到库而启用全部外设。

## 7. 生成后检查

通常会有 `.ioc`、Core/Inc、Core/Src、Drivers/CMSIS、Drivers/STM32F4xx_HAL_Driver、MDK-ARM等目录（老版本布局可能不同）。分文件生成时应能找到gpio/spi/usart/adc/dma的`.c/.h`、`stm32f4xx_hal_conf.h`、MSP/IRQ文件和HAL的TIM时基文件。由于本次未启TIM4，不要求一定生成独立tim.c。

检查：

- 主芯片宏STM32F407xx、USE_HAL_DRIVER、单一F407启动文件，Flash为1MB；DMA数据只放可访问主SRAM。
- SPI1=PA5/6/7，SPI2=PB13/14/15，USART2=PD5/6，ADC1=PC0；GPIO初值和片选相符。
- `HAL_InitTick`、TIM6_DAC_IRQHandler及SVC/PendSV/SysTick归属无重复；参考工程与正式环境工程不能整包混链接。
- 记录CubeMX、Cube固件包、HAL驱动、Keil版本与编译日志。生成成功只证明生成，不证明编译，更不证明实物功能。
- 将 `.ioc` 与生成结果审查后再按正式卡提取初始化改动，不能直接覆盖现有工程。当前S1-04及阶段2卡状态不因本文改变。

## 官方依据

- [ST CubeMX UM1718](https://www.st.com/resource/en/user_manual/um1718-stm32cubemx-for-stm32-configuration-and-initialization-c-code-generation-stmicroelectronics.pdf)：外设、HAL时基、工程与代码生成选项。
- [ST RM0090](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-415-stm32f407-417-stm32f427-437-and-stm32f429-439-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf)：F407时钟、DMA映射、ADC和定时器行为。
- 项目本地事实：[环境工程](../../firmware/README.md)、[HAL核对](../hardware/本地HAL库核对.md)、[引脚表](../hardware/引脚确定与接线表.md)。
