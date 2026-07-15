# cmiot OSD Platform Priority Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make one RV1106 firmware select GB28181 or CMIOT at startup, persist local and cmiot OSD independently, and apply the correct OSD source across updates and reboots.

**Architecture:** Add a persisted cloud-platform selector and a separate pointer-free JSON bank for cmiot OSD. `CmiotOsdControl` validates and stores SDK values without `VideoOsdState` or x/y scaling; a small media boundary converts normalized coordinates only when calling RK OSD. `AVManager` continues caching local OSD but suppresses local hardware refresh while the active CMIOT bank has `osdSwitch=1`.

**Tech Stack:** C++11, C, JsonCpp `CConfigTable`, project `IConfigManager`, RV1106 `gb_rkipc_osd_*`, CMake, Python 3 static regression scripts.

---

## Baseline and file map

Worktree: `/home/jerry/silver/rk_gb/.worktrees/cmiot-osd-platform-priority`

Merged baseline: `87c6133` (`origin/issue47_dg_ipc_replay_20260418_main` at `6ad8b24` plus the approved design).

Baseline observations:

- `python3 tools/tests/video_osd_todolist_regression.py` passes.
- `python3 tools/tests/gb_osd_protocol_compat_regression.py` passes.
- `python3 tools/tests/video_osd_auto_inverse_regression.py` passes.
- `python3 tools/tests/cmiot_osd_adapter_regression.py` fails because it still reads deleted `App/cmiot/*` paths.
- A final link requires private cmiot SDK archives under `Lib/Device/ChinaMobile`; they are not present in this checkout, so a missing-library link failure must be reported separately from source/test results.

Responsibilities after the change:

- `App/Manager/CloudPlatformControl.*`: validate and read the persisted platform selector.
- `App/Media/NormalizedOsdControl.*`: the only new boundary that converts `0-10000` cmiot coordinates to RK pixels.
- `App/ChinaMobile/CmiotOsdControl.*`: validate SDK input, deep-copy to JSON, rebuild query output, compute cmiot layout in normalized coordinates, and select cmiot/local application.
- `App/Media/AVManager.*`: cache/apply local OSD and suppress only its hardware refresh during an active cmiot override.
- `App/Main.cpp`: start exactly one cloud platform and restore cmiot OSD only on CMIOT startup.
- `tools/tests/*`: source-level regression checks for paths, platform gating, persistence, coordinate boundary, callback wiring, and GB compatibility.

### Task 1: Repair the migrated cmiot regression baseline

**Files:**
- Modify: `tools/tests/cmiot_osd_adapter_regression.py:6-16,64-69`

- [ ] **Step 1: Point the test at the moved SDK and adapter files**

Replace the four path definitions with:

```python
CMIOT_DEFINE = (ROOT / "Include/ChinaMobile/cmiot_define.h").read_text(
    encoding="utf-8-sig", errors="ignore"
)
CMIOT_H = (ROOT / "App/ChinaMobile/CmiotOsdControl.h").read_text(
    encoding="utf-8-sig", errors="ignore"
)
CMIOT_CPP = (ROOT / "App/ChinaMobile/CmiotOsdControl.cpp").read_text(
    encoding="utf-8-sig", errors="ignore"
)
```

Change the CMake assertion to require `ChinaMobile/CmiotOsdControl.cpp`.

- [ ] **Step 2: Run the repaired baseline test**

Run: `python3 tools/tests/cmiot_osd_adapter_regression.py`

Expected: `PASS: cmiot OSD adapter is wired to media OSD with reserved RGN capacity`.

- [ ] **Step 3: Commit the test migration**

```bash
git add tools/tests/cmiot_osd_adapter_regression.py
git commit -m "test: follow cmiot adapter move"
```

### Task 2: Add persisted cloud-platform and cmiot OSD config keys

**Files:**
- Create: `App/Manager/CloudPlatformControl.h`
- Create: `App/Manager/CloudPlatformControl.cpp`
- Create: `tools/tests/cloud_platform_config_regression.py`
- Modify: `Include/ExchangeAL/ExchangeKind.h:68-75`
- Modify: `Package/exchange/Source/ExchangeKind.cpp:81-87`
- Modify: `App/Media/MediaDefaultConfig.h:27-31`
- Modify: `App/Media/MediaDefaultConfig.cpp:17-32,298-325`
- Modify: `App/CMakeLists.txt:140-146`

- [ ] **Step 1: Write the failing platform/config regression**

Create `tools/tests/cloud_platform_config_regression.py`:

