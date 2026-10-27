# VMware Ubuntu开发与验证

主机端默认复用用户已有的VMware Ubuntu。用户已确认它就是S1-01报告中曾执行干净构建的同一台虚拟机。不安装第二套Ubuntu、不要求WSL、不迁移或删除WSL历史数据。

## 1. 环境与目录所有权

| 环境 | 职责 |
|---|---|
| Windows %REPO% | 当前源文件/契约/任务卡/正式证据的主副本，AI按卡修改 |
| 既有VM Ubuntu x86_64 | Linux C/CMake/CTest、服务/模拟器/Unix socket开发验证；使用当前主副本的固定快照 |
| Windows Keil | 已配置STM32 HAL/FreeRTOS/littlefs环境，继续保留 |
| RK3568 | 后续真实ABI/串口/屏幕/NPU/硬件验收，不由x86 VM结果代替 |

已通过VMware Tools和严格主机密钥SSH实际确认地址192.0.2.10，可使用Windows别名locker-vm登录；旧192.0.2.11条目保留。实际guest检查/源码批次/网络待修见[工程化记录](vm-inspection-and-code-layout.md)。SSH/共享文件夹均可用于传输；编译/数据库/socket/运行状态置于VM本地Linux文件系统。共享文件夹可以交换源包和日志，不能默认它支持所需权限与socket语义。

VM统一使用独立工作区$HOME/locker-workspace，src仅放源码；build/runtime/logs/artifacts与src同级，所有新编译、数据库/socket、日志和打包产物都在src之外。不得在原共享源目录或旧练习目录堆生成文件；不移动/删除现有源码。工作区目录已实际确认存在；当前开发AI的整改源在S1-01/src，顶层src为旧快照，先按WORKSPACE-INDEX.md与host-environment.json选源，待活跃卡交接后再统一canonical路径，不能覆盖正在使用的源码。保持一处源码权威：Windows AI修改后，向VM传固定指纹快照进行构建，回传日志；不让两端同时独立修改后互相覆盖。若以后改为VM直接开发，应明确切换主副本与同步流程。

用户入口：VM桌面或文件侧栏的“借还柜项目”进入工作区，“当前借还柜源码”进入S1-01/src；/home/%USER%/Projects包含同名快捷入口。驱动与散落资料归档已完成，根下不存散落文件；各类资料位置见[VM目录说明](vm-home-layout.md)。后续AI禁止在HOME根下新增临时脚本、构建、日志或下载包，必须写入所属工作区。

## 2. 第一张卡不重做

S1-01报告记录：整改前曾在VMware执行干净构建/CTest；最后整改后的回归在WSL执行，VM未重跑。此为报告提供的历史证据，该段描述环境文本切换时的历史；本轮已另行真实检查VM并对当前S1-01快照独立构建/CTest6项通过，具体指纹与限制见工程化记录，不覆盖原日志。

保留当前C代码、CMake、正式测试、依赖锁、旧WSL/VM日志和卡状态。当前源码搜索未发现host/、CMakeLists.txt、deployment/host/或现有脚本依赖WSL命令/挂载路径；因此环境切换以源快照同步及VM复验为主，不重写基础库。

S1-01完成后，由同一执行负责人按[VM最终复验指令](../stages/S1-01-vm-final-review.md)复核最新源。旧VM PASS只覆盖旧指纹，不移植为整改版PASS；只有新日志明确来自VM且版本一致，才能满足新的默认环境验收。最新独立审计已经PASS、存在N5文档勘误；[当前收尾指令](../stages/S1-01-current-handoff.md)供原开发AI核对指纹、同步文档并按原卡交接。未发生源码/构建输入变化且证据匹配时可复用已审计证据，不重复配置环境或把全部测试无条件重跑。不额外创建产品卡或重置IN_PROGRESS。

## 3. VM复验步骤

1. 开发负责人停止修改并提交当前源文件清单/SHA。只同步该快照，不带WSL的build目录、socket、DB、凭据。
2. VM在本地Linux文件系统接收源码。测试代码与Windows主副本比对，记录实际绝对路径/文件系统。
3. 记录实际uname、os-release、CPU架构、CMake、C编译器、Make和CTest版本。报告中的发行版标签仅是历史记录，不是强制升级目标；不为切环境升级VM内核或发行版。
4. 使用全新构建目录运行配置、构建、CTest及本卡必要回归。失败先按日志定位，不降低-Werror或删测试。
5. 在新的vm-final-*日志中记录环境、代码指纹、命令、退出码。旧日志不覆盖；最新source-manifest由卡负责人核实范围后更新，不能伪造文件时间/版本。
6. VM原始日志存工作区logs，不写src；只将需要归档的正式证据回传到Windows该卡目录，独立审计复核后才可收尾DONE；阶段1仍由S1-07完整验收。

