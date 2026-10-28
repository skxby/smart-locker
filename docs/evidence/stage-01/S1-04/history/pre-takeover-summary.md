# S1-04 业务核心 — 实施报告

日期 2026-10-27。范围仅 S1-04（`host/domain/`、`tests/unit/domain/`、核心相关 `tests/integration/`、根 `CMakeLists.txt` 三行最小接入）。本卡不是阶段 1 门禁，不把阶段 1 设为 PASS，不把任何硬件能力（真实 UART、GPIO 脉冲、MCU 掉电、RK 存储介质）标为已验证。

状态：**IN_PROGRESS**。实现、构建/CTest、ASan+UBSan、TSan 三套已在同一份最终源码上跑完（canonical `run_id=r205400`，三套各 27/27、`SANITIZER_REPORTS=0`）；**尚未 DONE**：round-6 复审未出结论、`stage-plan.json` 未同步。

六轮交付：round-1（`r194000`）三项业务缺陷 + 自查一项 → `r198000`；round-2（`audit-round2.md`）FAIL 15 项 → `r201300`；round-3（`audit-round3.md`）FAIL 14 项 → `r203300`；round-4（`audit-round4.md`）FAIL 9 项中 8 项属实并已整改（第 9 项 `R3-B-10` 属跨模块，维持 `CR-S104-2`）→ `r204700`；行尾与文档口径修正后复验 → `r204800`；round-5 复核收敛为 5 项阻塞、其中 4 项属实并已整改 → **`r205400`**（详见 §6）。本卡所有「已验证」只指 Linux + SQLite + 模拟器这条链路。

## 1. card / contract

| 项 | 值 |
|---|---|
| 卡 ID / 契约版本（指定 = 实际） | S1-04 业务核心 / 0.2.1（开工时从 `contracts/contract-source.json` 读取） |
| 关键真源指纹 | `contract-source.json` `14e938a6…369898f`、生成头 `locker_contracts.h` `34306a86…629340a`、任务卡 `6a842045…e7016473`、开工时 `stage-plan.json` `ebef6ddc…94a02ee` |
| 源码清单 / 版本门禁 | `source-manifest.txt`：**81 个文件**逐文件 sha256；根 `CMakeLists.txt:41-61` 配置期比对生成头与 contract-source，不一致 `FATAL_ERROR` |

12 个真源文件的完整 sha256 见 [`verification.md`](pre-takeover-verification.md) §F。仓库已按用户授权初始化（`git init -b main`、`core.autocrlf=false`），基线提交 `cd4505e`；逐文件指纹记在 `source-manifest.txt`，不依赖 Git。

## 2. prerequisites

S1-01（`locker_common`）、S1-02（`locker_storage`）、S1-03（`locker_device` + `locker_simulator`）均 DONE 且被直接复用：时钟、请求号、UUID 全走注入端口；仓储只经 `locker_repo_*`（`host/domain/` 下 `grep -i sqlite` 零命中）；设备只经 `locker_device_*` 十四个契约符号。开工前 `stage-plan.json` 为 `active_task="S1-04"`、`depends_on=["S1-02","S1-03"]`（均已 DONE），无本卡残留。

## 3. scope 与 dependencies

**可改目录内**：`host/domain/`、`tests/unit/domain/`、`tests/integration/`，外加根 `CMakeLists.txt` 三行 `add_subdirectory`。**未修改** `contracts/`、`scripts/`、`host/{common,storage,device,simulator}/` 的任何源文件与其它测试目录。逐文件行数见 [`verification.md`](pre-takeover-verification.md) §A。

Core 是 `host/domain` 内唯一的仓储写者与唯一的设备发送者：对外 API 一律入队后由单所有者线程处理，**没有任何路径在调用者线程直接读改写仓储或设备**。线程循环、处理器与状态机见 [`verification.md`](pre-takeover-verification.md) §B。依赖只有 SQLite 3.54.1（S1-02 已 vendor，经 `locker_storage` 间接）与 POSIX `pthread`/`clock_gettime`，**新增对外依赖 0**。

## 4. validation

### 4.1 命令与结果

