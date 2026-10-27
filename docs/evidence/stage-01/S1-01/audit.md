# S1-01 独立审计：当前 VMware 整改版复核

日期：2026-10-23（Asia/Shanghai）。本节为最新审计结论；后文保留前两轮原文作为历史证据，历史 FAIL 不代表本轮结果。

**结论：PASS，满足 S1-01 本卡技术交接条件，有非阻塞文档勘误。** 本轮没有发现未解决的阻塞缺陷。已在用户指定的现有 VMware Ubuntu 独立核对源码并全新构建，6/6 正式测试、505 个断言通过；B1/B2/B1-R/B3 原始与相邻复现均不再泄漏；ASan/UBSan 通过，故意恢复 B1-R/B3 后正式测试按预期失败。

这不是阶段 1 门禁 PASS，也不证明业务服务、设备模拟器或真机完成。本轮只写 audit.md，不修改 stage-plan、不置 DONE、不启动 S1-02。收尾负责人应同步下述非阻塞文档事项，再按既定流程处理状态和交接。

## 1. 输入与前置核对

执行前实际读取 contract-source：contract_version=0.2.1，status=REVIEW_DRAFT，与用户基线一致。已按要求读取 AGENTS、README、阶段入口、stage-plan、阶段 1 任务卡全文（本卡、公共约束、验收、附录），定向核对阶段 0 资源/架构、module-ownership、traceability、内部接口及本卡使用的 C 契约、firmware/README。

已读取当前 summary、handoff、source-manifest、VM 工作流/最终复验指令、vm-migration-review 和 vm-final2 证据，检查根/模块 CMake、实际 common 源码/头文件、六项正式测试和断言宏、依赖锁及整改探针。测试结论来自本轮执行，不取自文件存在或开发者自述。

| 输入 | 本轮身份 |
|---|---|
| contract-source SHA-256 | 14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f |
| 生成 C 头 SHA-256 | 34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a |
| 当前阶段 1 任务卡 SHA-256 | 4c71d6c8fcd06e685b4b040b13709de72db553dfe4ffc6c48e0469df4b8cb136 |
| 当前 source-manifest SHA-256 | ad0ebe41cd8fba5d9a025a6633b227d4c3300f3e50aae20d8f44cd15ad295176 |
| log.c / test_log.c SHA-256 | 1418a51fc49d28c69a022fa5ac985eff183f72a13e790d5db137fab25217a8ba / 7e8cf73515becf2c17aef356c2d3ab1f3878bf12a731c18f02f6365f9be10f5f |
| stage-plan SHA-256 | 54de2f04cdaa1e76858dabd37896f9755e6716f04b6304f8b16f151949da2f05 |
| 状态/前置 | 无前置实施卡，S1-01 IN_PROGRESS，active_task=S1-01，report=null，阶段 1 gate_result=NOT_RUN |

Windows 重新计算清单的 SHA-256/字节数：29/29 一致；将这份本地清单的预期散列通过 SSH stdin 送入 VM 执行 sha256sum -c：29/29 OK。另单独读取 VM 契约源/生成头 SHA，与上表一致。VM 源为 /home/%USER%/locker-workspace/S1-01/src，顶层旧 src 未使用或覆盖。

当前任务卡指纹已不同于 summary 中记录的旧值；已读取当前正文确认是 VM 默认环境/目录规则同步，本卡产出、验收和禁止越界条款未放宽，不静默使用旧任务卡副本。当前 Git 检查 exit=128，目录不是 Git 仓库，无法独立证明历史变动全集或开工前无文件。

## 2. 阻塞项与既有问题处置

**当前阻塞项：无。**

| 旧项 | 本轮核销证据 |
|---|---|
| B1 点号 token / 双引号空格密码 | 真实 logger 探针输出完整掩码，B1_VARIANTS_LEAK=0；正式测试通过 |
| B2 第 17 项后泄漏 | 流式脱敏无 span 预算；原探针 B2_17TH_LEAK=0，25 项回归通过 |
| B1-R 单引号密码 | log.c:264 起按传入 quote 扫描，键/值单双引号均处理；probe-boundary 的单引号、转义/未闭合与相邻输入通过 |
| B3 长键丢敏感后缀 | log.c:97 起对完整原始键逆向匹配后缀；69 字符键 LONG_KEY_SENSITIVE=1，sink 值为 [redacted]，LEAK_CASES=0 |
| N1 生命周期 | 头/交接说明明确 init/destroy/deinit 独占、先停线程，异步 sink 须复制借用 line；不再把并发 destroy 当安全用法 |
| N3 时钟说明 | 回调与契约一致；平台失败无错误通道、OK+0 限制已如实说明；本轮 handoff-example 编译/运行 0 |
| N4 UID 表示 | handoff:211–213 已明确 UID 不是 u64，保持 card_uid_len + bytes/hex，不改真源迎合文档 |
| N2 默认入口状态 | 当前 AGENTS/README/阶段入口已描述 S1-01 实施/复核，不再声称全部未开始；旧问题关闭，报告中的旧待办另见 N5 |

独立变异验证仅修改本轮构建目录的 log.c 副本：nonvacuity-mutate-r2.py 恢复双引号限定和截断后缀匹配后，直接运行正式 test_log 返回 1，12/164 项断言失败（328/329/334/339/344/349/354/355/361/380/390/391 行）。正式生产源码未动，证明新增断言确能识别两处缺陷。

## 3. 非阻塞问题

### N5 / P3：交付说明中的旧指纹与旧待办未同步

- **位置**：summary.md:18（旧任务卡 SHA）、:22（Git exit=1/空输出）、:252、:296（N2 尚未修）；handoff.md:451（默认入口仍全部未开始）；vm-migration-review.md:46 起的旧顶层 src 布局。
- **触发/复现**：读取当前文件/运行散列及 git 命令即可复现。实际任务卡 SHA 为上表 4c71…cb136，Git exit=128；默认入口已更新；实际当前源码路径是 S1-01/src，顶层 src 为旧快照，已通过 VM 的 WORKSPACE-INDEX 和实际散列确认。
- **影响**：可能误导后续读者选择旧源或重复处理已关闭问题。当前机器契约、29 文件散列和本轮真实构建已经独立消除输入歧义，不影响本卡技术验收。
- **建议**：收尾负责人将当前执行说明改成真实路径、指纹和 N2 已关闭状态；历史批次数据保留并明确归档，不能把旧日志改标签充当新日志。按当前值修正文档，不改契约、源码或验收预期。
- **阻塞级别**：非阻塞文档勘误；本轮仅可写 audit.md，未代改相关文件。

## 4. 按本卡范围逐项评价

