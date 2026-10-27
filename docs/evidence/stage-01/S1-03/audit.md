# S1-03 round-1 独立审计

日期：2026-10-25（Asia/Shanghai）。工作副本：%REPO%。契约：0.2.1 / REVIEW_DRAFT。

## 1. 结论与审计边界

**FAIL：存在 B-1～B-16 阻塞项，当前不满足 S1-03 技术交接条件。** 主要问题涉及响应关联、旧意图跨会话迁移、恢复/健康屏障、动作持久检查点、模拟脉冲生命周期、EXECUTE 响应丢失注入和测试结论。FAIL 来自下面可定位的源码反例，不是因为本轮未重跑 Linux。

本轮仅新增本 audit.md；不修改实现、正式测试、契约、生成器、README、stage-plan、summary/handoff 或既有证据，不置 DONE，不启动下一卡，不 commit/push。机器索引仍为 active_task/recommended_next=S1-03、S1-03 IN_PROGRESS/report=null；S1-01/S1-02 在索引为 DONE，阶段1 gate_result=NOT_RUN。S1-02 summary 首行状态仍 IN_PROGRESS，属于已有文档漂移，见 N-6，未自行改写或重新裁定该卡。

已按 AGENTS→README→stages/README→stage-plan→任务卡公共规则及 S1-03:58–65，读取阶段0架构、UART全文、internal-interfaces、module-ownership、traceability、三个机器契约/生成头、黄金帧、全部25个受管文件及本卡45个证据文件。复核前置卡报告/交接、S1-01审计及S1-02最新 round-9审计；复用其已锁定依赖。此次为既有实现的离线审计，无新方案、下载或依赖替换，适用调研豁免。

**证据措辞：**“本轮 PASS”只用于实际执行的 Windows Python、散列和静态结构核验；“历史日志记载 PASS”只表示读取开发方保留的运行记录；“静态反例”表示源码分支已足以否定要求，附复现步骤，C运行仍 NOT_RUN。下列修复建议不等于本轮已修改或验证。

## 2. 本轮实际执行与 NOT_RUN

普通 exec_command 在 helper setup 阶段失败，没有启动 shell，也没有 shell 退出码；改用 Node 文件接口与 child_process.execFile 参数数组，cwd 固定本项目，无提权/安装/网络配置变更。

| 操作 | 本轮实际结果 |
|---|---|
| git status --short | exit 128：fatal: not a git repository；不能用 Git 推定历史改动全集 |
| python -B scripts/check_contracts.py | exit 0，PASS；0.2.1，24 routes、82 ports、11 UART requests、4 vectors；c_declaration_syntax=PASS (parse only, not compile/link) |
| python -B scripts/check_stage_plan.py | exit 0，PASS；45 cards、active_task=S1-03、implementation_cards_done=2 |
| python -B scripts/check_vision_contracts.py | exit 0，PASS；camera_owned=false、rknn_available=false、hardware_tested=false |
| python -B scripts/check_feasibility.py | exit 0，PASS；runtime_or_hardware_tested=false |
| 25文件 SHA-256/字节数 | 25/25匹配 source-manifest；清单2794 B，完整SHA见第3节 |
| Python binascii.crc_hqx 独立计算 | exit 0；123456789→0x29b1，四向量与下表一致 |
| mutation-batch.py --check 原入口 | exit 1：脚本硬编码 /mnt/e/stm32项目，Windows不存在该路径；在读源指纹阶段终止，未执行变异/写日志 |
| Windows Python AST/literal_eval 读取 POINTS并检查 old.count | exit 0；48个 old 均恰好出现1次；未执行脚本主体、未替换源码、未写临时脚本 |
| 布局结构/黄金常量/日志统计 | Node/Python读取核验；见第6～8节；不冒充生产C执行 |

**本轮 NOT_RUN：**Linux configure、build、CTest、所有 C 测试二进制及下列针对性反例、daemon 实际启动、ASan/UBSan、TSan、C mutation全跑、WSL/VM SSH与快照/WORKSPACE-INDEX现场核验。原因是本轮在 Windows 做只读源码/证据审计，未进入 guest、未传输或编译源码；不能称为“本轮 Linux PASS”，也不能声称 guest 当前不可用或当前文件仍与历史快照一致。

真实 UART电气/时序、ARM/32位交叉编译、STM32 HAL/GPIO、锁脉冲硬截止、MCU/littlefs/RK存储介质掉电均 NOT_RUN；hardware profile仍 UNVERIFIED。CRC只证明误码检测算法一致，不承担认证或抗篡改。环境目标的 Keil历史构建不属于本卡硬件验证。

## 3. 指纹、范围和复用

source-manifest.txt：2794 B，SHA-256=e3a6448baa860a3d1b1e80492256260948e0e90dc61ba0c246bce51d375cc44f；与 vm-s103-manifest.sha256 内容一致。25项 SHA/字节数全部匹配当前文件和用户给定前12位。用户列表的行数实际是非空行数；以下同时列物理行数，避免把非空行号用作定位。

| 文件 | SHA-256前12位 | 字节 | 物理行/非空行 | 审查范围 |
| CMakeLists.txt | d8f1850b3979 | 4090 | 97/81 | 目标/依赖/注册 |
| host/device/CMakeLists.txt | 7bb766cd517a | 797 | 31/26 | 目标/依赖/注册 |
| host/device/README.md | bec4bb78be8c | 6571 | 91/69 | 边界/契约/说明 |
| host/device/include/locker/device/codec.h | 390af779943e | 7048 | 152/135 | 11对DTO/字段/错误形状 |
| host/device/include/locker/device/transport.h | e2245eab59ef | 2383 | 58/49 | 边界/契约/说明 |
| host/device/include/locker/device/wire.h | 6bb4ea57b45b | 4303 | 108/93 | 边界/契约/说明 |
| host/device/src/codec.c | bfc163be1e9f | 45855 | 1238/1178 | 11对DTO/字段/错误形状 |
| host/device/src/port.c | 1dd5669f8969 | 30638 | 842/771 | 收发/关联/生命周期 |
| host/device/src/transport.c | acb3b8b32253 | 5304 | 180/161 | 收发/关联/生命周期 |
| host/device/src/wire.c | b951abb050f6 | 7914 | 242/216 | 边界/契约/说明 |
| host/simulator/CMakeLists.txt | 38e323558eda | 1520 | 45/36 | 目标/依赖/注册 |
| host/simulator/README.md | f4804c4f82bd | 7113 | 115/90 | 边界/契约/说明 |
| host/simulator/include/locker/simulator/sim.h | 5e3118275071 | 4958 | 105/88 | 边界/契约/说明 |
| host/simulator/src/sim.c | d46a5c0757fb | 76539 | 2119/1933 | 状态机/Journal/重启 |
| host/simulator/src/sim_main.c | 6d98ac398212 | 25325 | 766/707 | socket/控制/拆帧/主循环 |
| tests/contract/device/CMakeLists.txt | 4cfe13ed159d | 2181 | 44/42 | 目标/依赖/注册 |
| tests/contract/device/daemon_smoke.py | 245b59bf8843 | 19088 | 421/372 | 正式断言与反例覆盖 |
| tests/contract/device/test_device_port.c | 625147be5978 | 46747 | 1237/1094 | 收发/关联/生命周期 |
| tests/contract/device/test_golden_frames.c | 421fe6fddf5d | 14849 | 355/318 | 正式断言与反例覆盖 |
| tests/unit/device/CMakeLists.txt | cb8a5a701287 | 984 | 26/24 | 目标/依赖/注册 |
| tests/unit/device/device_test_support.h | bf923a8f35bc | 10062 | 304/259 | 正式断言与反例覆盖 |
| tests/unit/device/test_codec.c | ed8a05cce1f8 | 37226 | 741/682 | 11对DTO/字段/错误形状 |
| tests/unit/device/test_sim_journal.c | 5d49a387ff50 | 18310 | 423/389 | 正式断言与反例覆盖 |
| tests/unit/device/test_sim_protocol.c | 32112b1c3d10 | 50274 | 1080/973 | 正式断言与反例覆盖 |
| tests/unit/device/test_wire.c | f45f786f8ae9 | 13079 | 332/303 | 正式断言与反例覆盖 |

