#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FONT_FACTORY_H = (ROOT / "Middleware/libmpp/rkipc/common/osd/font_factory.h").read_text(
    encoding="utf-8-sig", errors="ignore"
)
FONT_FACTORY_C = (ROOT / "Middleware/libmpp/rkipc/common/osd/font_factory.c").read_text(
    encoding="utf-8-sig", errors="ignore"
)
RK_VIDEO_C = (
    ROOT / "Middleware/libmpp/rkipc/src/rv1106_dual_ipc/video/video.c"
).read_text(encoding="utf-8-sig", errors="ignore")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require(
        "draw_argb8888_text_with_color_callback" in FONT_FACTORY_H,
        "font_factory should expose ARGB8888 text drawing with per-character color callback.",
    )
    require(
        "font_color_judge_cb" in FONT_FACTORY_H,
        "font_factory callback type should describe per-character color decisions.",
    )
    require(
        "draw_argb8888_text_with_color_callback" in FONT_FACTORY_C,
        "font_factory should implement the ARGB8888 callback drawing path.",
    )
    require(
        "font_color_to_bgra" in FONT_FACTORY_C,
        "font_factory should convert selected RGB colors to the existing BGRA buffer format.",
    )
    require(
        "RK_MPI_VI_GetChnFrame" in RK_VIDEO_C and "RK_MPI_VI_ReleaseChnFrame" in RK_VIDEO_C,
        "RV1106 OSD auto mode should sample a VI NV12 frame and release it.",
    )
    require(
        "rkipc_osd_auto_color_update" in RK_VIDEO_C,
        "RV1106 OSD layer should update a luminance map for auto black/white mode.",
    )
    require(
        "rkipc_osd_auto_color_judge" in RK_VIDEO_C,
        "RV1106 OSD layer should judge per-character black/white color from the luminance map.",
    )
    require(
        "draw_argb8888_text_with_color_callback" in RK_VIDEO_C,
        "RV1106 OSD bitmap refresh should use the ARGB8888 per-character callback in auto mode.",
    )
    require(
        'color = 0xffffff' not in RK_VIDEO_C,
        "font_color_mode=auto should not be implemented as fixed white.",
    )

    print("PASS: RV1106 OSD auto color uses NV12 luminance and ARGB8888 per-character callback")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
