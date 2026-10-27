"""Render the design delivery map; does not create implementations or evidence."""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = "docs/architecture/traceability.md"


def render() -> str:
    mapping = json.loads((ROOT / "config/development/interface-map.json").read_text(encoding="utf-8"))
    source = json.loads((ROOT / "contracts/contract-source.json").read_text(encoding="utf-8"))
    routes = {r["operation_id"]: r for r in source["public_routes"]}
    ports = {p["name"]: p for p in source["internal_interfaces"]}
    lines = ["# 接口实施归属与调用链追踪", "",
        "生成的设计索引；修改config/development/interface-map.json后运行scripts/build_stage_docs.py。", "",
        f"契约{mapping['contract_version']}。所有卡目前以阶段状态索引为准；表格只分配职责，不宣称实现或测试通过。",
        "完整函数签名见[C契约](../../contracts/include/locker_contracts.h)，请求/响应见[OpenAPI](../../contracts/openapi.json)。",
        "[阶段入口](../stages/README.md) / [分阶段调用链](stage-call-chains.md) / [机器映射](../../config/development/interface-map.json)。", "",
        "## 1. 公开 API", "", "全部地址相对https://<终端局域网地址>:8443/api/v1；WS为同origin的wss。", "",
        "| operationId | 路径 | 适配/核心 | 手机消费 | 验证卡 | 调用链 |",
        "|---|---|---|---|---|---|"]
    for item in mapping["public_routes"]:
        route = routes[item["operation_id"]]
        lines.append(f"| `{item['operation_id']}` | `{route['method'].upper()} {route['path']}` | {item['implementation']} / {item['core']} | {item['client']} | {', '.join(item['verification'])} | {item['flow']} |")
    lines += ["", f"LVGL使用{len(mapping['public_routes']) - 1}个同名业务RPC（不含WS）及私有presence；显示范围由服务端权限决定。视觉新增入口在S2-14实现，S1不编造视觉结果。", "",
        "## 2. 跨模块 C 端口", "", "初次实现与后续扩展分别列出；后续卡复用初次实现，不另造同名兼容层。", "",
        "| 函数 | 所有者 | 初次实现 | 后续扩展 | 首轮验证 | 调用链 |", "|---|---|---|---|---|---|"]
    for item in mapping["internal_ports"]:
        port = ports[item["name"]]
        lines.append(f"| `{item['name']}` | {port['owner']} | {item['implementation']} | {', '.join(item['refinement']) or '—'} | {', '.join(item['verification'])} | {item['flow']} |")
    lines += ["", "## 3. UART 请求", "", "每类请求都区分主机模拟、MCU解析、MCU语义和真实线路；11类消息不是11个随意重试的动作。", "",
        "| 消息 | 主机协议 | MCU解析 | MCU语义 | 真实线路 | 验证 | 链 |", "|---|---|---|---|---|---|---|"]
    for item in mapping["uart_requests"]:
        lines.append(f"| `{item['name']}` | {item['host']} | {item['mcu_transport']} | {item['mcu_semantics']} | {item['real_link']} | {', '.join(item['verification'])} | {item['flow']} |")
    lines += ["", "## 4. 私有 RPC", "", "SO_PEERCRED/部署权限与当前用户会话各按方法验证；这些不是手机URL。", "",
        "| method | socket | 实现 | 验证 | 调用链 |", "|---|---|---|---|---|"]
    for item in mapping["local_methods"]:
        lines.append(f"| `{item['method']}` | {item['socket']} | {item['implementation']} | {', '.join(item['verification'])} | {item['flow']} |")
    lines += ["", "## 5. 部署接口", "", "F5只使用受限本机工具/离线文件，不增加公开API。pair-export在S2-06可用；备份/制品/更新在S5逐卡实现，详见[部署维护接口](deployment-interfaces.md)。", ""]
    if mapping.get("vision_worker_messages"):
        lines += ["## 6. 视觉worker私有协议", "", "vision.sock不是service.sock的UI方法，也不是MCU串口。只允许本机service连接，无TCP classify/path入口。", "",
            "| 消息 | 适配实现 | 生产者 | worker消费者 | 验证 |", "|---|---|---|---|---|"]
        for item in mapping["vision_worker_messages"]:
            lines.append(f"| `{item['name']}` | {item['implementation']} | {item['producer']} | {item['consumer']} | {', '.join(item['verification'])} |")
        lines += ["", "详细时序及融合规则见[视觉架构](vision-architecture.md)和[视觉接口](vision-interfaces.md)。", ""]
    return "\n".join(lines)


if __name__ == "__main__":
    (ROOT / OUTPUT).write_text(render(), encoding="utf-8")
    print("Generated stage interface traceability; no implementation or gate advanced.")
