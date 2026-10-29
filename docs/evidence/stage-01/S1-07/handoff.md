# S1-07 当前交接

| 字段 | 当前事实 |
|---|---|
| 卡号 / 状态 | S1-07 / DONE；阶段1 gate PASS，S2-01未开始 |
| 契约版本 | 0.2.1 |
| 入口 | `tests/integration/runtime/test_stage_gate.py`；CTest名`integration.stage_gate_process` |
| 实际调用 | Python测试进程启动simulator和service，经HTTPS/IPC发送请求、控制socket驱动传感器/读卡、SQLite只读核对；所有子进程退出后删除测试临时目录 |
| 检查命令 / 退出码 | VM `cmake -S ... -B ...`、`cmake --build`、`ctest --test-dir ... --output-on-failure`：最终`vm-matrix01/`的Debug/ASan+UBSan/TSan均0且各36/36；进程测试连续3次退出0；Windows四项设计检查各0。 |
| 失败路径 | 历史实时读卡失败日志保留于`vm-gate01/`；修复前归还卡已入日志但操作停在REQUESTED。当前未闭合项见`summary.md`场景矩阵。 |
| 改动文件 | `host/domain/src/domain_core.c`、`tests/integration/runtime/CMakeLists.txt`、`tests/integration/runtime/test_stage_gate.py`、本卡报告与相关当前入口文档；硬件/Keil未提交改动不属于本卡。 |
| 已关闭问题 | 实时读卡偶发丢失已定位为Core处理设备事件前单调时间未刷新，`host/domain/src/domain_core.c`已修正；最终矩阵见`summary.md`。 |
| 限制 / 下一卡 | VM服务断链后需显式重启才重建UART会话；S2-01尚未开始。硬件原始记录待入库，不据口头确认推进S2实物门禁。 |
| 边界 | 不改变公开/私有契约；不进入S2-01；不触碰用户未提交硬件/固件文件 |
| 轮数 / 耗时 | 历史快照dev01、final01、gate01、fix01；开发者验证clock01、matrix01两轮，本卡累计约55分钟 |
