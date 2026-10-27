"""Static contract consistency checks, not firmware or business acceptance tests."""
from __future__ import annotations

import json
import re
import struct
import uuid
import sys
import importlib.util
from decimal import Decimal
from copy import deepcopy
from pathlib import Path

sys.dont_write_bytecode = True
from build_contracts import ROOT, build_artifacts, crc_ccitt_false, load_source, make_openapi, make_internal_schema


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def validate_value(schema: dict, value: object, schemas: dict, name: str = "") -> bool:
    if isinstance(schema, bool):
        return schema
    if "$ref" in schema:
        key = schema["$ref"].split("/")[-1]
        if key == "U64":
            return isinstance(value, str) and bool(re.fullmatch(r"0|[1-9][0-9]{0,19}", value)) and int(value) <= 2**64 - 1
        return validate_value(schemas[key], value, schemas, key)
    if "allOf" in schema and not all(validate_value(s, value, schemas, name) for s in schema["allOf"]):
        return False
    if "anyOf" in schema and not any(validate_value(s, value, schemas, name) for s in schema["anyOf"]):
        return False
    if "oneOf" in schema and sum(validate_value(s, value, schemas, name) for s in schema["oneOf"]) != 1:
        return False
    if "not" in schema and validate_value(schema["not"], value, schemas, name):
        return False
    if "if" in schema:
        branch = "then" if validate_value(schema["if"], value, schemas, name) else "else"
        if branch in schema and not validate_value(schema[branch], value, schemas, name):
            return False
    def equal_json(left: object, right: object) -> bool:
        if isinstance(left, bool) != isinstance(right, bool): return False
        return left == right
    if "const" in schema and not equal_json(value, schema["const"]):
        return False
    if "enum" in schema and not any(equal_json(value, option) for option in schema["enum"]):
        return False
    kind = schema.get("type")
    type_matches = {"null": value is None, "object": isinstance(value, dict), "array": isinstance(value, list),
        "string": isinstance(value, str), "integer": isinstance(value, int) and not isinstance(value, bool),
        "boolean": isinstance(value, bool), "number": isinstance(value, (int, float)) and not isinstance(value, bool)}
    if kind is not None and not type_matches.get(kind, False): return False
    if kind == "null":
        return value is None
    if kind == "object" and not isinstance(value, dict): return False
    if isinstance(value, dict):
        if not all(k in value for k in schema.get("required", [])): return False
        props = schema.get("properties", {})
        if schema.get("additionalProperties") is False and set(value) - set(props): return False
        if len(value) < schema.get("minProperties", 0): return False
        return all(validate_value(props[k], v, schemas, k) for k, v in value.items() if k in props)
    if kind == "array" and not isinstance(value, list): return False
    if isinstance(value, list):
        return schema.get("minItems", 0) <= len(value) <= schema.get("maxItems", 10**9) and ("items" not in schema or all(validate_value(schema["items"], x, schemas) for x in value))
    if kind == "string":
        if not isinstance(value, str): return False
        if not schema.get("minLength", 0) <= len(value) <= schema.get("maxLength", 10**9): return False
        if "pattern" in schema and not re.fullmatch(schema["pattern"], value): return False
        if schema.get("format") == "uuid":
            try: uuid.UUID(value)
            except (ValueError, TypeError): return False
        return True
    if kind == "integer":
        return isinstance(value, int) and not isinstance(value, bool) and schema.get("minimum", -10**30) <= value <= schema.get("maximum", 10**30)
    if kind == "boolean": return isinstance(value, bool)
    return True


def size_of(field: dict) -> int:
    kind = field["type"]
    fixed = {"u8": 1, "u16": 2, "u32": 4, "u64": 8, "uuid": 16, "cursor": 16, "event96": 96}
    if kind in fixed: return fixed[kind]
    if kind == "event96_array": return 96 * field["max_count"]
    if re.fullmatch(r"bytes\d+", kind): return int(kind[5:])
    raise AssertionError(f"unknown wire type: {kind}")


def check_refs(node: object, document: dict) -> int:
    count = 0
    if isinstance(node, dict):
        for k, v in node.items():
            if k == "$ref":
                require(v.startswith("#/"), "external refs require a separate resolver")
                target = document
                for part in v[2:].split("/"): target = target[part.replace("~1", "/").replace("~0", "~")]
                count += 1
            else: count += check_refs(v, document)
    elif isinstance(node, list):
        for item in node: count += check_refs(item, document)
    return count