根 CMake:88/89/95/96 的四条 add_subdirectory 删除后，字节数3943、SHA-256=f9e1964ee54465f5e29299721a3f342f929cc81e3ff6a6da8d00b77fc34258b4，**精确匹配 S1-02 根入口清单**，支持“仅四行接入”。其余25项文件合计将在末节记载。

S1-01清单中 host/common及tests/unit/common 共21项全部匹配；S1-02清单32项中，除根CMake上述预期变化外31项全部匹配，包括host/storage和vendored文件。S1-01旧清单的deployment/host README/依赖锁已有S1-02更新，不能误认本轮越界。SQLite 3.54.1（Public Domain）、cJSON 1.7.19（MIT）复用现有 locker_storage_thirdparty，模拟器不调用 Repository业务端口，也不新下第二份库。CivetWeb/libsodium/TLS保持既定后续卡职责；未在本卡引入HTTP/密码算法。

contracts源、生成头、UART布局、黄金帧和UART文档的当前SHA与summary:16–20记载一致；没有修改真源或用静态checker放宽验收。无Git历史和全项目开工前快照，无法证明此前所有scripts等目录“一字未动”；可证明的是上述已有清单覆盖项及本轮开始/结束散列差分。

## 4. 阻塞项 B-1～B-16

以下每项都是整改项；复现是交给开发方的正式回归输入，本轮C运行均 NOT_RUN。P1影响动作/恢复正确性；P2影响验收结论或可复验性。不得通过只改变测试预期来消除反例。

### B-1 / P1：响应没有 session 匹配

- 位置：host/device/src/port.c:36–41、97–117、342–367；codec.c:1154–1171。pending不存session，device_on_frame只按request_no+request_type取走条目，完全不比较response里的session；事件分支:305的检查不能保护响应。
- 反例：新会话S2挂起GET_SNAPSHOT(n)，向socket注入CRC正确、type=0x82/request_no=n、session=S1的合法成功响应；它会消费S2的pending、经apply_snapshot把session/epoch/boot改成旧值并回调sink。HELLO响应同样没有验证new_session_id。
- 要求：uart-v1:23、29要求session+request_no+response_type全匹配。修复须在消费pending/改变镜像前验证会话，HELLO用其预期新session；加“旧/外来响应后再来正确响应”的真实socket回归，确保错误响应没有抢走条目。现有test_foreign_event_dropped只测事件。

### B-2 / P1：协议错误被当作成功响应更新镜像

- 位置：port.c:348–352、176–196、241–273；codec.c:1154–1171。decode_response返回OK代表解码成功，非设备result=OK；错误DTO的成功body已清零。
- 反例：已握手后GET_SNAPSHOT收到18字节BUSY/LOG_UNAVAILABLE；apply_snapshot仍将epoch/boot/revision/watermark清零。随后QUERY/FETCH/ACK用零epoch。HELLO错误也会apply_hello并置hello_complete=true；FETCH错误会清epoch。
- 要求：正常关联并投递错误result，但只有设备result=0才能应用成功body；HELLO失败不得解除未握手状态。回归需跟随一次错误响应再发查询/快照，检查线上字段和可恢复状态，不只断言错误码。

### B-3 / P1：发送适配器把旧意图/守卫静默换到当前 session

- 位置：port.c:620–638、724–731、748–755；fetch/ack:679–706。
- 反例：intent.session_id=S1，适配器已重连S2（同epoch/boot/revision，期限尚有效）；execute_once忽略意图原session、写入S2再发送。ready_guard/context_guard也忽略session；FETCH/ACK丢弃传入event_key的device_id/epoch、只使用当前epoch，旧游标可能确认新日志。
- 要求：AGENTS及uart-v1:408禁止把旧指令迁移到新session；核验DTO绑定的session/epoch/设备归属，允许的空起点须有明确规则。拒绝不匹配输入且不发字节；覆盖断链前构造、重连后提交的旧意图及旧epoch非零cursor。不得靠Core未来“应该不会传错”代替端口防线。

### B-4 / P1：HELLO未核对设备身份，却用配置身份给事件贴标签

- 位置：port.c:176–184、310–312、521–523；internal-interfaces.md:122；stage-00-architecture.md:92。
- 反例：config.expected_device_id设设备A的24位UID十六进制，连接模拟设备B；HELLO包含B的device_uid，apply_hello不比较。随后B事件被填为A的event.key.device_id。非空任意字符串也可通过，handoff:125和测试使用locker-device-01，掩盖了这一点。
- 要求：握手身份必须和固定配置匹配，失败不建立可用会话/不交付冒名事件；以实际12字节UID派生并核对24位小写hex，补错设备与缺必要能力的握手负例。此处不是要求增加认证，CRC依然不是认证。

### B-5 / P1：在途请求号可重复，丢响应后表项无回收

- 位置：port.c:120–164、416–427、587–602、784–806；uart-v1:23。
- 反例：暂停服务器响应，连续两次get_snapshot(n)，pending_add只找空槽，两次均发送；同号不同type也允许。回复只能消费最先命中的条目。内部号避让仅发生在取号瞬间，公开request_no可重复；回绕时直接跳到1而非建立新session。连续丢16个响应，表项不会超时/失败收敛，后续QUERY同样BUSY直至close。
- 要求：在同锁内完成“检查唯一+保留关联号”；不允许未完成号复用，无论type。给有界pending明确超时/失联回收和结果未知语义，EXECUTE绝不因此重发；接近回绕要求新会话。补重复号、外部/内部号碰撞、丢包后仍可核对及回绕边界回归。continue并不能使重复号合法。

