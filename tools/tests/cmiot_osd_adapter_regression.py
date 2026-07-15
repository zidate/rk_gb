#!/usr/bin/env python3
from pathlib import Path
import re


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
        "CMIOT_APP_OSD_TEXT_MAX 5",
    ):
        require(token in CMIOT_H, f"CmiotOsdControl.h should expose {token}.")

    require(
        "media::ApplyVideoOsdConfig" in CMIOT_CPP,
        "cmiot OSD set should reuse the media OSD apply path for time/common settings.",
    )
    require(
        "ApplyCmiotTextConfig" in CMIOT_CPP and "timeState.has_text_items = false" in CMIOT_CPP,
        "cmiot OSD text should be applied by the cmiot-limited text path.",
    )
    require(
        "media::QueryVideoOsdState" in CMIOT_CPP,
        "cmiot OSD get should reuse the media OSD query path.",
    )
    require(
        "CMIOT_APP_OSD_TEXT_MAX" in CMIOT_CPP and
        re.search(r"(?<!CMIOT_APP_)OSD_TEXT_MAX", CMIOT_CPP) is None,
        "cmiot OSD should enforce its own text capacity instead of consuming every GB text slot.",
    )
    require(
        "CMIOT_OSD_POS_ALIGN_RIGHT" in CMIOT_CPP and "CMIOT_OSD_POS_ALIGN_LEFT" in CMIOT_CPP,
        "cmiot OSD should map cmiot alignment definitions.",
    )
    print("PASS: cmiot OSD adapter is wired to media OSD with reserved RGN capacity")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
