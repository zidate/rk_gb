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
    header = read("App/Media/NormalizedOsdControl.h")
    source = read("App/Media/NormalizedOsdControl.cpp")
    cmake = read("App/CMakeLists.txt")
    for token in ("ApplyNormalizedOsdCommon", "ApplyNormalizedOsdTime",
                  "ApplyNormalizedOsdText", "OsdCharacterMarginToNormalized"):
        require(token in header and token in source, f"missing media boundary {token}")
    require("CaptureGetResolution" in source and "10000" in source,
            "media boundary must scale normalized coordinates")
    require("gb_rkipc_osd_time_set" in source and "gb_rkipc_osd_text_set" in source,
            "media boundary must terminate at RK OSD")
    require("const double normalized" in source and
            "normalized >= static_cast<double>(kCoordinateMax)" in source,
            "large character margins must clamp before integer conversion")
    require("Media/NormalizedOsdControl.cpp" in cmake,
            "normalized OSD boundary must be compiled")
    print("PASS: normalized cmiot coordinates convert only at the RK media boundary")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