**VMware guest（唯一 Linux 验收环境）**，canonical `run_id=r205400`（`r194000`/`r198000`/`r201300`/`r203300`/`r204700`/`r204800` 日志一并保留），源码经 `tar` 快照传到 `~/locker-workspace/S1-04/batches/r205400/src`，同一份源码连跑三棵树。环境：ubuntu、`Linux 7.3.0-rc5 x86_64`、cmake 4.2.3、gcc 15.2.0、`FILES=105`。

| 步骤 | 退出码 | 关键输出 | 证据 |
|---|---|---|---|
| 普通树 / ASan+UBSan / TSan | 各全 **0** | 三棵树均 `CONFIGURE_EXIT=0`、`BUILD_EXIT=0`、`WARNINGS=0`、`CTEST_EXIT=0`、`SANITIZER_REPORTS=0`、`100% tests passed, 0 tests failed out of 27` | `vm-s104-r205400-{configure,asan,tsan}.log` |
| 三项设计检查 | 全 **0** | 见 4.3 | `check_{contracts,stage_plan,vision_contracts}.json` |

`environment.log` 记 `PLAIN`/`ASAN`/`TSAN` 三行 `configure build ctest reports` 四元组，本轮三行都是 `0 0 0 0`。**S1-04 断言合计 2950**（`text` 213 / `json` 159 / `repo` 344 / `state` 368 / `device` 931 / `commands` 623 / `core` 217 / `integration` 95）；另有前置卡 19 项测试。断言表与负向对照见 [`verification.md`](pre-takeover-verification.md) §G。

### 4.2 验收项、中断矩阵与关键不变量

**验收项**：卡片 13 条逐条对应实现位置与测试证据，全表见 [`verification.md`](pre-takeover-verification.md) §C。要点：正常借还走 `create → arm → EXECUTE → 证据 → confirm → 同一事务结账`；只开门不取物落 `NO_CHANGE`；提前确认/错误用户一律 CONFLICT/INVALID；两个申请竞争第二条 BUSY；15 秒证明过期、四类业务超时、UNKNOWN 输入、仅 ACK 不开门、物理变化与本人确认双满足、重启先核对不重放均有独立断言。

**中断矩阵**：模拟器五阶段故障注入（BEGIN/INTENT/EVENT/COMMIT/RESULT）全部 → `REVIEW_REQUIRED("UNKNOWN_RESULT")` 且**不重放**；另加「应答丢失 → 只查询不重发」「脉冲被抑制 → 30 s `OPEN_TIMEOUT`」「主机重启 → `RECOVERED_AFTER_RESTART`」「终结后崩溃 → 快照补登记释放」。见 [`verification.md`](pre-takeover-verification.md) §D。

**事件连续性**：`physical_ready_for_action` 要求 `cursor.continuity_ok && !fetch_gap`；`HISTORY_INCOMPLETE` 一旦成立不得被后续连续页洗白（`uart-v1.md:50`）。该锁存有**三条独立入口**：实时事件 `event->flags` bit2、补取页面内任一事件 bit2、快照 flags bit2；`operation_advance_from_evidence` 在三者任一成立时直接进核验，不再看物品是否等于基线。FETCH 的 `after` 取仓储水位 `import_cursor` 而非视图游标 `cursor.through`（用错会跳号 → CONFLICT → 误锁缺口）。

### 4.3 三项设计检查

`check_contracts.py` / `check_stage_plan.py` / `check_vision_contracts.py` 全部 `"status": "PASS"`、exit=0，输出原件与明细见 [`verification.md`](pre-takeover-verification.md) §E。这三项**只验证设计产物一致性**，不证明业务实现或真机可用。

## 5. deviations

无超出卡片的实现偏离。四处是**按契约或卡范围做的最小选择**：

1. **`device_state.simulated` 恒写 `false`** —— 契约有该字段，但 `locker_core_config_t` 没有来源能说明后端是不是模拟器。登记 `CR-S104-1`，由契约负责人决定加来源字段还是改可空。
2. **`locker_core_apply_supervisor_health` 未实现** —— 卡片明确归 S4-04，未用空实现冒充。
3. **`physical.observed_age_ms` 曾恒写 0，已修为真实龄期** —— `domain_write_physical` 增 `now_ms` 参数按 `now_ms - observed_mono_ms` 计算；未采样或时钟回拨写 `UINT32_MAX`（`defects.md` #31）。
4. **Core 线程同步等设备写 IO 未改** —— 修复要落在 `host/device`，不在本卡可改目录内。登记 `CR-S104-2` 移交设备协议负责人。

