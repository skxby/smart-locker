"""Generate design contracts only. This file implements no cabinet business logic."""
from __future__ import annotations

import json
import re
import struct
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_source() -> dict:
    source = json.loads((ROOT / "contracts/contract-source.json").read_text(encoding="utf-8"))
    if source.get("vision_source"):
        vision = json.loads((ROOT / source["vision_source"]).read_text(encoding="utf-8"))
        if vision["contract_version"] != source["contract_version"]:
            raise ValueError("vision/root contract version drift")
        if set(vision["schemas"]) & set(source["schemas"]):
            raise ValueError("duplicate vision/root schema name")
        source["schemas"] = {**source["schemas"], **vision["schemas"]}
    return source


def json_text(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2) + "\n"


def make_openapi(source: dict) -> dict:
    schemas = dict(source["schemas"])
    for name, values in source["enums"].items():
        schemas[name] = {"type": "string", "enum": values}
    schemas["Error"] = {
        "type": "object", "additionalProperties": False,
        "required": ["error", "request_id"],
        "properties": {
            "error": {"type": "object", "additionalProperties": False,
                "required": ["code", "message", "retryable", "operation_id"],
                "properties": {
                    "code": {"type": "string", "enum": [e["code"] for e in source["error_codes"]]},
                    "message": {"type": "string", "maxLength": 256},
                    "retryable": {"type": "boolean", "description": "只允许按规范查询/复用原幂等键重试；不代表可重放硬件动作。"},
                    "operation_id": {"anyOf": [{"$ref": "#/components/schemas/Uuid"}, {"type": "null"}]},
                }},
            "request_id": {"$ref": "#/components/schemas/Uuid"},
        },
    }
    for name in sorted({r["output_schema"] for r in source["public_routes"] if r.get("output_schema")}):
        schemas[name + "Response"] = {
            "type": "object", "additionalProperties": False,
            "required": ["data", "snapshot_revision", "request_id"],
            "properties": {
                "data": {"$ref": "#/components/schemas/" + name},
                "snapshot_revision": {"$ref": "#/components/schemas/U64"},
                "request_id": {"$ref": "#/components/schemas/Uuid"},
            },
        }
    paths = {}
    for route in source["public_routes"]:
        op = {
            "operationId": route["operation_id"],
            "summary": route.get("summary", route["operation_id"]),
            "x-roles": route["roles"],
            "security": [{"bearerSession": []}] if route["roles"] else [],
            "parameters": [{"name": "X-Request-ID", "in": "header", "required": False,
                "schema": {"$ref": "#/components/schemas/Uuid"}, "description": "可选排障关联；缺少时服务端生成，不承担幂等。"}], "responses": {},
        }
        op["x-first-implementation-card"] = route.get("implementation_card", "S1-05")
        for name in re.findall(r"\{([^}]+)\}", route["path"]):
            op["parameters"].append({"name": name, "in": "path", "required": True,
                "schema": {"$ref": "#/components/schemas/Uuid"}})
        if route.get("idempotency"):
            op["parameters"].append({"name": "Idempotency-Key", "in": "header", "required": True,
                "schema": {"$ref": "#/components/schemas/Uuid"},
                "description": "同一用户/方法/路径/键绑定规范化参数；同键不同参数409；与状态变化同事务保存30天。"})
        if route.get("pagination"):
            op["parameters"] += [
                {"name": "cursor", "in": "query", "schema": {"type": "string", "maxLength": 512}},
                {"name": "limit", "in": "query", "schema": {"type": "integer", "minimum": 1, "maximum": 100, "default": 20}},
            ]
        if route.get("query_operation"):
            op["parameters"].append({"name": "operation_id", "in": "query", "schema": {"$ref": "#/components/schemas/Uuid"}})
        if route.get("query_user_revision"):
            op["parameters"].append({"name": "user_revision", "in": "query", "required": True,
                "schema": {"$ref": "#/components/schemas/U64"}})
        if route.get("input_schema"):
            op["requestBody"] = {"required": True, "content": {"application/json": {
                "schema": {"$ref": "#/components/schemas/" + route["input_schema"]}}}}
        status = str(route.get("success_status", 200))
        response = {"description": "已提交的业务结果；不表示物理锁已打开。"}
        if route.get("output_schema"):
            response["content"] = {"application/json": {
                "schema": {"$ref": "#/components/schemas/" + route["output_schema"] + "Response"}}}
        if status == "201":
            response["headers"] = {"Location": {"schema": {"type": "string"}, "description": "已创建资源的相对路径"}}
        if route.get("websocket"):
            response["description"] = "WebSocket切换；Bearer鉴权；仅发送WsChange/WsResync，不承载控制命令。"
            op["x-websocket-message-schemas"] = ["WsChange", "WsResync"]
        op["responses"][status] = response
        for code in sorted({e["http"] for e in source["error_codes"]}):
            op["responses"][str(code)] = {"description": "统一错误；非本人资源按404隐藏。",
                "content": {"application/json": {"schema": {"$ref": "#/components/schemas/Error"}}}}
            if code == 429:
                op["responses"][str(code)]["headers"] = {"Retry-After": {"schema": {"type": "integer", "minimum": 1}, "description": "重试查询的最小等待秒数；不授权重放物理动作。"}}
        paths.setdefault(route["path"], {})[route["method"]] = op
    return {
        "openapi": "3.1.1",
        "info": {"title": "Locker Local API", "version": source["contract_version"],
            "description": "阶段0评审稿。无直接开锁/传感器覆盖/模拟注入公开接口；App与本机UI共享领域逻辑。"},
        "servers": [{"url": "https://{cabinet_host}:8443/api/v1", "variables": {"cabinet_host": {"default": "127.0.0.1", "description": "仅主机测试示例；Android从终端配对信息取实际局域网地址，不预设mDNS。"}}}],
        "paths": paths,
        "components": {"securitySchemes": {"bearerSession": {"type": "http", "scheme": "bearer", "bearerFormat": "opaque random session"}}, "schemas": schemas},
    }


