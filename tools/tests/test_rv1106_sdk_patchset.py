import pathlib
import shlex
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH_DIR = ROOT / "vendor" / "rv1106_sdk_patches"
FORBIDDEN_PRODUCT_PATH = "driver/uboot"


def patch_diff_paths(patch_text):
    for line in patch_text.splitlines():
        if line.startswith("diff --git "):
            yield from shlex.split(line.removeprefix("diff --git "))[:2]


def patch_touches_forbidden_product_path(patch_text):
    for path in patch_diff_paths(patch_text):
        _, separator, normalized_path = path.partition("/")
        if not separator:
            continue
        if normalized_path == FORBIDDEN_PRODUCT_PATH:
            return True
        if normalized_path.startswith(f"{FORBIDDEN_PRODUCT_PATH}/"):
            return True
    return False


class PatchsetTest(unittest.TestCase):
    def test_series_is_complete_and_ordered(self):
        expected = [
            "0001-ab-layout.patch",
            "0002-uboot-sd-protection.patch",
            "0003-kernel-protection.patch",
            "0004-rk-ota.patch",
        ]
        series = (PATCH_DIR / "series").read_text().splitlines()
        self.assertEqual(series, expected)

    def test_apply_script_never_touches_product_driver_uboot(self):
        script = (PATCH_DIR / "apply.sh").read_text()
        self.assertNotIn("driver/uboot", script)
        self.assertIn("patch -d", script)

    def test_patch_path_detector_catches_only_forbidden_diff_paths(self):
        forbidden_patch = """\
diff --git a/driver/uboot/Makefile b/driver/uboot/Makefile
--- a/driver/uboot/Makefile
+++ b/driver/uboot/Makefile
"""
        safe_patch = """\
diff --git a/project/Makefile b/project/Makefile
--- a/project/Makefile
+++ b/project/Makefile
@@ -1 +1 @@
+documentation mentions driver/uboot but does not modify it
"""
        self.assertTrue(patch_touches_forbidden_product_path(forbidden_patch))
        self.assertFalse(patch_touches_forbidden_product_path(safe_patch))

    def test_patch_path_detector_strips_any_patch_p1_prefix(self):
        patch_text = """\
diff --git x/driver/uboot/Makefile y/driver/uboot/Makefile
"""
        self.assertTrue(patch_touches_forbidden_product_path(patch_text))

    def test_patch_path_detector_handles_git_quoted_paths(self):
        patch_text = """\
diff --git "a/driver/uboot/file name" "b/driver/uboot/file name"
"""
        self.assertTrue(patch_touches_forbidden_product_path(patch_text))

    def test_patch_path_detector_ignores_hunk_body_markers(self):
        patch_text = """\
diff --git a/project/Makefile b/project/Makefile
@@ -1 +1 @@
--- driver/uboot/not-a-file-header
+++ replacement text
"""
        self.assertFalse(patch_touches_forbidden_product_path(patch_text))

    def test_patch_payloads_never_touch_product_driver_uboot(self):
        for patch_path in sorted(PATCH_DIR.glob("000*.patch")):
            with self.subTest(patch=patch_path.name):
                self.assertFalse(
                    patch_touches_forbidden_product_path(patch_path.read_text()),
                    f"{patch_path.name} modifies {FORBIDDEN_PRODUCT_PATH}",
                )

    def test_apply_script_handles_unterminated_series_line(self):
        script = (PATCH_DIR / "apply.sh").read_text()
        self.assertIn(
            'while IFS= read -r patch_name || [ -n "$patch_name" ]; do',
            script,
        )

    def test_apply_script_uses_option_safe_realpath(self):
        script = (PATCH_DIR / "apply.sh").read_text()
        self.assertIn('sdk_root=$(realpath -- "$1")', script)

    def test_apply_script_reports_missing_build_script(self):
        with tempfile.TemporaryDirectory() as sdk_root:
            result = subprocess.run(
                [PATCH_DIR / "apply.sh", sdk_root],
                capture_output=True,
                text=True,
            )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("project/build.sh", result.stderr)


if __name__ == "__main__":
    unittest.main()