```python
#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig", errors="ignore")


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
```

- [ ] **Step 2: Run the test to verify the red state**

Run: `python3 tools/tests/cloud_platform_config_regression.py`

Expected: FAIL because `CloudPlatformControl.h` does not exist.

- [ ] **Step 3: Add the config kinds and stable string mappings**

Insert before `CFG_NR` in `ExchangeKind.h`:

```cpp
    CFG_CLOUD_PLATFORM,  // 当前云平台：GB28181/CMIOT
    CFG_CMIOT_OSD,       // cmiot 独立 OSD 配置
```

Insert before `{NULL,}` in `ExchangeKind.cpp`:

```cpp
    {"CloudPlatform", CFG_CLOUD_PLATFORM},
    {"CMIOT_OSD", CFG_CMIOT_OSD},
```

- [ ] **Step 4: Add the platform reader**

Create `App/Manager/CloudPlatformControl.h`:

```cpp
#ifndef CLOUD_PLATFORM_CONTROL_H_
#define CLOUD_PLATFORM_CONTROL_H_

enum CloudPlatformType
{
    CLOUD_PLATFORM_GB28181 = 0,
    CLOUD_PLATFORM_CMIOT = 1,
};

CloudPlatformType GetCloudPlatform(void);
const char* GetCloudPlatformName(CloudPlatformType platform);

#endif
```

Create `App/Manager/CloudPlatformControl.cpp`. The function-local static deliberately freezes the configured platform for the process lifetime, so editing JSON does not partially switch SDK behavior before reboot:

```cpp
#include "CloudPlatformControl.h"

#include "Common.h"

namespace
{
CloudPlatformType ReadCloudPlatform(void)
{
    CConfigTable table;
    if (!g_configManager.getConfig(getConfigName(CFG_CLOUD_PLATFORM), table)) {
        AppErr("get cloud platform config failed, fallback GB28181\n");
        return CLOUD_PLATFORM_GB28181;
    }

    const int value = table["platform"].asInt();
    if (value == CLOUD_PLATFORM_CMIOT) {
        return CLOUD_PLATFORM_CMIOT;
    }
    if (value != CLOUD_PLATFORM_GB28181) {
        AppErr("invalid cloud platform %d, fallback GB28181\n", value);
    }
    return CLOUD_PLATFORM_GB28181;
}
}

CloudPlatformType GetCloudPlatform(void)
{
    static const CloudPlatformType platform = ReadCloudPlatform();
    return platform;
}

const char* GetCloudPlatformName(CloudPlatformType platform)
{
    return platform == CLOUD_PLATFORM_CMIOT ? "CMIOT" : "GB28181";
}
```

Add `Manager/CloudPlatformControl.cpp` to `Manager_SRC` in `App/CMakeLists.txt`.

- [ ] **Step 5: Add safe defaults**

Declare `setCloudPlatform()` and `setCmiotOsd()` in `MediaDefaultConfig.h`, call both from `start()`, and implement:

```cpp
void CMediaDefaultConfig::setCloudPlatform()
{
    CConfigTable table;
    table["platform"] = CLOUD_PLATFORM_GB28181;
    g_configManager.setDefault(getConfigName(CFG_CLOUD_PLATFORM), table);
}

void CMediaDefaultConfig::setCmiotOsd()
{
    CConfigTable table;
    table["valid"] = false;
    table["osd_switch"] = 0;
    table["mode"] = 1;
    table["custom_text"] = Json::arrayValue;
    table["district_text"] = Json::arrayValue;
    table["addition_text"] = Json::arrayValue;
    g_configManager.setDefault(getConfigName(CFG_CMIOT_OSD), table);
}
```

Include `Manager/CloudPlatformControl.h` in `MediaDefaultConfig.cpp` for the enum.

- [ ] **Step 6: Run the platform/config regression**

Run: `python3 tools/tests/cloud_platform_config_regression.py`

Expected: `PASS: cloud platform and cmiot OSD config namespaces are persistent`.

- [ ] **Step 7: Commit the configuration boundary**

```bash
git add App/Manager/CloudPlatformControl.h App/Manager/CloudPlatformControl.cpp \
  Include/ExchangeAL/ExchangeKind.h Package/exchange/Source/ExchangeKind.cpp \
  App/Media/MediaDefaultConfig.h App/Media/MediaDefaultConfig.cpp App/CMakeLists.txt \
  tools/tests/cloud_platform_config_regression.py
git commit -m "feat: persist cloud platform selection"
```