def c_name(name: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def make_header(source: dict) -> str:
    lines = [
        "/* Generated design declarations; do not edit. No product implementation. */",
        "#ifndef LOCKER_CONTRACTS_H", "#define LOCKER_CONTRACTS_H",
        "#include <stdint.h>", "#include <stddef.h>", "#include <stdbool.h>",
        '#ifdef __cplusplus\nextern "C" {\n#endif',
        '#define LOCKER_CONTRACT_VERSION "' + source["contract_version"] + '"',
    ]
    lines += [f"#define LOCKER_{name} {value}u" for name, value in source["constants"].items()]
    lines += ["typedef struct { uint8_t bytes[16]; } locker_uuid_t;",
        "typedef struct { char device_id[25]; locker_uuid_t journal_epoch; uint64_t boot_id; uint64_t event_seq; } locker_event_key_t;"]
    status_names = ["OK", "ACCEPTED", "QUEUED", "INVALID", "BUSY", "NOT_READY", "CONFLICT", "UNKNOWN", "IO_ERROR", "NO_SPACE", "REPLAY_BLOCKED"]
    lines += ["typedef enum { " + ", ".join(f"LOCKER_STATUS_{n}={i}" for i, n in enumerate(status_names)) + " } locker_status_t;"]
    mappings = {"DoorState": "door_state", "ItemState": "item_state", "OperationKind": "operation_kind", "CommandState": "command_state", "SupervisorState": "wire_supervisor_state"}
    for name, values in source["enums"].items():
        numbers = source["wire_enums"].get(mappings.get(name, ""), {})
        prefix = "LOCKER_" + c_name(name).upper()
        lines.append("typedef enum { " + ", ".join(f"{prefix}_{v}={numbers.get(v, i)}" for i, v in enumerate(values)) + " } locker_" + c_name(name) + "_t;")
    for name, values in source["wire_enums"].items():
        lines.append("enum { " + ", ".join(f"LOCKER_WIRE_{name.upper()}_{v}={i}" for v, i in values.items()) + " };")
    internal_source = json.loads((ROOT / "contracts/internal-data-source.json").read_text(encoding="utf-8"))
    for binding in internal_source["command_bindings"]:
        lines.append('#define LOCKER_CORE_' + binding["core_kind"] + ' "' + binding["core_kind"] + '"')
    lines.append('#define LOCKER_CORE_PRESENCE_SUBMIT "PRESENCE_SUBMIT"')
    lines.append('#define LOCKER_CORE_MAINTENANCE_RECONCILE "MAINTENANCE_RECONCILE"')
    lines += [
        "typedef struct { locker_uuid_t session_id; uint32_t request_no; uint8_t message_type; uint64_t received_mono_ms; } mcu_request_context_t;",
        "typedef struct { locker_door_state_t door; locker_item_state_t item; bool supply_valid; uint32_t supply_mv; uint64_t observed_mono_ms; } locker_board_inputs_t;",
        "typedef struct { locker_uuid_t operation_id, command_id, session_id, journal_epoch; uint64_t expected_boot_id, expected_physical_revision, action_no, not_after_mcu_ms; uint16_t slot_id; locker_operation_kind_t kind; } locker_unlock_intent_t;",
        "typedef struct { locker_uuid_t session_id, journal_epoch; uint64_t expected_boot_id, expected_physical_revision; locker_event_key_t persisted_through; } locker_ready_guard_t;",
        "typedef struct { locker_uuid_t session_id, journal_epoch, operation_id, command_id; uint64_t expected_boot_id, expected_physical_revision; } locker_context_guard_t;",
        "typedef struct { locker_uuid_t submission_token, request_id, operation_id; bool has_operation; locker_status_t status; } locker_receipt_t;",
        "typedef struct { locker_uuid_t user_id, session_id; locker_role_t role; bool authenticated; } locker_principal_t;",
        "typedef struct { locker_principal_t principal; locker_uuid_t request_id, idempotency_key, resource_id; uint64_t expected_revision; char kind[32]; char arguments_json[4097]; } locker_command_t;",
        "typedef struct { locker_principal_t principal; locker_uuid_t request_id, resource_id; char kind[32], cursor[513]; uint16_t limit; } locker_query_t;",
        "typedef struct { size_t length; char json[65536]; uint64_t snapshot_revision; locker_status_t status; } locker_query_result_t;",
        "typedef struct { locker_event_key_t key; uint64_t mono_ms, physical_revision, detail; uint16_t event_type, reason, flags; locker_uuid_t operation_id, command_id; locker_door_state_t door; locker_item_state_t item; uint32_t supply_mv; uint8_t card_uid_len, card_uid[10]; bool live_transport; } locker_device_event_t;",
        "typedef struct { locker_device_event_t events[4]; uint8_t count; bool has_more, gap_before_page; locker_event_key_t first_available, last_committed, next_after; } locker_event_batch_t;",
        "typedef struct { locker_uuid_t host_instance_id, ui_instance_id; uint64_t core_progress, ui_progress; uint32_t core_age_ms, ui_age_ms; uint8_t supervisor_state; } locker_health_report_t;",
        "enum { MCU_JOURNAL_RECOVER=0, MCU_JOURNAL_BARRIER=1, MCU_JOURNAL_FINALIZE=2, MCU_JOURNAL_RELEASE=3 };",
        "typedef struct { uint32_t token, barrier_after_token; uint8_t kind; locker_uuid_t journal_epoch, session_id, operation_id, command_id; uint64_t expected_boot_id, action_no; locker_command_state_t terminal_state; } mcu_journal_control_request_t;",
        "typedef struct { locker_uuid_t journal_epoch; uint64_t boot_id, action_watermark, dropped_events; locker_event_key_t first_available, last_committed, ack_persisted; locker_unlock_intent_t active_intent; locker_command_state_t command_state; uint8_t journal_state; bool has_active, continuity_ok; } mcu_journal_state_t;",
        "typedef struct { locker_uuid_t instance_id; uint64_t progress, sampled_mono_ms; uint32_t observed_age_ms; uint8_t target; } locker_progress_t;",
        "typedef struct { locker_uuid_t supervisor_instance_id; uint64_t sampled_mono_ms; bool service_known, ui_known, restart_count_known; locker_progress_t service, ui; locker_supervisor_state_t state; uint32_t restart_count; uint8_t window_attempts; uint32_t cooldown_remaining_ms; bool budget_ready; } locker_supervisor_health_t;",
        "typedef struct { locker_uuid_t request_id, incident_id; char reason[257]; } locker_restart_request_t;",
        "typedef struct { char id[65]; uint64_t snapshot_revision; char resource[16]; } locker_resource_change_t;",
        "typedef struct { uint32_t uid, gid, pid; bool verified_ui_peer, verified_admin_peer; } locker_peer_t;",
        "typedef struct { locker_principal_t principal; locker_uuid_t request_id, idempotency_key, operation_id; uint64_t operation_revision; } locker_presence_t;",
        "typedef struct { locker_principal_t principal; locker_uuid_t request_id, idempotency_key, journal_epoch; uint64_t expected_boot_id, physical_revision, snapshot_revision; char reason[1025]; } locker_maintenance_t;",
        "typedef struct { bool configured; uint8_t port_index; uint16_t pin_mask; uint8_t alternate_function; } locker_gpio_descriptor_t;",
        "typedef struct { bool verified; locker_gpio_descriptor_t door, item, lock_output, alarm, rc522_cs, rc522_reset, supply_adc, uart_link_tx, uart_link_rx, uart_console_tx, uart_console_rx, flash_cs; uint8_t door_closed_level, item_present_level, lock_active_level, alarm_active_level; uint16_t debounce_ms, pulse_ms; uint8_t uart_link_instance, uart_console_instance, flash_spi_instance, rc522_spi_instance, adc_instance, adc_channel, lock_timer_instance, lock_timer_channel, hal_tick_timer_instance; uint32_t rk_uart_io_mv, host_input_mv, divider_upper_ohms, divider_lower_ohms, adc_reference_mv; } mcu_board_profile_t;",
        "typedef struct { void *user; uint64_t (*monotonic_ms)(void *user); bool (*wall_utc_ms)(void *user, uint64_t *out); locker_status_t (*new_uuid)(void *user, locker_uuid_t *out); } locker_clock_port_t;",
        "typedef struct { size_t length; char json[4097]; } locker_vision_policy_t;",
        "typedef struct { size_t length; char json[8193]; } locker_vision_request_t;",
        "typedef struct { size_t length; char json[16385]; uint64_t received_mono_ms; } locker_vision_result_t;",
        "typedef struct { size_t length; char json[4097]; uint64_t received_mono_ms; } locker_vision_status_t;",
        "typedef struct { char socket_path[108]; uint32_t worker_uid, queue_capacity; locker_uuid_t host_instance_id; locker_clock_port_t clock; } locker_vision_config_t;",
        "typedef struct { void *user; locker_status_t (*result)(void *user, const locker_vision_result_t *result); locker_status_t (*status)(void *user, const locker_vision_status_t *status); } locker_vision_sink_t;",
        "typedef struct { bool simulated; char path[108], expected_device_id[25]; uint32_t baud_rate; locker_uuid_t host_instance_id; locker_clock_port_t clock; } locker_device_config_t;",
        "typedef struct { size_t token_length; char token[129]; } locker_auth_request_t;",
        "typedef struct { char path[256]; uint32_t schema_version; bool initialize_empty; } locker_repo_config_t;",
        "typedef struct { uint8_t schema_version; char collection[32], key[193], after_key[193]; uint16_t limit; } locker_repo_query_t;",
        "typedef struct { size_t length; char json[65536]; } locker_change_t;",
        "typedef struct { size_t length; char json[65536]; uint64_t snapshot_revision; } locker_repo_result_t;",
        "typedef struct { size_t length; char json[65536]; uint64_t snapshot_revision; } locker_recovery_t;",
        "typedef struct { char cabinet_name[257]; locker_uuid_t instance_id; locker_clock_port_t clock; uint32_t queue_capacity, completion_retention_ms; bool runtime_health_owned, device_simulated; locker_vision_policy_t vision_policy; } locker_core_config_t;",
        "typedef struct { char ui_executable[256], service_socket[108], supervisor_socket[108], checkpoint_path[256]; uint32_t ui_uid, service_uid; bool recovery_enabled; } locker_supervisor_config_t;",
        "typedef struct { char bind_address[64], certificate_path[256], private_key_path[256]; uint16_t port, worker_count; } locker_http_config_t;",
        "typedef struct { char socket_path[108]; uint32_t allowed_ui_uid, allowed_admin_uid, file_mode; } locker_ipc_config_t;",
        "typedef struct { bool board_verified; uint16_t pulse_ms; uint32_t prepare_deadline_ms; } mcu_control_config_t;",
        "typedef struct { bool geometry_verified; uint32_t jedec_id, capacity_bytes, sector_bytes; uint16_t page_bytes; uint8_t chip_select; } mcu_journal_config_t;",
        "typedef struct { uint32_t task_deadline_ms[4], host_timeout_ms; } mcu_health_config_t;",
        "/* Only module contexts remain opaque. Cross-module data above is concrete. */",
    ]
    layout_path = ROOT / "contracts/uart-layouts.json"
    if layout_path.exists():
        layout = json.loads(layout_path.read_text(encoding="utf-8"))
        lines += ["/* Decoded native DTOs: never send sizeof(struct) bytes to UART. */",
            "typedef struct { uint64_t boot_id, event_seq; } locker_wire_cursor_t;"]

        def native_field(field: dict) -> str:
            name, kind = field["name"], field["type"]
            primitive = {"u8": "uint8_t", "u16": "uint16_t", "u32": "uint32_t", "u64": "uint64_t", "uuid": "locker_uuid_t", "cursor": "locker_wire_cursor_t", "event96": "locker_wire_event96_t"}
            if kind in primitive:
                return primitive[kind] + " " + name + ";"
            if kind == "event96_array":
                return f"locker_wire_event96_t {name}[{field['max_count']}];"
            if re.fullmatch(r"bytes\d+", kind):
                return f"uint8_t {name}[{int(kind[5:])}];"
            raise ValueError("unsupported native field: " + kind)

        lines.append("typedef struct { " + " ".join(native_field(f) for f in layout["event_record"]) + " } locker_wire_event96_t;")
        union_fields = []
        for message in layout["messages"]:
            for direction, prefix in (("request", layout["request_prefix"]), ("response", layout["response_prefix"])):
                name = f"locker_wire_{message['name'].lower()}_{direction}_t"
                lines.append("typedef struct { " + " ".join(native_field(f) for f in prefix + message[direction]) + " } " + name + ";")
                if direction == "response": union_fields.append(name + " " + message["name"].lower() + ";")
        lines.append("typedef struct { uint32_t request_no; uint8_t message_type; uint64_t request_started_host_mono_ms, received_host_mono_ms; union { " + " ".join(union_fields) + " } body; } locker_device_reply_t;")
        lines.append("typedef struct { void *user; locker_status_t (*reply)(void *user, const locker_device_reply_t *reply); locker_status_t (*event)(void *user, const locker_device_event_t *event); } locker_device_sink_t;")
    lines.append("typedef struct { void *user; locker_status_t (*reply)(void *user, const locker_device_reply_t *reply); } mcu_control_sink_t;")
    lines.append("typedef struct { void *user; locker_status_t (*committed)(void *user, uint32_t token, locker_status_t result); locker_status_t (*event_committed)(void *user, const locker_device_event_t *event); locker_status_t (*control_result)(void *user, uint32_t token, locker_status_t result, const mcu_journal_state_t *state); } mcu_journal_sink_t;")
    declarations = "\n".join(i["prototype"] for i in source["internal_interfaces"])
    known = set(re.findall(r"}\s*(\w+_t);", "\n".join(lines)))
    needed = set(re.findall(r"\b(?:locker|mcu)_\w+_t\b", declarations)) - known
    for name in sorted(needed):
        lines.append(f"typedef struct {name[:-2]} {name};")
    lines += ["/* Host-only JSON DTO buffers must not be instantiated as MCU queues or allocated on small stacks. */",
        "/* Repo JSON fields follow internal-data.schema.json, not arbitrary SQL or public API payloads. */"]
    for interface in source["internal_interfaces"]:
        lines += ["/* " + interface["owner"] + ": " + interface["semantics"] + " */", interface["prototype"]]
    lines += ['#ifdef __cplusplus\n}\n#endif', "#endif", ""]
    return "\n".join(lines)


def crc_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def frame(message_type: int, request_no: int, payload: bytes) -> bytes:
    header_without_magic = struct.pack("<BBHI", 1, message_type, len(payload), request_no)
    return b"LK" + header_without_magic + payload + struct.pack("<H", crc_ccitt_false(header_without_magic + payload))


def make_examples(source: dict) -> dict[str, str]:
    uid = lambda n: f"00000000-0000-4000-8000-{n:012x}"
    event_key = {"device_id": "00112233445566778899aabb", "journal_epoch": uid(8), "boot_id": "1", "event_seq": "6"}
    physical = {**event_key, "physical_revision": "5", "door": "CLOSED", "item": "ABSENT", "supply_mv": 12000,
        "observed_age_ms": 40, "time_quality": "HOST_SYNCED", "continuity_ok": True, "dropped_events": "0", "durable_cursor": event_key}
    physical.pop("event_seq")
    operation = {"id": uid(3), "tool_id": uid(2), "kind": "BORROW", "actor_id": uid(1), "state": "WAIT_CONFIRM", "operation_revision": "5",
        "command_id": uid(4), "reason_code": None, "created_at": "2026-10-20T10:00:00+08:00", "updated_at": "2026-10-20T10:00:20+08:00",
        "allowed_actions": ["CONFIRM", "CANCEL"], "evidence": {"history_complete": True, "door_open_seen": True, "item_transition_seen": True,
            "physical": physical, "source_events": [event_key], "manual_resolution": False}}
    examples = [
        {"name": "create_borrow", "schema": "CreateOperationRequest", "valid": True, "value": {"tool_id": uid(2), "kind": "BORROW", "tool_revision": "1"}},
        {"name": "waiting_for_user_confirmation", "schema": "Operation", "valid": True, "value": operation},
        {"name": "ws_invalidation", "schema": "WsChange", "valid": True, "value": {"type": "resource.changed", "resource": "operation", "id": uid(3), "snapshot_revision": "12"}},
        {"name": "ws_resync", "schema": "WsResync", "valid": True, "value": {"type": "resync_required", "snapshot_revision": "12"}},
        {"name": "client_cannot_supply_actor", "schema": "CreateOperationRequest", "valid": False, "value": {"tool_id": uid(2), "kind": "BORROW", "tool_revision": "1", "actor_id": uid(9)}},
        {"name": "u64_must_not_be_number", "schema": "RevisionRequest", "valid": False, "value": {"operation_revision": 5}},
        {"name": "invalid_operation_state", "schema": "Operation", "valid": False, "value": {**operation, "state": "LOCK_OPEN_SUCCESS"}},
        {"name": "u64_overflow", "schema": "RevisionRequest", "valid": False, "value": {"operation_revision": "18446744073709551616"}},
    ]
    session = uuid.UUID(uid(10)).bytes
    hello = bytes(16) + session + uuid.UUID(uid(11)).bytes + b"\x01"
    vectors = [
        {"name": "hello_request", "type": 1, "request_no": 1, "payload_hex": hello.hex(), "frame_hex": frame(1, 1, hello).hex(), "valid": True},
        {"name": "snapshot_request", "type": 2, "request_no": 2, "payload_hex": session.hex(), "frame_hex": frame(2, 2, session).hex(), "valid": True},
    ]
    corrupt = bytearray(frame(2, 2, session)); corrupt[-1] ^= 1
    vectors.append({"name": "crc_error", "frame_hex": corrupt.hex(), "valid": False})
    too_long = b"LK" + struct.pack("<BBHI", 1, 2, 513, 3)
    vectors.append({"name": "oversized_header", "frame_hex": too_long.hex(), "valid": False})
    vectors_doc = {"algorithm": "CRC-16/CCITT-FALSE", "canonical_check": "123456789 -> 0x29b1", "vectors": vectors}
    scenarios = [
        {"id": "C01", "given": "无有效操作且闭门/物品在位/READY", "when": "两个用户并发申请BORROW", "then": "恰有一个REQUESTED，另一SLOT_BUSY；无EXECUTE"},
        {"id": "C02", "given": "申请已提交但HTTP响应丢失", "when": "同键同正文重试", "then": "同operation_id；无额外开锁意图"},
        {"id": "C03", "given": "同幂等键已经绑定请求", "when": "改正文重试", "then": "IDEMPOTENCY_CONFLICT；旧结果不改"},
        {"id": "C04", "given": "WAIT_CONFIRM且完整事件和当前物理状态符合", "when": "本人确认", "then": "一事务完成操作与借用；返回COMPLETED"},
        {"id": "C05", "given": "MCU意图提交成功但ACK丢失", "when": "RK超时", "then": "只QUERY_COMMAND，不重发EXECUTE"},
        {"id": "C06", "given": "动作日志已经提交、脉冲前MCU重启", "when": "建立新session", "then": "REVIEW_REQUIRED，旧command不驱动"},
        {"id": "C07", "given": "历史CARD_OBSERVED补传", "when": "用户当前有REQUESTED", "then": "只补记录；无presence、无EXECUTE"},
        {"id": "C08", "given": "日志序号连续但dropped_events>0/continuity失效", "when": "请求RESUME_READY", "then": "拒绝READY；不能据连续seq宣称完整"},
        {"id": "C09", "given": "意图写入等待中", "when": "物理版本改变或准备超过2000ms", "then": "不驱动；消耗水位，不重用command"},
        {"id": "C10", "given": "已取物未确认", "when": "取消或超时", "then": "REVIEW_REQUIRED且保持占位，不变AVAILABLE"},
        {"id": "C11", "given": "快照已取且补齐边界", "when": "ARM前物理版本改变", "then": "CAS拒绝，重新核对"},
        {"id": "C12", "given": "事件到达但RK事务失败", "when": "准备确认MCU游标", "then": "不ACK；可重复补取且不重复推进账本"},
        {"id": "C13", "given": "普通用户或网络客户端", "when": "尝试local presence、模拟输入、直接unlock", "then": "公开路由不存在/权限拒绝；无动作"},
        {"id": "C14", "given": "锁脉冲正在进行", "when": "存储任务阻塞/调度延迟", "then": "硬件截止仍关断；真实证据阶段2验证"},
        {"id": "C15", "given": "服务重启前有未完成操作", "when": "恢复SQLite与MCU日志", "then": "核对，不发送旧outbox；有不确定则待核验"},
        {"id": "C16", "given": "WS提示丢失或队列溢出", "when": "客户端重连/定时刷新", "then": "GET snapshot恢复显示；不把WS当可靠账本"},
    ]
    guard_document = {"collection": "slot_guards", "key": "1", "value": {"slot_id": 1, "operation_id": uid(3)}}
    internal_examples = [
        {"name": "slot_claim_transaction", "schema": "RepositoryChange", "valid": True, "value": {
            "schema_version": 1, "expected_snapshot_revision": "0", "new_snapshot_revision": "1",
            "checks": [{"collection": "slot_guards", "key": "1", "condition": "ABSENT"}],
            "mutations": [{"op": "PUT", "document": guard_document}]}},
        {"name": "exact_user_lookup", "schema": "RepositoryQuery", "valid": True, "value": {
            "schema_version": 1, "collection": "users", "key": uid(1), "after_key": None, "limit": 1}},
        {"name": "recovery_page", "schema": "RepositoryRecoveryPage", "valid": True, "value": {
            "schema_version": 1, "snapshot_revision": "1", "collection": "slot_guards", "documents": [guard_document], "next_after_key": None}},
        {"name": "wrong_collection_payload", "schema": "RepoDocument", "valid": False, "value": {**guard_document, "collection": "users"}},
        {"name": "exact_and_page_conflict", "schema": "RepositoryQuery", "valid": False, "value": {
            "schema_version": 1, "collection": "users", "key": uid(1), "after_key": uid(2), "limit": 1}},
    ]
    rpc_base = {"v": 1, "request_id": uid(20), "method": "getSnapshot", "session_token": "sample-token-" + "x" * 40,
        "path_ids": {}, "query": {}, "idempotency_key": None, "params": None}
    progress = {**rpc_base, "method": "progress.report", "session_token": None,
        "params": {"target": "UI", "instance_id": uid(11), "progress": "5", "observed_age_ms": 20, "sampled_mono_ms": "980"}}
    rpc_examples = [
        {"name": "local_snapshot", "schema": "InternalRpcRequest", "valid": True, "value": rpc_base},
        {"name": "forged_local_source", "schema": "InternalRpcRequest", "valid": False, "value": {**rpc_base, "source": "LOCAL_UI"}},
        {"name": "ws_not_rpc", "schema": "InternalRpcRequest", "valid": False, "value": {**rpc_base, "method": "subscribeChanges"}},
        {"name": "missing_path_id", "schema": "InternalRpcRequest", "valid": False, "value": {**rpc_base, "method": "getOperation"}},
        {"name": "local_progress", "schema": "InternalRpcRequest", "valid": True, "value": progress},
        {"name": "supervisor_rejects_session", "schema": "InternalRpcRequest", "valid": False, "value": {**progress, "session_token": rpc_base["session_token"]}},
        {"name": "token_is_not_an_object", "schema": "InternalRpcRequest", "valid": False, "value": {**rpc_base, "session_token": {}}},
        {"name": "version_bool_is_not_number", "schema": "InternalRpcRequest", "valid": False, "value": {**rpc_base, "v": True}},
        {"name": "logout_null_body", "schema": "InternalRpcResponse", "valid": True, "value": {"v": 1, "request_id": uid(20), "status": 204, "body": None}},
        {"name": "progress_ack", "schema": "InternalRpcResponse", "valid": True, "value": {"v": 1, "request_id": uid(20), "status": 200,
            "body": {"kind": "PROGRESS_RECORDED", "target": "UI", "instance_id": uid(11), "progress": "5"}}},
    ]
    health_request = {**rpc_base, "method": "health.get", "session_token": None}
    health = {"supervisor_instance_id": uid(21), "sampled_mono_ms": "1000", "service": None,
        "ui": {"instance_id": uid(11), "progress": "5", "observed_age_ms": 20, "sampled_mono_ms": "980"},
        "state": "RUNNING", "restart_count": 0, "window_attempts": 0, "cooldown_remaining_ms": 0, "budget_ready": True}
    rpc_examples += [
        {"name": "supervisor_health_query", "schema": "InternalRpcRequest", "valid": True, "value": health_request},
        {"name": "health_query_no_session", "schema": "InternalRpcRequest", "valid": False, "value": {**health_request, "session_token": rpc_base["session_token"]}},
        {"name": "health_response", "schema": "SupervisorHealthResponse", "valid": True, "value": health},
        {"name": "health_unknown", "schema": "SupervisorHealthResponse", "valid": True, "value": {**health, "ui": None, "state": "UNKNOWN", "restart_count": None, "budget_ready": False}},
        {"name": "health_invalid_restart_count", "schema": "SupervisorHealthResponse", "valid": False, "value": {**health, "restart_count": -1}},
        {"name": "health_ack_envelope", "schema": "InternalRpcResponse", "valid": True, "value": {"v": 1, "request_id": uid(20), "status": 200, "body": health}},
        {"name": "progress_missing_source_sample", "schema": "InternalRpcRequest", "valid": False, "value": {**progress, "params": {k: v for k, v in progress["params"].items() if k != "sampled_mono_ms"}}},
        {"name": "vision_status_read", "schema": "InternalRpcRequest", "valid": True, "value": {**rpc_base, "method": "getVisionStatus"}},
        {"name": "vision_evidence_read", "schema": "InternalRpcRequest", "valid": True, "value": {**rpc_base, "method": "getOperationVisionEvidence", "path_ids": {"operation_id": uid(3)}, "query": {"limit": 20}}},
        {"name": "vision_read_requires_operation", "schema": "InternalRpcRequest", "valid": False, "value": {**rpc_base, "method": "getOperationVisionEvidence"}},
    ]
    return {
        "contracts/examples/api-examples.json": json_text(examples),
        "contracts/examples/uart-golden.json": json_text(vectors_doc),
        "contracts/examples/acceptance-scenarios.json": json_text(scenarios),
        "contracts/examples/internal-data-examples.json": json_text(internal_examples),
        "contracts/examples/local-rpc-examples.json": json_text(rpc_examples),
    }


def make_public_api_doc(source: dict) -> str:
    lines = ["# 公共 API 速查", "", "生成的阶段 0 评审稿；真源是 `contracts/contract-source.json`，不要手改。",
        "", "基地址：`https://<终端地址>:8443/api/v1`。App 与本机 UI 调用相同领域核心；本机存在证明不注册到公共 HTTP。",
        "", "## 路由清单", "", "| 方法与路径 | 权限 | 请求模型 | 成功结果 | 幂等 | 首实现卡 |", "|---|---|---|---|---|---|"]
    for route in source["public_routes"]:
        output = route.get("output_schema") or ("WS 提示" if route.get("websocket") else "无正文")
        status = route.get("success_status", 200)
        lines.append(f"| `{route['method'].upper()} {route['path']}` | {'/'.join(route['roles']) or '无需登录'} | {route.get('input_schema') or '无正文'} | {status} {output} | {'必须同键重试' if route.get('idempotency') else '查询/接口自身语义'} | {route.get('implementation_card', 'S1-05')} |")
    lines += ["", "OWNER 是操作原用户，不是客户端自报身份；ADMIN 只能在明确标注的接口核验，不能伪装本人确认。",
        "", "## 通用输入与响应", "", "- 已登录请求使用 Authorization: Bearer；会话不放 URL。路径 UUID 为小写标准文本。",
        "- 有副作用的业务请求需 Idempotency-Key UUID；同用户/方法/路径/键绑定规范化正文和查询参数，与业务事务一起保存 30 天。",
        "- operation_revision、tool_revision、user_revision 分别比较对应资源版本；snapshot_revision 是全局快照版本，不能混用。",
        "- JSON 的 u64 是十进制字符串并检查完整 uint64 上界，禁止经浮点。未知供电/物理状态显式为 null/UNKNOWN。",
        "- 成功：`{data, snapshot_revision, request_id}`；错误：`{error:{code,message,retryable,operation_id},request_id}`。",
        "- Core 排队不直接返回成功；最多等 1000ms，结果未到返回 RESULT_PENDING。超时不会撤销命令，客户端复用原幂等键。",
        "- HTTP 201 只表示申请提交；ACCEPTED ACK 只表示 MCU 动作意图提交；门打开和借还成功分别依事实判定。",
        "- POST 创建资源返回 Location；列表分页默认 20、最大 100，游标不透明，不允许客户端构造 SQL。",
        "", "## 关键模型字段", "",
        "| 模型 | 字段/限制 |", "|---|---|",
        "| CreateOperationRequest | tool_id、kind、tool_revision；没有 actor_id、通电时长、成功状态 |",
        "| Operation | id、tool_id、kind、actor_id、state、operation_revision、command_id、reason_code、allowed_actions、evidence、时间 |",
        "| Evidence | 完整历史、开门见证、物品变化见证、当前物理快照、事件键、人工核验标记 |",
        "| ResolveRequest | operation_revision、physical_revision、resolution、至少5字节原因；原用户与类型不可换 |",
        "| PhysicalSnapshot | device_id、journal_epoch、boot、physical_revision、门/物品/供电、观察年龄、连续性与dropped_events、持久游标 |",
        "| CardObservation | 短期 observation_id、脱敏UID、年龄、是否已消费；绑定只引用观测ID |",
        "", "完整字段、必填项和长度以 [OpenAPI](../../contracts/openapi.json) 为准。",
        "", "## 错误码", "", "| code | HTTP | retryable |", "|---|---:|---|"]
    lines += [f"| {e['code']} | {e['http']} | {str(e['retryable']).lower()} |" for e in source["error_codes"]]
    lines += ["", "retryable 只允许规范中的查询/复用原幂等键，绝不表示可再次发送开锁。",
        "", "## WS 与私有入口", "",
        "WS 只推 resource.changed 与 resync_required，客户端重查权威快照；无动作命令、无可靠历史回放。",
        "建立 WS 后先处理 resync，再 GET snapshot；重连、队列溢出和服务实例变化一律重查。前台10秒刷新兜底。",
        "", "私有 IPC 方法不属于上述网络路由：", ""]
    lines += [f"- `{m['method']}`：{m['authorization']}。" for m in source["local_methods"]]
    lines += ["", "API 样例与16项故障走查在 [examples](../../contracts/examples/acceptance-scenarios.json)；这里只定义合同，不声明已运行。", ""]
    return "\n".join(lines)


def build_artifacts(source: dict | None = None) -> dict[str, str]:
    source = source or load_source()
    return {
        "contracts/openapi.json": json_text(make_openapi(source)),
        "contracts/include/locker_contracts.h": make_header(source),
        "docs/architecture/public-api.md": make_public_api_doc(source),
        "contracts/internal-data.schema.json": json_text(make_internal_schema(source)),
        "contracts/vision.schema.json": json_text(make_vision_schema(source)),
        "contracts/examples/vision-examples.json": json_text(json.loads((ROOT / source["vision_source"]).read_text(encoding="utf-8"))["examples"]),
        **make_examples(source),
    }


def make_vision_schema(source: dict) -> dict:
    vision = json.loads((ROOT / source["vision_source"]).read_text(encoding="utf-8"))
    definitions = make_openapi(source)["components"]["schemas"]
    selected = set()

    def collect(node: object) -> None:
        if isinstance(node, dict):
            for key, value in node.items():
                if key == "$ref":
                    name = value.rsplit("/", 1)[-1]
                    if name not in selected:
                        selected.add(name)
                        collect(definitions[name])
                else:
                    collect(value)
        elif isinstance(node, list):
            for value in node:
                collect(value)

    for name, schema in vision["schemas"].items():
        selected.add(name)
        collect(schema)

    def convert(node: object) -> object:
        if isinstance(node, dict):
            return {k: v.replace("#/components/schemas/", "#/$defs/") if k == "$ref" else convert(v) for k, v in node.items()}
        if isinstance(node, list):
            return [convert(v) for v in node]
        return node

    return {"$schema": "https://json-schema.org/draft/2020-12/schema", "$id": "urn:locker:vision:" + source["contract_version"],
        "$defs": {name: convert(definitions[name]) for name in sorted(selected)}, "x-transport": vision["transport"],
        "description": "视觉证据/私有worker/策略的设计契约，禁止图像上传、任意文件路径或视觉直接开锁。"}


def make_internal_schema(source: dict) -> dict:
    internal_source = json.loads((ROOT / "contracts/internal-data-source.json").read_text(encoding="utf-8"))
    definitions = {**make_openapi(source)["components"]["schemas"], **internal_source["schemas"]}
    definitions.update({name: {"type": "string", "enum": values} for name, values in source["enums"].items()})

    def convert(node: object) -> object:
        if isinstance(node, dict):
            return {k: v.replace("#/components/schemas/", "#/$defs/") if k == "$ref" else convert(v) for k, v in node.items()}
        if isinstance(node, list): return [convert(v) for v in node]
        return node

    return {"$schema": "https://json-schema.org/draft/2020-12/schema", "$id": "urn:locker:contracts:internal-data:" + source["contract_version"],
        "$defs": convert(definitions), "x-collection-schemas": internal_source["repository_collections"],
        "x-rules": internal_source["rules"], "x-command-bindings": internal_source["command_bindings"],
        "description": "内部仓储/恢复DTO设计契约，不是公共HTTP API或SQL执行入口。"}


def main() -> None:
    artifacts = build_artifacts()
    for relative, content in artifacts.items():
        target = ROOT / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8", newline="\n")
    print(f"Generated {len(artifacts)} design artifacts; no business implementation.")


if __name__ == "__main__":
    main()
