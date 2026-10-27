# VM项目与资料目录

VM用户为china。主目录 /home/%USER% 根下不保存散落脚本、日志、安装包或单个模型产物。后续AI只能在所属工作区内创建文件，不能重新把HOME当作临时工作目录。

## 项目入口

打开桌面“借还柜项目”“当前借还柜源码”“内核驱动项目”，或进入 /home/%USER%/Projects。

| 入口/路径 | 用途 |
|---|---|
| /home/%USER%/locker-workspace | 借还柜工作区 |
| /home/%USER%/locker-workspace/S1-01/src | 当前已审计S1-01源码快照，顶层src是旧快照 |
| /home/%USER%/kernel-driver-workspace | Rust hwmon/TMP108旧驱动源码、补丁、历史实验 |
| /home/%USER%/Projects/legacy-learning/originals | hhh、myproject、test、xyx.git练习原件，包含Git历史和原有swap文件 |
| /home/%USER%/Projects/ai-lab/imported-assets | 根目录原有ONNX/GGUF、BGE压缩包/转换脚本/requirements及vllm历史日志 |
| /home/%USER%/Projects/tools/installers | 原有deb/whl、Rust安装程序，仅归档未执行 |
| /home/%USER%/Projects/tools/maintenance | 本轮正式目录整理工具 |
| /home/%USER%/Projects/system-maintenance/originals | 网络、APT、GRUB、系统诊断脚本和日志、配置副本、公钥备份 |

内核驱动 archive/home-originals 保留第一批636个HOME直属条目、1797文件；archive/home-supplement 保留第二批驱动散件、内核配置备份、旧ko和initramfs目录/压缩包。没有加载旧ko、执行旧脚本或修改系统生效配置。

## 保留原路径的目录

根目录保留23个目录，没有可见散落文件。其中Ubuntu标准用户目录为下载、公共、图片、文档、桌面、模板、视频、音乐；snap是应用数据目录。

以下既有目录不搬动，避免破坏当前安装或路径依赖：

- linux-rust：正在运行内核的模块build链接指向它。
- aarch64-linux-musl-cross：交叉编译工具链。
- proj101env、rknn-env、rknn-env152、tf4env、vllm-env：现有Python环境，启动脚本可能含绝对路径。
- bge_model、bge_work、models、llama.cpp-src：现有模型、转换工程和源码树，保持原有调用路径。
- locker-workspace、kernel-driver-workspace、Projects：正式项目和归档入口。

这些是有用途的目录，不是遗漏的散落临时文件。以后若要统一迁移模型工程或重建虚拟环境，应作为独立环境迁移任务验证，不能靠移动目录冒称兼容。

## 清单与核验

- /home/%USER%/Projects/home-organization.json：第二批164个HOME直属条目（159文件+5目录）的来源、目标、278个普通文件SHA及目录/符号链接/特殊节点元数据。合计普通文件约1.77GB，迁移后核验PASS。
- /home/%USER%/kernel-driver-workspace/docs/home-relocation.json：第一批636条目、1797文件的迁移记录，当前PASS、无受阻目录。
- /home/%USER%/kernel-driver-workspace/docs/import-manifest.json：最初复制归档的历史来源表，不表示旧路径仍存在。

所有移动均为同一文件系统内的重命名，未永久删除文件。旧脚本可能引用原HOME路径，归档是保留证据，不能直接当作新的构建入口执行。
