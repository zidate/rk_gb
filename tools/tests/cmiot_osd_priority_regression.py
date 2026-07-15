#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
AV_H = (ROOT / "App/Media/AVManager.h").read_text(encoding="utf-8-sig", errors="ignore")
AV_CPP = (ROOT / "App/Media/AVManager.cpp").read_text(encoding="utf-8-sig", errors="ignore")
MAIN = (ROOT / "App/Main.cpp").read_text(encoding="utf-8-sig", errors="ignore")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require("ApplyLocalOsdConfig" in AV_H and "ApplyLocalOsdConfig" in AV_CPP,
            "AVManager must expose local OSD restore")
    require(AV_CPP.count("cmiot_osd_is_override_active()") >= 2,
            "both local time and text callbacks must suppress hardware refresh")
    require("m_OSDTimeConf = OSDTimeConfig" in AV_CPP and
            "m_OSDTextAllConf = OSDTextAllConfig" in AV_CPP,
            "local updates must still be cached")
    require("cmiot_osd_initialize()" in MAIN,
            "CMIOT boot must restore the persisted OSD bank")
    require("cloudPlatform == CLOUD_PLATFORM_CMIOT" in MAIN,
            "restore must be scoped to CMIOT startup")
    print("PASS: cmiot override suppresses local refresh and restores local on disable")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