| 条款 | 当前评价 |
|---|---|
| 模块化 CMake、干净 Linux 配置构建 | PASS：指定 VM 的 ext4、本轮全新构建目录；C11/Threads::Threads、6 源 common 静态库，无配置期下载 |
| 队列满明确失败 | PASS：NO_SPACE、不覆盖旧项且统计拒绝；空 pop/peek 为 NOT_READY，FIFO/环回、重复 init=CONFLICT 有正式测试 |
| 消息生命周期 | PASS：内嵌消息按值复制及生产者栈破坏测试通过；storage 由调用方提供并长于队列，含指针元素仍需深拷贝/显式所有权，不可误当通用深复制 |
| 时钟/超时/重复/重启 | PASS：真实单调钟与墙钟分离、测试端口使用既有契约；零超时、无效 deadline、饱和/到期与真实 20ms 烟测通过。common 重启重新 init，不持久化 mutex，不承担业务动作重放；时钟系统失败注入仍 NOT_RUN |
| UUID/u64 | PASS：小写规范 UUID、随机 v4/失败返回；u64 零/MAX/越界/非法文本/数值比较/溢出检查及失败保持输出有正式测试 |
| 日志不输出 token/密码 | PASS（已支持形态和本卡正式测试/独立复现范围）：单双引号/转义/未闭合、长键、无 span 预算均已核对；不是任意无标记秘密的数据流证明，调用方仍须用敏感字段 |
| 资源上限/线程/回调 | 固定 storage、无堆分配，日志行 512 字节有界；队列数据操作 mutex 保护，生命周期调用独占。sink 可并发/阻塞，user/component/clock.user 必须存活，line 只在回调期间借用。输出内存有界不证明任意输入耗时有界 |
| 权限/持久化/副作用 | 未见公共调试 API、业务监听、SQL、GPIO/锁、进程执行或默认凭据；当前副作用为平台时间/熵读取和调用方同步日志 sink。无固定成功业务占位，过滤级别返回 OK 是明确语义 |
| 正式类型/接口 | 使用 locker_status_t/uuid_t/clock_port_t；机器契约/生成头无漂移，旧 UID 文档漂移已修。进程内测试注入未接公共 API；未来 Runtime 装配仍归后续卡 |
| 依赖锁/边界 | 已检查固定 tag/commit/归档摘要与许可材料；common 只用 C/POSIX，不依赖后续第三方链接。未重新下载/审查上游维护安全状态，不把 lock 的 verified=true 当作本轮网络验证 |
| 阶段资源与范围 | Core64/HTTP4/WS16 属后续装配，不要求本卡造空实现。当前无业务状态/REST/锁驱动/模拟器框架；不把本卡基础库成功扩大到后续卡 |

## 5. 本轮真实命令与退出码

Windows 工作目录 %REPO%，分别运行并读取 $LASTEXITCODE：

    git status --short                         # 128，非 Git 仓库
    python -B scripts/check_contracts.py        # 0，PASS，0.2.1，hardware UNVERIFIED
    python -B scripts/check_stage_plan.py       # 0，PASS，0 卡 DONE，阶段 gate 未运行
    python -B scripts/check_vision_contracts.py # 0，PASS，非推理/硬件验收
    python -B scripts/check_feasibility.py      # 0，PASS，非业务运行验收

环境访问使用 ssh -o BatchMode=yes -o ConnectTimeout=10 locker-vm，脚本经 stdin 传 bash -s，未使用/安装 WSL、未改网络/系统。读取 pwd、uname -a、cat /etc/os-release、findmnt -T 源目录、cmake --version、gcc --version，成功退出 0。实际 VM：/home/%USER%，Ubuntu 26.04.1 LTS，Linux 7.3.0-rc5 x86_64，ext4 /dev/sda2，CMake 4.2.3，GCC 15.2.0。

源校验：Windows 对 source-manifest 每行 path/hash/bytes 重算，29/29 一致；将同一份预期值转为 hash + 两空格 + path 格式，经 SSH 在以下 s 目录执行 sha256sum -c（heredoc），29/29 OK，退出 0。另执行 sha256sum "$s/contracts/contract-source.json" "$s/contracts/include/locker_contracts.h"，退出 0、输出与第 1 节一致。

以下为本轮 VM 内完整构建/复现命令；各成功步骤在 set -eu 下执行并打印退出码。b 起初不存在，未复用既有构建缓存，所有生成物均在 src 之外：

    s=/home/%USER%/locker-workspace/S1-01/src
    b=/home/%USER%/locker-workspace/build/S1-01/independent-audit3-20261023
    e="$s/docs/evidence/stage-01/S1-01"
    cmake -S "$s" -B "$b" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
    cmake --build "$b" -j2
    ctest --test-dir "$b" --output-on-failure -V
    for p in probe-boundary probe-b1-b2 handoff-example-compile; do
      gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -I"$s/host/common/include" -I"$s/contracts/include" "$e/$p.c" "$b/host/common/liblocker_common.a" -pthread -o "$b/$p"
      "$b/$p"
    done

配置/构建/CTest 各 0；6/6 PASS，队列/build_info/clock/log/numeric/uuid 分别 97/5/53/164/133/53 断言，共 505。三个探针/示例各编译 0、运行 0；真实输出：

    LONG_KEY_SENSITIVE=1
    long_key ... aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa=[redacted]
    quoted_password ... note=password='[redacted]'
    json_single_quoted ... note={'password':'[redacted]'}
    single_quoted_key ... note='password'='[redacted]'
    LEAK_CASES=0
    B1_VARIANTS_LEAK=0
    B2_17TH_LEAK=0

probe-b1-b2 是现象输出型，不能只凭 exit=0 判定，本轮核对了实际掩码和泄漏位。probe-boundary 同时断言无泄漏、普通散文未被吞掉。测试均为合成秘密，无真实凭据。

加固和非空洞性命令：

    cmake -S "$s" -B "$b/asan" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON -DCMAKE_BUILD_TYPE=Debug '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
    cmake --build "$b/asan" -j2
    ctest --test-dir "$b/asan" --output-on-failure
    cp "$s/host/common/src/log.c" "$b/mutated-log.c"
    python3 -B "$e/nonvacuity-mutate-r2.py" "$b/mutated-log.c"
    gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -I"$s/host/common/include" -I"$s/contracts/include" -I"$s/tests/unit/common" "$s/tests/unit/common/test_log.c" "$b/mutated-log.c" "$s/host/common/src/clock.c" -pthread -o "$b/mutated-test"
    "$b/mutated-test"

ASan 配置/构建/CTest 各 0，6/6 PASS，无 sanitizer 报告；复制/mutation/编译各 0，mutated-test **预期退出 1**，12/164 断言失败。直接测试程序的 1 与交付方 CTest 的 8 是不同命令的退出码，不混写为同一个结果。变异只发生在 b 内，正式源码不变。

## 6. NOT_RUN、残留与交接

