# 当前给开发AI的收尾指令

适用状态：S1-01 的最新独立审计已 PASS，机器索引仍 IN_PROGRESS。先读取实际文件；状态变化时以新证据为准，不重复开工或回退 DONE。

将下面整段发给原 S1-01 开发 AI 或本卡唯一收尾负责人。它是现有卡的交接收尾，不是再次开发基础库。独立审计 AI 保持审计职责，不与开发负责人并发修改报告和状态。

```text
项目主副本：%REPO%。请完成当前 S1-01 的文档勘误和交接收尾，暂不开始 S1-02。

先读 AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
以及 docs/evidence/stage-01/S1-01/ 下的 summary.md、handoff.md、audit.md、
source-manifest.txt、vm-migration-review.md。
读取 audit.md 顶部最新结论；后面的历史 FAIL 不作为当前审计结论。
目前最新审计是 PASS、无阻塞项、存在 N5/P3 文档勘误。先核实它仍对应当前代码，不能只看 PASS 字样。

访问现有 VMware Ubuntu：Windows 执行 ssh locker-vm，当前地址 %USER%@192.0.2.10。
VM 项目入口 /home/%USER%/locker-workspace，已审计的 S1-01 源码快照为
/home/%USER%/locker-workspace/S1-01/src；顶层 locker-workspace/src 是旧快照，不作为本次依据。
先读 VM 的 WORKSPACE-INDEX.md 和 docs/development/vm-ubuntu-workflow.md。
VM 桌面“当前借还柜源码”及文件侧栏可直接进入当前快照。
不安装 WSL、不重装工具链、不升级或重启 VM，驱动与散落资料归档已完成。
build/runtime/logs/artifacts 放项目独立工作区，不在用户目录根或 src 生成杂物。
Windows 是源文件主副本，VM 只接收固定指纹快照；不要两边独立修改再覆盖。

1. 按最新审计 N5 修正 summary/handoff/vm-migration-review 的当前说明：
   实际任务卡 SHA、真实 Git 检查退出码、N2 已关闭、当前 VM 源路径及旧 src 身份。
   读取当前值，不照抄历史散列；旧批次日志保留，不能改名充当新证据。
2. 核对 source-manifest 对应的 29 个文件、契约及 VM 当前快照，
   说明复用哪些 vm-final2/独立审计证据。仅文档勘误且代码和相关构建输入指纹未变时，
   复用对应已通过证据；代码或构建输入发生变化时再补跑受影响的测试并复审。
   TSan 等未通过/受环境限制项按实际保留，不写成全环境全部 PASS。
3. 检查本卡临时残留，保留正式测试、审计探针、变异验证和历史诊断证据。
   按卡执行契约、阶段计划、视觉契约和可行性检查，记录实际结果。
4. 作为本卡唯一收尾负责人，在证据和文档一致后，按执行手册的交接流程
   同步 summary/handoff、stage-plan 中 S1-01 的 DONE/report 和当前入口/推荐下一卡。
   若发现新的阻塞问题，保持 IN_PROGRESS 并列出待审项，不能借已有 PASS 掩盖新修改。
   不自行重写 audit.md 的独立结论；确需复审则交回原审计 AI。
5. 交付本卡收尾清单、证据路径、限制和下一张 S1-02 的输入后停止。
   不自动开发 S1-02、不把阶段1 gate 设 PASS，不 commit/push。

本轮不搬动或覆盖活跃的 S1-01/src；后续 canonical src 提升由唯一集成负责人
在没有并发构建/编辑时单独安排，并同步路径索引、指纹、文档及新构建目录。
内核驱动工作区独立位于 /home/%USER%/kernel-driver-workspace，旧脚本不可当借还柜构建入口。
```

若交给新 AI，先发送项目目录和本文件全文，让它读取磁盘上的报告；不要只粘贴上一轮聊天摘要。S1-01 收尾后，再单独使用 [S1-02 任务指令](../ai-prompts/S1-02.md) 的开发段落。
