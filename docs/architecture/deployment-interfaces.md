# 部署与维护接口设计

状态：后续实施规格，无现成工具。首用网络/真实UART/公开配对导出在S2-06实现；以下维护能力在[阶段5](../stages/stage-05-delivery-task-cards.md)落实。没有新增远程维护API，不改变24个公开入口。

## 1. 运行配置所有权

视觉模型/策略/profile部署见[视觉接口](vision-interfaces.md)：Runtime固定socket/UID和Core策略，worker固定相机/机位/模型包，手机不能写。模型/管线/ROI/阈值仅在无未决操作的维护窗口切换，不复用旧采集。deployment.unverified.json始终为未验证模板；S2-11/12新增带真实证据的profile/bundle并由运行配置引用，不把模板改VERIFIED冒充验收。

| 项目 | 生产者/消费者 | 固定约束 |
|---|---|---|
| RK运行配置 | 现场维护→Runtime | DB/串口/证书/进程/UID路径仅本机受限配置；业务请求不能写 |
| HAL profile | S2实测→BSP | 板型/引脚/极性/ADC/定时器全部验证；App不可上传 |
| 配对公开材料 | 本机导出→Android | 与客户端5字段一致，≤8192字节，不含私钥/凭据 |
| release-manifest | 构建→离线制品检查 | 版本、契约/schema/Journal兼容、目标ABI/芯片、各制品SHA256和许可 |
| 监督检查点 | supervisor独占 | 限次/incident先持久化后启动；部署/回退不重置额度 |
| 备份manifest | 离线备份工具→现场维护 | 契约/DB schema、源设备/epoch、备份哈希/时间/一致性范围，不声称实物已回到备份状态 |

私钥、密码、token、APK签名密钥不进入版本库或公开包。业务数据库备份包含账户哈希/会话及历史等敏感数据，保存在本机受限维护目录，禁止作为演示fixture分发。首版不新增云备份服务或远程文件下载接口。

## 2. 本机命令边界

下列命令名是待实现约定，不宣称已经可运行。路径参数只接受已配置维护目录的实际规范绝对路径；不拼shell、不接外部网络参数执行任意程序。

| 命令 | 使用阶段 | 前置/结果 |
|---|---|---|
| `locker-admin pair-export --output <公开文件>` | S2-06 | 读取本机证书/地址及已核对设备ID，输出5字段；不读/复制私钥，不创建用户会话 |
| `locker-admin maintenance-reconcile` | S2-07 | 受限admin peer+当前ADMIN会话，经service.sock现有maintenance.reconcile，不直接SQL/驱动锁 |
| `locker-admin backup --output <备份目录>` | S5-03 | service停止、离线维护互斥成功后sqlite3_backup；失败不替换旧副本 |
| `locker-admin inspect-backup --input <备份目录>` | S5-03 | 只读检查独立副本/哈希/schema/完整性，不接管营业库 |
| `locker-admin verify-release --input <发布目录>` | S5-01/04 | 只读验证manifest/哈希/兼容/目标；不刷固件、不执行包内脚本 |

没有通用`restore --force`、在线任意SQL、网络格式化或任意进程入口。首个管理员仍用S1本机显式初始化；维护命令不能自动重建默认管理员。忘记密码不能引导AI临时加远程重置API。

## 3. 模块内函数规格

只由host/cli同一维护工具负责人实现，以下是模块内static辅助接口，不加入跨进程/跨模块公共C端口计数。共享数据通过既有接口或离线文件取得，不能把Core内部指针传给工具。

```c
static locker_status_t pair_export(const PairExportInput *in,
    PairExportResult *out);
static locker_status_t offline_acquire(const OfflinePaths *paths,
    OfflineLease *out);
static locker_status_t backup_create(OfflineLease *lease,
    const BackupOptions *options, BackupReport *out);
static locker_status_t backup_inspect(const BackupOptions *options,
    BackupReport *out);
static locker_status_t release_verify(const ReleaseOptions *options,
    ReleaseReport *out);
static void offline_release(OfflineLease *lease);
```