本轮 NOT_RUN：TSan、专门 MPMC/sink 并发压力、时钟/熵源故障注入、fuzz 重跑、第一轮 B1/B2 mutation 重跑、配置版本不一致动态负例、旧发行版/CMake3.16/GCC12、32 位/ARM；第三方归档重下载/上游安全维护与许可来源独立验证及构建链接。历史 vm-final2 的相关结果只作背景，未当成本轮执行。

业务服务/HTTPS/IPC/SQLite/设备模拟器闭环全部未在本卡验收；Keil 本轮重构建、STM32/RK/Android/NPU/相机/UART/锁、掉电/硬截止实测均 NOT_RUN。硬件 profile 仍 UNVERIFIED。真实 VM 基础库测试、设计静态检查、设备模拟和真实硬件分开评价。

本轮仅更新此报告；临时构建/变异副本位于独占 b 目录并在交付前清理，既有 VM 源码/正式工作区和历史日志不动。Windows 清单 29 文件及 stage-plan 在收尾再核对，未修改业务、契约、验收预期、原 HAL/vendor、equipment-vision 或硬件模板。没有需用户裁定的本轮不明残留，缺 Git 历史不能保证全工作区历史状态。

**可交接 S1-01；无未解决阻塞项。** 非阻塞证据说明 N5 交由收尾负责人勘误，状态由负责人按原流程处理。本审计不改卡状态、不开始下一卡、不把阶段 1 设 PASS。

---

# 历史附录：前轮整改复核原文

以下保持历史输入/结论，仅用于追踪；当前结论以本文件顶部为准。插入本轮报告前文件 SHA-256：25537ce630bd87684f6bc13154815a2e7ddbfc18d04b2f81629df3a5e3997303。

# S1-01 独立审计：整改复核

日期：2026-10-23（Asia/Shanghai）。当前结论以本节为准；文末保留上轮审计原文作为历史证据。仅审 S1-01，不修改业务、契约、验收预期、其他卡或 stage-plan。

**结论：FAIL，仍不满足交接条件。** 本轮独立构建、6 项正式测试（474 断言）、ASan/UBSan、原始 B1/B2 探针、200000 次输出边界 fuzz 均通过，故不能继续声称原始 B1/B2 复现未修复。但当前仍可通过单引号密码形态及长敏感字段名，使密码/token 明文进入日志 sink（本轮 B1-R、B3，均为 P1 阻塞）。

本轮全部 PASS 均来自本轮命令和当前输入。交付方的 vm-*.log、TSan 等记录只作背景，不移植为本轮证据。

## A. 输入、前置与实际审计范围

已依次读取 AGENTS.md、README.md、docs/stages/README.md、stage-plan，随后读取阶段 1 任务卡本卡/公共约束/阶段验收/附录；定向核对阶段 0 主规格、traceability、module-ownership、internal-interfaces、机器契约及本卡消费的 C 类型。读取 firmware/README.md 以确认既有 STM32 环境边界。

已读取当前 summary.md、handoff.md、source-manifest.txt、实际 common 源码/头文件、根及模块构建文件、全部六项正式单测及测试断言宏、主机依赖锁/许可材料、整改探针/示例/mutation 源。只读分工交叉复核了日志和非日志实现；无人修改仓库源码，持久写入仅此 audit.md。

| 输入/状态 | 本轮独立核对 |
|---|---|
| contract-source 实际版本 | contract_version=0.2.1，status=REVIEW_DRAFT；与用户基线一致 |
| contract-source SHA-256 | 14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f |
| 生成头 SHA-256 | 34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a；版本宏 0.2.1 |
| 任务卡 SHA-256 | 7b92272791acd09cd2bc25ab85e533fc3eb850ba200ce5ad5c8f84ed5981b05e |
| source-manifest SHA-256 | 304d39df82d9d50874d992d993fce137fe793b360e4a9dc69884c68f1400b696；逐项重算 SHA-256 和字节数，29/29 一致 |
| 当前 log.c / test_log.c SHA-256 | e44065c619ae1ade7026c726f1a6d8f96302a9102d4a272aeaf01b3ca7123709 / f33add207977abe4ce038aad71f686d4e9a255dd39cf8c4942e5fc6b8e5ce856 |
| 当前 summary / handoff SHA-256 | 51765f87c03085bb6b70eff4dd510ad45d4cd00f01cc255c3456b5e3d705294a / 62a2a00d534544cf16bb1817bfdb81c3ebe44a339091aefd714453203ad43726 |
| stage-plan | S1-01 IN_PROGRESS，depends_on=[]，active_task=S1-01，report=null，阶段 1 gate_result=NOT_RUN；无前置实施卡 |
| stage-plan SHA-256 | 54de2f04cdaa1e76858dabd37896f9755e6716f04b6304f8b16f151949da2f05 |
| 工作区 | git status --short exit=128：不是 Git 仓库，不能独立证明开发轮历史变更全集；summary 所述“git exit=1、空输出”不符合本轮实测 |

从实际源码生成全新 Linux 本地快照再构建，未复用既有 build/host，也未使用原 HAL/vendor/equipment-vision。开工前不存在 host/tests、未改其他文件等历史陈述无 Git 基线可证，不认定为本轮已证事实。

## B. 上轮问题的本轮核销

| 上轮项 | 当前证据及状态 |
|---|---|
| B1 JWT 点号/双引号空格密码泄漏 | 原始输入本轮已不泄漏，正式 test_log 及原始探针通过；该精确复现已修复。引号值处理仍有 B1-R 变体，不能宣布整个类别无缺口 |
| B2 第 17 个片段泄漏 | 本轮第 17 项探针泄漏位=0；25 项正式用例通过；源码删除固定 span 预算。关闭原问题 |
| N1 队列销毁/日志生命周期说明 | 当前 bounded_queue.h:16–23 明确所有者独占 init/destroy、先 join；log.h:34–37 与 handoff:288–296 明确并发 sink、禁止并发 deinit、回调 line 借用和复制责任。关闭 |
| N3 时钟回调签名/平台失败说明 | 与契约头:134 一致；已明确 monotonic 平台失败只能 OK+0，不再声称 wrapper 可区分失败；本轮重新编译运行 handoff-example 成功。关闭旧问题 |
| N2 默认入口状态陈旧 | 仍未修，保留为下节非阻塞文档事项 |

本轮另将 log.c 的仓库外副本故意恢复点号截断和 16 段预算。正式 test_log 返回 1、133 个断言中 7 个失败（281/286/337/338/339/354/355 行），证明新增测试能拦住对应旧缺陷，而不是只据报告声称回归有效。

## C. 本轮阻塞项

### B1-R / P1：单引号密码值被当作空值，秘密原样留下

