# S2-03 MCU UART传输

`transport.c`由Communication任务独占；静态对象包含最多524字节流式累积、4个524字节TX槽、session与错误计数。没有堆分配、Flash调用、GPIO动作或ISR业务。2048字节DMA区和稳定副本由`firmware/tasks/communication.c`管理，DMA不得放CCM。

## 协议与复用

协议真源是`contracts/uart-layouts.json`与生成的`locker_contracts.h`。固件直接编译本仓库`host/device/src/wire.c`及`codec.c`，包含`host/device/include`；这两个源文件只依赖标准C和生成契约，不链接主机服务、POSIX或数据库。复用已有S1-03黄金帧/字段校验器，避免第二套字节布局。

HAL驱动复用锁定的本地ST HAL V1.8.5，不修改第三方文件。设计依据包括[ST官方HAL UART](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Src/stm32f4xx_hal_uart.c)与[ST官方ReceiveToIdle DMA示例](https://github.com/STMicroelectronics/STM32CubeF4/tree/master/Projects/STM32446E-Nucleo/Examples/UART/UART_ReceptionToIdle_CircularDMA)。HAL有完整许可且已有依赖锁；示例作为位置语义参考，不引入其板级配置。未额外采用Arduino封装或自制RTOS/串口协议。适配成本为F407资源与回调绑定，主要风险是DMA覆盖、通知延迟和旧session异步回执。

## 分派和资格

所有11类请求都先通过共享codec校验固定长度、枚举、游标、保留位及UUID。CRC错误或超过512字节长度只计数并重同步；有效CRC的非法版本/字段返回协议错误。输入response/event和request_no=0不分派，避免双向错误循环。旧session回BAD_SESSION并回显原session；HOST_HEALTH主机实例必须与HELLO一致。

HELLO支持未初始化日志诊断：epoch/boot/动作水位为0，Journal=UNINITIALIZED、模式=DEGRADED；能力仅允许门/物品和读卡，不宣称持久日志、合格ADC或限时脉冲。相同session+host重试幂等，相同session更换host拒绝。该诊断HELLO的成功只表示通信握手，不提供动作资格。

S2-03运行时不安装Control/Storage分派后端，其余10类请求均回NOT_READY，不伪造快照、持久ACK、开锁接纳或健康进度。`mcu_transport_dispatch_t`是后续装配入口：必须将完整`mcu_request_context_t`及DTO按值复制到真实所有者的有界队列；仅ACCEPTED/QUEUED表示异步等待。S2-05需同时装配HELLO的Control撤销/对账处理和真实Journal状态，不能把本卡诊断HELLO视为成品Control会话处理。

`mcu_transport_queue_reply`按原message_type/request_no编码，异步旧session回执丢弃计数；主机专用时间不编码。跨任务调用方先复制到Communication的RTOS inbox，不直接并发调用本模块。`queue_event`只用于Storage已经提交并分配epoch/boot/seq的事件；发送失败不清除日志，不推进ACK。

## DMA位置和背压

`mcu_transport_feed`接受任意分段、半帧、多帧。`dma_event`提供有序HT/TC/IDLE位置的纯C验证：Size是[1,2048]位置，重复不再消费，TC后从0开始下一圈；过时HT不重放。该辅助函数不负责证明硬件整圈未丢通知，不能用于替代HAL生产者/消费者覆盖检查。

生产HAL路径由ISR读取当前NDTR累计producer并通知；Communication取稳定副本、复核覆盖后再feed。未知整圈/延迟/硬件错误调用`rx_lost`，清半帧、撤销session并锁存故障。发送4槽满时不等待，不再接纳依赖响应的新工作并锁存故障；后续EXECUTE继续NOT_READY，HELLO不清故障。TX队头在DMA发送完成前保持地址和内容稳定。

## 验证入口

```text
cmake -S tests/firmware/transport -B <workspace-build>/transport -DCMAKE_BUILD_TYPE=Debug
cmake --build <workspace-build>/transport
ctest --test-dir <workspace-build>/transport --output-on-failure
```

Linux证据只在`ssh locker-vm`的卡工作区运行，src与build/log分离。正式测试包括契约黄金HELLO、所有切分位置、多帧、CRC/超长/版本/结构错误、11类请求、旧session/两个异步关联、TX过载、2048 DMA半帧/回绕/重复/过时HT/越界canary及已提交事件编码。主机测试与HAL shim不证明物理UART额定电平、真实抓帧或板上DMA时序通过，实物结果以本卡报告为准。

## 调研比较和诊断

2026-10-29交叉查看ST官方循环DMA示例及[MaJerle UART DMA方案](https://github.com/MaJerle/stm32-usart-uart-dma-rx-tx)（MIT许可，含HT/TC/IDLE位置解释与多系列示例）。官方示例与当前HAL API更直接对应；MaJerle方案有参考价值，但不导入其跨系列工程、LL或额外环形库，避免改动既有HAL路线和依赖锁。官方/社区示例的维护不能替代当前锁定HAL的实测；本卡使用本地源码核对回调，在同优先级IRQ中读NDTR消除延后HT通知的旧位置，再由任务检查覆盖。CRC仅用于误码检测，不能代替业务权限。

`s203_diag[0..2]`分别为S203标识、UART状态、任务循环数；状态0默认禁用、1已启动、2初始化失败、3错误恢复RX重启失败、4abort失败。`[4..9]`分别为RX通知数、HAL错误数、RX丢失/超时数、已消费字节、TX完成数、TX启动失败数；`[10..17]`为CRC、超长、旧session、TX满、payload、版本、unsupported、过时回复的低32位计数。完整64位计数在Communication拥有的transport对象中。诊断不提供输入注入或动作旁路。HAL shim仅顺序模拟IRQ/任务边界；TSan通过不证明真实DMA或目标板RTOS竞争不存在。

## 真实板验证

[本卡报告](../../docs/evidence/stage-02/S2-03/summary.md)记录HSE启用镜像和RK UART4经ADuM的真实字节流。`tests/firmware/transport/uart_board_check.py`在RK用标准库termios打开已核对的ttyS4，保存原始TX/RX、逐项结果并恢复原串口设置；只发HELLO/GET_SNAPSHOT，不发动作。`--self-test`仅检查黄金帧，不能替代物理测试。Windows的`rk-console.ps1`以直接SerialPort API访问已登录COM7控制台，需先由SecureCRT释放串口；不操作Windows终端UI，不输入凭据、不改保存的会话。

实测RX11103字节完整消费、HAL错误/覆盖0；CRC/超长/旧session计数各1；128突发请求回复118、发送队列满10次恰好对应10个缺失响应，后续请求恢复。默认/HSI构建仍仅编译，真实RX强制过载与硬件错误不冒称上板通过。CLI显式`--enable-uart`用于已核线本板，Journal/Control与锁资格保持关闭。
