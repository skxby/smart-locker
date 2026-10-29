# S1-06 交接

| 字段 | 当前事实 |
|---|---|
| 卡号 / 契约 / 状态 | S1-06 / 0.2.1 / DONE；前置426d04e；本地一次提交，不push |
| 入口 | README → docs/stages/README → stage-plan → S1-06 summary；完整空目录操作见host/runtime/README.md |
| 停止点 | active_task=null、recommended_next=S1-07；S1-07 NOT_STARTED、阶段1 gate NOT_RUN；本次不进入S1-07 |
| 复现命令 | python -B docs/evidence/stage-01/S1-06/verify-vm.py --run <新唯一批次> --sanitizers；仅ssh locker-vm的VMware Ubuntu |
| 最终候选 | final-20261028-03.tar.gz，SHA256 be5b0a736bf36ed6f6fb0a8e6e51774ec5771f130e2eb3905bc7d69b3211d439；3982文件manifest；收尾只更新文档/状态，不改已测C/Python/CMake输入 |
| 验证 | Debug、ASan/UBSan、TSan配置/构建/CTest均0；分别35/35，48.43/51.91/55.99秒；静态五检查0；原始日志vm-final-03/及export/LastTest |
| 程序 | locker-admin init --config FILE --username NAME --tool-name NAME [--password-stdin]；locker-service --config FILE；模拟器沿用--uart/--control/--journal独立进程 |
| 配置 | deployment/host/simulator.example.json固定11字段；mode只准SIMULATOR_ONLY；无凭据，默认loopback；state规范绝对路径<80字节、0700，业务库0600、socket0600 |
| 初始化 / 互斥 | 显式管理员+索引+单工具同事务；拒绝任何已有主DB/sidecar；先取得共享instance.lock再开DB，关DB后释放；不可删除锁inode，不清库掩盖失败 |
| 密码 | 控制终端关闭回显；受限stdin显式选择，匿名pipe或私有本人FIFO/普通文件；12–128可打印ASCII；无argv/env/log口令；PTY成功和SIGINT恢复实测 |
| 装配 / 生命周期 | 同一个随机host_instance传Core/HELLO/健康；先Core绑定sink再device_open；先停HTTP/IPC、关闭设备回调、停止Core，最后销毁依赖/关库/释放锁；stop未成功继续等待 |
| 健康 | runtime_health_owned=true关闭旧Core sender；Runtime每秒搬运真实不可变样本，同计数不重置时刻；UI零UUID/UINT32_MAX、监督UNKNOWN；真实SQLite锁竞争线上progress149不变、age1008→4016ms，解锁恢复 |
| 共享变更 | CR-S106关闭CR-S104-1与READY误报：Core配置device_simulated传递来源，Device/持久投影一致；有适配器时缺当前会话/快照RECONCILING，其余跟session；生成器维护头，所有内部C调用方需重编 |
| 资源 | Core队列64、HTTP4、WS16；新增fixture只编入正式测试。既有Core容量、publisher环/慢WS回归继续通过 |
| 失败路径 | 配置错误2，运行/初始化失败1，正常退出0；重复init、sidecar、端口、损坏DB、锁占用均有诊断；DB不删除；异常socket不自动覆盖 |
| 环境恢复 | 2026-10-29 VMware Tools报告172.100.1.99；原192.0.2.10主机密钥严格匹配，locker-vm别名更新HostName并保留HostKeyAlias；没有关闭SSH/TLS核验。10-28测试日志日期保持原样 |
| 文件范围 | runtime/cli与正式测试、根CMake、生成器/契约头/semantics、Core必要投影/发送门、配置与当前文档；完整文件清单以本卡git diff和summary为准 |
| 残留 / 限制 | 无本轮临时生产代码/凭据/私钥入库；正式测试及日志保留。原硬件/Keil变更不混入提交。仅x86模拟，实物/RK/掉电/UI/视觉均未验收；非阻塞范围见debt.md |
| 验证轮数 | 3个固定快照：assembly01 Debug33/33；assembly02 Debug35/35；最终03 Debug/ASan+UBSan/TSan各35/35。sanitizer仅终轮一次，未因文档收尾重复跑 |
| 耗时 | 可证实现/验证窗口10-28 21:24:52（首个源码创建）至21:47:52，共23分钟；前置调研未单独计时。10-29约09:06起恢复环境/回传/交接另计；中间离线等待不算活跃开发工时，总活跃工时未完整计量，不伪报精确总数 |

下一卡须核对S1-07正文的完整场景矩阵；本卡启动测试不是该矩阵的替代物。