- **文件/行号**：host/common/src/log.c:322（仅进入双引号分支）、:247–257（单引号立即结束 unquoted 值）、:340–359（输出掩码后继续处理剩余原文）；支持承诺见 host/common/include/locker/common/log.h:22–24。
- **触发/复现**：第 F 节最小探针通过真实 locker_logger_log，字段 note 的值为 password='alpha beta'。这是明确的敏感键加等号加引号值，非无标记任意散文秘密。
- **实际结果**：sink 收到 note="password=[redacted]'alpha beta'"；完整密码 alpha beta 留在输出。探针编译 0、运行 1，LEAK_CASES 中计入该泄漏。
- **影响**：仍不满足任务卡不输出密码的验收；模式被识别却只替换空串，不能依赖调用方先 scrub 消除此路径。双引号 JSON 原复现通过不覆盖这一形态。
- **修复建议**：引号扫描支持相匹配的单双引号及转义/未闭合情况，或对无法支持的敏感值形态保守屏蔽整个剩余字段；加入单独 sink 场景及 scrub 回归，避免被其他长字段截断掩盖。不能只改文档/预期来取消密码不泄漏验收。
- **阻塞级别**：P1，阻塞本卡交接；是旧 B1 类别的剩余边界，不否认原 JWT/双引号复现已修。

### B3 / P1：归一化截断长字段名，敏感后缀被丢弃

- **文件/行号**：host/common/src/log.c:114–127，特别是 :119 的 63 字符截断；:130–149 的敏感判定；:552 的双重判定。log.h:15–19 对归一化等值/后缀匹配未声明输入键长限制。
- **触发/复现**：结构化字段名为 63 个 a 后接 _token（总长 69），字段值 LONG_KEY_SECRET；第 F 节探针直接调用 logger，按文档把秘密放在带敏感后缀的字段中。
- **实际结果**：locker_log_key_is_sensitive 返回 0；sink 收到 63 个 a 组成的截断字段名及明文 LONG_KEY_SECRET。归一化和输出键截断均丢掉 token 后缀，两次检查都失效。
- **影响**：即便按 log.h:29–32 要求使用敏感键字段，也会泄漏 token。库未拒绝超长键，不能事后假设输入无效；没有内存越界不等于脱敏正确。
- **修复建议**：按完整原始键判断归一化后缀；或检测截断后失败关闭（掩码值/明确拒绝记录）。维持固定内存，不必扩充框架。补 63 字符边界两侧、长前缀及多分隔符的结构化字段回归，至少断言 sink 不含秘密。
- **阻塞级别**：P1，阻塞本卡交接。

## D. 非阻塞项与接口漂移

### N2 / P2，仍开放：默认入口状态与实际索引不符

- **位置**：README.md:13、:15、:31；docs/stages/README.md:3、:25、:47；AGENTS.md:3。
- **触发/复现**：默认入口说“全部未开始 / active_task 为空”，实际已有本卡源码和测试，索引是 S1-01 IN_PROGRESS。
- **影响**：后续 Agent 可能重复开工；将问题登记移交不等于入口已经同步。
- **建议**：由有权限的收尾负责人将当前正文同步为 S1-01 已实现、仍待整改/复核，其他卡未开始，并链接本卡报告；阶段 gate 仍为 NOT_RUN。本轮受仅写 audit.md 限制，不代改。
- **级别**：非阻塞于本轮技术判定，正式收尾前需完成的文档同步。

### N4 / P2：handoff 将 card_uid 错归为 u64

- **位置**：docs/evidence/stage-01/S1-01/handoff.md:211；相反真源为 contracts/include/locker_contracts.h:119、:158，docs/architecture/uart-v1.md:44、:336–337，contracts/internal-data-source.json:1936、:2868。
- **触发/复现**：按该段把卡 UID 转成 u64/规范十进制。真源允许 4/7/10 字节 UID，结构为 card_uid_len 加 card_uid[10]；仓储用 card_uid_hex/uid_hex。10 字节不能放入 u64，前导零与长度也会丢失。此为静态类型/表示核对，未运行业务调用。
- **影响**：当前没有此类业务调用方，不是已发生数据损坏；但交接说明可能误导后续卡的数据映射，属于文档接口漂移。
- **建议**：用 boot_id/event_seq/action_no 举例，UID 保持契约规定的长度加字节或 hex 表示；不修改公共真源来迎合文档。
- **级别**：非阻塞文档修正。

公共机器契约、生成头及 common 对其消费未发现漂移；N4 是文档漂移。时钟失败缺状态通道是已披露的现有合同限制，不在本审计自行加新 API。

## E. 任务卡原文逐项核对

| 范围/验收 | 本轮评价 |
|---|---|
| 模块化 CMake、Linux 基础库配置构建 | PASS：新建 Linux 本地源码/构建树，C11、Threads::Threads、6 源静态库；配置不联网，无第三方隐藏链接依赖 |
| 有界队列、满时明确失败 | PASS：满返回 NO_SPACE，不覆盖旧项并统计拒绝；空 pop/peek 为 NOT_READY；FIFO/环回/重复 init=CONFLICT 有正式测试 |
| 消息生命周期不依赖调用方栈 | PASS：内嵌消息字节复制入调用方 storage，生产栈破坏测试通过；含指针元素由调用方深拷贝或显式移交所有权，storage 须长于队列 |
| 单调时钟、可注入时钟、超时边界 | PASS：使用既有 locker_clock_port_t；真实 CLOCK_MONOTONIC/墙钟分开；deadline 零/饱和/无效/到期有测试及真实 20ms 烟测；故障注入仍 NOT_RUN |
| 生产时间不由公共 API 控制 | 所见没有 HTTP/IPC/环境时间 setter 或公共调试旁路；真实工厂可用、替身只在进程内测试构造。未来 Runtime 尚未实现，不能凭“无 setter”证明未来装配必然正确 |
| UUID/u64 | PASS：严格小写 UUID、随机 v4、失败保持输出；u64 0/MAX/越界/非法文本、数值排序/加法有正式测试；词法 is_canonical_decimal 不替代 parse 的数值范围检查 |
| 受限日志、无 token/密码 | FAIL：原 B1/B2 精确复现已通过，但 B1-R/B3 仍泄漏；内存边界和脱敏语义分别评价 |
| 资源上限 | common 无堆分配；queue 容量和 storage 由调用者提供；日志行 512 字节、内部 key 缓冲 64 字节，输出截断有界；长键策略不安全见 B3。输入 strlen/字段扫描耗时未被 512 字节输出上限约束，不声称已验证延迟上限 |
| 内存/线程/回调 | ASan/UBSan 现有用例通过；数据访问持队列 mutex，init/destroy 所有者独占；logger 及 clock.user/component/sink 上下文须存活，异步 sink 需复制 line；真实 MPMC/跨线程 sink 压力 NOT_RUN |
| 重复/重启 | common 为易失对象；重复 init 拒绝、销毁后拒绝访问；重启重新初始化且不能持久化 mutex。业务幂等、账本/动作重放不属本卡，不虚构实现或测试 |
| 权限/持久化/副作用 | 未见业务网络监听、SQL、GPIO、锁驱动、shell 命令、默认凭据或持久化入口；仅平台时间/熵读取及调用者提供的同步日志 sink。未见固定成功的业务占位实现；过滤日志返回 OK 属明确过滤语义 |
| 依赖锁/许可 | 固定来源/tag/commit/归档摘要、5 份许可材料可读且随 manifest 核对；common 实际无这些链接依赖。未重新下载/核验上游归档或安全维护状态，不继承 lock 的 verified=true 为独立网络核验 |
| 范围/后续卡 | 当前实现集中 common/根构建/正式测试/主机依赖材料，无业务状态、路由、锁或另一套框架；本轮未启动后续卡。缺 Git 历史，不宣称已证明开发轮所有文件的历史变动范围 |