### B-6 / P1：sink背压只终止reader，仍允许执行和健康发送

- 位置：port.c:330–331、368–369、399–404、448–459、622–638、768–781；生成头:310、internal-interfaces:37。
- 反例：reply/event回调返回BUSY或NO_SPACE，stream_feed错误使reader退出，但opened/hello_complete/transport.connected仍可为true。后续execute_once/send_health依然写socket，且再无reader核对结果。
- 要求：锁存需要重新核对状态并暂停新动作和健康授权；保留/可补取未交付事实，不静默吞掉剩余字节；只在明确重连/核对后恢复。补真实socket回调拒绝用例，验证执行/健康不再发出、查询/关闭策略明确且不自锁。test_parser_handler_abort实际仅传NULL handler并检查恒为0的calls，未测handler返回错误，更未测端口背压。

### B-7 / P1：恢复与主机健康守卫缺失，驱动前不重查期限

- 位置：sim.c:1173–1199、1237–1276、1329–1336、1357–1434、1477–1490；uart-v1:376、382–386、394；internal-interfaces:154。
- 反例1：HELLO→SNAPSHOT后不FETCH/ACK/RESUME、不HOST_HEALTH，EXECUTE可接纳并写PULSE_STARTED；只排除DEGRADED，没有要求READY，health_seen=false反而跳过健康检查。DISARM后也可如此继续。test_sim_protocol:604–607/676–684与test_sim_journal:318–327把此行为断言为OK。
- 反例2：RESUME persisted_through=(0,0)、本地ACK=(0,0)时只检查不大于ACK，就可READY，即使BOOT屏障未补齐；活动上下文、journal FAULT、UNKNOWN输入、缺少主机健康均未阻止READY。
- 反例3：HOST_HEALTH只更新接收时间，忽略core_progress/core_observed_age；同计数/未知年龄反复搬运可无限续命。UI未知允许仅告警，不能借此豁免Core新鲜度。
- 反例4：now_ms只在接收时取一次，SQLite事务可跨过not_after/2000ms截止；持久化后无重新采时、会话/物理/健康检查，仍记录脉冲。
- 要求：实现完整恢复屏障及READY/新鲜Core守卫；记录快照对应提交边界并要求覆盖，禁止活动/故障放行；注入提交延迟后验证已接纳水位保留、过期不驱动且结果检查点可靠。模拟也需按合同运行，真实硬截止仍留阶段2实测。

### B-8 / P1：正常模拟脉冲永不结束，不能释放再进入下一次操作

- 位置：sim.c:1400–1434、1527–1529、1730；test_sim_protocol.c:757–760、820–823、858–896；summary:291、handoff:380。
- 反例：正常EXECUTE后任意推进注入时钟/关门，pulse_active持续true，没有PULSE_FINISHED路径；匹配RELEASE_CONTEXT永久BAD_PRECONDITION。suppress_pulse只让脉冲从未发生，无法覆盖“已经发生→结束→释放→下一动作”。
- 要求：提供可确定推进的模拟结束转换、结束事件/查询状态/flag与持久结果，完成正常BORROW/RETURN的重复循环；仍明确硬件脉冲只模拟计数。UART:346/400和本卡DevicePort模型没有把脉冲结束语义豁免到固件；summary称“不是验收缺陷”不能成立。

### B-9 / P1：action_no用了跨会话全局主键，重复参数比较不完整

- 位置：sim.c:25、251–256、439–441、480–536、954–990、1281–1303；uart-v1:41、362、388。
- 反例1：S1接纳action1并安全处理后，新HELLO S2仍报告watermark1；S2从1开始的首个合法动作会被旧sim_commands(action_no=1)挡住。按当前代码继续用2绕过，违反“当前session从1”。重启后测试也把新session的1直接断言REPLAY_BLOCKED（test_sim_journal:333–351），没有区分保留历史检查点与当前session水位。
- 反例2：同session/action/command但更换operation_id或not_after（仍在窗口内）、修改expected条件后适配为当前值，existing分支只比较session/command/kind/slot，可能返回OK；out.operation_id还是新请求值，混合新operation和旧结果。重复在期限已过后又会先被期限检查拒绝，无法稳定回原结果。
- 要求：按session分隔当前水位和历史去重检查点，保留旧command可查询；同session HELLO重试不重置，新session不能复用旧动作。幂等必须比较全部意图参数，返回原记录，不拼接新关联。补新session首动作、改operation/deadline/物理绑定、跨会话历史查询与旧动作拒绝回归。

### B-10 / P1：重启丢未决上下文，RELEASE无持久清空检查点

- 位置：sim.c:1357–1363、1515–1546、1727–1738；test_sim_journal:333–336；UART:362、386、398–408。
- 反例：意图/脉冲已落库后restart，代码无条件has_active=false/command_state=NONE，不从sim_commands恢复待核对关联；snapshot无活动上下文，但QUERY还显示旧PULSE_STARTED。旧操作未处理也能通过缺失的恢复守卫开始下一个动作。正常释放也只改内存，不提交清空检查点、不写CONTEXT_RELEASED，日志故障仍可回复成功释放。
- door/item重启直接变CLOSED/PRESENT，若此前门开/工具离位，会凭空产生确定物理事实；“没有物理观测”不能推出“确认闭门且在位”。constructor的显式初始场景可以确定，但restart必须保持模拟场景或报告UNKNOWN再显式注入，不能假装重新观测。
- 要求：原子保存/恢复活动意图及终结/清空检查点，restart不自动清未决；上下文匹配且清空提交成功后才回复释放。覆盖意图已提交/脉冲结果前后正常重开和进程重开、释放提交失败及物品离位重启；SQLite层测试不提升为MCU掉电验收。

### B-11 / P1：drop_ack没有注入EXECUTE接纳响应丢失

- 位置：sim.c:1837–1839；test_sim_protocol:1004–1040、test_device_port:1175–1201、daemon_smoke.py:281–300；任务卡:63、阶段门禁:115。
- 实际只在type=4（ACK_PERSISTED）隐藏回复，EXECUTE(type=7)始终返回。控制suppress_pulse则跳过脉冲；两者没有覆盖“EXECUTE已接纳/可能已脉冲，但主机没收到ACK”的关键场景。daemon smoke没有一次EXECUTE。
- 要求：保留ACK_PERSISTED丢响应测试，同时提供与PULSE_STARTED可独立控制的EXECUTE接纳响应丢失；真实socket/daemon上观察无自动重发、只QUERY/补取且动作最多一次。不得用“ACK请求响应丢失”替代“动作ACK丢失”。

### B-12 / P1：通知队列满被混同关键日志丢失

