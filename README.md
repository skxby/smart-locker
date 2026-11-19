# 智能工具借还柜

> **把「谁借了什么、柜门到底开没开、东西真被取走了吗」做成一条有物理证据、断电不丢、结果未知就不敢自动重放的借还链。**

[![MCU](https://img.shields.io/badge/STM32F407-HAL_1.8.5-03234B?logo=stmicroelectronics&logoColor=white)](#)
[![RTOS](https://img.shields.io/badge/FreeRTOS-littlefs-2C8EBB)](#)
[![SoC](https://img.shields.io/badge/RK3568-Linux-C51A4A)](#)
[![Vision](https://img.shields.io/badge/MobileNetV3--Large-7_class-EE4C2C)](#)
[![Contract](https://img.shields.io/badge/contract-0.2.1-blue)](#)
[![Tests](https://img.shields.io/badge/CTest-29_suites-brightgreen)](#)
[![Cards](https://img.shields.io/badge/cards-13%2F45_DONE-orange)](#)

## 项目简介

实验室工具借还场景的三端系统。三块算力各管一件事，谁也不越权：

| 角色 | 职责 | 不做什么 |
|---|---|---|
| **STM32F407** | 锁驱动、门磁/微动、RC522 刷卡、独立 Journal | 不记业务账本、不做身份判断 |
| **RK3568** | 借还账本（唯一写者）、局域网 HTTPS、LVGL 触摸界面 | 不直接写 GPIO、不替 MCU 判物理状态 |
| **Android** | 远程查询、申请、本人确认 | 不直连设备、不参与开锁授权 |

**设计立场** —— 物理真相归 MCU，账本归 RK，决定权归人。系统里任何一处「不确定」，都必须停在原地等人，而不是靠重试去猜。

## 亮点速览

- **锁脉冲的硬截止不靠软件** —— 定时器单脉冲直接驱动 MOS 关断，不依赖 Linux、Flash 完成或 RTOS 任务及时调度。空载 PB6 实测五组波形通过：正常、关中断、挂起调度器、软件提前关断、强制复位；脉冲结束 0.9s 后读回 phase 仍为 2/3，证明软件全程无法运行时脉冲照样按时结束
- **ACK 不是开门，意图不是执行** —— 持久化一条开锁意图，不等于锁已经动过；`ACK` 只表示收到。历史补传不会产生开锁，结果未知时**禁止自动重放动作**，只能进人工复核
- **唯一写者约束** —— RK 是借还账本唯一写者，MCU 控制任务是锁驱动唯一入口；HTTP 工作线程、客户端和 UI 都不直接写账本或 GPIO
- **断电后能说清发生了什么** —— littlefs Journal 提交后真实断电重启，恢复通过；诊断 epoch 保留 `DEGRADED` 与未 ACK 记录，不靠「看起来正常」掩盖丢帧
- **契约先于代码** —— 0.2.1 契约是机器真源：24 个公开入口、82 个内部 C 端口、11 类 UART 请求由生成器产出 C 头文件与 OpenAPI，**禁止手改生成物**，三端不允许各挑一种解释
- **每张卡都要留证据** —— `docs/evidence/` 按卡归档真实 VM 日志、示波器波形、抓包与断电记录；没有证据的卡不能标 DONE

## 架构

```
        Android 客户端              LVGL 触摸屏
              └──────────┬──────────────┘
                   HTTPS（局域网 · 证书固定）
              ┌──────────▼───────────┐
              │      RK3568 业务核    │  借还账本唯一写者
              │  HTTP · 账本 · UI 泵  │
              └───┬──────────────┬───┘
        UART4     │              │  vision.sock
      （ADuM 隔离）│              │
        ┌─────────▼──────┐  ┌────▼─────────────┐
        │   STM32F407    │  │  v11 视觉进程     │
        │ 锁 · 传感 · 刷卡 │  │ MobileNetV3-Large │
        │ 独立 Journal    │  │ 七分类 · 不接 UART │
        └────────────────┘  └───────────────────┘
```

**关键语义**（都在契约里，不靠口头约定）：

1. `ACK` 表示帧已收到，不表示动作已执行
2. 持久化的意图不等于已执行的动作
3. 历史补传不产生开锁
4. 结果未知 → 进 `REVIEW_REQUIRED`，不自动重放
5. 锁脉冲硬截止由硬件保证，软件崩溃不影响关断

## 当前进度

45 张产品任务卡，按依赖序推进，一次只做一张。

| 阶段 | 内容 | 状态 |
|---|---|---|
| 1 · 主机模拟闭环 | 7 卡：骨架 / SQLite 仓储 / 协议与模拟器 / Journal / 鉴权 / 业务 / 验收 | **DONE · 门禁 PASS** |
| 2 · HAL 实物闭环 | 8 卡 + 视觉 8 卡（S2-09..16） | **进行中** · S2-01..06 DONE，S2-07 BLOCKED |
| 3 · Android 与触摸端 | 7 卡 | 未开始 |
| 4 · 独立记录与监护 | 8 卡 | 未开始 |
| 5 · 部署与现场交付 | 7 卡 | 未开始 |

已完成部分：主机 Debug / ASan+UBSan / TSan 三套各 13/13；MCU 侧 UART DMA、真实 littlefs 断电恢复、Control 硬截止、RK 串口/ABI/TLS/配对均已在实物上验证。

**如实说明**：硬件 profile 仍为 `UNVERIFIED`，阶段二门禁 `NOT_RUN`；PC0 供电 ADC 标定按授权后移，`supply_valid` 保持 `false`；真锁线圈、MOS、12V 回路与断电瞬态尚未验收。真机证据只来自 VMware Ubuntu 与实板，不用 mock 替代实物门禁。

## 文档地图

| 想了解 | 读这里 |
|---|---|
| **开发入口与真实状态**（后续 AI 从这里开始） | [`docs/开发入口.md`](docs/开发入口.md) |
| 阶段划分、单卡规约、依赖门禁 | [`docs/stages/README.md`](docs/stages/README.md) |
| 机器可读状态（active_task / 门禁结果） | [`config/development/stage-plan.json`](config/development/stage-plan.json) |
| 接口契约真源 | [`contracts/contract-source.json`](contracts/contract-source.json) · [`contracts/README.md`](contracts/README.md) |
| 架构与状态机 | [`docs/stages/stage-00-architecture.md`](docs/stages/stage-00-architecture.md) |
| 内部接口与线程模型 | [`docs/architecture/internal-interfaces.md`](docs/architecture/internal-interfaces.md) |
| UART 线协议 | [`docs/architecture/uart-v1.md`](docs/architecture/uart-v1.md) |
| 接线图册（14 页逐引脚） | [`docs/hardware/接线图册/接线图册.html`](docs/hardware/接线图册/接线图册.html) |
| 视觉链路（v11 七分类） | [`docs/architecture/vision-architecture.md`](docs/architecture/vision-architecture.md) |
| 各卡验收证据 | [`docs/evidence/`](docs/evidence/) |
| 提交改动前的硬约束 | [`AGENTS.md`](AGENTS.md) |

## 设计检查

```bash
python -B scripts/check_contracts.py        # 契约与生成物一致
python -B scripts/check_stage_plan.py       # 阶段索引与卡状态一致
python -B scripts/check_vision_contracts.py # 视觉契约与帧样例
```

三条命令只验证**设计产物一致性**，不证明业务实现或真机可用 —— 真机结论只认 `docs/evidence/` 里的原始记录。

## 说明

个人工程实践项目，尚未声明开源许可证。仓库内的第三方组件（STM32 HAL、FreeRTOS、littlefs、civetweb、SQLite、OpenSSL、duktape 等）各自遵循其原始许可，详见 `deployment/*/licenses/`。
