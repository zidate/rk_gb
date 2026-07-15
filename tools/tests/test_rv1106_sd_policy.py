"""Verify the RV1106 U-Boot SD update policy before any flash I/O."""

import hashlib
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH_DIR = ROOT / "vendor" / "rv1106_sdk_patches"
DUAL_BASELINE = pathlib.Path(
    os.environ.get("RV1106_SDK_BASELINE", "/tmp/rk_dual_backup_ref/RV1106_IPC_SDK")
)
PLAN_BASELINE = pathlib.Path("/tmp/rk_plan_sdk/RV1106_IPC_SDK")
UBOOT = "sysdrv/source/uboot/u-boot"
BOARD_CONFIG = (
    "project/cfg/BoardConfig_IPC/"
    "BoardConfig-SPI_NAND-NONE-RV1106_IPC38_DEMO_V10-IPC-XWR60440.mk"
)
DEFCONFIG = f"{UBOOT}/configs/rv1106-XWR60440_defconfig"
ANDROID_AB = f"{UBOOT}/common/android_ab.c"
AVB_AB_FLOW = f"{UBOOT}/lib/avb/libavb_ab/avb_ab_flow.c"
SPINAND_HEADER = f"{UBOOT}/include/linux/mtd/spinand.h"
SPINAND_CORE = f"{UBOOT}/drivers/mtd/nand/spi/core.c"
CHUCUN = f"{UBOOT}/drivers/mtd/nand/spi/chucun.c"
COMMAND = f"{UBOOT}/cmd/ab_sd_update.c"
CMD_KCONFIG = f"{UBOOT}/cmd/Kconfig"
CMD_MAKEFILE = f"{UBOOT}/cmd/Makefile"

BASELINE_INPUTS = {
    BOARD_CONFIG: (DUAL_BASELINE, "05a466237c9c0f8dc9a9769efb291e5ddb8c80a29e0066347806350ef81ed8d2"),
    DEFCONFIG: (DUAL_BASELINE, "e762bccd96ebd1be962fe845f5da24471c2301e4abc73ba0d236c7580bfea697"),
    ANDROID_AB: (DUAL_BASELINE, "95a848a050e0e85948f8ae5fc8f5a169c82aee101920e6dc77adca51f9675d73"),
    AVB_AB_FLOW: (DUAL_BASELINE, "581520340c3b06169755b3192a74d339738524119af9c61cbfd71b479b7a48f1"),
    SPINAND_HEADER: (DUAL_BASELINE, "69a6b6146b4f0fa2a46e9fadb9a4e0c264d176ba22be7216469a5637fbbd6066"),
    SPINAND_CORE: (DUAL_BASELINE, "fa8ddbed4500fb0b4191cfa76f85d828bccd97a8c4496482ea8d47af9187380a"),
    CHUCUN: (DUAL_BASELINE, "649994b0727e29cbec1af1b0e21a1aab1f5df11257d3fc5586db9f1f19e52a3f"),
    CMD_KCONFIG: (PLAN_BASELINE, "344a72e300d59409b145ef8bb2fe698997fbf6eebb7b21d099d85d3301cd2a0f"),
    CMD_MAKEFILE: (PLAN_BASELINE, "c3550590185aa6591614318560c012b5bbdcc172048dcd56422e38b1e955f074"),
}
POST_SHA256 = {
    COMMAND: "091aabbdfd2ac7fb0bf6e3d17b9ff903c5e13c234accb4fbdf6ac81ab9fa80ee",
    CMD_KCONFIG: "835bf2ebc533dc5c4c359441d566076b1c85023c268811c3f41b9476a669ed9f",
    CMD_MAKEFILE: "89ed26b3e1eaea7914970174262cf0887f2a586ef473ad886cc58f0da7234d14",
    DEFCONFIG: "c239063d8188eec1126d0b322443c33bfd6ff564d9055c3af7c7a6dd0433d1e8",
}
PATCH_TARGETS = [
    SPINAND_HEADER,
    SPINAND_CORE,
    CHUCUN,
    COMMAND,
    CMD_KCONFIG,
    CMD_MAKEFILE,
    DEFCONFIG,
]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_git_patch(patch_text):
    changes = {}
    target = None
    old_header = None
    new_header = None
    in_hunk = False

    def validate_headers():
        if target is None:
            return
        expected_old = target[0]
        if old_header == "/dev/null":
            expected_old = "/dev/null"
        if (old_header, new_header) != (expected_old, target[1]):
            raise ValueError(f"file headers do not match diff target: {target}")

    for line in patch_text.splitlines():
        if line.startswith("diff --git "):
            validate_headers()
            paths = shlex.split(line.removeprefix("diff --git "))
            if len(paths) != 2:
                raise ValueError(f"malformed diff target: {line}")
            target = tuple(paths)
            if target in changes:
                raise ValueError(f"duplicate diff target: {line}")
            changes[target] = {"added": [], "deleted": []}
            old_header = None
            new_header = None
            in_hunk = False
        elif not in_hunk and line.startswith("--- "):
            paths = shlex.split(line.removeprefix("--- "))
            if target is None or len(paths) != 1:
                raise ValueError(f"malformed old file header: {line}")
            old_header = paths[0]
        elif not in_hunk and line.startswith("+++ "):
            paths = shlex.split(line.removeprefix("+++ "))
            if target is None or old_header is None or len(paths) != 1:
                raise ValueError(f"malformed new file header: {line}")
            new_header = paths[0]
        elif line.startswith("@@"):
            validate_headers()
            in_hunk = True
        elif in_hunk and line.startswith("+"):
            changes[target]["added"].append(line[1:])
        elif in_hunk and line.startswith("-"):
            changes[target]["deleted"].append(line[1:])

    validate_headers()
    return changes


class SdPolicyPatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tempdir = tempfile.TemporaryDirectory()
        cls.sdk_root = pathlib.Path(cls.tempdir.name)
        for relative, (baseline, expected_hash) in BASELINE_INPUTS.items():
            source = baseline / relative
            if sha256(source) != expected_hash:
                raise AssertionError(f"baseline hash mismatch: {source}")
            destination = cls.sdk_root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)

        for patch_name in ("0001-ab-layout.patch", "0002-uboot-sd-protection.patch"):
            result = subprocess.run(
                [
                    "patch",
                    "-p1",
                    "--fuzz=0",
                    "--batch",
                    "-i",
                    str(PATCH_DIR / patch_name),
                ],
                cwd=cls.sdk_root,
                capture_output=True,
                text=True,
            )
            output = result.stdout + result.stderr
            if result.returncode != 0:
                raise AssertionError(f"{patch_name} failed to apply:\n{output}")
            if re.search(r"\b(?:offset|fuzz)\b", output, re.IGNORECASE):
                raise AssertionError(f"{patch_name} applied non-exactly:\n{output}")

        cls.patch_text = (PATCH_DIR / "0002-uboot-sd-protection.patch").read_text(
            encoding="utf-8"
        )
        cls.changes = parse_git_patch(cls.patch_text)
        cls.command = (
            (cls.sdk_root / COMMAND).read_text(encoding="utf-8")
            if (cls.sdk_root / COMMAND).is_file()
            else None
        )

    @classmethod
    def tearDownClass(cls):
        cls.tempdir.cleanup()

    def test_0002_has_exact_seven_targets_and_valid_new_file_header(self):
        self.assertEqual(
            list(self.changes),
            [(f"a/{path}", f"b/{path}") for path in PATCH_TARGETS],
        )
        command_change = self.changes[(f"a/{COMMAND}", f"b/{COMMAND}")]
        self.assertEqual(command_change["deleted"], [])

    def test_postimages_match_pinned_hashes(self):
        for relative, expected_hash in POST_SHA256.items():
            self.assertNotEqual(expected_hash, "TODO")
            self.assertEqual(sha256(self.sdk_root / relative), expected_hash, relative)

    def test_all_sixteen_masks_switch_only_with_complete_slot_trio(self):
        self.assertIsNotNone(self.command)
        bits = {
            name: int(bit)
            for name, bit in re.findall(
                r"#define AB_SD_(BOOT|ROOTFS|OEM|UBOOT) BIT\((\d)\)",
                self.command,
            )
        }
        self.assertEqual(bits, {"BOOT": 0, "ROOTFS": 1, "OEM": 2, "UBOOT": 3})
        self.assertIn(
            "return (present & AB_SD_SLOT_TRIO) == AB_SD_SLOT_TRIO;",
            self.command,
        )
        trio = 0x7
        for present in range(16):
            expected = present in (trio, trio | 0x8)
            actual = (present & trio) == trio
            with self.subTest(present=present):
                self.assertEqual(actual, expected)

    def test_fixed_table_has_exact_files_bits_and_overflow_safe_maxima(self):
        self.assertIn("#define AB_SD_MIB(n) ((loff_t)(n) * 1024 * 1024)", self.command)
        expected_rows = (
            '{ "uboot.img", "uboot", AB_SD_UBOOT, AB_SD_MIB(1), false },',
            '{ "boot.img", "boot", AB_SD_BOOT, AB_SD_MIB(4), true },',
            '{ "rootfs.img", "rootfs", AB_SD_ROOTFS, AB_SD_MIB(10), true },',
            '{ "oem.img", "oem", AB_SD_OEM, AB_SD_MIB(32), true },',
        )
        for row in expected_rows:
            self.assertIn(row, self.command)

    def test_sd_scan_rejects_errors_empty_and_oversize_but_allows_absence(self):
        for snippet in (
            'fs_set_blk_dev("mmc", "1", FS_TYPE_FAT)',
            "exists = fs_exists(image->filename);",
            "if (exists < 0)",
            "if (!exists)",
            "ret = fs_size(image->filename, &size);",
            "if (ret || size <= 0 || size > image->max_size)",
        ):
            self.assertIn(snippet, self.command)

    def test_runtime_slot_mapping_is_bounded_and_defaults_invalid_to_a(self):
        for snippet in (
            "ab_get_slot_suffix(current_suffix)",
            'strcmp(current_suffix, "_a")',
            'strcmp(current_suffix, "_b")',
            'strlcpy(target_suffix, "_b", target_len)',
            'strlcpy(target_suffix, "_a", target_len)',
            'snprintf(target, target_len, "%s%s", image->partition, target_suffix)',
        ):
            self.assertIn(snippet, self.command)

    def test_command_is_wired_without_flash_io_or_slot_activation(self):
        self.assertIn(
            "U_BOOT_CMD(ab_sd_update, 1, 1, do_ab_sd_update,", self.command
        )
        self.assertIn(
            "config CMD_AB_SD_UPDATE",
            (self.sdk_root / CMD_KCONFIG).read_text(encoding="utf-8"),
        )
        self.assertIn(
            "obj-$(CONFIG_CMD_AB_SD_UPDATE) += ab_sd_update.o",
            (self.sdk_root / CMD_MAKEFILE).read_text(encoding="utf-8"),
        )
        defconfig = (self.sdk_root / DEFCONFIG).read_text(encoding="utf-8")
        self.assertIn("CONFIG_CMD_AB_SD_UPDATE=y", defconfig)
        self.assertIn('CONFIG_ROCKCHIP_CMD="ab_sd_update -"', defconfig)
        self.assertIn("# CONFIG_CMD_SCRIPT_UPDATE is not set", defconfig)
        forbidden = (
            "spinand_set_block_lock",
            "mtd_write",
            "mtd_erase",
            "avb_ab_mark_slot_active",
            "sd_update.txt",
            "DG_sdupdate",
        )
        for symbol in forbidden:
            self.assertNotIn(symbol.casefold(), self.command.casefold())


if __name__ == "__main__":
    unittest.main()
