#!/usr/bin/env python3
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
VIDEO_OSD_H = (ROOT / "App/Media/VideoOsdControl.h").read_text(
    encoding="utf-8-sig", errors="ignore"
)
VIDEO_OSD_CPP = (ROOT / "App/Media/VideoOsdControl.cpp").read_text(
    encoding="utf-8-sig", errors="ignore"
)
COMM_EXCHANGE_H = (ROOT / "Include/ExchangeAL/CommExchange.h").read_text(
    encoding="utf-8-sig", errors="ignore"
)
COMM_EXCHANGE_CPP = (ROOT / "Package/exchange/Source/CommExchange.cpp").read_text(
    encoding="utf-8-sig", errors="ignore"
)
AVMANAGER_CPP = (ROOT / "App/Media/AVManager.cpp").read_text(
    encoding="utf-8-sig", errors="ignore"
)
RK_VIDEO_C = (
    ROOT / "Middleware/libmpp/rkipc/src/rv1106_dual_ipc/video/video.c"
).read_text(encoding="utf-8-sig", errors="ignore")
OSD_C = (ROOT / "Middleware/libmpp/rkipc/common/osd/osd.c").read_text(
    encoding="utf-8-sig", errors="ignore"
)
OSD_COMMON_H = (ROOT / "Middleware/libmpp/rkipc/common/osd/osd_common.h").read_text(
    encoding="utf-8-sig", errors="ignore"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, signature: str) -> str:
    match = re.search(signature, source, re.MULTILINE)
    if match is None:
        return ""
    open_brace = source.find("{", match.end())
    if open_brace == -1:
        return ""
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1 : index]
    return ""


def main() -> int:
    for token in (
        "has_font_size",
        "font_size",
        "has_font_color_mode",
        "font_color_mode",
        "has_font_color",
        "font_color",
        "has_time_display_week_enabled",
        "time_display_week_enabled",
        "has_time_alignment",
        "time_alignment",
        "has_alignment",
        "alignment",
    ):
        require(token in VIDEO_OSD_H, f"VideoOsdState should expose {token}.")

    for token in (
        "display_week_enabled",
        "alignment",
        "font_size",
        "font_color_mode",
        "font_color",
    ):
        require(token in COMM_EXCHANGE_H, f"OSD config structs should persist {token}.")
        require(token in COMM_EXCHANGE_CPP, f"OSD config exchange should serialize {token}.")

    apply_body = function_body(
        VIDEO_OSD_CPP,
        r"int\s+ApplyVideoOsdConfig\s*\(\s*const\s+VideoOsdState&\s+desired\s*\)",
    )
    query_body = function_body(
        VIDEO_OSD_CPP,
        r"bool\s+QueryVideoOsdState\s*\(\s*VideoOsdState\*\s+state\s*\)",
    )
    require("kVideoOsdCoordinateMax = 10000" in VIDEO_OSD_CPP,
            "external OSD coordinates should be normalized to 0-10000.")
    require("ScaleVideoOsdXToDevice" in apply_body and "ScaleVideoOsdYToDevice" in apply_body,
            "ApplyVideoOsdConfig should convert external x/y to device pixels.")
    require("ScaleVideoOsdXFromDevice" in query_body and "ScaleVideoOsdYFromDevice" in query_body,
            "QueryVideoOsdState should report external x/y as 0-10000.")
    require("kVideoOsdCoordinateMax / 2" in VIDEO_OSD_CPP,
            "OSD coordinate conversion should round instead of truncating.")
    require("NormalizeVideoOsdDateType" in apply_body,
            "ApplyVideoOsdConfig should support the requested date formats.")
    require("NormalizeVideoOsdTimeType" in apply_body,
            "ApplyVideoOsdConfig should support 12/24 hour formats.")

    require("gb_rkipc_osd_time_set(int date_type, int time_type, int display_week_enabled" in AVMANAGER_CPP,
            "AVManager should call the expanded time OSD setter.")
    require("gb_rkipc_osd_common_set" in AVMANAGER_CPP,
            "AVManager should pass common font/color settings to the RK OSD layer.")
    require("gb_rkipc_osd_text_set(int index, const char *text, int x, int y, int show, int alignment" in AVMANAGER_CPP,
            "AVManager should call the expanded text OSD setter.")

    require("#define OSD_FMT_YMD_DOT" in OSD_COMMON_H,
            "OSD common format constants should include YYYY.MM.DD.")
    require("OSD_FMT_YMD_DOT" in OSD_C,
            "date/time generator should render YYYY.MM.DD.")
    require("OSD_FMT_WEEK0" in function_body(OSD_C, r"int\s+generate_date_time_2\s*\("),
            "RV1106 OSD date generator should support display-week output.")

    require("gb_rkipc_osd_common_set" in RK_VIDEO_C,
            "RV1106 OSD layer should implement common font/color configuration.")
    require("display_week_enabled" in RK_VIDEO_C,
            "RV1106 time OSD setter should accept display_week_enabled.")
    require("alignment" in RK_VIDEO_C and "rkipc_osd_aligned_x" in RK_VIDEO_C,
            "RV1106 OSD layer should apply left/right alignment.")
    require("s_osd_font_size = 64" not in RK_VIDEO_C,
            "RV1106 OSD font size should no longer be hard-coded to 64 only.")

    print("PASS: video OSD todo requirements are wired through media and RK layers")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
