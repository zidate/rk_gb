import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH_DIR = ROOT / "vendor" / "rv1106_sdk_patches"


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


if __name__ == "__main__":
    unittest.main()
