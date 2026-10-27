"""Validate task dependencies, delivery coverage and truthful evidence status."""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from build_stage_docs import ROOT, OUTPUT, render


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def read(relative: str) -> dict:
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def check(plan: dict, mapping: dict) -> dict:
    source = read("contracts/contract-source.json")
    internal = read("contracts/internal-data-source.json")
    require(plan["contract_version"] == mapping["contract_version"] == source["contract_version"], "stage/contract version drift")
    require(plan["execution_policy"] == "ONE_CARD_AT_A_TIME", "unexpected execution policy")
    tasks = {t["id"]: t for t in plan["tasks"]}
    require(len(tasks) == len(plan["tasks"]), "duplicate card id")
    stages = {s["id"]: s for s in plan["stages"]}
    require(set(stages) == set(range(1, 6)), "stage 1-5 coverage incomplete")
    active = [t["id"] for t in tasks.values() if t["status"] == "IN_PROGRESS"]
    require(len(active) <= 1, "multiple cards active under single-card policy")
    require(active == ([] if plan["active_task"] is None else [plan["active_task"]]), "active_task/status mismatch")
    require(plan["recommended_next"] in tasks, "unknown next card")
    visiting, visited = set(), set()

    def visit(card: str) -> None:
        require(card in tasks, "unknown dependency: " + card)
        require(card not in visiting, "dependency cycle: " + card)
        if card in visited:
            return
        visiting.add(card)
        task = tasks[card]
        require(task["status"] in ("NOT_STARTED", "IN_PROGRESS", "BLOCKED", "DONE"), "invalid card status")
        require(task["primary_paths"], "missing module scope: " + card)
        if task["stage"] > 1 and task["status"] in ("IN_PROGRESS", "DONE"):
            require(stages[task["stage"] - 1]["gate_result"] == "PASS", "previous stage gate not PASS: " + card)
        for dep in task["depends_on"]:
            require(dep != card, "self dependency: " + card)
            visit(dep)
            require(tasks[dep]["stage"] <= task["stage"], "task depends on later stage: " + card)
            if task["status"] in ("IN_PROGRESS", "DONE"):
                require(tasks[dep]["status"] == "DONE", "card ran before dependency passed: " + card)
        if task["status"] == "DONE":
            require(task["report"] == f"docs/evidence/stage-{task['stage']:02d}/{card}/summary.md", "DONE report outside assigned evidence path: " + card)
            require(bool(task["report"]) and (ROOT / task["report"]).is_file(), "DONE missing evidence: " + card)
        visiting.remove(card)
        visited.add(card)

    for card in tasks:
        visit(card)
    for number, stage in stages.items():
        listed = stage["tasks"]
        require(listed and len(listed) == len(set(listed)), "invalid stage task list")
        require(listed == [f"S{number}-{n:02d}" for n in range(1, len(listed) + 1)], "noncontiguous stage cards")
        require(set(listed) == {t["id"] for t in tasks.values() if t["stage"] == number}, "stage/card ownership mismatch")
        require(stage["gate_task"] == listed[-1], "gate must be last card")
        require(stage["gate_result"] in ("NOT_RUN", "PASS", "FAIL"), "invalid gate result")
        specs = stage.get("task_specs", [stage["spec"]])
        headings = []
        for relative in specs:
            spec = (ROOT / relative).read_text(encoding="utf-8")
            found = re.findall(r"^#{2,3} (S\d-\d\d)(?=[：: ])", spec, re.M)
            require(set(found) == {c for c in listed if tasks[c]["spec"] == relative}, "task file ownership drift: " + relative)
            headings.extend(found)
        require(set(headings) == set(listed) and len(headings) == len(listed), "task headings/index drift: " + stage["spec"])
        for card in listed:
            require(tasks[card]["spec"] in specs, "card specification drift")
        if number > 1:
            require(stages[number - 1]["gate_task"] in tasks[listed[0]]["depends_on"], "missing previous stage gate")
        all_done = all(tasks[card]["status"] == "DONE" for card in listed)
        expected = "DONE" if all_done else "NOT_STARTED" if all(tasks[card]["status"] == "NOT_STARTED" for card in listed) else "IN_PROGRESS"
        require(stage["implementation_status"] == expected, "stage implementation status drift")
        if tasks[stage["gate_task"]]["status"] == "DONE":
            require(stage["gate_result"] == "PASS", "gate card DONE but gate not PASS")
        if stage["gate_result"] == "PASS":
            require(stage["evidence"] == f"docs/evidence/stage-{number:02d}/summary.md", "stage gate evidence path drift")
            require(all_done and bool(stage["evidence"]) and (ROOT / stage["evidence"]).is_file(), "gate PASS missing complete evidence")

    expected_sets = {
        "public_routes": {r["operation_id"] for r in source["public_routes"]},
        "internal_ports": {p["name"] for p in source["internal_interfaces"]},
        "uart_requests": {m["name"] for m in source["uart_messages"]},
        "local_methods": {(m["method"], m["socket"]) for m in internal["internal_rpc"]["method_bindings"]},
    }
    if source.get("vision_source"):
        expected_sets["vision_worker_messages"] = {"HELLO", "STATUS", "CAPTURE"}
    fields = ("implementation", "core", "client", "verification", "refinement", "host", "mcu_transport", "mcu_semantics", "real_link", "producer", "consumer")
    for group, expected in expected_sets.items():
        items = mapping[group]
        keys = [(x["method"], x["socket"]) if group == "local_methods" else x["operation_id"] if group == "public_routes" else x["name"] for x in items]
        require(len(keys) == len(set(keys)) and set(keys) == expected, "delivery coverage drift: " + group)
        for item in items:
            require(item["flow"] in mapping["flows"], "unknown call chain")
            for field in fields:
                value = item.get(field, [])
                for card in value if isinstance(value, list) else [value]:
                    require(card in tasks, "unknown delivery card: " + card)
    require((ROOT / OUTPUT).read_text(encoding="utf-8") == render(), "traceability drift; run build_stage_docs.py")
    return {"status": "PASS", "contract_version": source["contract_version"], "stages": len(stages), "task_cards": len(tasks),
        "public_routes_mapped": len(mapping["public_routes"]), "internal_ports_mapped": len(mapping["internal_ports"]),
        "uart_requests_mapped": len(mapping["uart_requests"]), "private_rpc_mapped": len(mapping["local_methods"]),
        "vision_worker_messages_mapped": len(mapping.get("vision_worker_messages", [])),
        "active_task": plan["active_task"], "implementation_cards_done": sum(t["status"] == "DONE" for t in tasks.values()),
        "runtime_or_hardware_tested": False}


if __name__ == "__main__":
    print(json.dumps(check(read("config/development/stage-plan.json"), read("config/development/interface-map.json")), ensure_ascii=False))
