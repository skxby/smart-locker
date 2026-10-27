"""Static vision contract/fixture checks; no inference, camera or product logic."""
from __future__ import annotations

import hashlib
import json
import sys
from copy import deepcopy
from decimal import Decimal
from pathlib import Path

sys.dont_write_bytecode = True
from build_contracts import ROOT, load_source, make_vision_schema
from check_contracts import check_refs, require, validate_value


def audit_capture_fixture(request: dict, result: dict, now: int) -> None:
    """Check recorded fixture relations only, never an operational authorization."""
    for key in ("capture_id", "operation_id", "phase", "slot_id", "camera_id", "service_instance_id", "guard",
                "bundle_sha256", "pipeline_sha256", "camera_profile_sha256", "policy_sha256"):
        require(request[key] == result[key], "fixture correlation mismatch: " + key)
    require(request["expected_worker_instance_id"] == result["worker_instance_id"], "fixture old worker")
    require(request["expected_camera_generation"] == result["camera_generation"], "fixture old camera")
    require(result["status"] == "OK" and len(result["frames"]) == 3, "fixture has no usable result")
    previous_sequence, previous_time = -1, None
    for frame in result["frames"]:
        sequence, captured = int(frame["sequence"]), int(frame["captured_mono_ms"])
        require(sequence > previous_sequence, "fixture repeated frame sequence")
        require(int(request["issued_mono_ms"]) < captured <= int(request["deadline_mono_ms"]), "fixture cached/outside capture window")
        require(0 <= now - captured <= 2000, "fixture stale/future frame")
        require(previous_time is None or captured - previous_time >= 100, "fixture frames too close")
        require(frame["quality"] == "VALID", "fixture unusable image quality")
        require(abs(sum(frame["scores_ppm"]) - 1000000) <= 7, "fixture score sum")
        previous_sequence, previous_time = sequence, captured
    require(previous_time <= int(result["completed_mono_ms"]) <= int(request["deadline_mono_ms"]), "fixture completion outside deadline")
    require(int(result["completed_mono_ms"]) <= now, "fixture future completion")


