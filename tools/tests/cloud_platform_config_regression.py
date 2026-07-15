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

    for token in ("CFG_CLOUD_PLATFORM", "CFG_CMIOT_OSD"):
        require(token in kinds and token in kind_map, f"missing config key {token}")
    require('"CloudPlatform"' in kind_map, "missing CloudPlatform JSON namespace")
    require('"CMIOT_OSD"' in kind_map, "missing CMIOT_OSD JSON namespace")
    require('table["platform"] = CLOUD_PLATFORM_GB28181' in defaults,
            "GB28181 must be the persisted default platform")
    require('table["valid"] = false' in defaults and 'table["osd_switch"] = 0' in defaults,
            "cmiot OSD must default to an invalid, disabled bank")
    for token in ("CLOUD_PLATFORM_GB28181", "CLOUD_PLATFORM_CMIOT", "GetCloudPlatform"):
        require(token in platform_h + platform_cpp, f"missing platform API {token}")
    require("static const CloudPlatformType platform = ReadCloudPlatform()" in platform_cpp,
            "platform selection must stay fixed until process restart")
    require("Manager/CloudPlatformControl.cpp" in cmake,
            "CloudPlatformControl.cpp must be compiled")
    print("PASS: cloud platform and cmiot OSD config namespaces are persistent")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
