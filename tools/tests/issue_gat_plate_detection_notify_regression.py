#!/usr/bin/env python3
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]

PM_HEADER = ROOT / "App/Protocol/ProtocolManager.h"
PM_SOURCE = ROOT / "App/Protocol/ProtocolManager.cpp"
GAT_HEADER = ROOT / "App/Protocol/gat1400/GAT1400ClientService.h"
GAT_SOURCE = ROOT / "App/Protocol/gat1400/GAT1400ClientService.cpp"
LOWER_HEADER = ROOT / "third_party/platform_sdk_port/CommonFile/CommonLib/LowerGAT1400SDK.h"
LOWER_SOURCE = ROOT / "App/Protocol/gat1400/LowerGAT1400SDK.cpp"
DOC = ROOT / "helloagents/wiki/modules/external_module_demos.md"


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig", errors="ignore")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def extract_function(text: str, marker: str) -> str:
    start = text.find(marker)
    require(start >= 0, f"missing function marker: {marker}")
    brace = text.find("{", start)
    require(brace >= 0, f"missing function body: {marker}")
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:index + 1]
    raise AssertionError(f"unterminated function: {marker}")


def main() -> int:
    pm_header = read_text(PM_HEADER)
    pm_source = read_text(PM_SOURCE)
    gat_header = read_text(GAT_HEADER)
    gat_source = read_text(GAT_SOURCE)
    lower_header = read_text(LOWER_HEADER)
    lower_source = read_text(LOWER_SOURCE)
    doc = read_text(DOC)

    require(
        "int NotifyGatPlateDetections(const std::list<GAT_1400_Motor>& motorList);" in pm_header,
        "ProtocolManager should expose async plate detection notify API.",
    )
    require(
        "int NotifyPlateDetections(const std::list<GAT_1400_Motor>& motorList);" in gat_header,
        "GAT1400ClientService should expose async plate detection notify API.",
    )
    require(
        "LOWER_1400_NOTIFY_PLATEDETECTIONS" in lower_header,
        "LowerGAT1400SDK should export plate detection notify API.",
    )

    service_body = extract_function(gat_source, "int GAT1400ClientService::NotifyPlateDetections")
    require("EnqueuePendingUpload" in service_body, "Plate detection notify should use async queue.")
    require('"/VIID/MotorVehicles"' in service_body, "Plate detection notify should reuse MotorVehicles resource.")
    require("GAT1400Json::PackMotorVehicleListJson" in service_body, "Plate detection notify should pack GAT_1400_Motor.")
    require("kNotifyAsyncMaxAttemptCount" in service_body, "Plate detection notify should keep at-most-two attempt policy.")
    require("RequestPendingReplay" in service_body, "Plate detection notify should wake async replay when registered.")

    pm_body = extract_function(pm_source, "int ProtocolManager::NotifyGatPlateDetections")
    require("NotifyPlateDetections" in pm_body, "ProtocolManager plate API should delegate to GAT service.")
    require("post_plate" in pm_body, "ProtocolManager plate API should log a distinct event.")

    require(
        re.search(r"LOWER_1400_NOTIFY_PLATEDETECTIONS\s*\([^)]*GAT_1400_Motor", lower_source, re.DOTALL),
        "LowerGAT1400SDK.cpp should implement plate detection API with GAT_1400_Motor.",
    )
    require("NotifyPlateDetections" in lower_source, "Lower plate API should delegate to async service notify.")
    require("GAT_1400_Plate" not in gat_header + gat_source + pm_header + pm_source + lower_header + lower_source,
            "Plate detection should not invent a non-standard GAT_1400_Plate object.")

    require("NotifyGatPlateDetections" in doc, "External module demo should document plate detection notify API.")
    require("HasPlate" in doc and "PlateNo" in doc, "External module demo should show plate fields on GAT_1400_Motor.")

    print("PASS: GAT1400 plate detection notify API reuses MotorVehicles asynchronously")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
