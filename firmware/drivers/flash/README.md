# W25Q128 littlefs 块设备

S2-04 使用板载 EF4018 的 16 MiB SPI NOR，4096 个 4 KiB 扇区、256 字节页、24 位地址。`w25q128_init` 只读核对已验证几何、JEDEC、保护/挂起状态，不擦写、解除保护或格式化。未知几何/型号拒绝就绪。littlefs 配置使用静态 read/program/lookahead 缓冲，read_size=16、prog/cache_size=256、block_cycles=500，不分配堆。

Storage 唯一所有者静态持有 `w25q128_t`，配置 `context/transfer/now_ms/wait_ms`，调用 `w25q128_init(&flash, &geometry)` 后将 `w25q128_lfs_config(&flash)` 交给 Journal。`transfer` 对应 `mcu_board_spi_transfer(BOARD_SPI_FLASH, tx, rx, length, 50000)`；HSI16/SPI分频128时260字节约16.6ms，不能使用5ms超时；每次最多260字节，整条命令在一次片选事务内完成。每次调用返回时必须释放 CS 和总线互斥，`wait_ms` 在此后才调用，内部 busy 不占 SPI。对象、回调和上下文在整个挂载期间有效，不允许并发调用同一对象。read/program/erase/sync 都是阻塞的 Storage 内部操作，不能在 ISR/Control/通信任务调用。

页编程按页边界拆分；每次写/擦前检查保护和 WEL，完成后检查 busy/WEL 并读回验证。页编程超时10ms、扇区擦除/首次busy/sync超时1000ms，每1ms释放总线后等待；轮数上限同时防止时间源停止导致无限等待。任一传输、busy、WEL或读回异常锁存为不可用，后续写入拒绝，重新初始化只能由 Storage 在恢复流程进行；不能把失败写入按成功提交。禁止整片擦除、改保护寄存器、自动 format 或写锁 GPIO。原始读写/扇区擦除供内部块适配和受限现场验收使用，没有公开 UART/HTTPS 调试入口。

## 复用调研与来源

| 方案 | 许可/维护核对 | 结论 |
|---|---|---|
| [littlefs 官方](https://github.com/littlefs-project/littlefs) | 本地锁定 v2.11.2、BSD-3-Clause、依赖SHA校验；成熟维护项目 | 直接复用锁定实现和静态 `lfs_config`，保留许可，不改原库 |
| [STM32-pio-libs/W25Q64-lfs](https://github.com/STM32-pio-libs/W25Q64-lfs) | 调研页仅3个提交，未显示明确LICENSE；容量为8MiB且示例自动format | 仅比较静态缓存与回调适配思路，不复制代码、不引入PlatformIO，不采用自动format |
| [nimaltd/spif](https://github.com/nimaltd/spif) | 多型号HAL/RTOS功能较广；本次未确认其LICENSE文件路径 | 仅比较支持面；避免引入额外HAL句柄/配置及许可不确定源码 |

命令及保护语义依据 [Winbond W25Q128JV Revision F 官方手册](https://www.winbond.com/resource-files/w25q128jv%20revf%2003272018%20plus.pdf)：03/02/20/06/05/35/15/9F 指令、BUSY/WEL、SR1 BP/TB/SEC、SR2 CMP/SUS 与 SR3 WPS。芯片丝印具体后缀尚须现场确认；EF4018 与几何不能替代所有电气/供电条件测量。1-1-1模式不改QE、保护位或OTP。实物擦写、busy期间RC522及掉电恢复证据由 S2-04 实物报告记录，协议fixture不能代替。

`tests/firmware/journal/test_flash.c` 是正式协议fixture，覆盖几何拒绝、末端地址、跨页拆分、WEL/保护/挂起、读回错误、传输错误、停钟/永久busy、静态littlefs配置及故障锁存；由同目录 `flash-test.cmake` 接入本卡CTest。
