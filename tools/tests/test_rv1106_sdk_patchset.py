import pathlib
import shlex
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH_DIR = ROOT / "vendor" / "rv1106_sdk_patches"
SOURCE_DIR = PATCH_DIR / "source"
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
            "0005-uboot-mtdparts-unit-suffix.patch",
            "0006-uboot-sd-ubi-init.patch",
            "0007-rk-ota-ubi-init.patch",
            "0008-uboot-env-mtdparts-prefix.patch",
            "0009-uboot-sd-fixed-raw-offset.patch",
            "0010-uboot-sd-oem-switch.patch",
        ]
        series = (PATCH_DIR / "series").read_text().splitlines()
        self.assertEqual(series, expected)

    def test_uboot_mtdparts_parser_accepts_binary_unit_suffixes(self):
        patch = (PATCH_DIR / "0005-uboot-mtdparts-unit-suffix.patch").read_text()

        self.assertIn("drivers/mtd/mtdpart.c", patch)
        self.assertNotIn("project/build.sh", patch)
        for suffix in ("'K'", "'k'", "'M'", "'m'", "'G'", "'g'"):
            self.assertIn(suffix, patch)
        self.assertIn(
            "u64 value = simple_strtoull(ptr, (char **)retptr, 0);", patch
        )
        self.assertNotIn(
            "+\tu64 value = ustrtoull(ptr, (char **)retptr, 0);", patch
        )
        self.assertEqual(patch.count("mtd_memsize_parse(mtdparts"), 2)
        self.assertNotIn(
            '+RK_PARTITION_ARGS="mtdparts=spi-nand0:$GLOBAL_PARTITIONS"', patch
        )

    def test_spi_nand_env_uses_legacy_mtdparts_value_prefix(self):
        patch = (PATCH_DIR / "0008-uboot-env-mtdparts-prefix.patch").read_text()

        self.assertIn("project/build.sh", patch)
        self.assertIn(
            'RK_PARTITION_ARGS="mtdparts=mtdparts=spi-nand0:$RK_PARTITION_CMD_IN_ENV"',
            patch,
        )
        self.assertIn("sysdrv/Makefile", patch)
        self.assertIn(
            'mtdparts=mtdparts=spi-nand0:$(CONFIG_SYSDRV_PARTITION)', patch
        )
        self.assertNotIn(
            '+\t@echo "mtdparts=rk-nand:$(CONFIG_SYSDRV_PARTITION)"', patch
        )

    def test_sd_fixed_images_use_bounded_raw_master_ranges(self):
        patch = (PATCH_DIR / "0009-uboot-sd-fixed-raw-offset.patch").read_text()

        self.assertIn('mtd_name = image->slot_scoped ? target : "spi-nand0";', patch)
        self.assertIn("range_start = image->slot_scoped ? 0 : image->raw_offset;", patch)
        self.assertIn("range_size > mtd->size - range_start", patch)
        self.assertIn("IS_ALIGNED(range_start, mtd->erasesize)", patch)
        self.assertIn("offset < range_end", patch)
        self.assertIn("memalign(ARCH_DMA_MINALIGN", patch)

    def test_sd_oem_image_is_the_slot_switch_trigger(self):
        patch = (PATCH_DIR / "0010-uboot-sd-oem-switch.patch").read_text()

        self.assertIn("#define AB_SD_SLOT_SWITCH_TRIGGER AB_SD_OEM", patch)
        self.assertIn(
            "return (present & AB_SD_SLOT_SWITCH_TRIGGER) != 0;", patch
        )
        self.assertIn('"contains oem"', patch)

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

    def test_complete_source_snapshots_cover_all_patch_targets(self):
        patch_targets = set()
        for patch_path in sorted(PATCH_DIR.glob("000*.patch")):
            for path in patch_diff_paths(patch_path.read_text()):
                prefix, separator, relative = path.partition("/")
                if separator and prefix == "b":
                    patch_targets.add(relative)

        snapshots = {
            path.relative_to(SOURCE_DIR).as_posix()
            for path in SOURCE_DIR.rglob("*")
            if path.is_file()
        }
        self.assertEqual(snapshots, patch_targets)

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
