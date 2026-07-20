#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    file_path = ROOT / path
    return file_path.read_text(encoding="utf-8-sig", errors="ignore") if file_path.exists() else ""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    kinds = read("Include/ExchangeAL/ExchangeKind.h")
    kind_map = read("Package/exchange/Source/ExchangeKind.cpp")
    defaults = read("App/Media/MediaDefaultConfig.cpp")
    platform_h = read("App/Manager/CloudPlatformControl.h")
    platform_cpp = read("App/Manager/CloudPlatformControl.cpp")
    cmake = read("App/CMakeLists.txt")

    require("CFG_CMIOT_OSD" in kinds and "CFG_CMIOT_OSD" in kind_map,
            "missing config key CFG_CMIOT_OSD")
    require('"CMIOT_OSD"' in kind_map, "missing CMIOT_OSD JSON namespace")
    require('table["valid"] = false' in defaults and 'table["osd_switch"] = 0' in defaults,
            "cmiot OSD must default to an invalid, disabled bank")

    # CloudPlatformControl has been removed; GetCloudPlatform no longer exists
    require(platform_h == "" and platform_cpp == "",
            "CloudPlatformControl.h/.cpp must be deleted")
    require("CloudPlatformControl.cpp" not in cmake,
            "CloudPlatformControl.cpp must be removed from CMakeLists.txt")
    print("PASS: cmiot OSD config persisted; CloudPlatformControl removed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