### Task 3: Gate GB28181 and CMIOT startup paths

**Files:**
- Create: `tools/tests/cloud_platform_startup_regression.py`
- Modify: `App/Main.cpp:1-38,1572-1693`

- [ ] **Step 1: Write the failing startup-gating regression**

Create `tools/tests/cloud_platform_startup_regression.py`:

```python
#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "App/Main.cpp").read_text(encoding="utf-8-sig", errors="ignore")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require('#include "Manager/CloudPlatformControl.h"' in MAIN,
            "Main must use the persisted platform selector")
    require("static bool s_bStartCmiot" not in MAIN,
            "hard-coded cmiot selection must be removed")
    require("GetCloudPlatform()" in MAIN and "CLOUD_PLATFORM_CMIOT" in MAIN,
            "startup must branch on the configured platform")
    cmiot_pos = MAIN.find("cmiot_start();")
    protocol_pos = MAIN.find("protocolManager.Start()")
    require(cmiot_pos >= 0 and protocol_pos > cmiot_pos,
            "both startup paths must remain present")
    require("if (cloudPlatform == CLOUD_PLATFORM_CMIOT)" in MAIN,
            "CMIOT startup must be an explicit exclusive branch")
    require("cloud_platform=%s" in MAIN,
            "selected platform must be logged")
    print("PASS: startup selects exactly one configured cloud platform")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
```

- [ ] **Step 2: Verify the test fails on the hard-coded boolean**

Run: `python3 tools/tests/cloud_platform_startup_regression.py`

Expected: FAIL with `Main must use the persisted platform selector`.

- [ ] **Step 3: Replace the hard-coded selector with the persisted branch**

Add:

```cpp
#include "Manager/CloudPlatformControl.h"
```

Remove `static bool s_bStartCmiot = true;`. In the normal-start path replace the existing cmiot block and unconditional loop with:

```cpp
        const CloudPlatformType cloudPlatform = GetCloudPlatform();
        AppErr("cloud_platform=%s\n", GetCloudPlatformName(cloudPlatform));
        if (cloudPlatform == CLOUD_PLATFORM_CMIOT)
        {
            g_NetConfigHook.SetQrcodeEnable(false);
            const int cmiotRet = cmiot_start();
            if (cmiotRet != 0) {
                AppErr("cmiot_start failed ret=%d\n", cmiotRet);
                return false;
            }
            while (1) {
                sleep(1);
            }
        }
```

Leave the existing network/DM/ProtocolManager sequence after this block. It is now reachable only for the default GB28181 path.

- [ ] **Step 4: Run the startup regression**

Run: `python3 tools/tests/cloud_platform_startup_regression.py`

Expected: `PASS: startup selects exactly one configured cloud platform`.

- [ ] **Step 5: Commit the exclusive startup routing**

```bash
git add App/Main.cpp tools/tests/cloud_platform_startup_regression.py
git commit -m "fix: select one cloud platform at startup"
```

### Task 4: Add the normalized-to-RK OSD boundary

**Files:**
- Create: `App/Media/NormalizedOsdControl.h`
- Create: `App/Media/NormalizedOsdControl.cpp`
- Create: `tools/tests/normalized_osd_boundary_regression.py`
- Modify: `App/CMakeLists.txt:120-140`

- [ ] **Step 1: Write the failing coordinate-boundary regression**

Create `tools/tests/normalized_osd_boundary_regression.py`:

```python
#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig", errors="ignore")


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
```

- [ ] **Step 2: Verify the missing boundary fails**

Run: `python3 tools/tests/normalized_osd_boundary_regression.py`

Expected: FAIL because `NormalizedOsdControl.h` is absent.

- [ ] **Step 3: Add the narrow media API**

Create `NormalizedOsdControl.h`:

```cpp
#ifndef NORMALIZED_OSD_CONTROL_H_
#define NORMALIZED_OSD_CONTROL_H_

int ApplyNormalizedOsdCommon(int fontSize,
                             const char* fontColorMode,
                             const char* fontColor);
int ApplyNormalizedOsdTime(int dateType, int timeType, int displayWeek,
                           int x, int y, int show, int alignment);
int ApplyNormalizedOsdText(int index, const char* text,
                           int x, int y, int show, int alignment);
int OsdCharacterMarginToNormalized(float characters, int fontSize,
                                   int horizontal);

#endif
```

Create `NormalizedOsdControl.cpp` with scaling confined to this file:

```cpp
#include "NormalizedOsdControl.h"

#include "PAL/Capture.h"

extern "C" {
int gb_rkipc_osd_common_set(int, const char*, const char*);
int gb_rkipc_osd_time_set(int, int, int, int, int, int, int);
int gb_rkipc_osd_text_set(int, const char*, int, int, int, int);
}

namespace {
const int kCoordinateMax = 10000;
const int kDefaultWidth = 1920;
const int kDefaultHeight = 1080;

int ScaleCoordinate(int value, int canvas)
{
    if (value < 0 || value > kCoordinateMax || canvas <= 0) {
        return -1;
    }
    return (value * canvas + kCoordinateMax / 2) / kCoordinateMax;
}

void QueryCanvas(int* width, int* height)
{
    if (CaptureGetResolution(0, width, height) != 0 || *width <= 0 || *height <= 0) {
        *width = kDefaultWidth;
        *height = kDefaultHeight;
    }
}
}

int ApplyNormalizedOsdCommon(int fontSize, const char* mode, const char* color)
{
    return gb_rkipc_osd_common_set(fontSize, mode, color);
}

int ApplyNormalizedOsdTime(int dateType, int timeType, int week,
                           int x, int y, int show, int alignment)
{
    int width = 0;
    int height = 0;
    QueryCanvas(&width, &height);
    const int deviceX = ScaleCoordinate(x, width);
    const int deviceY = ScaleCoordinate(y, height);
    if (deviceX < 0 || deviceY < 0) {
        return -1;
    }
    return gb_rkipc_osd_time_set(dateType, timeType, week,
                                 deviceX, deviceY, show, alignment);
}

int ApplyNormalizedOsdText(int index, const char* text,
                           int x, int y, int show, int alignment)
{
    int width = 0;
    int height = 0;
    QueryCanvas(&width, &height);
    const int deviceX = ScaleCoordinate(x, width);
    const int deviceY = ScaleCoordinate(y, height);
    if (deviceX < 0 || deviceY < 0) {
        return -1;
    }
    return gb_rkipc_osd_text_set(index, text ? text : "",
                                 deviceX, deviceY, show, alignment);
}

int OsdCharacterMarginToNormalized(float characters, int fontSize,
                                   int horizontal)
{
    if (characters < 0.0f || fontSize <= 0) {
        return -1;
    }
    int width = 0;
    int height = 0;
    QueryCanvas(&width, &height);
    const int canvas = horizontal ? width : height;
    const float pixels = characters * static_cast<float>(fontSize);
    const int normalized = static_cast<int>(
        pixels * static_cast<float>(kCoordinateMax) / canvas + 0.5f);
    return normalized > kCoordinateMax ? kCoordinateMax : normalized;
}
```

Add `Media/NormalizedOsdControl.cpp` to `Media_SRC`.

- [ ] **Step 4: Run the coordinate-boundary regression**

Run: `python3 tools/tests/normalized_osd_boundary_regression.py`

Expected: `PASS: normalized cmiot coordinates convert only at the RK media boundary`.

- [ ] **Step 5: Commit the media boundary**

```bash
git add App/Media/NormalizedOsdControl.h App/Media/NormalizedOsdControl.cpp \
  App/CMakeLists.txt tools/tests/normalized_osd_boundary_regression.py
git commit -m "feat: add normalized RK OSD boundary"
```

### Task 5: Replace the cmiot adapter with direct persistent parsing

**Files:**
- Modify: `App/ChinaMobile/CmiotOsdControl.h:1-37`
- Modify: `App/ChinaMobile/CmiotOsdControl.cpp:1-939`
- Modify: `tools/tests/cmiot_osd_adapter_regression.py`

- [ ] **Step 1: Strengthen the cmiot regression before implementation**

Replace the old `VideoOsdState` expectations with these checks:

```python
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
    ):
        require(token in CMIOT_CPP + CMIOT_H,
                f"missing persistent cmiot OSD behavior {token}")
```

Also assert the source contains `custom_text`, `district_text`, `addition_text`, rejects counts over `CMIOT_APP_OSD_TEXT_MAX`, and does not use `std::min` to truncate input counts.

- [ ] **Step 2: Run the strengthened regression to verify failure**

Run: `python3 tools/tests/cmiot_osd_adapter_regression.py`

Expected: FAIL because the old adapter still includes `VideoOsdControl.h`.

- [ ] **Step 3: Define the lifecycle API**

Keep the existing set/get/capacity functions and add to `CmiotOsdControl.h`:

```cpp
/* Load persisted cmiot OSD and apply it only when CMIOT is the active platform. */
int cmiot_osd_initialize(void);

/* True only for active CMIOT platform with a valid persisted osdSwitch=1 bank. */
int cmiot_osd_is_override_active(void);
```

- [ ] **Step 4: Replace pointer caching with a pointer-free internal model**

In `CmiotOsdControl.cpp`, remove `VideoOsdControl.h`, `PAL/Capture.h`, `CachedCmiotOsd`, and all x/y scale helpers. Include:

```cpp
#include "Manager/CloudPlatformControl.h"
#include "Media/AVManager.h"
#include "Media/NormalizedOsdControl.h"
```

Use fixed arrays:

```cpp
struct StoredCmiotOsd
{
    bool valid;
    cmiot_bool_t osdSwitch;
    cmiot_uint8_t mode;
    cmiotOsdDateInfo_t date;
    cmiot_uint32_t fontSize;
    char fontColor[16];
    cmiot_uint32_t customCount;
    cmiotOsdTextInfo_t customText[CMIOT_APP_OSD_TEXT_MAX];
    cmiot_uint32_t districtCount;
    cmiotGBOsdTextInfo_t districtText[CMIOT_APP_OSD_TEXT_MAX];
    cmiotGBOsdPosInfo_t districtPos;
    cmiot_uint32_t additionCount;
    cmiotGBOsdTextInfo_t additionText[CMIOT_APP_OSD_TEXT_MAX];
    cmiotGBOsdPosInfo_t additionPos;

    StoredCmiotOsd() : valid(false), osdSwitch(CMIOT_FALSE), mode(1),
        fontSize(32), customCount(0), districtCount(0), additionCount(0)
    {
        memset(&date, 0, sizeof(date));
        memset(fontColor, 0, sizeof(fontColor));
        memset(customText, 0, sizeof(customText));
        memset(districtText, 0, sizeof(districtText));
        memset(&districtPos, 0, sizeof(districtPos));
        memset(additionText, 0, sizeof(additionText));
        memset(&additionPos, 0, sizeof(additionPos));
    }
};
```

Validate before copying:

```cpp
static int ValidateInput(const cmiotOSDInfo_t& info)
{
    if (info.osdSwitch == 0) {
        return 0;
    }
    if (info.mode != 1 && info.mode != 2) {
        return -1;
    }
    if (info.date.pos.x > 10000 || info.date.pos.y > 10000) {
        return -1;
    }
    if (info.mode == 1) {
        if (info.osdText.customText.textNum > CMIOT_APP_OSD_TEXT_MAX ||
            (info.osdText.customText.textNum > 0 && info.osdText.customText.text == NULL)) {
            return -1;
        }
        for (cmiot_uint32_t i = 0; i < info.osdText.customText.textNum; ++i) {
            if (info.osdText.customText.text[i].pos.x > 10000 ||
                info.osdText.customText.text[i].pos.y > 10000) {
                return -1;
            }
        }
    } else {
        const cmiot_uint32_t district = info.osdText.gbText.districtText.textNum;
        const cmiot_uint32_t addition = info.osdText.gbText.additionText.textNum;
        if (district + addition > CMIOT_APP_OSD_TEXT_MAX ||
            (district > 0 && info.osdText.gbText.districtText.text == NULL) ||
            (addition > 0 && info.osdText.gbText.additionText.text == NULL)) {
            return -1;
        }
    }
    return 0;
}
```

Keep the existing bounded string/color/date helpers where useful, but reject invalid values instead of silently converting unsupported input.

- [ ] **Step 5: Serialize every field to `CFG_CMIOT_OSD`**

Implement `StoreToTable()`/`LoadFromTable()` using these stable keys:

```cpp
table["valid"]
table["osd_switch"]
table["mode"]
table["date"]["status"]
table["date"]["date_format"]
table["date"]["time_format"]
table["date"]["weekday"]
table["date"]["pos"]["x"]
table["date"]["pos"]["y"]
table["date"]["pos"]["align_set"]
table["date"]["pos"]["align_pos"]
table["date"]["gb_pos"]["side_space"]
table["date"]["gb_pos"]["vert_space"]
table["date"]["gb_pos"]["line_space"]
table["font_size"]
table["font_color"]
table["custom_text"][i]["content"]
table["custom_text"][i]["pos"]
table["district_text"][i]["content"]
table["district_pos"]
table["addition_text"][i]["content"]
table["addition_pos"]
```

Use Json arrays rebuilt from `Json::arrayValue` on every save. On `osdSwitch=0`, load the previous bank, change only `valid=true` and `osdSwitch=0`, then save; do not validate or overwrite irrelevant payload fields.

