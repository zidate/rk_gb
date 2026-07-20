#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "App/Main.cpp").read_text(encoding="utf-8-sig", errors="ignore")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require('#include "Manager/CloudPlatformControl.h"' not in MAIN,
            "CloudPlatformControl include must be removed")
    require("GetCloudPlatform" not in MAIN,
            "GetCloudPlatform must be removed from Main.cpp")
    require("cmiot_osd_initialize()" in MAIN,
            "cmiot_osd_initialize must be called unconditionally")
    require("cmiot_start()" not in MAIN,
            "cmiot_start gateway must be removed with GetCloudPlatform")
    require("while (1)" not in MAIN,
            "infinite loop blocking GB startup must be removed")
    print("PASS: startup no longer uses CloudPlatform; cmiot_osd_initialize called unconditionally")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