| 本地类型 | 字段/所有权 |
|---|---|
| PairExportInput | 已配置HTTPS地址、已核对24字符device_id、公开证书路径、输出路径；不接受从未认证网络读出的身份作为真值 |
| PairExportResult | 文件路径、DER完整SHA256、导出字节数；输出为客户端固定5字段格式 |
| OfflinePaths | 运行状态/单实例锁/数据库/维护目录的规范路径；加载自本机配置，不能由HTTP指定 |
| OfflineLease | 工具持有的OS互斥FD和服务停止核对状态，失败无有效句柄；只在当前进程生命周期有效 |
| BackupOptions | 配置解析后的源/目标路径、schema允许列表；不含业务SQL、密码或远程URI |
| BackupReport | 完成/失败、输出哈希、schema、完整性检查结果、错误分类；写开始不等于成功 |
| ReleaseOptions | 发布包和当前已知目标/契约/schema/Journal版本；只读验证配置 |
| ReleaseReport | 哈希/目标/兼容各项结果、拒绝原因；verify成功不等于已经安装 |

生命周期：先验证所有路径/权限，再获取互斥；所有退出路径释放FD/关闭库。备份先写新独立文件，检查成功才以本机文件系统原子重命名发布manifest；跨文件的半完成目录不作为有效备份。临时文件位于受限目录，失败可清理本次未发布文件，不能删旧有效数据。库读取/检查使用已有SQLite，不自研DB复制格式。

## 4. 停机、备份与恢复边界

```text
现场管理员核验未决操作/门已关/脉冲已停
→ 受控停止service（拒绝新请求/停止IO/收敛提交/关闭DB）
→ 取得同一离线维护互斥，确认服务未运行
→ sqlite3_backup读取完整已提交状态，产生独立副本
→ 校验/哈希/manifest → 退出并释放互斥
→ 人工启动service → 新session/屏障/补取/核对/CAS
```

Runtime在打开营业DB前必须持有同一单实例/离线互斥，防止“检查PID后服务又启动”的竞态；服务退出后才释放。不能仅凭PID不存在决定可离线写库。备份工具只读取营业库、写独立副本；业务写者仍为Core。

旧备份恢复首版仅允许到隔离目录检查，不能覆盖营业库再READY。备份时刻之后可能已有真实借还、锁动作、ACK回收；DB回退会丢事实与水位。灾难恢复需单独维护流程，对照现存MCU日志/实物/外部证据，保留缺口与人工核验；无法证明完整不能恢复营业。MCU Journal损坏同样不自动format。

## 5. 更新与回退边界

release检查必须验证目标、所有SHA、契约、DB schema和Journal兼容。首版只允许不需数据格式迁移的更新；不兼容版本退回独立迁移设计，不临时加入自动转换代码。

程序更新先受控停机，切换固定程序/配置，人工重启service再核对。失败时可回退兼容程序/配置；**数据库、MCU Journal/动作水位、监督额度不回退**。固件通过既有SWD工具在受控维护状态刷写，禁止顺带擦外部Flash或绕过板型/profile核对。首版不设计OTA、双分区或远程刷机。

## 6. 复用与验证依据

- [SQLite Backup API](https://www.sqlite.org/backup.html)：采用官方备份机制生成一致副本；不另写裸文件复制协议。复用项目SQLite依赖，不增加新数据库或网络服务。
- [SQLite WAL](https://www.sqlite.org/wal.html)：WAL属于已提交状态的一部分；不能把.db单文件复制或删WAL当正确恢复。
- [littlefs](https://github.com/littlefs-project/littlefs)：沿用已锁定BSD-3-Clause版本；文件系统恢复不能证明账本回退后与现实一致。

本轮只核对设计可执行性；命令、备份、更新和24小时现场验收均未运行。