- 位置：sim.c:738–746、792–804、831–833；test_sim_protocol:901–927；UART:50、356。
- 第33个通知到达前记录已COMMIT，日志行完整，代码却锁存continuity_ok=false、使后续RESUME退DEGRADED；既不增加dropped_events也不持久化该状态，restart又可从meta恢复true。既错误阻断可补取历史，又让声称锁存的故障随重启消失。
- 要求：区分实时发送背压与关键日志实际丢失；丢通知应通过FETCH补齐，不因通知满宣告历史不完整。真实丢关键事实要持久锁存计数/标志且RESUME/restart不能清除。分别覆盖填满通知后完整补取、真实gap后重启；现有队列测试期望必须按规格修正。

### B-13 / P1：已知缺口未反馈页/ACK；物理日志失败可被恢复开关掩盖

- 位置：sim.c:1128–1135、1149–1169、1889–1894、1913–1919、1978–2003；UART:50、370–372。
- inject_gap锁存丢失后FETCH从(0,0)读取，gap_before_page仅看after与first_available，不参考已知缺口，仍可为0；ACK只检查范围，不检查跨未承认缺口，能确认缺口后的尾记录。ACK直接meta autocommit，也绕过fail_journal的COMMIT故障注入。
- 物理控制先改door/item和revision，随后sim_emit_event提交失败，只置journal FAULT；没有持久化历史不完整/丢失计数。把fail_journal关掉立即设HEALTHY，旧物理变化没有事件却可继续操作。失败日志测试:395–402只触发execute的早退，根本没进入sim_run_txn/ROLLBACK，不能证明故障中断事务无半状态。
- 要求：已知历史缺口在分页、连续确认和恢复资格中一致表达，不能靠ACK清掉；关键事实丢失持久锁存并现场核验。补失败发生在BEGIN/意图写入/事件写入/COMMIT/结果更新的正式测试，读取DB验证事务和水位，区分当前前置拒绝与真实提交失败。

### B-14 / P1：daemon CRC重同步与请求号入口不符合帧规则

- 位置：sim_main.c:121–141、196–200；wire.c:215–222；codec.c:1101–1127；sim.c:1834–1842；UART:15、21。
- daemon先按不可信payload_len切出total，调用内部stream；CRC失败时外层仍offset+=total，丢掉其覆盖范围内的合法帧。wire.c单独解析会逐字节重新搜索，二者不同。最小输入：10字节伪LK header声明payload_len=49，后面紧接61字节合法HELLO；伪帧CRC错误，daemon丢前61字节、合法HELLO前51字节一起被吞，不能回复；使用相同流的wire.c则能找出内部HELLO。
- 手工构造CRC正确、request_no=0的HELLO/EXECUTE，decode_request不检查非零号，先修改session/接纳动作，直到emit响应由encode_frame拒绝request_no=0。非法请求不应先产生副作用后才因回复编码失败被丢弃。
- 要求：daemon与单元解析器使用一致的逐字节CRC失败恢复；在dispatch副作用之前校验请求号等头规则。加入上述损坏长度+合法帧、拆分/连续多帧、0号请求的daemon负例；未知type当前直接INVALID丢弃（sim.c:1830–1832）也须按UART:21/406的UNSUPPORTED语义由契约负责人澄清，不能自行另立规则。

### B-15 / P2：4个NOT_CAUGHT“全部语义等价”的证据结论不成立

- 位置：summary:263–272、330；handoff:5、相关mutation说明；mutation-batch.py:85–88、180–183、215–218、323–350。
- m13只在open且hello完成的路径能由编码器兜底；未打开合法对象调用time_anchor(0)，原实现INVALID，变异后NOT_READY，非全域等价。m24当前调用图下确有decoder重复守卫。m32仅在fail_journal=true支路有ROLLBACK兜底，不覆盖journal_state=FAULT而fail_journal=false。m39删除decode错误早退会放行别的非法字段：非零HELLO前缀、但new_session/host/version合法时decoder返回BAD_PAYLOAD，handler不查前缀而建立会话；DELETE早退显然非等价。
- 要求：保留真实44/48结果，按第8节分类补负例/归因或缩小论断，不把“当前测试没分辨”写成“已证明等价”。补回归并重新产出匹配新源码的运行证据；不能改日志标签或删除生存变异。

### B-16 / P1：正式复验脚本删除活跃源码，校验失败不阻止继续

- 位置：evidence-run-vm.sh:6、11–14、31–56、74–87、171–176；mutation-batch.py:5–7、278–288；AGENTS:22–24。
- run-vm固定SRC=$HOME/locker-workspace/S1-03/src并rm -rf整个目录；没有读取WORKSPACE-INDEX、隔离批次或确认当前活跃快照。哈希检查只打印SHACHECK_EXIT，Python本身不退出失败；解包/配置/测试失败后脚本仍可继续归档，最终cat可能返回0。mutation在HOME直属s103_mut建工作区，违反已有目录纪律。
- 要求：复验使用全新批次快照目录并校验路径/清单/固定摘要，绝不删除覆盖活跃源；任何解包/指纹/构建/测试失败必须传播非零并停止后续验收；变异工作区进入locker-workspace所属build/runtime/logs/artifacts结构。先修脚本再由开发方产生新Linux证据。本轮没有执行这些危险脚本，也未删除/移动VM任何目录。

## 5. 六项待裁定约定 A～F 与非阻塞项

| 项 | 独立裁定 | 依据/后续 |
|---|---|---|
| A message_type带0x80 | 接受当前表示；不是两个方向相撞就必然必须如此的共享契约定论 | UART:13明确线上响应0x81..0x8b；原生reply是响应union，现选择自洽且有断言。公开DTO字段未写死，须由契约负责人统一host/Core/未来HAL sink约定，不能各端另裁；不要求本轮手改生成头 |
| B pending_take遇type不符continue | 局部分支可接受；不能批准其背后的重复号设计 | UART:23要求未完成号不得复用，且session匹配仍缺失。修B-1/B-5后，该扫描不应靠“陈旧条目复用号”解释正常行为 |
| C open重置CLOSED/PRESENT | constructor可定义初始模拟场景；restart不经观测强改事实不能接受 | 工具离位/门打开重启反例见B-10；不能把“无观测”作为确定状态依据 |
| D 没有PULSE_FINISHED | 阻塞，B-8 | suppress_pulse无法代表已发生脉冲的结束，无法按正常路径释放/循环；硬件定时器实测仍NOT_RUN |
| E 32通知满锁存continuity=false | 阻塞，B-12 | 日志已提交与关键事实丢失不同；当前既误报又未持久锁存 |
| F 语法/引擎error token | 接受可诊断的token分类；具体文本是私有控制协议 | status_token:251–294覆盖列出的引擎错误；语法控制有下面N-2的严格性问题。现daemon测试并非覆盖所有token/命令 |

