# 显式本机初始化（S1-06）

`locker-admin init` 只在未营业的新状态目录创建首位管理员、用户名索引和单工具，
经正式Repository事务写入；不提供清库、密码重置、直接开锁或任意SQL入口。
运行时与 `locker-service` 共用先于业务库打开取得的 `instance.lock`。
密码只从控制终端或显式受限stdin读取，存储Argon2id哈希。

构建、配置、完整空目录操作、输入限制及失败恢复边界见
[运行说明](../runtime/README.md)。初始化失败保留数据库，不自动重试覆盖。
