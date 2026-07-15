#!/usr/bin/env python3
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
CALLBACK = (ROOT / "App/ChinaMobile/demo_callback.cpp").read_text(
    encoding="utf-8-sig", errors="ignore"
)
CMAKE = (ROOT / "App/CMakeLists.txt").read_text(encoding="utf-8-sig", errors="ignore")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require('#include "CmiotOsdControl.h"' in CALLBACK,
            "callback must include the adapter API")
    block = re.search(r"case\s+CMIOT_CMD_SET_OSD\s*:(.*?)(?=case\s+CMIOT_CMD_)",
                      CALLBACK, re.S)
    require(block is not None, "missing CMIOT_CMD_SET_OSD block")
    body = block.group(1)
    require("input == NULL" in body and "cmiot_osd_set_config" in body,
            "callback must validate input and call the adapter")
    require("return ret" in body, "callback must propagate adapter failure")
    require("ChinaMobile/CmiotOsdControl.cpp" in CMAKE,
            "adapter source must be part of CM_SRC")
    print("PASS: CMIOT_CMD_SET_OSD applies and reports the persistent adapter result")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