def main() -> None:
    source = load_source()
    artifacts = build_artifacts(source)
    for relative, expected in artifacts.items():
        actual = (ROOT / relative).read_text(encoding="utf-8")
        require(actual == expected, f"generated artifact drift: {relative}; run build_contracts.py")
    api = make_openapi(source)
    refs = check_refs(api, api)
    internal_schema = make_internal_schema(source)
    internal_refs = check_refs(internal_schema, internal_schema)
    internal_source = json.loads((ROOT / "contracts/internal-data-source.json").read_text(encoding="utf-8"))
    routes = source["public_routes"]
    ids = [r["operation_id"] for r in routes]
    require(len(ids) == len(set(ids)), "duplicate operationId")
    require(set(ids) == {b["operation_id"] for b in internal_source["command_bindings"]}, "public operationId/Core kind binding incomplete")
    require(len({(r["method"], r["path"]) for r in routes}) == len(routes), "duplicate public route")
    for route in routes:
        path = route["path"].lower()
        require(not any(word in path for word in ("unlock", "presence", "maintenance", "profile", "simulate", "gpio", "shell")), f"unsafe public route: {path}")
        if route["method"] in ("post", "put", "patch", "delete") and route["path"] not in ("/sessions", "/sessions/current"):
            require(route.get("idempotency") is True, f"missing idempotency: {path}")
    names = [i["name"] for i in source["internal_interfaces"]]
    require(len(names) == len(set(names)), "duplicate internal port")
    examples = json.loads((ROOT / "contracts/examples/api-examples.json").read_text(encoding="utf-8"))
    schemas = api["components"]["schemas"]
    for example in examples:
        valid = validate_value({"$ref": "#/components/schemas/" + example["schema"]}, example["value"], schemas)
        require(valid == example["valid"], f"example validity: {example['name']}")
    internal_examples = json.loads((ROOT / "contracts/examples/internal-data-examples.json").read_text(encoding="utf-8"))
    for example in internal_examples:
        valid = validate_value({"$ref": "#/$defs/" + example["schema"]}, example["value"], internal_schema["$defs"])
        require(valid == example["valid"], f"internal DTO example validity: {example['name']}")
    rpc_examples = json.loads((ROOT / "contracts/examples/local-rpc-examples.json").read_text(encoding="utf-8"))
    for example in rpc_examples:
        valid = validate_value({"$ref": "#/$defs/" + example["schema"]}, example["value"], internal_schema["$defs"])
        require(valid == example["valid"], f"local RPC example validity: {example['name']}")
    require(len(internal_source["internal_rpc"]["method_bindings"]) == len(routes) - 1 + len(source["local_methods"]), "local RPC bindings incomplete")
    rpc_methods = [b["method"] for b in internal_source["internal_rpc"]["method_bindings"]]
    require(len(rpc_methods) == len(set(rpc_methods)), "duplicate local RPC method")
    require(set(rpc_methods) == (set(ids) - {"subscribeChanges"}) | {m["method"] for m in source["local_methods"]}, "local RPC/root method set drift")
    header = (ROOT / "contracts/include/locker_contracts.h").read_text(encoding="utf-8")
    for port in source["internal_interfaces"]:
        require(port["prototype"] in header, f"C declaration missing: {port['name']}")
    for data_type in ("locker_change_t", "locker_recovery_t", "locker_repo_query_t", "locker_device_config_t", "locker_repo_config_t"):
        require(not re.search(r"typedef struct \w+ " + data_type + r";", header), f"cross-module DTO cannot be opaque: {data_type}")
    c_syntax = "NOT_RUN (optional pycparser unavailable)"
    if importlib.util.find_spec("pycparser") is not None:
        from pycparser import c_parser
        c_text = re.sub(r"/\*.*?\*/", "", header, flags=re.S)
        c_lines = [line for line in c_text.splitlines() if not line.lstrip().startswith("#") and line.strip() not in ('extern "C" {', '}')]
        preamble = "typedef unsigned char uint8_t; typedef unsigned short uint16_t; typedef unsigned int uint32_t; typedef unsigned long long uint64_t; typedef unsigned long size_t; typedef _Bool bool;\n"
        c_parser.CParser().parse(preamble + "\n".join(c_lines), filename="locker_contracts.h")
        c_syntax = "PASS (parse only, not compile/link)"
    require(crc_ccitt_false(b"123456789") == 0x29B1, "CRC oracle mismatch")
    vectors = json.loads((ROOT / "contracts/examples/uart-golden.json").read_text(encoding="utf-8"))["vectors"]
    for v in vectors:
        raw = bytes.fromhex(v["frame_hex"])
        valid = len(raw) >= 12 and raw[:2] == b"LK"
        if valid:
            version, _, length, _ = struct.unpack("<BBHI", raw[2:10])
            valid = version == 1 and length <= source["constants"]["UART_MAX_PAYLOAD"] and len(raw) == 12 + length
        if valid: valid = int.from_bytes(raw[-2:], "little") == crc_ccitt_false(raw[2:-2])
        require(valid == v["valid"], f"UART vector: {v['name']}")
    layout = json.loads((ROOT / "contracts/uart-layouts.json").read_text(encoding="utf-8"))
    require(sum(f["bytes"] for f in layout["header"]) == 10, "wire header length")
    require(sum(size_of(f) for f in layout["event_record"]) == 96, "wire event length")
    message_ids = {m["name"]: m["type"] for m in source["uart_messages"]}
    require(message_ids == {m["name"]: m["type"] for m in layout["messages"]}, "UART message IDs/names drift")
    maximum = 0
    wire_types = source["wire_enums"]
    for message in layout["messages"]:
        for direction, prefix in (("request", layout["request_prefix"]), ("response", layout["response_prefix"])):
            fields = prefix + message[direction]
            require(len({f["name"] for f in fields}) == len(fields), f"duplicate field in {message['name']} {direction}")
            length = sum(size_of(f) for f in fields)
            maximum = max(maximum, length)
            require(length <= 512, f"payload overflow: {message['name']} {direction}")
            for field in fields:
                if "enum_ref" in field: require(field["enum_ref"] in wire_types, f"unknown enum {field['enum_ref']}")
    for field in layout["event_record"]:
        if "enum_ref" in field: require(field["enum_ref"] in wire_types, f"unknown event enum {field['enum_ref']}")
    hardware_schema = json.loads((ROOT / "contracts/hardware-profile.schema.json").read_text(encoding="utf-8"))
    hardware = json.loads((ROOT / "config/hardware/skystar-f407vgt6-pro.unverified.json").read_text(encoding="utf-8"))
    require(validate_value(hardware_schema, hardware, {}), "unverified hardware profile schema")
    bad_driver = deepcopy(hardware); bad_driver["driver_stack"] = "SPL"
    require(not validate_value(hardware_schema, bad_driver, {}), "non-HAL profile accepted")
    fake_verified = deepcopy(hardware); fake_verified["status"] = "VERIFIED"
    require(not validate_value(hardware_schema, fake_verified, {}), "unmeasured profile accepted as verified")
    require(hardware["status"] == "UNVERIFIED", "this deliverable must not claim hardware verified")
    require("HAL_HandleTypeDef" not in header and "stm32f4xx_hal.h" not in header, "HAL handles leaked across common contracts")
    bom = json.loads((ROOT / "config/materials/selected-bom.json").read_text(encoding="utf-8"))
    bom_total = sum((Decimal(i["amount"]) for i in bom["items"] if i["included"]), Decimal(0))
    require(bom_total == Decimal(bom["totals"]["with_both_spares"]), "selected BOM sum drift")
    require(bom_total <= Decimal(bom["budget_cap"]), "selected nominal budget exceeds cap; report before changing scope")
    from build_materials_doc import render as render_materials
    require((ROOT / "docs/材料清单与搜索比价-2026-10-20.md").read_text(encoding="utf-8") == render_materials(), "selected BOM/document drift")
    from build_shopping_list import render as render_shopping
    require((ROOT / "docs/hardware/手机选购清单.html").read_text(encoding="utf-8") == render_shopping(), "shopping list drift")
    from build_system_wiring import build as build_wiring
    require((ROOT / "docs/hardware/系统接线图.svg").read_text(encoding="utf-8") == build_wiring(), "system wiring drift")
    for f in ROOT.rglob("*.md"):
        if "research" in f.parts: continue
        text = f.read_text(encoding="utf-8")
        require("\ufffd" not in text, f"encoding replacement: {f}")
        for link in re.findall(r"\]\(([^)]+)\)", text):
            if link.startswith(("http:", "https:", "#", "mailto:")): continue
            target = link.split("#", 1)[0]
            require((f.parent / target).exists(), f"broken local link: {f}: {link}")
    print(json.dumps({"status": "PASS", "contract_version": source["contract_version"], "public_routes": len(routes), "internal_ports": len(names), "private_rpc_methods": len(rpc_methods),
        "resolved_schema_refs": refs, "resolved_internal_refs": internal_refs, "api_examples": len(examples), "internal_examples": len(internal_examples), "local_rpc_examples": len(rpc_examples), "uart_vectors": len(vectors),
        "uart_requests": len(layout["messages"]), "max_wire_payload": maximum,
        "c_declaration_syntax": c_syntax,
        "hardware_profile": "UNVERIFIED", "hardware_profile_cases": 3,
        "nominal_bom_cny": str(bom_total), "prices_settled": False,
        "business_or_hardware_tested": False}, ensure_ascii=False))


if __name__ == "__main__":
    main()