## F. 本轮真实命令、退出码与关键结果

Windows 工作目录 %REPO%。以下四条分别执行并读取 $LASTEXITCODE，均为 0 / PASS；前三条为项目要求，第四条为补充设计检查。它们均不证明业务或真机通过：

    python -B scripts/check_contracts.py
    python -B scripts/check_stage_plan.py
    python -B scripts/check_vision_contracts.py
    python -B scripts/check_feasibility.py

git status --short 为 128。实际环境读取 uname -a、cat /etc/os-release、cmake --version、gcc --version、make --version，均成功：WSL2 Ubuntu 26.04 LTS / Linux 6.18.33.2 x86_64 / GCC 15.2.0 / CMake/CTest 4.2.3 / GNU Make 4.4.1。WSL 有 localhost 代理转发警告，离线构建未受影响。

以下命令在该 WSL Linux 中执行（PowerShell 通过 wsl -d Ubuntu -- bash -s 输入脚本；移除脚本中 CR，末行用注释结束）。临时目录起初不存在；源树拷贝不含宿主 build：

    base=/var/tmp/s101-audit-r2-20261023
    mkdir -p "$base/src"
    cp -a /mnt/e/stm32项目/CMakeLists.txt /mnt/e/stm32项目/host /mnt/e/stm32项目/tests /mnt/e/stm32项目/contracts "$base/src/"
    cmake -S "$base/src" -B "$base/build" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
    cmake --build "$base/build" -j2
    ctest --test-dir "$base/build" --output-on-failure -V

配置/构建/CTest 各 exit=0；6/6 PASS。队列/build_info/clock/log/numeric/uuid 断言分别 97/5/53/133/133/53，共 474。是当前源码在现有 WSL 的全新本地树构建，不称全新 OS 或本轮 VMware 验证。

原始整改探针：

    evidence=/mnt/e/stm32项目/docs/evidence/stage-01/S1-01
    gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -I"$base/src/host/common/include" -I"$base/src/contracts/include" "$evidence/probe-b1-b2.c" "$base/build/host/common/liblocker_common.a" -pthread -o "$base/probe"
    "$base/probe"

编译/执行 exit=0，输出 B1_BEARER note="Bearer [redacted]"、B1_QUOTED note={"password":"[redacted]"}、B1_VARIANTS_LEAK=0、B2_17TH_LEAK=0。探针自身无条件 return 0，因此通过结论依据实际输出，而非单凭退出码。

ASan/UBSan：

    cmake -S "$base/src" -B "$base/asan" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON -DCMAKE_BUILD_TYPE=Debug '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
    cmake --build "$base/asan" -j2
    ctest --test-dir "$base/asan" --output-on-failure
    gcc -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I"$base/src/host/common/include" -I"$base/src/contracts/include" "$evidence/probe-scrub-fuzz.c" "$base/asan/host/common/liblocker_common.a" -pthread -o "$base/fuzz"
    "$base/fuzz"

各步骤 exit=0，6/6 PASS，无 sanitizer 报告；fuzz 200000 次、最大输出 511（缓冲 512）、失败数 0。此 fuzz 检查 NUL/长度边界，**不检查秘密是否完整遮盖**。

非空洞性及配置版本漂移负例，仅改仓库外副本：

    cp "$base/src/host/common/src/log.c" "$base/log-mutated.c"
    python3 -B "$evidence/nonvacuity-mutate.py" "$base/log-mutated.c"
    gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -I"$base/src/host/common/include" -I"$base/src/contracts/include" -I"$base/src/tests/unit/common" "$base/src/tests/unit/common/test_log.c" "$base/log-mutated.c" "$base/src/host/common/src/clock.c" -pthread -o "$base/mutation-test"
    "$base/mutation-test"
    mkdir "$base/stale"
    sed 's/#define LOCKER_CONTRACT_VERSION "0.2.1"/#define LOCKER_CONTRACT_VERSION "0.2.0"/' "$base/src/contracts/include/locker_contracts.h" > "$base/stale/locker_contracts.h"
    cmake -S "$base/src" -B "$base/drift" -DLOCKER_CONTRACTS_INCLUDE_DIR="$base/stale" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON

复制/mutation/编译各 exit=0；mutation-test **预期 exit=1**，7/133 断言失败；漂移配置 **预期 exit=1**，报 Contract version drift: header=0.2.0 source=0.2.1。正式契约文件未改。

handoff 修正示例：

    gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -I"$base/src/host/common/include" -I"$base/src/contracts/include" "$evidence/handoff-example-compile.c" "$base/build/host/common/liblocker_common.a" -pthread -o "$base/handoff-example"
    "$base/handoff-example"

编译/执行均 exit=0。

本轮新增泄漏探针，完整源与命令：

    cat <<'C' | gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -I"$base/src/host/common/include" -I"$base/src/contracts/include" -x c - -x none "$base/build/host/common/liblocker_common.a" -pthread -o "$base/boundary-probe"
    #include <stdio.h>
    #include <string.h>
    #include "locker/common/log.h"
    static char last[4096];
    static void sink(void *u, locker_log_level_t l, const char *line) {
        (void)u; (void)l; snprintf(last,sizeof last,"%s",line);
    }
    int main(void) {
        locker_logger_t log={0}; int leaks=0;
        if (locker_logger_init(&log,sink,NULL,"audit",LOCKER_BACKEND_SIMULATOR,
                               LOCKER_LOG_LEVEL_INFO,NULL)!=LOCKER_STATUS_OK) return 2;
        char key[80]; memset(key,'a',63); memcpy(key+63,"_token",7);
        printf("LONG_KEY_SENSITIVE=%d\n",locker_log_key_is_sensitive(key));
        locker_log_field_t f={key,"LONG_KEY_SECRET"};
        if(locker_logger_log(&log,LOCKER_LOG_LEVEL_INFO,"long_key",&f,1)!=LOCKER_STATUS_OK) return 2;
        puts(last); leaks+=(strstr(last,"LONG_KEY_SECRET")!=NULL);
        f.key="note"; f.value="password='alpha beta'";
        if(locker_logger_log(&log,LOCKER_LOG_LEVEL_INFO,"quoted_password",&f,1)!=LOCKER_STATUS_OK) return 2;
        puts(last); leaks+=(strstr(last,"alpha beta")!=NULL);
        printf("LEAK_CASES=%d\n",leaks);
        locker_logger_deinit(&log);
        return leaks ? 1 : 0;
    }
    C
    "$base/boundary-probe"

