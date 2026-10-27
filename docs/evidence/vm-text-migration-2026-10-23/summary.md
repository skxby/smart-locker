# 默认主机环境切换：执行文本核对

日期2026-10-23；用户确认复用S1-01历史VM验证的同一台VMware Ubuntu。本轮修改执行规格与指令，不连接guest、不安装/升级OS、不运行C测试或改变产品卡状态。

## 已更新

README、AGENTS、阶段1公共环境约束、阶段导航、通用交接流程、45卡指令生成器/全部180份指令/完整手册及指令索引、源指纹manifest和执行资料包。新增VM工作流、S1-01 VM最终复验指令及host-environment.json。

主机端默认VM Ubuntu，Windows继续保存当前源主副本与Keil；固定指纹快照在VM本地Linux文件系统构建。无需WSL启动器或/mnt/e路径转换，不假定共享目录具备全部POSIX/Unix socket语义。RK板端ABI/NPU和真机验收仍另做。

## S1-01保留与待完成

读取已有summary/handoff/audit和vm-configure.log发现：已有VM干净构建证据，但报告明确最后整改只在WSL重跑，整改版VM仍NOT_RUN。用户确认同一台VM，不等于当前代码已通过VM。旧WSL/VM日志、报告、源码/测试/依赖锁/source-manifest全部保留，本轮未改写这些证据，也未操作stage-plan状态。

修改前后代码搜索未发现host/、CMakeLists.txt、deployment/host/或既有执行脚本依赖WSL专用命令/挂载路径，因此不重写C基础库。待本卡整改结束后，原负责人按新的VM最终复验指令同步最新源、记录真实环境/指纹，在新构建目录复跑并写vm-final-*日志，再交独立审计收尾。

实际VM版本/SSH地址/长期源目录未在本轮重新探测，配置以null/待复验表示；不把旧报告版本硬设为新环境要求。历史报告继续保留其WSL事实，绝不批量替换历史日志中的运行环境。

## 校验范围

验证指令生成一致性、45卡/180段VM默认语义、当前文档链接与项目设计检查。结果只代表文本与设计一致；没有新增VM运行PASS，没有重置/完成S1-01或启动下一卡。没有临时业务代码/假日志、没有commit/push。
