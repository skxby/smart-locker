# VM检查与代码工程化整理

日期2026-10-23。已真实登录guest、整理并迁移驱动原件、核验SHA和运行C构建测试。

## 1. 实际环境与访问

运行VM为D:/VMs/Ubuntu24.04.3/Ubuntu24.04.3.vmx。名称保留Ubuntu24.04.3，但实际guest为Ubuntu26.04.1、x86_64、7.3.0-rc5自编译内核；系统名以guest实际读取为准。

VMware Tools报告当前host-only地址192.0.2.10，SSH已有主机密钥/公钥认证验证成功。Windows新增本机SSH别名locker-vm，旧192.0.2.11条目保留。现在可执行ssh locker-vm。

## 2. 两个独立工作区

| 位置 | 内容 |
|---|---|
| /home/%USER%/locker-workspace | 借还柜源码快照、独立build/logs/runtime/artifacts |
| /home/%USER%/kernel-driver-workspace | 旧Rust hwmon/TMP108源码/补丁/实验的工程化入口 |

借还柜当前S1-01开发AI使用S1-01/src子目录。独立检查发现顶层src是旧快照，日志相关3文件与Windows整改源不同；S1-01/src中的19个CMake/源码/头/测试文件与Windows当前版一致。已在VM写WORKSPACE-INDEX.md指明当前入口，没有在活跃交付期间覆盖或搬动两份源码。S1收尾时由唯一集成负责人统一提升已验证快照到约定canonical src并归档旧快照，不能混用构建缓存。

可直接打开VM桌面的“借还柜项目”“当前借还柜源码”“内核驱动项目”，或文件管理器侧栏同名书签；同样的快捷入口位于 /home/%USER%/Projects。快捷入口指向原工作区，没有搬动活跃的S1源码。

## 3. 旧驱动整理结果

29个已识别实验目录、根目录散落C/Rust/补丁，共1217文件，约59.96MB原内容，按来源路径复制归档并验证SHA。布局：

```text
kernel-driver-workspace/
├── src/snapshots/                按原目录分组的源码/构建说明
├── patches/                      各版补丁系列
├── logs/legacy-import/           旧测试日志/记录
├── archive/original-snapshots/   完整原文件副本，含Git元数据/历史产物
├── build/                       后续构建输出
├── artifacts/                   后续交付产物
├── docs/import-manifest.json    每个原文件/副本/SHA/字节数
├── docs/import-verification.json 原件/归档/源码副本二次复核
├── kernel-tree -> /home/%USER%/linux-rust
├── env.sh                       会话内选择匹配的Rust1.85工具
└── README.md
```

未自动选择某个“final”或最高版本名当正式驱动，也未执行未知Makefile/装载旧ko。多个目录是完整Linux树的局部片段，先选定审查过的系列及对应内核，再准备独立输出构建；不能把所有目录直接make成模块。完整linux-rust树保持原位且未修改，没有重复复制大内核树或重编内核。

用户授权清理后，原1217文件和额外580个明确驱动历史脚本/日志共1797文件，已按SHA核验并将636个HOME直属条目全部迁到 archive/home-originals/。docs/home-relocation.json当前PASS、无受阻目录；10个root目录也已经完成迁移，不再需要sudo收尾。没有永久删除文件。

HOME剩余159个散落文件及5个旧目录也已按用途迁移，共164个直属条目、278个普通文件，约1.77GB。驱动散件和initramfs归archive/home-supplement；小型Git练习、AI产物、安装包、系统维护资料分别放Projects/legacy-learning、ai-lab、tools、system-maintenance，逐项来源/目标/散列和元数据见 /home/%USER%/Projects/home-organization.json，迁移后核验PASS。

现在HOME根下只有23个有用途的目录，没有可见散落文件。完整linux-rust、交叉工具链、现有Python环境和模型工程保留原路径；hhh/test/myproject/xyx.git已归档且保留Git元数据。详见[目录用途与查找入口](vm-home-layout.md)。初次unclassified-home-entries.json只记录整理前历史清单，不能当作当前目录表或删除清单。未把任何旧脚本作为新构建入口，也未宣称旧驱动已移植到新API。

## 4. 实际检查/待修事项

| 项目 | 结果 |
|---|---|
| SSH/VMware Tools/系统服务 | SSH成功、Tools运行，无failed systemd unit |
| 磁盘/内存 | 根ext4，约22GB可用；内存约6.2GiB，检查时可用约4.9GiB |
| 当前boot严重内核故障 | 未检出BUG/Oops/panic/根盘EXT4错误，tainted=0；不是历史所有故障都排除 |
| C工具链 | GCC15.2、CMake/CTest4.2.3、Make4.4.1可用 |
| 当前S1源 | 独立构建/链接exit0，6项CTest全通过，-Werror启用；构建前后源SHA不变 |
| Rust/内核构建关联 | 7.3.0-rc5的build指向linux-rust，有Module.symvers；Clang/LLD21.1.8、Rust1.85实际存在。SSH普通PATH无rustc，已提供仅会话生效env.sh，未改变全局工具链 |
| 主网络 | ens33原bridged且NO-CARRIER；已备份VMX并通过vmrun配置NAT/startConnected，但当前仍无carrier/默认路由，不声称互联网已恢复。ens37 host-only管理通道可用 |
| 自编译内核告警 | CONFIG_PARPORT未开，解释lp/ppdev缺失；CONFIG_SQUASHFS_XATTR未开，与SquashFS扩展属性报错相符。未重装snap/删除数据或重编内核 |
| 系统级修复 | sudo -n要求交互认证，本轮未使用密码/绕过认证、未重启或切内核。已有发行版7.0.0-38内核可作为后续维护选择，但未擅自切换 |

借还柜用户态C开发可以使用当前VM，不必为打印模块告警先重装系统。互联网、Snap及旧内核实验问题并未全部修复，不能将本轮C测试PASS扩大到全部驱动/系统功能。

原始C构建/指纹和驱动整理证据见[本轮记录](../evidence/vm-inspection-2026-10-23/summary.md)。本轮不替原开发AI标S1-01 DONE，也不代替它的全部整改回归/独立审计。

依据：[内核外部模块构建](https://docs.kernel.org/kbuild/modules.html)、[内核taint说明](https://www.kernel.org/doc/html/latest/admin-guide/tainted-kernels.html)。
