#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "App/Main.cpp").read_text(encoding="utf-8-sig", errors="ignore")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require('#include "Manager/CloudPlatformControl.h"' in MAIN,
            "Main must use the persisted platform selector")
    require("static bool s_bStartCmiot" not in MAIN,
            "hard-coded cmiot selection must be removed")
    require("GetCloudPlatform()" in MAIN and "CLOUD_PLATFORM_CMIOT" in MAIN,
            "startup must branch on the configured platform")
    cmiot_pos = MAIN.find("cmiot_start();")
    protocol_pos = MAIN.find("protocolManager.Start()")
    require(cmiot_pos >= 0 and protocol_pos > cmiot_pos,
            "both startup paths must remain present")
    require("if (cloudPlatform == CLOUD_PLATFORM_CMIOT)" in MAIN,
            "CMIOT startup must be an explicit exclusive branch")
    require("cloud_platform=%s" in MAIN,
            "selected platform must be logged")
    print("PASS: startup selects exactly one configured cloud platform")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