| 编号/级别 | 位置与事实 | 建议 |
|---|---|---|
| N-1 / P2 | sim_main.c:205只有消费UART请求后pop_event；控制面door/card改变并不会立即刷新发送，poll空转也不刷。daemon_smoke:251先发SNAPSHOT才收两个“异步”事件，证明的是请求触发排空。 | 明确实时事件发送时机，在控制产生事件及输出可写时推进，增加握手后仅控制注入、不再发UART请求的接收用例；本版未给独立推送延迟门槛，不将此一项单独升级安全阻塞。 |
| N-2 / P2 | sim_main.c:356–373的%2x允许部分hex，例如0g0g0g0g会被解析成4个零字节；parse_cursor/inject_gap的sscanf允许尾垃圾/符号；:550–554超长只清当前chunk，没有丢到下一换行，后续同一长行尾部可被当新命令。 | 按完整token、范围和整行状态解析，超长丢弃到换行；加跨read超长、多行粘连、非法hex、负数/溢出/尾垃圾负例。未知无参命令目前给missing_argument，是已测私有约定。 |
| N-3 / P2 | transport.c:137–150及sim_main.c:99–115用阻塞send/write且无写超时；慢读客户端可卡住调用方/daemon。transport.connected在reader和writer故障路径无统一锁/原子（transport.c:143/169/176，port.c:388–393）。 | 在接入Core前明确IO线程/有界非阻塞排队与关闭互斥；补断链同时写、慢读背压的TSan/退出回归。历史TSan全绿不证明未执行故障交错无race。 |
| N-4 / P2 | sim.c:502/560/1057把sqlite3_step非ROW都近似当结束，没分DONE/IO错误；:513/575/1080等复制BLOB不检查sqlite3_column_bytes；u64解析:164–179无溢出检查。 | 对损坏/截短/IO错误fail closed，不将读错写成UNKNOWN_RESULT或短页；覆盖短UUID blob、非法u64和SQLITE错误。此处不要求引入第二套Repository或修改原库。 |
| N-5 / P2 | handoff:64把close写void，生成头:291实际locker_status_t；:202称destroy后仍能调用并返回INVALID，但destroy已free且未清created，这是UAF；:204称sim_destroy不关库，但sim.c:1654明确调用close。 | 修交接的签名/所有权；销毁后指针失效不得再用，调用者置NULL。DTO示例是伪结构，不应称“逐字”。14个locker_device_*符号被反复称十二端口/六端口，也应统一。 |
| N-6 / P3 | summary:40称depends_on含S1-02，机器索引只有S1-01；S1-02 summary:6仍IN_PROGRESS而索引/入口DONE；本卡scope表多个行号/字节和合计陈旧。 | 各文档负责人改成当前事实；不由审计AI擅改状态。具体数值差异见第7节。 |
| N-7 / P2 | tests/contract/device/CMakeLists:32–44没有Python时不注册daemon_smoke；“C测试仍是验收”不能覆盖sim_main。 | 验收环境必须有Python或把进程级验收显式NOT_RUN；不能以变少的CTest总数全绿替代0600/控制/daemon检查。 |
| N-8 / P3 | device_test_support.h:192无条件unlink固定/tmp/locker_s103_wal_cleanup，未找到正式使用它的路径。 | 看似旧验证清理残留；由开发方确认并移除无关unlink，保留正式夹具/证据。审计未删除该文件或任何不确定项。 |

## 6. 用户要求的12项逐项评价

这里“符合”仅限已审范围，动态结果仍按第2/7节区分。

| # | 审查项 | 评价与依据 |
|---|---|---|
| 1 | 黄金帧/CRC/11对长度 | 静态及独立Windows计算符合：4条C常量与JSON逐字一致；长度表见下；CRC不含magic/CRC本身。C编码器逐字节执行本轮NOT_RUN，历史测试记录通过。常量照抄未自动绑定JSON，未来漂移需正式门禁。 |
| 2 | 半帧/粘帧/CRC/长度 | wire.c缓冲524、daemon缓冲1048，超长>512与非magic均有界丢字节，无对端长度malloc；wire.c CRC逐字节恢复。daemon CRC切片丢弃不同，0号请求副作用见B-14，故整体FAIL。 |
| 3 | 旧session/重复action/跳号 | sim_dispatch拒旧session，非零action和跳号/低水位分支存在；重复不新增脉冲。但port旧响应/旧意图、session水位、全参数比较、READY/健康均有反例，B-1/3/5/7/9，FAIL。 |
| 4 | 持久化/重启/事件键 | SQLite三表、WAL+FULL、boot递增及BOOT seq1、20位文本排序/查询可复核，旧记录可跨boot读。活动检查点/释放结果未恢复/持久化，默认物理事实被改，B-10，FAIL；不宣称MCU掉电。 |
| 5 | ACK与PULSE_STARTED独立 | ACK_PERSISTED响应可抑制、后续脉冲可禁止，代码如此；缺EXECUTE接纳响应丢失，B-11，FAIL。daemon没有执行/脉冲用例，不能补足。 |
| 6 | Journal失败拒绝新动作 | fail_journal/FAULT早退会LOG_UNAVAILABLE、保留水位0/无active，有正式断言；但仅验证前置拒绝。COMMIT回滚、结果失败以及恢复后连续性存在缺口，B-13，验收证据不足。 |
| 7 | 0600控制口/无HTTP | listen_unix先umask0077、bind、chmod0600；历史daemon确有两个0600实测。项目这两个模块无HTTPserver/公共路由/HAL；本轮socket运行NOT_RUN。标识模拟未写进公共schema，但daemon启动缺指定backend标记，见第7节。 |
| 8 | 独立Journal与复用 | 三表只在独立journal_path中创建，不调用业务Repository；复用locker_common与现有sqlite/cJSON，未重造DB/HTTP。路径不隔离不是由目录测试自动证明：调用者仍须配置独立业务/设备文件，不能把SQLite文件头当“未共享业务库”的全部证明。 |
| 9 | 目录边界/残留 | 25项匹配；根CMake四行精确证明；common/storage清单匹配。无Git不能认证历史全集；本轮范围差分见末节。VM脚本会删除活跃src、mutation HOME路径违规，B-16；不以本地无产物推断VM清理完毕。 |
| 10 | 日志/报告一致性 | 19/19、2768、110、run_id和25/25源校验的原始记录能对上；是历史数据，不是本轮执行。语义结论、所有命令覆盖、文件数/字节等有偏差，见第7节。 |
| 11 | mutation48与4未捕获 | old唯一48/48，本轮独立核验；历史所有configure/build=0，CTest失败44、生存4；只m24可按当前调用图接受等价，其余有边界/反例，B-15与第8节。 |
| 12 | 测试非空洞/生命周期 | codec/流解析有实际边界、固定向量和故意变异；socket测试使用真transport+sim但不链接daemon，Python确实启动真实进程。多项期望镜像错误实现（未READY即执行、重启清active、通知满视缺口、屏蔽脉冲代替结束）。handler abort是空洞断言，未知响应/身份/背压/失败提交缺覆盖，不能仅凭断言数认定完整。 |