编译 exit=0，运行 **exit=1（探测到缺陷）**。真实 sink 关键输出：

    LONG_KEY_SENSITIVE=0
    aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa=LONG_KEY_SECRET
    note="password=[redacted]'alpha beta'"
    LEAK_CASES=2

两项均是合成测试字符串，无真实凭据。完整源码和结果在本报告保留；临时可执行程序不留作正式产品代码。

## G. 未验证、残留与交接决定

- **NOT_RUN**：本轮 VMware/新装 OS、旧发行版/CMake 3.16/GCC 12、32 位/ARM；前轮 vm 日志不代表整改版通过。
- **NOT_RUN**：本轮 TSan、真实多生产者多消费者及跨线程 sink 压力、时钟/熵源平台故障注入；既有测试均非并发压力，不能以历史 TSan 通过替代。
- **NOT_RUN**：依赖归档重下载/来源与上游许可独立核验、维护/漏洞评估、第三方构建链接；当前 common 不使用这些库，不将其未链接扩大为本卡业务实现要求。
- **NOT_RUN**：HTTPS/IPC/SQLite/设备模拟闭环与全部后续卡；本卡没有实现模拟设备，不称模拟程序验收通过。
- **NOT_RUN**：本轮 Keil 重构建、STM32/RK/Android/NPU/相机/UART/锁、硬截止及掉电实测。硬件 profile 仍 UNVERIFIED，静态设计、真实 Linux common 测试、设备模拟、真实硬件严格分开评价。

本轮只更新 audit.md；原 HAL、锁定 vendor、E:/equipment-vision、硬件模板、业务、契约、正式验收及 stage-plan 保持边界。临时快照、构建、故意变异副本和交叉审阅探针在交付前清理；仓库内正式测试、整改探针和既有证据保留，无本轮不明残留需用户判定。缺 Git 历史，不作全工作区历史残留保证。

**交接决定：不满足。** 先修 B1-R/B3 并补可独立触发的正式回归，再重新构建/CTest和必要复核；同时同步当前入口和 UID 文档说明。状态仍由有权限的负责人据实处理，本审计不改 DONE/report、不推进下一卡、阶段 1 gate 仍 NOT_RUN。

---

# 历史附录：上轮审计原文

以下属于整改前输入，不能作为本轮测试结果。原文件 SHA-256：48e294d90e1ebe78cb28ada5ae58201c6b40bdcc88db11cceb0da18625198995。保留用于追踪 B1/B2/N1/N2/N3；当前处置见上方复核章节。

# S1-01 独立审计

日期：2026-10-23（Asia/Shanghai）。仅审计 S1-01 构建与基础设施。指定及实际契约基线均为 **0.2.1**。

**结论：FAIL，暂不满足交接条件。** 本轮真实 Linux 配置、编译、6 项正式测试及 ASan/UBSan 下的同套测试通过，但独立复现两项日志脱敏缺陷 B1、B2，不满足本卡“日志不输出 token/密码”的硬验收。现有 CTest PASS 不足以将本卡置 DONE。本报告不改变 stage-plan、不启动下一卡、不判定阶段 1 PASS。

## 1. 输入身份与前置

已读取 AGENTS.md、根 README、阶段入口、stage-plan、阶段 1 任务卡全文（含公共约束、阶段验收和附录）、阶段 0 主规格；定向核对 module-ownership、traceability、internal-interfaces、契约 README、生成 C 头及本卡消费的 status/UUID/clock 类型；读取 firmware/README.md。已核对 summary.md、handoff.md、source-manifest.txt、实施日志、清单中实际源码/构建/正式测试/依赖许可材料。

| 输入 | 本轮独立结果 |
|---|---|
| contracts/contract-source.json | contract_version=0.2.1；SHA-256 14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f |
| contracts/include/locker_contracts.h | LOCKER_CONTRACT_VERSION=0.2.1；SHA-256 34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a |
| docs/stages/stage-01-task-cards.md | SHA-256 7b92272791acd09cd2bc25ab85e533fc3eb850ba200ce5ad5c8f84ed5981b05e |
| source-manifest.txt | 重新计算 29 个文件的 SHA-256 和字节数，29/29 一致；不是仅检查清单存在 |
| stage-plan | active_task=S1-01，S1-01=IN_PROGRESS，depends_on=[]，report=null；阶段 1 gate_result=NOT_RUN |
| 工作区 | git status --short 实际 exit=128：不是 Git 仓库；不能证明历史新增/删除范围或“开工前不存在 host/tests”，仅能证明当前内容及清单身份 |

没有前置实施卡，不移植前轮 PASS。原 HAL、firmware/third_party、固件依赖锁、E:/equipment-vision、硬件未验证模板不由本审计修改。既有 Keil/ARMCC 环境证据不替代本卡 Linux 验收或 S2-01。

本轮仓库持久写入仅此 audit.md。探针从 stdin 编译，构建放在 WSL 临时目录；未新增正式测试或修改源码、契约、验收预期、其他卡或状态。

## 2. 阻塞项

### B1 / P1：敏感值扫描提前结束，尾部明文进入 sink

- **位置**：host/common/src/log.c:172–177（Bearer 分支）、:195–202（字段值扫描）。
- **触发/复现**：第 5 节探针经真实 locker_logger_log，字段键 note，值分别为 Bearer part1.part2.part3 和 {"password":"alpha beta"}。均为现有 scrub 宣称支持的形态。
- **实际结果**：sink 分别收到 Bearer [redacted].part2.part3 和 {"password":"[redacted] beta"}。Bearer 分支以 is_word_char 切词，遇点号停止；引号密码仍在空格处停止。
- **影响**：token/密码部分明文泄露；调用方即使按 handoff 建议先 scrub 也不能完整脱敏。敏感键直接掩码的通过用例不能覆盖此缺陷。
- **修复建议**：按语法消费完整认证 token；引号字符串处理完整值及转义边界，无法可靠解析时保守掩码整个字段。正式测试覆盖点号/认证 token 标点、空格、转义，断言尾部秘密不进入 sink；不能放宽验收为只遮一部分。
- **阻塞级别**：阻塞本卡交接，与日志硬验收直接相关。

