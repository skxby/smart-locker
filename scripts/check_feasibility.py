"""Regression checks for reviewed design gaps, not operational authorization."""
import json
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def require(value, message):
    if not value:
        raise AssertionError(message)


def exposure_fixture(driver_ms, bound_ms, issued_ms, now_ms):
    require(bound_ms is not None and bound_ms >= 0, "unknown exposure bound")
    earliest = driver_ms - bound_ms
    require(issued_ms < earliest <= now_ms and now_ms - earliest <= 2000, "old/future exposure")
    return earliest


def main():
    source = json.loads((ROOT / "contracts/contract-source.json").read_text(encoding="utf-8"))
    ports = {p["name"]: p for p in source["internal_interfaces"]}
    header = (ROOT / "contracts/include/locker_contracts.h").read_text(encoding="utf-8")
    for name in ["mcu_control_submit_unlock", "mcu_control_make_snapshot", "mcu_control_release_context"]:
        require("const mcu_request_context_t *request" in ports[name]["prototype"], "async request lost context: " + name)
    for token in ["mcu_control_bind_sink", "mcu_journal_control", "(*control_result)", "(*event_committed)", "barrier_after_token"]:
        require(token in header, "missing completion/storage boundary: " + token)
    constants = source["constants"]
    require(constants["VISION_STATUS_PERIOD_MS"] < constants["VISION_STATUS_TTL_MS"], "status polling cannot meet TTL")
    require(constants["VISION_CONTROL_TIMEOUT_MS"] <= constants["VISION_STATUS_TTL_MS"], "status timeout exceeds validity")
    require(exposure_fixture(2000, 100, 1800, 2200) == 1900, "bounded fresh exposure")
    rejected = 0
    for args in [(2000, 600, 1500, 2200), (2000, None, 1500, 2200), (4000, 0, 1500, 2200)]:
        try:
            exposure_fixture(*args)
        except AssertionError:
            rejected += 1
        else:
            raise AssertionError("unsafe exposure fixture accepted")
    # Two-wire NO inputs have the same reading when open and disconnected.
    require(len(set({"unpressed": 1, "broken_wire": 1}.values())) == 1, "fixture must model observational equivalence")
    print(json.dumps({"status": "PASS", "contract_version": source["contract_version"], "async_request_contexts": 3,
        "control_and_journal_completion_channels": "DECLARED", "exposure_negative_cases": rejected,
        "two_wire_break_detection": "NOT_CLAIMED", "runtime_or_hardware_tested": False}))


if __name__ == "__main__":
    main()