### 独立黄金帧复算

用Windows Python标准库binascii.crc_hqx(data, 0xffff)，data取完整帧[2:-2]；不调用被审C CRC实现。完整合法帧为payload_len+12。

| JSON向量 | 帧字节 | payload_len | 独立CRC / 线上CRC | 结论 |
|---|---|---|---|---|
| hello_request | 61 | 49 | 0xbdd8 / d8 bd | 一致，C常量逐字相同 |
| snapshot_request | 28 | 16 | 0xea6e / 6e ea | 一致，C常量逐字相同 |
| crc_error | 28 | 16 | 0xea6e / 6e eb（0xeb6e） | 必须拒绝，C常量逐字相同 |
| oversized_header | 10 | 513 | 仅header，无CRC | 长度立即拒绝，C常量逐字相同 |

算法向量ASCII 123456789→0x29b1（b1 29）；它不是完整UART帧。

| 消息type | 请求payload | 成功响应payload |
|---|---:|---:|
| HELLO (1) | 49 | 71 |
| GET_SNAPSHOT (2) | 16 | 189 |
| FETCH_EVENTS (3) | 49 | 85+96N，N=0..4，最大469 |
| ACK_PERSISTED (4) | 48 | 34 |
| RESUME_READY (5) | 64 | 27 |
| DISARM (6) | 18 | 27 |
| EXECUTE_UNLOCK (7) | 99 | 93 |
| QUERY_COMMAND (8) | 48 | 93 |
| HOST_HEALTH (9) | 73 | 27 |
| TIME_ANCHOR (10) | 36 | 46 |
| RELEASE_CONTEXT (11) | 80 | 27 |

event96=96；DEVICE_EVENT=16+16+96=128；非0结果响应严格18。逐字段宽度/顺序与uart-layouts/生成DTO/uart-v1相符，C未用sizeof(struct)直接上线。错误码0..14匹配源枚举；部分字段合法性/状态守卫的问题已单独列出，不把长度相符等同语义正确。

## 7. summary/handoff声明与现有历史证据对照

| 声明 | 独立读取结果 | 能证明什么/限制 |
|---|---|---|
| VMware run 20261025-050650 | environment/run-summary、configure/build/ctest路径一致；Ubuntu26.04.1、kernel7.3.0-rc5、x86_64/ext4、gcc15.2/cmake4.2.3 | 历史日志身份一致；本轮未SSH确认该guest/文件系统，不能把读取记为现场验证 |
| WSL2 run 20261025-050426 | wsl-run-id、environment、run-summary及各build路径一致，kernel含microsoft-standard-WSL2 | 与VM身份分开，未改名混用 |
| configure/build正常，0警告 | 两环境原始configure/build记录exit0、警告/错误计数0 | 日志自洽；没有重编译，非本轮运行结果 |
| CTest 19/19 | 两环境各三个ctest日志（普通/ASan/TSan）均有19条Passed及100% | 19是17个C二进制+schema漂移Python+daemon Python；不是19个C单元测试 |
| 普通/ASan/TSan耗时 | VM 3.55/4.81/5.18s；WSL 3.14/4.75/4.96s，与报告一致 | 历史统计可复算，不证明缺失场景正确 |
| TOTAL_ASSERTIONS=2768 | 两份unit-tests日志逐项合计2768：common505、storage1212、本卡C1051；两文件内容相同 | 不是全属S1-03；宏/循环多次检查和测试完整性是不同概念 |
| DAEMON_CHECKS=110，FAILURES=0 | 两份smoke日志均exit0、两个socket_mode=0600、snapshot响应+2事件、drop_link/reconnect和restart记录 | 独立进程路径确实有证据；没有EXECUTE/QUERY历史命令恢复、正常脉冲结束、拒绝/抑制/日志失败等完整控制面场景 |
| 48点捕获44 | mutation.log原始48块全部configure=0/build=0，44个ctest=8、4个=0 | 数量属实；“4个均证明等价”不属实，见第8节 |
| MANIFEST 25/25与SRC_STRAY_ARTIFACTS=0 | vm-source-verify25条OK、SHACHECK_FAILED=0；VM清单与本地原清单字节一致 | 证明历史脚本记录的25项；扫描仅host/tests下.o/.a/.so/.pyc/CMakeCache，未扫描DB/socket/任意日志，不能泛化为整个快照无残留 |
| 快照82036352 B/fe6fa3fc…0addd、SRC_FILES1193 | run-summary记录摘要/字节与source-verify1193一致；extract为EXTRACT_EXIT=0 | tar原文件和VM完整树未取得，1304条tar/完整摘要本轮无法独立重算，不把报告自述提升为全快照认证 |
| 31个日志=17VM+14WSL、总45文件 | 实际30个运行.log：16VM+14WSL；另vm-s103-manifest.sha256使VM文件数为17；再加mutation.log才是31个.log | 总45个原文件属实，分类文字有误，不能把manifest叫VM日志 |
| 25源码文件443686 B | 当前清单合计443158 B；全部25指纹一致 | 报告合计陈旧；sim_main现25325 B/766行（summary写24802 B/756行），simulator README7113 B/115行（summary写6917 B/114行）等须同步 |
| 按三元组匹配、已完整通过验收 | 实际响应仅匹配请求号/type，且错误body会应用 | B-1/B-2直接否定该行为声明，历史绿灯不覆盖它 |
| 无FINISHED是允许的限制 | handoff承认未实现，但summary直接裁为非缺陷 | B-8；不把纯软件模型必需语义说成硬件未购造成的NOT_RUN |
| 控制面覆盖“全部控制命令” | daemon smoke主要测试door/card、drop_ack、inject_gap、restart、drop_link/status/quit；没有item、reject_next、suppress_pulse、fail_journal、cursor或超长行 | 直接引擎单元测试和daemon文本协议覆盖不同，不能相互冒充 |
| 4项设计检查全绿 | 保存JSON及两Linux design-checks均PASS；check_contracts的pycparser为NOT_RUN；本轮Windows parse-only PASS | 这是环境依赖差异，summary:204已解释，非造假矛盾；四项均标明不证明业务/硬件 |
| UART/GPIO/HAL/littlefs/CRC认证 | summary/handoff明确NOT_RUN/不声称 | 边界说明合理，继续保留；CRC认证应写不提供，非待CRC实验即可认证 |