## 6. 本卡修复的真实缺陷

累计修复 **62 项真实缺陷**，另有 **2 项跨模块移交**，分七批：

| 阶段 | 编号 | 来源 |
|---|---|---|
| 开发期（`r192000` 前） | #1–#15 | VM 实跑中 ASan / TSan / 集成测试暴露：启动死锁、响应信封多一层 `{`、无 body 失败应答被静默丢弃、`continuity_ok` 从不置真、快照游标三方约束等 |
| round-1（`r198000`） | #16–#19 | codex 审计三项（终结与账本分两个事务、管理员核验不查物品、`ACCESS_ACTIVE` 未开门缺 30 秒出口）+ 自查一项（`domain_change_check` 在 mutation 之后会写出非法 JSON） |
| round-2（`r201300`） | #20–#34 | 复审 FAIL：14×P1 + 1×P2（动作事务与发送顺序、占位释放、`RELEASE_CONTEXT`、物理冲突、时钟域、`TIME_ANCHOR`、UI 健康、幂等、工具投影、核验审计、`execute_in_flight`、过期缓存、查询权限、终结回滚） |
| round-3（`r203300`） | #35–#49 | 复审 FAIL：13×P1 + 1×P2（期限透传、释放屏障、快照冲突、重启可确认、幂等与审计同事务、先改内存、丢应答锁死、版本绕过、阻塞 IO、基线误用、事件跳号、时钟域、槽复用）+ 自查 #49 晚到旧快照回退 |
| round-4（`r204700`） | #50–#58 | 复审 FAIL 9 项中 8 项整改（释放补登记、确认依据绑代际、UTF-8 按码点截断、恢复核验失败即失败、事件锁存门、按令牌释放、证明期限收紧、事件级 HISTORY_INCOMPLETE）；第 9 项跨模块 |
| round-5（`r205400`） | #59–#62 | 复核 FAIL 5 项中 4 项属实：审计缓冲按字符算却按字节开（栈溢出）、未经账本核对就发 `RELEASE_CONTEXT`、完成槽身份核对与写入不原子、`reason` 用 `strlen` 比字符下限；第 5 项仍是 `R3-B-10` 跨模块 |

逐条「缺陷 / 现象 / 根因 / 修法 / 回归断言」见 [`defects.md`](../defects.md)。

## 7. 未验证项与已知限制

设备侧全部来自模拟器（`device backend: simulator`），因此 **真实 MCU / UART 物理层 / GPIO 脉冲硬截止 UNVERIFIED**；**RK 存储介质掉电可靠性 UNVERIFIED**；**相机 / RKNN / 视觉融合未开始**（S2-14 起，`vision_policy` 固定 `DISABLED_FOR_FOUNDATION`）；**`getVisionStatus` / `getOperationVisionEvidence` 未交付**（归 S2-14，未用空成功占位）；**阶段 1 门禁 NOT_RUN**（门禁卡 S1-07）；**round-6 审计进行中**——出结论前本卡保持 IN_PROGRESS。

## 8. 临时残留自查与交接

临时目录 `.s104-tmp/`（构建与传输脚本）**属本轮临时内容，交接前清理**；正式测试、证据与诊断日志已归档在 `docs/evidence/stage-01/S1-04/`。VM 侧保留可复核现场：`~/locker-workspace/S1-04/batches/{r194000,r198000,r201300,r203300,r204700,r204800,r205400}/` 与 `~/locker-workspace/logs/S1-04/{r203000,r203300,r204700,r204800,r205400}/`（其余历史 run 目录因 guest 盘写满已清理，被引用的 canonical run 全部保留）。未删除失败测试、未放宽断言、未修改 `contracts/` 或生成器。审计留痕：round-1 的 codex 会话在写出 `audit.md` 前被宿主重启中断（三条缺陷已全部整改）；round-2/3/4 见 `audit-round{2,3,4}.md`；round-5 报告文件被 `apply_patch` 冲突删除，结论从 `audit-round5-codex-output.txt` 恢复并记入 `audit-round5.md`；四件套逐轮归档。