## 独立目录布局

```text
$HOME/locker-workspace/
├── src/                  当前固定指纹的源码副本
├── build/S1-01/<批次>/   CMake缓存、对象、库、测试程序与CTest输出
├── runtime/service/      开发服务DB/socket/运行状态
├── runtime/simulator/    独立模拟设备数据
├── logs/S1-01/<批次>/    环境、构建、测试、探针及sanitizer日志
└── artifacts/            源快照包、证据归档、交付包
```

在VM现有源码副本中可执行`python3 -B scripts/prepare_vm_workspace.py`，只创建/检查这些目录，不复制、不覆盖、不删除源码。也可在VM终端执行：

```bash
mkdir -p "$HOME/locker-workspace"/{src,build,runtime/service,runtime/simulator,logs,artifacts}
```

先将固定指纹源码导入src（不带旧build/数据库/socket），核对一致后再构建。目录创建不等于源码同步或测试通过。

以下命令在VM Bash执行，不依赖当前终端处于哪个源目录：

```bash
set -euo pipefail
workspace_root="$HOME/locker-workspace"
src="${LOCKER_VM_SOURCE_ROOT:-$workspace_root/S1-01/src}"
# 当前已验快照为S1-01/src；后续由集成负责人统一后，可设置LOCKER_VM_SOURCE_ROOT。
test -f "$src/CMakeLists.txt"
test -f "$src/contracts/contract-source.json"
run_id="$(date +%Y%m%d-%H%M%S)-$$"
build_dir="$workspace_root/build/S1-01/$run_id"
log_dir="$workspace_root/logs/S1-01/$run_id"
mkdir -p "$build_dir" "$log_dir"
{
  uname -a
  cat /etc/os-release
  printf 'source=%s\nbuild=%s\nlogs=%s\n' "$src" "$build_dir" "$log_dir"
  findmnt -T "$workspace_root"
  cmake --version
  gcc --version
  make --version
  ctest --version
} > "$log_dir/environment.log" 2>&1
cmake -S "$src" -B "$build_dir" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON 2>&1 | tee "$log_dir/configure.log"
cmake --build "$build_dir" -j2 2>&1 | tee "$log_dir/build.log"
ctest --test-dir "$build_dir" --output-on-failure 2>&1 | tee "$log_dir/ctest.log"
```

这是复验方法，不是本轮已执行记录。断言数量随真实测试源码核对，不靠固定数字判断测试充分性。ASan/UBSan/TSan及审计探针按原卡需要重新运行，不能从WSL日志换标签充当VM证据。

完整项目的设计检查可在源根运行python3 -B scripts/check_contracts.py、check_stage_plan.py、check_vision_contracts.py、check_feasibility.py。若只同步了CMake最小树、没有scripts/docs/config，则它只支持该范围的C构建，不能声称设计检查也在VM执行。

## 4. 后续各阶段统一规则

S1服务/模拟器与后续本机IPC在该VM开发验证；S2-06仍核对真实RK用户态ABI和交叉工具链，不把x86产物放到RK运行。S3 Flutter/Android与Windows工具可保留，LVGL Linux主机测试复用VM，屏幕/触摸最终在RK验证。

RKNN离线转换优先复用同一VM，但Python版本/架构和SDK wheel必须按实际兼容检查，在独立虚拟环境隔离。不能因为VM能编译C，就宣称已具备RKNN；不随意降级系统Python或覆盖现有训练环境。

阶段1默认loopback测试不要求改变VM网络。以后需要手机访问开发VM时再核对桥接或实际端口转发、地址与证书SAN；NAT不等于自动可访问，不能靠关闭TLS实现连接。正式手机/RK同LAN目标不变。

## 5. 历史与当前规则

docs/evidence和reviews内WSL命令、版本与日志是实际历史，必须保留。当前阶段卡、180份执行指令和环境文档以VM默认环境为准。历史报告与新规则有差异时，补新的环境复验说明与证据，不改写历史PASS/FAIL。
