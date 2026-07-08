#!/usr/bin/env python3
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
PROTOCOL_MANAGER = (ROOT / "App/Protocol/ProtocolManager.cpp").read_text(
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
                return source[open_brace + 1:index]
    return ""


def main() -> int:
    normalize_body = function_body(
        PROTOCOL_MANAGER,
        r"static\s+void\s+NormalizeVideoOsdStateForProtocol\s*\(\s*media::VideoOsdState\*\s+state\s*\)",
    )
    match_body = function_body(
        PROTOCOL_MANAGER,
        r"static\s+bool\s+IsVideoOsdStateMatched\s*\(\s*const\s+media::VideoOsdState&\s+desired,\s*const\s+media::VideoOsdState\*\s+runtimeState\s*\)",
    )
    date_body = function_body(
        PROTOCOL_MANAGER,
        r"static\s+std::string\s+NormalizeVideoOsdDateStyleValue\s*\(\s*const\s+std::string&\s+formatIn\s*\)",
    )

    require(normalize_body, "missing NormalizeVideoOsdStateForProtocol body")
    require(match_body, "missing IsVideoOsdStateMatched body")
    require(date_body, "missing NormalizeVideoOsdDateStyleValue body")

    for token in (
        "has_font_size",
        "has_font_color_mode",
        "has_font_color",
        "has_time_display_week_enabled",
        "has_time_alignment",
        "has_alignment",
    ):
        require(token not in normalize_body,
                f"GB OSD protocol normalization should not consume new media-only field {token}.")
        require(token not in match_body,
                f"GB OSD protocol compare should not depend on new media-only field {token}.")

    require(
        "desiredCustomTextEnabled != 0" not in match_body,
        "GB OSD master switch compatibility should not change when adding new external OSD text controls.",
    )

    for token in ("mm/dd/yyyy", "dd/mm/yyyy", "mm-dd-yyyy", "dd-mm-yyyy"):
        require(token in date_body, f"GB OSD date compatibility mapping should keep {token}.")
    require("yyyy.mm.dd" not in date_body,
            "GB OSD date compatibility mapping should not add new external YYYY.MM.DD behavior.")
    require("年" not in date_body and "月" not in date_body and "日" not in date_body,
            "GB OSD date compatibility mapping should not add new external Chinese date behavior.")

    print("PASS: GB OSD protocol interface stays compatible with pre-existing fields")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
