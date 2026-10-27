# S1-01现有VM最终复验指令

给正在完成S1-01的原开发AI。它是当前卡的环境收尾，不开始S1-02，不重复开发基础库。

```text
用户确定后续主机端使用已有VMware Ubuntu，即原vm-*.log所在那台guest。
不要再安装或要求WSL，不重置S1-01，不重写已经符合POSIX的基础代码。
读取docs/development/vm-ubuntu-workflow.md、docs/stages/S1-01-current-handoff.md及最新audit.md，保留原源码/正式测试/依赖锁与历史日志。
已有当前指纹对应的VM复验与独立审计时核对后复用；源码或构建输入变化才补受影响检查。

等本卡整改完成并冻结源指纹后，把当前快照同步到该VM本地Linux文件系统。
先确认实际guest身份/OS/架构、工具版本、目录/文件系统、源文件SHA，
不要只引用原报告的版本或把Windows共享路径转换成/mnt/e。
未确定连接方式时说明缺哪项；不猜IP/凭据、不修改VM网络或升级系统。

使用已存在的独立$HOME/locker-workspace，build/runtime/logs/artifacts在源码之外；保留旧源目录。
当前已审计固定快照为S1-01/src，顶层src是旧快照，按WORKSPACE-INDEX.md选源，不覆盖活跃源码。
需要补复验时在build/S1-01下全新目录配置、编译、CTest和整改回归/探针，
日志放logs/S1-01，DB/socket放runtime，归档放artifacts，不在src或旧共享源下生成这些文件。
记录完整命令、退出码、实际测试/断言结果及源码指纹。
VM日志先保存在工作区logs，必要正式证据回传Windows本卡目录并采用新的vm-final-environment.log、vm-final-configure.log、
vm-final-build.log、vm-final-ctest.log及必要的vm-final回归日志；不覆盖旧vm-*.log。

更新本卡summary/handoff，区分原WSL整改结果、旧VM结果、当前VM复验结果。
补一份vm-migration-review.md说明POSIX兼容核对、同步内容、实际改动和剩余NOT_RUN。
若只改文档而没跑VM，明确VM复验NOT_RUN，不把原WSL PASS视为VM已通过。
当前缺硬件/旧发行版/RK/Android的未验证项继续保留，不因VM复验而消失。

完成后交原独立审计AI复核当前VM日志及对应源码，不自行改审计结论。
审计前保持本卡IN_PROGRESS；只有负责人确认必要条件满足才按原收尾流程标DONE。
不跳到S1-02、不把阶段1设PASS，不commit/push，不改公共契约或Keil/模型原包。
```