### B2 / P1：16 段掩码容量耗尽后原文放行

- **位置**：host/common/src/log.c:146、:223–230、:258–285。
- **触发/复现**：第 5 节第 3 个 note 值，16 个 token=a 后接 token=LAST_SECRET；输入及输出均未触及 512 字节截断边界。
- **实际结果**：前 16 项被替换，第 17 项 token=LAST_SECRET 原样进入 sink；scrub 不报告容量耗尽错误，logger 返回 OK。
- **影响**：长文本可耗尽 span，使后续敏感值绕过脱敏。“最多 16 段”是资源限制，不应成为泄密许可。
- **修复建议**：在固定预算内边扫描边输出，或耗尽时掩码剩余内容/整个字段，失败关闭。正式测试覆盖第 16/17 项、多个密码字段和截断边界。
- **阻塞级别**：阻塞本卡交接。

## 3. 非阻塞项

以下为合同/文档收尾，不要求扩大业务范围；本审计未代修。

### N1 / P2：队列线程安全声明缺少生命周期例外

- **位置**：host/common/include/locker/common/bounded_queue.h:16–18；host/common/src/bounded_queue.c:28–40。
- **触发/复现**：头声明 all functions 多线程安全；若 A 通过 push/pop 的 initialized 检查，B 同时 destroy，后者直接销毁 mutex、清空 storage，不等待访问结束。此交错已静态核对，**动态并发复现 NOT_RUN**。
- **影响**：误按字面保证并发销毁会有数据竞争/失效缓冲访问；正常停线程再销毁的使用方式未发现该问题。
- **建议**：明确 init/destroy 由所有者独占，停生产者并 join 全部访问线程后才 destroy，禁止与访问并发。无需强加引用计数框架。logger deinit 也需同样约束；sink 的 line 是回调期间借用的栈缓冲，异步 sink 必须复制。
- **级别**：非阻塞生命周期澄清；现有队列测试都是单线程，不能外推压力验证通过。

### N2 / P2：默认阅读入口仍描述“全部未开始”

- **位置**：README.md:13、:15、:31；docs/stages/README.md:3、:25、:47；AGENTS.md:3。
- **触发/复现**：入口写全部卡未开始、active_task 为空；索引实际 S1-01 IN_PROGRESS，当前源码及独立构建已存在。
- **影响**：后续开发者可能重复启动本卡。实施报告承认差异，不等于同步默认入口。
- **建议**：由有权限的收尾负责人将正文写成 S1-01 已实现、审计发现问题待整改，其他卡未开始，并链接本卡证据；阶段 gate 保持 NOT_RUN。
- **级别**：非阻塞于本轮技术判定，正式收尾前必须同步的文档事项。仅写 audit.md 的限制使本审计不能代改。

### N3 / P2：handoff 时钟示例及失败状态说明漂移

- **位置**：docs/evidence/stage-01/S1-01/handoff.md:126、:130、:151；真源 contracts/include/locker_contracts.h:134；host/common/src/clock.c:148–154。
- **触发/复现**：示例 fake_mono 是 locker_status_t(const locker_clock_port_t *, uint64_t *)，真实回调应为 uint64_t(void *)；位置初始化还把函数放进首字段 user。类型已静态核对，示例独立编译 **NOT_RUN**。另，真实 clock_gettime 失败返回 0 时，wrapper 只验证端口/输出指针，仍返回 OK。
- **影响**：复制示例会编译失败或错误初始化；“改用 wrapper 即可区分失败与真实 0”会误导后续超时处理。正式 test_clock.c 的字段赋值与回调才符合真源。
- **建议**：采用正式测试的正确示例；说明现有 monotonic 回调无法携带平台失败状态。如需扩展合同须交契约负责人，不能本卡自行改签名。
- **级别**：非阻塞文档/示例漂移；公共契约本身未变。

## 4. 逐项对照卡原文

| 范围/验收 | 独立评价 |
|---|---|
| 模块化 CMake，Linux 可配置构建 | 本轮 PASS：WSL Linux、新建构建目录、真实编译链接；未重跑 VMware 全新 OS |
| 不另造 Windows/Python/Node 框架 | 所见生产实现是 C11/POSIX；根 CMake 只接 common，无业务服务/REST/锁驱动 |
| 有界队列、满时失败 | PASS：NO_SPACE、不覆盖旧元素、统计拒绝；空 pop=NOT_READY；FIFO/环回有正式测试 |
| 消息生命周期独立于调用方栈 | PASS：内嵌数据值复制及栈破坏测试；含指针消息仍须调用方深拷贝或显式移交固定池所有权，不能入队后释放指向内容 |
| 单调钟与注入时钟 | PASS：使用既有 locker_clock_port_t、CLOCK_MONOTONIC，墙钟另取；deadline 饱和加法、零超时/无效/到期均测试；真实 20ms 烟测通过 |
| 生产真实时钟、无公共调试旁路 | 静态所见有真实工厂与进程内测试注入，无 HTTP/IPC setter 或外部时间切换。未来 Runtime 尚未实现，不能仅据无 setter 推定未来调用必然正确 |
| UUID/u64 边界 | PASS：严格 UUID、长度、随机 v4；u64 零/MAX/越界/非法文本/失败保持输出、数值比较与加法有正式测试 |
| 受限日志、无 token/密码 | **FAIL：B1/B2**。直接敏感键及单行内存上限测试通过不代表所有已支持文本形态通过 |
| 资源上限 | common 无堆分配，queue 由调用方提供足够且长寿命 storage；日志行 512 字节、key 64 字节、span 16；输出内存有界不等于输入扫描耗时有界。Core=64/HTTP=4/WS=16 是后续装配，本卡不伪造通过 |
| 错误/重复/重启 | 参数拒绝、重复 init=CONFLICT、空 pop、destroy 后拒绝、deadline 边界有测试。common 是进程内易失库，重启重新 init，不能持久化 mutex 或重放消息；本卡不负责动作幂等/账本恢复 |
| 内存/线程/回调 | 值复制、固定缓冲及失败保持输出有测试；ASan/UBSan 通过。生命周期例外见 N1。logger 无串行锁，sink 可并发/阻塞；clock.user、logger.user/component 属借用，不能提前释放 |
| 权限/持久化/副作用 | 所见无监听、SQL、业务 GPIO、进程执行、默认凭据或持久化写入；时钟/系统熵读取及用户提供的同步 sink 是当前副作用。未发现固定成功空实现冒充业务端口；过滤日志返回 OK 属既定语义 |
| 依赖来源/许可 | lock 有 tag/commit/归档摘要及 5 份许可；common 实际未链接这些依赖，配置期不联网。许可文件身份随 manifest 核对；未重新下载归档/在线核验来源及安全维护状态，不把 verified=true 继承为独立网络或链接验证 |
| 共享接口和范围 | 清单覆盖根 CMake、common、正式测试与主机依赖材料；契约/头指纹吻合，设计检查无漂移。缺 Git 历史，不能证明开发轮未修改其他文件 |