- [ ] **Step 6: Apply the stored model without `VideoOsdState`**

Implement an `ApplyStoredCmiotOsd(const StoredCmiotOsd&)` function that:

1. Normalizes supported font size/color values.
2. Converts date strings to RK `dateType/timeType` integers.
3. Computes custom/GB alignment and margin layout in normalized `0-10000` space, using `OsdCharacterMarginToNormalized()` for character margins and line spacing. Direct SDK `pos.x/pos.y` values are passed through unchanged.
4. Calls `ApplyNormalizedOsdCommon`, `ApplyNormalizedOsdTime`, and `ApplyNormalizedOsdText`.
5. Calls `ApplyNormalizedOsdText(i, "", 0, 0, 0, 0)` for every unused index through `OSD_TEXT_MAX-1`, so stale local regions 6/7 cannot remain visible.
6. Returns the first nonzero RK apply result.

The set/lifecycle logic must be:

```cpp
extern "C" int cmiot_osd_set_config(const cmiotOSDInfo_t* info)
{
    if (info == NULL || ValidateInput(*info) != 0) {
        return -1;
    }

    pthread_mutex_lock(&g_cmiot_osd_mutex);
    StoredCmiotOsd next = g_cmiot_osd;
    if (!g_cmiot_osd_loaded) {
        LoadStoredConfig(&next);
    }
    CopyInput(*info, &next);
    const int saveRet = SaveStoredConfig(next);
    if (saveRet == 0) {
        g_cmiot_osd = next;
        g_cmiot_osd_loaded = true;
    }
    pthread_mutex_unlock(&g_cmiot_osd_mutex);
    if (saveRet != 0) {
        return saveRet;
    }
    if (GetCloudPlatform() != CLOUD_PLATFORM_CMIOT) {
        return 0;
    }
    return next.osdSwitch ? ApplyStoredCmiotOsd(next)
                          : g_AVManager.ApplyLocalOsdConfig();
}
```

`cmiot_osd_initialize()` loads the bank once and applies only a valid enabled bank on CMIOT. `cmiot_osd_is_override_active()` checks platform, loaded/valid state, and `osdSwitch` while holding the mutex. `cmiot_osd_get_config()` rebuilds only the stored cmiot bank and never falls back to `QueryVideoOsdState` or local OSD.

- [ ] **Step 7: Run the direct-adapter regression**

Run: `python3 tools/tests/cmiot_osd_adapter_regression.py`

Expected: PASS with a revised final message: `PASS: cmiot OSD is persisted independently and applied without VideoOsdState`.

- [ ] **Step 8: Commit the persistent adapter**

```bash
git add App/ChinaMobile/CmiotOsdControl.h App/ChinaMobile/CmiotOsdControl.cpp \
  tools/tests/cmiot_osd_adapter_regression.py
git commit -m "feat: persist cmiot OSD independently"
```

### Task 6: Enforce local/cmiot OSD priority and boot restore

**Files:**
- Create: `tools/tests/cmiot_osd_priority_regression.py`
- Modify: `App/Media/AVManager.h:14-23`
- Modify: `App/Media/AVManager.cpp:60-150`
- Modify: `App/Main.cpp:1460-1580`

- [ ] **Step 1: Write the failing priority regression**

Create `tools/tests/cmiot_osd_priority_regression.py`:

```python
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
```

- [ ] **Step 2: Verify the missing priority logic fails**

Run: `python3 tools/tests/cmiot_osd_priority_regression.py`

Expected: FAIL with `AVManager must expose local OSD restore`.

- [ ] **Step 3: Centralize the existing local hardware application**

Add public `int ApplyLocalOsdConfig();` to `AVManager.h`. Implement it from the existing `VideoParamInit` calls:

```cpp
int CAVManager::ApplyLocalOsdConfig()
{
    int ret = gb_rkipc_osd_common_set(m_OSDTimeConf.font_size,
                                      m_OSDTimeConf.font_color_mode.c_str(),
                                      m_OSDTimeConf.font_color.c_str());
    if (ret != 0) {
        return ret;
    }
    ret = gb_rkipc_osd_time_set(m_OSDTimeConf.date_type,
                                m_OSDTimeConf.time_type,
                                m_OSDTimeConf.display_week_enabled,
                                m_OSDTimeConf.x, m_OSDTimeConf.y,
                                m_OSDTimeConf.show,
                                m_OSDTimeConf.alignment);
    if (ret != 0) {
        return ret;
    }
    for (int i = 0; i < OSD_TEXT_MAX; ++i) {
        const std::string text = hexToStr(m_OSDTextAllConf.osd_text[i].text);
        ret = gb_rkipc_osd_text_set(i, text.c_str(),
                                    m_OSDTextAllConf.osd_text[i].x,
                                    m_OSDTextAllConf.osd_text[i].y,
                                    m_OSDTextAllConf.osd_text[i].show,
                                    m_OSDTextAllConf.osd_text[i].alignment);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}
```

