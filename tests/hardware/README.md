# S2-07 真实硬件台架入口

先测试台架，柜体部署后在 S5-05 补安装验收。这里的客户端只消费契约0.2.1已有接口；
没有直接开锁、模拟传感器、清库、格式化Journal或任意命令入口。

## 使用条件

- 产品固件与 RK 服务运行，设备身份已核实；测试管理员与普通用户分别登录。
- 通电动作前必须补齐供电/保护、profile、日志健康和稳定台架条件。
  PC0 未标定时保持供电无效；不以客户端绕过服务端或MCU资格。
- TLS 证书必须受信任，访问地址匹配 SAN；本工具不接受跳过校验。
- `presence` 可能触发真实动作，只在本机操作者已经就绪时提交。
  假负载验证不证明锁带载通过。

## 启动与命令

Python3标准库，无额外pip依赖。公开查询/业务操作可以从受信任的同LAN机器使用；
`presence` 和 `reconcile` 必须在RK运行并提供本机 `service.sock`。
socket须为当前UID所有、0600、规范绝对路径；客户端还验证SO_PEERCRED与inode。
没有IPC时不会改走HTTP到柜证明接口。

```bash
python3 -B tests/hardware/bench_client.py \
  --host <证书SAN内地址> --ca <受信任证书> \
  --device-id <已核实的24位小写十六进制ID> --user <测试用户> \
  --socket /userdata/locker/state/service.sock \
  --evidence <src之外的新证据文件.jsonl>
```

密码只在控制终端隐藏输入，不进argv或环境变量。每次启动显式登录一次，交互命令复用会话，
避免每个动作重新登录触发限流。启动时及每个操作前核对设备ID和`simulated=false`；
本机IPC动作还通过IPC读取身份，防止把HTTPS会话送到另一设备。
证据文件独占创建、0600，不覆盖旧文件；请求意图先记录，再发送，响应关联保留。
凭据字段和已知秘密值会被遮盖，原始文件仍须作为私有测试证据保管。

输入 `help` 查看完整命令，`key` 生成幂等UUID。借还的固定顺序：

1. `snapshot`、`device`、`tools` 查当前事实、模式和工具版本。
2. `borrow TOOL_UUID TOOL_REV IDEMPOTENCY_UUID submit` 或 `return ...` 提交申请。
3. `operation OPERATION_UUID` 查原操作；本人用最新操作版本执行
   `presence OPERATION_UUID OP_REV IDEMPOTENCY_UUID`，或刷已绑定的真实卡。
4. 观察真实锁/门磁/工具微动，不使用软件输入；继续查询原操作。
5. 仅本人在WAIT_CONFIRM用 `confirm OPERATION_UUID OP_REV IDEMPOTENCY_UUID`。
   读取结果、账本与设备状态，不能用HTTP 200或ACK代替脉冲/开门/完成。
6. NO_CHANGE、未确认取物、竞争、提前确认、UNKNOWN和故障恢复按卡验收矩阵记录。

`cancel` 携带原操作版本和原因；不保证已接纳动作可取消。
`resolve` 仅管理员现场核验未决操作，保留原用户与类型、物理版本和原因；
支持COMPLETE_BORROW、COMPLETE_RETURN、NO_CHANGE，不能覆盖原事实或在UNKNOWN时强制完成。
`reconcile EPOCH BOOT PHYSICAL_REV SNAPSHOT_REV IDEMPOTENCY_UUID "reason"`
仅本机管理员在无未决操作、闭门账实一致且日志健康时使用，参数来自当前快照；
它不驱动锁，也不能解除Flash故障或证据缺口。DEGRADED时先查真实原因，不自动清故障。

遇超时/断链：工具不自动重试、不刷新版本重提动作。保存幂等键，先查询原operation与实物；
同一意图需要重查结果时沿用原键和原正文，不以新键重放可能执行过的动作。
`outcome_unknown` 表示结果未知，不能判定未执行；有记录意图但无响应的情况同样如此。
独立断电/丢ACK/日志失败测试必须按真实硬件条件实施，本工具不提供故障注入后门。

## 验证与复用

`python3 -B tests/hardware/test_bench_client.py` 是客户端边界回归，已接入CTest；
其中失败路径使用测试替身，不能作为任何实物门禁证据。业务回归仍运行项目全量CTest。
正式台架结果记录在 `docs/evidence/stage-02/S2-07/`，包括operation/command/事件键、
真实脉冲计数、账本与实物对照、人工操作方式及未测项。

实现复用本仓库 `tests/integration/runtime/test_stage_gate.py` 的现有HTTPS/SEQPACKET包络，
不新建业务状态机。对比额外HTTP客户端包，标准库无需新依赖，适配成本更低；
对比模拟器控制口，本工具只能调用正式业务接口，不具备传感器注入能力。
参考 [CPython ssl](https://github.com/python/cpython/blob/main/Doc/library/ssl.rst) 和
[HTTPSConnection](https://github.com/python/cpython/blob/main/Lib/http/client.py)：
复用运行环境维护的PSF许可标准库，不复制上游源码、不下载新包；
维护/安全更新由环境Python与TLS库承担，设备上的Python版本、证书和IPC可用性仍需实际核对。
