# S2-03 HAL UART DMA 与通信任务开发者验证

| 字段 | 当前事实 |
|---|---|
| 卡号 / 契约 / 前置 | S2-03 / 0.2.1 / S2-02 DONE（031e140）、阶段一主机门禁PASS |
| 状态 | DONE；本卡软件及真实UART验收通过，阶段2门禁NOT_RUN、profile仍UNVERIFIED；S2-04未开始 |
| 入口 | `python -B scripts/build_firmware.py --target s203 --clock hse8_168`默认禁UART；`--check-uart-build`只编译启用分支；已核线本板用`--enable-uart`独立目录；`generate_keil_s201.py --target s203`生成默认禁UART工程 |
| 修改范围 | firmware/transport、通信任务、app装配、独立S203构建/工程、正式C/HAL/串口验收工具及当前文档；复用wire/codec，不改host实现/契约/HAL |
| 产物 | 11请求校验/分派、诊断HELLO、旧session拒绝、关联上下文复制约定、4槽TX背压、提交事件编码；2048B循环DMA+稳定副本、NDTR去重/覆盖检查、错误恢复/诊断 |
| 安全边界 | Journal/Control后端为NULL，除诊断HELLO外NOT_READY；不接纳动作，不伪造快照/持久ACK。profile UNVERIFIED、supply_valid=false；PB6持续输入/低 |
| ARM构建 | ARMCC5.05u1/MDK-Lite5.14，两个时钟的默认/启用编译检查均exit0，HSE实际启用目标另构建exit0；40单元、254依赖哈希匹配，原HAL未改 |
| 内存 | 默认HSI/HSE ROM=16540/17948 B、RAM=23320 B；UART ROM=22296/23704 B、RAM=27704 B；低于32KiB Lite/128KiB主SRAM，DMA不在CCM，map-memory.json记录地址 |
| VM普通验证 | 最终本卡3/3；首个配置遇文件写入中失败保留，随后正式测试通过；最终清理后再Debug/全量回归，日志见vm-final |
| VM sanitizer | ASan/UBSan 3/3、TSan 3/3，各收尾一轮exit0；shim顺序模拟IRQ/任务，TSan不代表真实DMA/目标RTOS竞态动态验证 |
| 主机回归 | 源码未变，S1-07 VM Debug入口全量36/36；2872个主机输入再次与Windows/S2-02清单逐文件SHA一致；含清理后复验 |
| 正式C场景 | HELLO黄金帧/所有切分、多帧/CRC/超长/512上限/版本/字段、全部11请求、旧session/host、两request_no与迟到回执、背压、DMA回绕/重复/过时HT/canary、event96 |
| HAL shim场景 | 默认禁引脚、115200 8N1/DMA资源、ISR只通知、半帧/跨圈/重复通知、2048整圈覆盖、170ms失调度、TX寿命、abort/重启失败、TX提交重试 |
| 接线 / RK | 用户确认ADuM接线、去耦、V1由5V改MCU侧3V3、V2实测3.3V及UART4方向。COM7/1.5Mbps只作RK控制台，业务字节经J703 UART4/ttyS4→ADuM→PD5/PD6 USART2 |
| RK就绪 | Linux4.19.232/aarch64、Python3.8.6；ttyS4=fe680000.serial，GPIO3_B1/B2为uart4m1-xfer，无占用。RK驱动用中断模式，未改设备树/系统；不是S2-06部署交付 |
| 上板 / 指纹 | HSE启用镜像HEX SHA=8ed563d9ad8198183edbe83709c9bae0d60a1538972d49e42fde687d312493cb；整片旧状态守卫、前两扇区32KiB回读、镜像后原尾保留和0x8000后未变均PASS；board-uart-startup-02.json |
| 运行 / 时基 | 6次采样fault=0、拒绝诊断15、UART状态1、Communication持续进展，HAL/RTOS约1000 tick/s；NDTR在2048环内。默认/HSI未在本卡烧录，栈高水位未配置 |
| 实物黄金 / 恢复 | uart-real-01.json：11项PASS；HELLO完整响应逐字节比对，UID=000000000000000000000000与SWD芯片一致；半帧100ms、多帧、旧session BAD_SESSION、CRC/513超长拒绝及恢复通过 |
| 实物回绕 / 过载 | 连续7168垃圾字节后合法帧恢复；全轮TX=11103字节，MCU消费=11103，HAL错误0、RX覆盖/失调度0；CRC/超长/旧session计数各1。128突发请求回复118个，MCU TX满10次与缺失10个精确一致，随后合法请求恢复；总TX完成128，无重复帧 |
| 整改 / 失败 | 修复TX错误无完成通知时必须由任务abort后释放缓冲；复制后复核覆盖/长延迟。烧录工具初版IntelHex成员测试陷入预检，停止自有进程，未连接SWD/未擦写，记录board-preflight-aborted.json；改为地址集合后02版上板通过 |
| 文档 / 残留 | 入口、阶段索引、资源、固件/传输README与交接同步；临时传输包/编码包清理，正式fixture/SWD工具/原始日志保留；既有硬件/环境改动不混入本卡 |
| 耗时 / 轮数 | 2026-10-29约20:35–21:22，约47分钟墙钟（含接线/完整Flash校验等待）；统一Debug4轮、ASan/UBSan及TSan各1轮、主机全量3轮；真实烧录成功1轮、UART实测1轮 |
| 下一卡 | recommended_next=S2-04，仅推荐；本次授权止于S2-04之前，不自动开始 |

证据与命令见vm-final、run-vm-validation.sh、build-*.json、source-manifest.json、integration-manifest.json及[接线记录](wiring-check.md)。主机固定清单引用[S2-02清单](../S2-02/host-source-manifest.json)，本卡两端再次核对。实际RX过载/强制硬件故障、HSI串口、冷启动、栈高水位不冒称通过；软件边界/真实TX背压已分别验证。后端、持久Journal、锁硬截止/复位瞬态及整机门禁仍属后续卡，不因本卡DONE而获资格。
