#!/usr/bin/env python3
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
CMIOT_DEFINE = (ROOT / "Include/ChinaMobile/cmiot_define.h").read_text(
    encoding="utf-8-sig", errors="ignore"
)
CMIOT_H = (ROOT / "App/ChinaMobile/CmiotOsdControl.h").read_text(
    encoding="utf-8-sig", errors="ignore"
)
CMIOT_CPP = (ROOT / "App/ChinaMobile/CmiotOsdControl.cpp").read_text(
    encoding="utf-8-sig", errors="ignore"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    for token in (
        "cmiotOSDInfo_t",
        "cmiotOsdDateInfo_t",
        "cmiotOsdTextInfoList_t",
        "cmiotOsdGBInfoList_t",
        "CMIOT_MAX_OSD_CONTENT_LEN",
    ):
        require(token in CMIOT_DEFINE, f"cmiot_define.h should provide {token}.")

    for token in (
        "cmiot_osd_get_text_capacity",
        "cmiot_osd_set_config",
        "cmiot_osd_get_config",
        "CMIOT_APP_OSD_TEXT_MAX 7",
    ):
        require(token in CMIOT_H, f"CmiotOsdControl.h should expose {token}.")

    for forbidden in (
        "VideoOsdState",
        "ApplyVideoOsdConfig",
        "QueryVideoOsdState",
        "Media/VideoOsdControl.h",
        "CaptureGetResolution",
        "ScaleCoordinateXToDevice",
        "ScaleCoordinateYToDevice",
    ):
        require(forbidden not in CMIOT_CPP,
                f"cmiot parsing must not depend on {forbidden}")

    for token in (
        "CFG_CMIOT_OSD",
        "cmiot_osd_initialize",
        "cmiot_osd_is_override_active",
        "ApplyNormalizedOsdCommon",
        "ApplyNormalizedOsdTime",
        "ApplyNormalizedOsdText",
        '"custom_text"',
        '"district_text"',
        '"addition_text"',
    ):
        require(token in CMIOT_CPP + CMIOT_H,
                f"missing persistent cmiot OSD behavior {token}")
    require(
        "textNum > CMIOT_APP_OSD_TEXT_MAX" in CMIOT_CPP and
        "district > CMIOT_APP_OSD_TEXT_MAX" in CMIOT_CPP and
        "addition > CMIOT_APP_OSD_TEXT_MAX" in CMIOT_CPP and
        "district > CMIOT_APP_OSD_TEXT_MAX - addition" in CMIOT_CPP,
        "cmiot OSD must reject over-capacity text instead of truncating it.",
    )
    require("std::min" not in CMIOT_CPP,
            "cmiot input and output counts must be handled explicitly without silent truncation")
    require(
        "info.mode == kCmiotOsdModeCustom && !IsPositionValid(info.date.pos)" in CMIOT_CPP,
        "custom mode must validate only its active date position",
    )
    require(
        "info.mode == kCmiotOsdModeGb && !IsGbPositionValid(info.date.gbPos)" in CMIOT_CPP,
        "cmiot GB layout mode must validate only its active date margin",
    )
    require("ApplyEffectiveConfigLocked" in CMIOT_CPP,
            "persisted state and hardware apply must be serialized under one lock")
    require("value <= static_cast<float>(kCoordinateMax)" in CMIOT_CPP,
            "character margins must reject infinity and unreasonable values")
    require(
        "CMIOT_OSD_POS_ALIGN_RIGHT" in CMIOT_CPP and "CMIOT_OSD_POS_ALIGN_LEFT" in CMIOT_CPP,
        "cmiot OSD should map cmiot alignment definitions.",
    )
    print("PASS: cmiot OSD is persisted independently and applied without VideoOsdState")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
