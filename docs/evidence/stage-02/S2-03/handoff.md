# S2-03 交接

| 字段 | 当前事实 |
|---|---|
| 卡号 / 契约 / 起点 | S2-03 / 0.2.1 / S2-02 DONE 031e140；阶段一PASS |
| 状态 | DONE，active_task=null、recommended_next=S2-04；S2-04 NOT_STARTED；阶段2 gate NOT_RUN、profile UNVERIFIED |
| 构建入口 | `python -B scripts/build_firmware.py --target s203 --clock hse8_168`默认禁UART；`--check-uart-build`只编译；已核线本板`--enable-uart`输出build/firmware-s203-enabled；Keil生成：`generate_keil_s201.py --target s203` |
| 当前板上镜像 | HSE启用目标，HEX SHA=8ed563d9ad8198183edbe83709c9bae0d60a1538972d49e42fde687d312493cb；前两扇区回读/尾部保护通过；默认/HSI仅编译 |
| VM入口 / 退出码 | locker-workspace/S2-03/src/tests/firmware/transport；build/S2-03/{debug,asan,tsan}及logs在src之外；本卡3/3、ASan/UBSan3/3、TSan3/3，主机全量36/36，最终命令均0；三个设计checker通过 |
| 真实UART | RK ttyS4/uart4m1→J703/ADuM→STM32 USART2 PD5/PD6，115200 8N1；RK控制台COM7/1.5Mbps仅作指令/日志搬运。V1由5V改MCU3V3，V2实测3.3V、去耦/方向用户确认 |
| 实测结果 | uart-real-01.json 11项PASS：完整HELLO黄金响应、半帧/多帧/旧session/CRC/超长恢复、7168字节跨圈、128突发回复118及后续恢复；MCU消费11103/发送11103字节、HAL错误/RX覆盖0、TX满10=缺失10，UID与SWD一致 |
| 失败路径 | 旧session/字段/CRC拒绝，TX满锁存；RX丢失撤销session；abort或重启失败停止任务。烧录预检初版IntelHex成员查找挂起，连接探针前停止并保留记录；修正后烧录与实测成功 |
| 所有权 | Communication独占parser/TX，ISR不解析/不Flash/不业务；2048B环与副本、4槽TX。上下文沿用生成契约；后端必须复制请求/DTO到有界队列 |
| 未实现项 | Journal/Control后端NULL，除诊断HELLO外NOT_READY；S2-05正式装配回传inbox/HELLO撤销/Journal状态。栈高水位未配置，强制真实RX/硬件故障仅软件shim，不冒称实物通过 |
| 保持安全 | PB6输入/低、拒绝诊断15、fault0；supply_valid=false，不连接/驱动真锁，不push；用户授权止于S2-04之前 |
| 改动文件清单 | firmware/{transport/*,tasks/*,app/main.c,README.md,board/README.md,keil/locker_s203.uvprojx}；scripts/{build_firmware.py,generate_keil_s201.py}；tests/firmware/transport/*；阶段索引、AGENTS/根README、执行/阶段文档、docs/evidence/stage-02/S2-03/* |
| 证据 | summary/debt/wiring-check、源SHA清单、ARM摘要、vm-development/final、board-uart-startup-02、uart-real-01、board-after-uart-01；原始失败记录保留 |
| 清理 | Windows/VM传输包与RK中间base64清理；正式fixture/SWD脚本/日志保留；上一轮硬件/环境改动不混入本卡；保留原CRLF，按cr-at-eol检查diff |
| 耗时 / 轮数 | 2026-10-29约20:35–21:22、约47分钟墙钟（含接线/Flash校验等待）；统一Debug4轮、ASan/UBSan/TSan各1轮、主机全量3轮、真实烧录成功1轮、UART实测1轮 |
| 下一卡 | S2-04，仅交接推荐，未开始 |

具体回放：RK上`python3 uart_board_check.py --device /dev/ttyS4`输出JSON并恢复termios；Windows直接串口工具rk-console.ps1需SecureCRT释放COM7，不修改其保存的会话。当前自动采集已释放COM7，可由用户重新连接控制台。受保护烧录工具仅支持已验S202→S203首次转换，不盲目重跑；后续镜像状态必须先核对。
