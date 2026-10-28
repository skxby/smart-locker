# S1-04当前验证入口

当前结论、验收矩阵和限制见[summary.md](summary.md)，调用与生命周期见[handoff.md](handoff.md)。原验证长附录归档为[历史记录](history/pre-takeover-verification.md)，其行数、批次和未完成项不作为当前结论。

机器结果：[takeover-validation.json](takeover-validation.json)。最终125项输入：[takeover-snapshot-manifest.json](takeover-snapshot-manifest.json)。正式复现脚本：[takeover-verify.py](takeover-verify.py)；新增连续归还集成的sanitizer复验：[takeover-return-sanitizers.sh](takeover-return-sanitizers.sh)。

本次为开发者验证；不新增或伪造独立审计报告。VM日志明确区分全量三套、最终Debug、定向集成sanitizer和预期失败的旧实现负向对照。固定快照均保留于VM本地文件系统，不用WSL证据。