Call this once at the end of `VideoParamInit()` instead of duplicating the time/text loops.

- [ ] **Step 4: Cache local updates while suppressing their hardware refresh**

Include `ChinaMobile/CmiotOsdControl.h`. In both OSD callbacks, parse the new local config and test the override before any hardware call. Use the matching block in each callback:

```cpp
    if (cmiot_osd_is_override_active()) {
        AppInfo("cmiot OSD override active, cache local OSD without applying\n");
        m_OSDTimeConf = OSDTimeConfig;
        return;
    }
```

```cpp
    if (cmiot_osd_is_override_active()) {
        AppInfo("cmiot OSD override active, cache local text OSD without applying\n");
        m_OSDTextAllConf = OSDTextAllConfig;
        return;
    }
```

Outside the override branch, compare/apply against the old member cache exactly as today, then assign `m_OSDTimeConf` or `m_OSDTextAllConf` after the hardware call. Assigning before the comparison would make every text item look unchanged and is forbidden.

- [ ] **Step 5: Restore persisted cmiot OSD during CMIOT startup**

Include `ChinaMobile/CmiotOsdControl.h` in `Main.cpp`. After `VideoParamInit()` and `VideoInit()`, but before `cmiot_start()`, add inside the CMIOT branch:

```cpp
            const int osdRet = cmiot_osd_initialize();
            if (osdRet != 0) {
                AppErr("cmiot_osd_initialize failed ret=%d\n", osdRet);
            }
```

Do not call this in the GB28181 branch.

- [ ] **Step 6: Run priority and existing OSD regressions**

Run:

```bash
python3 tools/tests/cmiot_osd_priority_regression.py
python3 tools/tests/video_osd_todolist_regression.py
python3 tools/tests/gb_osd_protocol_compat_regression.py
python3 tools/tests/video_osd_auto_inverse_regression.py
```

Expected: all four print `PASS`.

- [ ] **Step 7: Commit priority and restore behavior**

```bash
git add App/Media/AVManager.h App/Media/AVManager.cpp App/Main.cpp \
  tools/tests/cmiot_osd_priority_regression.py
git commit -m "feat: prioritize persisted cmiot OSD"
```

### Task 7: Wire the SDK callback and build source

**Files:**
- Create: `tools/tests/cmiot_osd_callback_regression.py`
- Modify: `App/ChinaMobile/demo_callback.cpp:1-6,941-999`
- Modify: `App/CMakeLists.txt:163-165`

- [ ] **Step 1: Write the failing callback/build regression**

Create `tools/tests/cmiot_osd_callback_regression.py`:

```python
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
```

- [ ] **Step 2: Verify the callback test fails**

Run: `python3 tools/tests/cmiot_osd_callback_regression.py`

Expected: FAIL with `callback must include the adapter API`.

- [ ] **Step 3: Replace the print-only callback block**

Include `CmiotOsdControl.h`. Replace the full `CMIOT_CMD_SET_OSD` block with:

```cpp
        case CMIOT_CMD_SET_OSD:
        {
            if (input == NULL) {
                DEMO_PRINT("CMIOT_CMD_SET_OSD invalid input\n");
                return -1;
            }
            cmiotOSDInfo_t* info = static_cast<cmiotOSDInfo_t*>(input);
            const int ret = cmiot_osd_set_config(info);
            DEMO_PRINT("CMIOT_CMD_SET_OSD switch=%d mode=%u ret=%d\n",
                       info->osdSwitch, info->mode, ret);
            return ret;
        }
```

Add `ChinaMobile/CmiotOsdControl.cpp` to `CM_SRC` in CMake.

- [ ] **Step 4: Run callback and adapter regressions**

Run:

```bash
python3 tools/tests/cmiot_osd_callback_regression.py
python3 tools/tests/cmiot_osd_adapter_regression.py
```

Expected: both print `PASS`.

- [ ] **Step 5: Commit callback/build wiring**

