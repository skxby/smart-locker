# S2-03 后续边界与非阻塞待验

| 项目 / 位置 | 当前事实与后续责任 |
|---|---|
| UART运行裕量 | HSE真实链路通过；HSI仅编译、栈高水位未配置、冷启动/长期误码未验。S2-05四任务正式装配时测实际栈/堆裕量，S2-08核集成证据 |
| RX失调度/硬件错误 | 主机HAL shim验证覆盖/170ms失调度、HAL error、abort/重启失败；实物本轮错误0。没有通过调试内存注入来冒充真实故障验收 |
| 后端/HELLO装配 | S2-05接真实Control/Storage有界队列与Communication回传inbox，HELLO须正式撤销旧session资格、结合Journal状态；当前只诊断且动作NOT_READY |
| RK部署 | ttyS4/uart4m1已实查，本轮安全测试采用Python/termios；RK驱动提示无DMA而用中断。业务服务、目标ABI/权限/断接恢复仍由S2-06实现，不在本卡修改设备树 |
| 独立隔离资格 | 用户报告两侧去耦、V1改3V3、V2实测3.3V及方向确认；串口功能通过不等同隔离耐压/无USB接地旁路资格，profile仍UNVERIFIED |
| 既有待验 | PC0实物标定按授权后移，supply_valid=false；Flash擦写/busy/Journal留S2-04，硬截止与真锁留S2-05，整机门禁未通过 |

本卡未采购或发生费用。正式诊断和测试保留；旧硬件/环境未提交材料未删除，亦未混入本卡提交。