启动输出sim_main:691实际只有ready uart=... control=... journal=...；25文件中未出现阶段公共规则:19指定的“device backend: simulator”。当前文件/文档多处明确模拟，未发现把结果冒称实物；仍列 **N-9/P2**：启动日志/配置说明和测试报告应统一醒目标识，补精确断言，不往公共schema加字段。

### 原45个证据输入指纹

下表为本轮读取时的原文件；新audit不计入其中。12位散列仅供定位，开始/结束差分使用完整SHA-256。脚本只审查，未执行；原始日志不覆盖。

| 文件 | 字节 | SHA-256前12位 |
| check_contracts.json | 535 | 0ac0a396fffb |
| check_feasibility.json | 242 | cbe1da1d34c5 |
| check_stage_plan.json | 319 | 97cacc89c27d |
| check_vision_contracts.json | 572 | 3f8b58703a9d |
| evidence-make-manifest.sh | 789 | 8bff25f827fe |
| evidence-make-snapshot.sh | 543 | eba7cd69e1fb |
| evidence-run-vm.sh | 8400 | 789959ac41c2 |
| evidence-run-wsl.sh | 6782 | fe2a058b1267 |
| handoff.md | 27703 | ae22914e6d87 |
| mutation-batch.py | 16804 | 038dcdb86df1 |
| mutation.log | 73135 | 3c5c8590ec13 |
| source-manifest.txt | 2794 | e3a6448baa86 |
| summary.md | 30818 | 829e9d60ee3a |
| vm-s103-asan-build.log | 5232 | def982e1050c |
| vm-s103-asan-configure.log | 707 | 6fbaa24868a4 |
| vm-s103-asan-ctest.log | 2370 | 20d7779e3bcc |
| vm-s103-build.log | 5269 | e46a5ccce015 |
| vm-s103-configure.log | 723 | 7f3127863903 |
| vm-s103-ctest.log | 2345 | f43093c41893 |
| vm-s103-daemon-smoke.log | 2745 | 1e4c57917ff2 |
| vm-s103-design-checks.log | 1860 | b2ed01ad0568 |
| vm-s103-environment.log | 236 | 832d8d698bad |
| vm-s103-extract.log | 15 | 605c3b093f42 |
| vm-s103-manifest.sha256 | 2794 | e3a6448baa86 |
| vm-s103-run-summary.log | 938 | 29685153908b |
| vm-s103-source-verify.log | 1019 | eca959b7b27d |
| vm-s103-tsan-build.log | 5232 | 9174693957ee |
| vm-s103-tsan-configure.log | 707 | 82a64c12cd3c |
| vm-s103-tsan-ctest.log | 2367 | 370bfc25760d |
| vm-s103-unit-tests.log | 1130 | 2ec380be094e |
| wsl-asan-build.log | 5232 | f8fe9e307e56 |
| wsl-asan-configure.log | 715 | 903cf3b0e9a8 |
| wsl-asan-ctest.log | 2378 | 881d172e8834 |
| wsl-build.log | 5269 | 5a270f97f7a8 |
| wsl-configure.log | 731 | b58b928c0f6d |
| wsl-ctest.log | 2353 | f3d470a34060 |
| wsl-daemon-smoke.log | 2754 | 695cceb2177c |
| wsl-design-checks.log | 1860 | abf0df5c7cdf |
| wsl-environment.log | 269 | a80e693533c3 |
| wsl-run-id.txt | 110 | 69e5c12a8b53 |
| wsl-run-summary.log | 1524 | 4ed39c187b29 |
| wsl-tsan-build.log | 5232 | 382d2639b05d |
| wsl-tsan-configure.log | 715 | f3e830f76423 |
| wsl-tsan-ctest.log | 2375 | 231845ba84e5 |
| wsl-unit-tests.log | 1130 | 2ec380be094e |

## 8. mutation48逐点核验与四个生存变异

mutation.log：73135 B、865物理行，SHA-256=3c5c8590ec1367f1363c7c9a9b994fa034c936c9dfbf5a991246e81722b417b2。七条SOURCE_FINGERPRINT前12位与当前文件一致；old唯一性由本轮Windows Python从AST读取POINTS字面量后逐项count验证，48/48为1，没有执行被审脚本的副作用。

所有历史点位configure=0/build=0；下面CAUGHT表示历史ctest=8，NOT_CAUGHT表示0，本轮动态变异全跑NOT_RUN。数字为日志assertion_failures/首个失败测试分母，**不是目标缺陷独占的唯一断言数**。

| 点位 | 历史结果 | 日志失败行数/首分母 |
| m1_crc_poly | CAUGHT | 45/124 |
| m2_magic | CAUGHT | 7/124 |
| m3_oversize_len | CAUGHT | 4/124 |
| m4_crc_mismatch | CAUGHT | 12/124 |
| m5_reqno_zero | CAUGHT | 2/124 |
| m6_event_reqno | CAUGHT | 2/124 |
| m7_pending_type | CAUGHT | 2/188 |
| m8_event_session | CAUGHT | 2/188 |
| m9_reply_flag | CAUGHT | 63/188 |
| m10_simulated_gate | CAUGHT | 2/188 |
| m11_slot_id | CAUGHT | 2/188 |
| m12_fetch_limit | CAUGHT | 3/188 |
| m13_anchor_reqno | NOT_CAUGHT | 0/0 |
| m14_cursor_halfzero | CAUGHT | 5/168 |
| m15_event_type_range | CAUGHT | 3/168 |
| m16_card_type | CAUGHT | 2/168 |
| m17_reserved | CAUGHT | 3/168 |
| m18_supply_unknown | CAUGHT | 2/168 |
| m19_decode_version | CAUGHT | 2/168 |
| m20_failed_len | CAUGHT | 2/168 |
| m21_fetch_len | CAUGHT | 2/168 |
| m22_queue_full | CAUGHT | 2/300 |
| m23_queue_no_session | CAUGHT | 2/103 |
| m24_hello_version | NOT_CAUGHT | 0/0 |
| m25_has_more | CAUGHT | 2/300 |
| m26_fetch_after_bound | CAUGHT | 2/300 |
| m27_ack_regression | CAUGHT | 4/300 |
| m28_ack_upper | CAUGHT | 3/300 |
| m29_resume_mode | CAUGHT | 3/300 |
| m30_execute_horizon | CAUGHT | 6/300 |
| m31_execute_replay | CAUGHT | 4/103 |
| m32_execute_journal | NOT_CAUGHT | 0/0 |
| m33_execute_door | CAUGHT | 4/300 |
| m34_borrow_item | CAUGHT | 8/300 |
| m35_health_timeout | CAUGHT | 2/300 |
| m36_pulse_flag | CAUGHT | 6/300 |
| m37_release_pulse | CAUGHT | 4/300 |
| m38_release_idempotent | CAUGHT | 6/300 |
| m39_dispatch_decode | NOT_CAUGHT | 0/0 |
| m40_dispatch_session | CAUGHT | 6/103 |
| m41_continuity_flag | CAUGHT | 2/103 |
| m42_inject_latch | CAUGHT | 8/103 |
| m43_boot_increment | CAUGHT | 219/103 |
| m44_socket_mode | CAUGHT | 2/0 |
| m45_status_token | CAUGHT | 2/0 |
| m46_drop_link | CAUGHT | 1/0 |
| m47_frame_resync | CAUGHT | 1/0 |
| m48_event_flush | CAUGHT | 1/0 |

