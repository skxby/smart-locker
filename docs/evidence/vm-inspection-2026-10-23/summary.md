# 实际VM检查、使用与代码整理

SSH已真实连接%USER%@192.0.2.10，guest为Ubuntu26.04.1/7.3.0-rc5/x86_64。旧地址192.0.2.11不可达不是系统损坏的充分证据；VMware Tools返回的新地址已通过严格主机密钥SSH验证。

已核对并保留两个源码批次：vm-build-summary.json是顶层src的旧快照，其source-correspondence.json明确记录与Windows三个日志相关文件不同；不能作为当前整改源PASS。current-vm-build-summary.json才是S1-01/src批次，其CMake/源/头/测试共19文件与Windows当前版本对照；构建前后不变，独立配置/编译/链接及6项CTest通过。两个批次的构建和日志均在src之外，未覆盖开发AI正在使用的build/日志。

driver-import-manifest.json包含1217文件的历史来源/归档/源码副本/指纹；driver-import-verification.json记录最初副本复核。第一批迁移已完成636个HOME直属条目/1797文件，含10个原root目录；driver-home-relocation.json当前PASS，无受阻条目。第二批home-organization.json记录剩余159个根文件、5个旧目录（164条目、278普通文件、1,772,545,215字节）的分类迁移和元数据核验PASS。两批共800个直属条目，未永久删除文件、未执行未知构建/脚本或装载旧ko。

VM桌面、Projects目录和GTK书签保留借还柜项目/当前源码/内核驱动入口。HOME根下不再有可见散落文件，仅23个有用途目录，详细位置见[目录说明](../../development/vm-home-layout.md)。unclassified-home-entries.json是第二次归类前的历史快照。完整内核树、交叉工具链、现有Python环境和模型工程未搬动；单个ONNX/GGUF等原根文件已归AI资料目录。

VMX网络变更前备份为vm-config-before-network.vmx.backup；原bridged改NAT并设置startConnected，但当前ens33仍NO-CARRIER、无默认路由，修复尚未闭合。host-only SSH正常。Windows增加locker-vm别名，不改旧条目。sudo -n要求认证，未绕过、未重启/切换/重编内核。打印模块/SquashFS xattr配置告警已定位，未声称修好。

本轮没有改变产品卡状态、原HAL、模型、BOM或S1源码；正式检查报告/备份/迁移工具保留，无临时业务mock或固定成功API。详见[工程化布局与问题清单](../../development/vm-inspection-and-code-layout.md)。