```bash
git add App/ChinaMobile/demo_callback.cpp App/CMakeLists.txt \
  tools/tests/cmiot_osd_callback_regression.py
git commit -m "feat: handle cmiot OSD callback"
```

### Task 8: Document and verify the complete behavior

**Files:**
- Modify: `helloagents/wiki/modules/rk_media_pipeline.md:120-164`
- Modify: `helloagents/CHANGELOG.md`

- [ ] **Step 1: Update durable project knowledge**

Replace the old cmiot OSD note with the verified priority table:

```markdown
| 当前平台 | cmiot osdSwitch | 生效 OSD |
|---|---:|---|
| GB28181 | 任意 | `CFG_OSD_TIME` / `CFG_OSD_TEXT` |
| CMIOT | 0 | `CFG_OSD_TIME` / `CFG_OSD_TEXT` |
| CMIOT | 1 | 独立持久化的 `CFG_CMIOT_OSD` |
```

Document that `CFG_CLOUD_PLATFORM` is read at startup, defaults to GB28181, and platform changes require reboot. State that cmiot `mode=2` is an OSD layout mode, not the selected cloud platform. Record that cmiot coordinates remain `0-10000` until `NormalizedOsdControl` converts them at the RK boundary.

- [ ] **Step 2: Add a changelog entry**

Under the current unreleased section add:

```markdown
- cmiot OSD now persists independently from local/GB OSD, restores by platform and `osdSwitch` priority after reboot, and is applied from `CMIOT_CMD_SET_OSD` without `VideoOsdState` conversion.
- Added persisted single-platform startup selection; GB28181 is the safe default and CMIOT/GB services no longer start together.
```

- [ ] **Step 3: Run all focused regressions**

Run:

```bash
python3 tools/tests/cloud_platform_config_regression.py
python3 tools/tests/cloud_platform_startup_regression.py
python3 tools/tests/normalized_osd_boundary_regression.py
python3 tools/tests/cmiot_osd_adapter_regression.py
python3 tools/tests/cmiot_osd_priority_regression.py
python3 tools/tests/cmiot_osd_callback_regression.py
python3 tools/tests/video_osd_todolist_regression.py
python3 tools/tests/gb_osd_protocol_compat_regression.py
python3 tools/tests/video_osd_auto_inverse_regression.py
```

Expected: 9 `PASS` results and no traceback.

- [ ] **Step 4: Run whitespace and repository checks**

Run:

```bash
git diff --check
git status --short
```

Expected: `git diff --check` has no output; status lists only intentional documentation changes before commit.

- [ ] **Step 5: Attempt the RV1106 application build**

If `Lib/Device/ChinaMobile/libandmu_sdk.a` and the other cmiot SDK archives are available, run:

```bash
./build.sh app
```

Expected: App, daemon, and burntool build successfully with no undefined `cmiot_osd_*` symbols.

If the archives are absent, run `test -f Lib/Device/ChinaMobile/libandmu_sdk.a` to record the exact missing prerequisite and report the build as blocked by unavailable vendor binaries; do not claim compilation passed.

- [ ] **Step 6: Commit documentation**

```bash
git add helloagents/wiki/modules/rk_media_pipeline.md helloagents/CHANGELOG.md
git commit -m "docs: record cmiot OSD priority"
```

- [ ] **Step 7: Review the complete change set**

Run:

```bash
git log --oneline 87c6133..HEAD
git diff --stat 87c6133..HEAD
git diff --check 87c6133..HEAD
```

Expected: focused commits for test migration, platform config, startup routing, normalized media boundary, persistent cmiot adapter, OSD priority, callback wiring, and docs; no whitespace errors.

## Device acceptance checklist

The host regressions and build do not replace board verification. On an RV1106 target:

- [ ] Set platform to GB28181, reboot, and confirm only GB services start and historical cmiot OSD is ignored.
- [ ] Set platform to CMIOT, reboot with saved `osdSwitch=1`, and confirm the saved cmiot date/text OSD is restored before a new SDK command.
- [ ] While cmiot override is active, change local OSD and confirm the JSON changes but the picture remains cmiot-controlled.
- [ ] Send `osdSwitch=0` and confirm the latest local time plus all 7 local text slots are restored.
- [ ] Reboot with cmiot OSD disabled and confirm local OSD remains active.
- [ ] Exercise custom and cmiot `mode=2` layouts, including left/right alignment and five total text rows.
- [ ] Send null, invalid mode, coordinate `10001`, and six text rows; confirm failure is returned and the previous valid OSD remains visible.