## 5. 本轮真实命令、退出码与复现

Windows 工作目录 %REPO%，各检查后独立读取 $LASTEXITCODE：

    git status --short                         # 128；不是 Git 仓库
    python -B scripts/check_contracts.py        # 0；PASS，0.2.1，硬件 UNVERIFIED
    python -B scripts/check_stage_plan.py       # 0；PASS，0 卡 DONE，active_task=S1-01
    python -B scripts/check_vision_contracts.py # 0；PASS，非推理/真机验证
    python -B scripts/check_feasibility.py      # 0；PASS，非运行/真机验证

环境命令：

    wsl -d Ubuntu -- bash -lc 'uname -a; cat /etc/os-release; cmake --version; gcc --version'

exit=0：Ubuntu 26.04 LTS / WSL2 Linux 6.18.33.2 x86_64 / GCC 15.2.0 / CMake 4.2.3。WSL 有 localhost 代理警告，离线构建未受影响。

以下为该 Linux 内执行的完整构建命令（从 PowerShell 经 wsl 的 bash -lc 或 stdin bash -s 调用）：

    cmake -S /mnt/e/stm32项目 -B /tmp/s101-independent-audit-20261023 -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
    cmake --build /tmp/s101-independent-audit-20261023 -j2
    ctest --test-dir /tmp/s101-independent-audit-20261023 --output-on-failure -V

三步分别 exit=0，顶层 && 调用 exit=0；6/6 PASS。按队列/build_info/clock/log/numeric/uuid 顺序为 97/5/53/94/133/53 断言，共 435。没有复用交付方 build/host。

此后 /tmp 目录在后续 WSL 调用不可见，第一次探针编译 exit=1（输出目录不存在），运行尝试 exit=127，没有记为通过。迁移至 /var/tmp 后重新执行：

    b=/var/tmp/s101-independent-audit-20261023
    cmake -S /mnt/e/stm32项目 -B "$b" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
    cmake --build "$b" -j2
    ctest --test-dir "$b" --output-on-failure

三步均成功，6/6 PASS。探针完整编译命令与源：

    cat <<'C' | gcc -std=c11 -Wall -Wextra -Werror -I/mnt/e/stm32项目/host/common/include -I/mnt/e/stm32项目/contracts/include -x c - -x none "$b/host/common/liblocker_common.a" -pthread -o "$b/log_probe"
    #include <stdio.h>
    #include <string.h>
    #include "locker/common/log.h"
    static void sink(void *u, locker_log_level_t l, const char *s) {
        (void)u; (void)l; puts(s);
    }
    int main(void) {
        locker_logger_t log = {0};
        locker_logger_init(&log,sink,NULL,"audit",LOCKER_BACKEND_SIMULATOR,
                           LOCKER_LOG_LEVEL_INFO,NULL);
        const char *values[] = {
            "Bearer part1.part2.part3",
            "{\"password\":\"alpha beta\"}",
            "token=a token=a token=a token=a token=a token=a token=a token=a token=a token=a token=a token=a token=a token=a token=a token=a token=LAST_SECRET"
        };
        for (size_t i=0; i<3; ++i) {
            locker_log_field_t f={"note",values[i]};
            locker_logger_log(&log,LOCKER_LOG_LEVEL_INFO,"probe",&f,1);
        }
        return 0;
    }
    C
    /var/tmp/s101-independent-audit-20261023/log_probe

编译成功。同一 stdin 脚本尾部 CR 附着到可执行文件名，首次调用 exit=127；随后独立执行以下命令 **exit=0**：

    wsl -d Ubuntu -- bash -lc /var/tmp/s101-independent-audit-20261023/log_probe

探针仅打印，不把进程 exit=0 当作脱敏 PASS。真实 sink 输出的关键内容（合成字符串，无真实凭据）：

    note="Bearer [redacted].part2.part3"
    note="{"password":"[redacted] beta"}"
    note="token=[redacted] token=[redacted] token=[redacted] token=[redacted] token=[redacted] token=[redacted] token=[redacted] token=[redacted] token=[redacted] token=[redacted] token=[redacted] token=[redacted] token=[redacted] token=[redacted] token=[redacted] token=[redacted] token=LAST_SECRET"

额外内存/未定义行为检查：

    b=/var/tmp/s101-independent-audit-sanitize-20261023
    cmake -S /mnt/e/stm32项目 -B "$b" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON -DCMAKE_BUILD_TYPE=Debug '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
    cmake --build "$b" -j2
    ctest --test-dir "$b" --output-on-failure

set -e 脚本成功结束，三步 exit=0，6/6 PASS，无 sanitizer 报告；仅覆盖已有测试，不证明并发或脱敏正确。

## 6. 未验证项、接口漂移与交接

- **NOT_RUN：全新 OS/VMware 独立重验**。已读交付方 vm-*.log，但本轮只有现有 WSL 中的全新构建，不移植前轮 VMware PASS。
- **NOT_RUN：第三方归档重下载/上游许可来源核验、维护与漏洞评估、第三方编译链接**。common 未链接这些库；未来消费前仍需按 lock 验证，不以元数据代替构建。
- **NOT_RUN：TSan、并发销毁/压力测试、时钟及熵源故障注入、32 位/ARM、CMake 3.16/GCC 12 精确下限、契约版本不一致的配置动态负例**。漂移门禁代码已静态核对，不能称动态测过。
- **NOT_RUN：业务 HTTPS/IPC/SQLite/设备模拟闭环**。属后续卡，本卡不先造空实现；模拟器未在本卡实现，不能称模拟设备验收通过。
- **NOT_RUN：本轮 Keil 重构建、STM32/RK/Android/NPU/相机/UART/柜锁及掉电/硬截止实测**。硬件 profile 仍 UNVERIFIED；设计一致性、真实 Linux 单测、未来设备模拟、真实硬件是不同证据层级。

公共机器契约/生成接口未发现输入漂移；common 使用真实 locker_clock_port_t，没有另造同义 DTO。存在 handoff 示例/错误说明漂移 N3 和当前入口状态漂移 N2，不能擅改真源或验收来消除发现。

交接条件：先修 B1/B2 并补正式负例，重新构建/CTest及必要回归，复核敏感值不再进入 sink；同步生命周期和当前入口文档，再由有权限的负责人据实处理 DONE/report。**本轮不批准 S1-01 交接，不建议启动依赖卡。** 没有提出范围外功能要求。

本轮临时探针及构建目录在审计完成时清理；正式源码、测试、既有证据保留。没有需用户决定的本轮不明残留；缺 Git 历史，不能保证整个工作区的历史残留状态。