计数注意：脚本:330按所有以FAIL开头的行计数，把单条断言与“FAIL test (N of M)”汇总行一起计入；分母又只取第一个失败测试。因此m43的219/103不是单测试219条独立断言，更不应当作覆盖率；daemon分母0不代表0检查。脚本:342/349只凭ctest非0认定捕获，通用地会把构建/环境错误也误计；本批48次configure/build确实全0，未发现以编译失败冒充捕获，但仍应要求预期测试/断言归因。

| NOT_CAUGHT | 独立裁定 | 必须补充的回归/结论范围 |
|---|---|---|
| m13_anchor_reqno | **受路径条件限制的冗余**，不是全域等价。open+HELLO完成时编码器拒0号，现test_port_guards只覆盖此条件。created但未open的对象，原INVALID、变异NOT_READY。 | 分开记录输入无效/未就绪优先级；补未open/握手未完成的0号调用。若不把返回码优先级当产品要求，可以接受为非关键生存变异，但不能称全部行为不可观察。 |
| m24_hello_version | **当前调用图下可接受为冗余守卫**。sim_dispatch只把decoder已检查requested_version的请求交给static handler；删除handler重复检查仍BAD_VERSION。 | 限定结论在当前入口；保留decoder负例，不把此单点证明外推到其它decode守卫。 |
| m32_execute_journal | **未证明等价，缺FAULT-only测试**。mutation删的是fail_journal OR journal_state==FAULT两个条件；当前测试同时设置两者，只证明fail_journal下COMMIT兜底。真实事务/事件失败可留下FAULT、而fail_journal仍false；若瞬态SQL错误恢复，变异将能继续提交/脉冲。 | 注入一次真实提交或事件存储失败，再恢复底层IO但不显式修复FAULT；必须继续拒绝新动作。分别证明fault latch与COMMIT回滚，不用一个开关覆盖所有路径。 |
| m39_dispatch_decode | **非等价，明确测试缺口**。构造请求body的new_session/host/version合法、HELLO请求前缀非0，CRC正确。decoder:801返回BAD_PAYLOAD但DTO已填好；变异忽略错误，handler:961/964检查仍过，建立session并返回OK。 | 在sim帧入口/daemon测非0HELLO前缀，断言BAD_PAYLOAD、会话/水位/日志不变；增加其它codec-only结构拒绝的入口回归。当前只测requested_version=0恰好被handler二次挡住，不能说明可移除dispatch检查。 |

这些反例是静态控制流推导；本轮没有编译变异C，更没有新增/篡改mutation.log。开发方需要真实运行预期失败/修复通过的证据，保留旧日志并产出新批次。

## 9. 修复与复审交接条件

1. 在同一S1-03范围逐条复现并修复B-1～B-16，先补最小正式失败用例，再修实现；N项分别按风险同步文档/测试。共享接口语义A及未知type错误响应若需澄清，由契约负责人统一，不能普通卡手改生成头或扩公共schema。
2. 按真实模型补齐握手/身份、恢复屏障、Core健康、正常模拟脉冲结束、活动/释放检查点、旧会话与完整意图幂等、真实gap和背压收敛。保持Core唯一写业务账本，模拟器独立Journal，硬件驱动仍不在本卡。
3. 先修B-16脚本；读取VM WORKSPACE-INDEX和现有配置，选择全新批次快照。不得删除/覆盖活跃src、旧共享源或HOME杂放。固定完整构建输入的指纹，核对后在src外build/runtime/logs/artifacts中执行。
4. 在用户已有VMware Ubuntu跑configure/build/CTest及针对性反例，ASan/UBSan、断链/写入/背压交错TSan、正式daemon测试和有归因的mutation；Windows四项checker继续仅为设计一致性。Linux缺条件就逐项NOT_RUN，不补写PASS。不要复用本批绿灯作为修改后代码的复验。
5. 同步本卡README、summary/handoff、文件清单和新证据中的当前事实，撤销“4个全等价”“未READY可执行”“无FINISHED不算缺陷”等误导；历史日志原样保留并标批次，纠正API生命周期和模拟标识。审计只记录，不替开发方改实现或状态。
6. 再次独立复审。所有阻塞关闭且证据匹配后才由有权限的收尾负责人处理DONE；本轮不提前批准S1-04，不自动跨卡，阶段1 gate仍由S1-07决定。

## 10. 文档同步、残留和本轮变更边界

本轮审计的正式交付物只有本audit.md，文档中同步记录了当前事实/旧声明冲突；遵守仅写审计报告的范围，不覆盖被审README/summary/handoff。无源码、接口、业务数据或模块边界变更，不需要重新生成契约；待整改的过期说明已逐项指给文档负责人，不能据此误读为已同步修好。

未创建临时脚本、构建目录、DB/socket或下载包；Python均-B，计算脚本通过-c/内存执行。没有自行清理任何正式测试、日志、fixture、源码或VM产物；N-8的不确定旧残留保留并列明。C:/Users/Public及VM历史临时物是否已清理，本轮未现场核验，不能沿用handoff自述当清理证据。

收尾核验：写报告前完整工作树1703个既有文件与审计开始的完整SHA-256逐项比对：1702项保持一致，summary.md出现1项变更，新增/删除为0；本卡四个目录共24文件，未发现.db/.sqlite/.sock/.o/.a/.so/.pyc/.log、CMakeCache或__pycache__残留。根CMake加24项合计25文件443158 B。本Agent仅写入本audit.md；除下述已观察的summary变化外，原25源码、另外44证据、契约、状态与入口均再次核对原SHA。

**审计期间输入漂移记录：**summary.md开始SHA-256=179bed40466c30da1ff593eb80f5e5600eeb9030d2cfbf356f2c010df9932dd2，写报告前SHA-256=829e9d60ee3a773f887c2ff24b4c512cd84b62adbd2c89305e48dda63a2af235（30818 B）。本Agent尚未发出任何文件写入；已核对运行的四项checker无写summary代码，未由本Agent创建其它工作代理。变化来源未确认，不能宣称全树仅有审计报告的客观变化；不回滚或覆盖该文件。已重读最终summary的状态、指纹、实现边界和验收/变异结论，本文所引问题仍存在；上表summary指纹取最后读取值，其它证据取开始值且未变。若该文件继续变化，后续复审须重新固定输入。