def main() -> None:
    source = load_source()
    vision = json.loads((ROOT / source["vision_source"]).read_text(encoding="utf-8"))
    schema = make_vision_schema(source)
    require((ROOT / "contracts/vision.schema.json").read_text(encoding="utf-8") == json.dumps(schema, ensure_ascii=False, indent=2) + "\n", "vision generated schema drift")
    refs = check_refs(schema, schema)
    examples = json.loads((ROOT / "contracts/examples/vision-examples.json").read_text(encoding="utf-8"))
    for case in examples:
        actual = validate_value({"$ref": "#/$defs/" + case["schema"]}, case["value"], schema["$defs"])
        require(actual == case["valid"], "vision example validity: " + case["name"])
    request, result = examples[0]["value"], examples[1]["value"]
    audit_capture_fixture(request, result, 1300)
    mutations = []
    old = deepcopy(result); old["worker_instance_id"] = request["operation_id"]; mutations.append(("old_worker", old, 1300))
    old = deepcopy(result); old["guard"]["physical_revision"] = "6"; mutations.append(("different_physical_guard", old, 1300))
    old = deepcopy(result); old["frames"][1]["sequence"] = old["frames"][0]["sequence"]; mutations.append(("replayed_frame", old, 1300))
    old = deepcopy(result); old["frames"][0]["scores_ppm"][0] = 900000; mutations.append(("invalid_probability_sum", old, 1300))
    mutations.append(("stale_result", deepcopy(result), 8000))
    for name, candidate, now in mutations:
        try:
            audit_capture_fixture(request, candidate, now)
        except AssertionError:
            pass
        else:
            raise AssertionError("bad semantic fixture accepted: " + name)
    identical = deepcopy(result)
    for frame in identical["frames"]:
        frame["frame_sha256"] = result["frames"][0]["frame_sha256"]
    audit_capture_fixture(request, identical, 1300)
    operation = request["operation_id"]
    prefix = operation + "/"
    key = prefix + request["capture_id"]
    require(len(key) == 73, "operation/evidence composite key length")
    valid_page = [{"key": key, "operation_id": operation}]
    def audit_page_fixture(page: list[dict]) -> None:
        require(all(row["key"].startswith(prefix) and row["operation_id"] == operation for row in page), "cross-operation evidence page")
    audit_page_fixture(valid_page)
    rejected_pages = 0
    for row in [{"key": request["capture_id"] + "/" + operation, "operation_id": operation},
                {"key": key, "operation_id": request["capture_id"]}]:
        try:
            audit_page_fixture([row])
        except AssertionError:
            rejected_pages += 1
        else:
            raise AssertionError("cross-operation evidence fixture accepted")
    record = json.loads((ROOT / "config/vision/v11-source-audit.json").read_text(encoding="utf-8"))
    require(record["classes"] == vision["class_order"] and record["arch"] == "mobilenet_v3_large", "v11 model/class metadata drift")
    files = {f["relative_path"]: f for f in record["files"]}
    require(files["models/v11_with_empty/best.pt"]["sha256"] == files["models/equipment_cls/best.pt"]["sha256"], "default/v11 source checkpoint mismatch")
    require(record["onnx_input"] == ["batch", 3, 224, 224] and record["onnx_output"] == ["batch", 7], "ONNX inspected shape drift")
    local_verified = 0
    if "--verify-local-assets" in sys.argv:
        for file in record["files"]:
            path = Path(record["source_root"]) / file["relative_path"]
            with path.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            require(path.stat().st_size == file["bytes"] and digest == file["sha256"], "external model file changed: " + file["relative_path"])
            local_verified += 1
    candidate = json.loads((ROOT / "config/vision/deployment.unverified.json").read_text(encoding="utf-8"))
    require(candidate["status"] == "UNVERIFIED" and candidate["camera"]["owned"] is False, "unowned camera cannot be claimed verified")
    require(candidate["model"]["rknn_path"] is None and candidate["policy"]["thresholds_validated"] is False, "candidate must not claim RKNN/calibration complete")
    require(candidate["additional_cost"]["camera_cny"] is None and candidate["additional_cost"]["covered_by_existing_200_cny_budget"] is False, "unknown camera cost cannot be zero/covered")
    cam = candidate["camera"]["candidate"]
    require(cam["interface"].startswith("USB2.0 UVC"), "camera interface must stay UVC until RK CSI wiring is verified")
    require(cam["focus"] in ("fixed", "manual_locked") and cam["requires_manual_exposure"] is True, "camera selection must use fixed/locked focus and manual exposure")
    require(cam["requires_monotonic_timestamp"] is True, "camera selection must require monotonic timestamps")
    require(cam["price_settled"] is False and cam["board_usb_port_verified"] is False, "camera price/board cannot be claimed settled/verified")
    require(bool(cam.get("item_id")) and bool(cam.get("sku_id")), "selected camera needs a concrete item/SKU")
    require("自动对焦" not in cam["sku_label"] and "光敏" not in cam["sku_label"], "selected SKU must not be autofocus or light-sensor")
    require(candidate["additional_cost"]["prices_settled"] is False, "vision increment cannot claim settled prices")
    bom = json.loads((ROOT / "config/materials/selected-bom.json").read_text(encoding="utf-8"))
    increment = bom["vision_increment"]
    require(increment["settled"] is False and increment["counts_against_200_cap"] is False, "vision increment must stay outside the 200 CNY cap")
    inc_total = sum((Decimal(i["amount"]) for i in increment["items"] if i["included"]), Decimal(0))
    require(inc_total == Decimal(increment["total_cny"]), "vision increment total drift")
    require(inc_total <= Decimal(increment["budget_cap_cny"]), "vision increment exceeds its own cap")
    require(Decimal(increment["total_cny"]) == Decimal(candidate["additional_cost"]["vision_increment_candidate_total_cny"]), "camera cost mismatch between BOM and deployment template")
    require(not ({i["id"] for i in increment["items"]} & {i["id"] for i in bom["items"]}), "vision increment folded into the 200 CNY BOM")
    require(next(i for i in increment["items"] if i["id"] == "C01")["amount"] == cam["target_price_cny"], "camera target price mismatch")
    audit = increment["sku_audit"]
    require(sum(1 for o in audit["options"] if o["decision"] == "SELECTED") == 1, "exactly one camera SKU may be selected")
    selected = next(o for o in audit["options"] if o["decision"] == "SELECTED")
    require(selected["sku_id"] == cam["sku_id"] and selected["price"] == cam["target_price_cny"], "selected SKU/price drift")
    require(all(o["decision"] in ("SELECTED", "ALTERNATIVE") or o["decision"].startswith("REJECT_") for o in audit["options"]), "unexpected SKU decision")
    print(json.dumps({"status": "PASS", "contract_version": source["contract_version"], "vision_schema_refs": refs,
        "schema_examples": len(examples), "rejected_semantic_fixtures": len(mutations), "distinct_frames_with_identical_pixels": "ACCEPTED",
        "rejected_cross_operation_pages": rejected_pages,
        "source_assets_hash_verified": local_verified, "source_model": "MobileNetV3-Large v11 / 7 classes",
        "camera_owned": False, "camera_candidate": cam["selection_id"], "camera_interface": cam["interface"],
        "vision_increment_cny": increment["total_cny"], "vision_increment_settled": False,
        "rknn_available": False, "model_inference_tested": False, "hardware_tested": False}, ensure_ascii=False))


if __name__ == "__main__":
    main()
