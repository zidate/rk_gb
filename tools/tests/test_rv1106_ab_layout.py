"""Lock the RV1106 XWR60440 SPI NAND A/B partition layout."""

import configparser
import pathlib
import re
import shlex
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
IMAGE_DIR = ROOT / "packaging" / "image"
PATCH_PATH = ROOT / "vendor" / "rv1106_sdk_patches" / "0001-ab-layout.patch"
LINKMOUNT_PATH = ROOT / "packaging" / "rootfs_pub" / "etc" / "init.d" / "S20linkmount"
ALIGNMENT = 0x20000
FLASH_SIZE = 0x8000000
EXPECTED = {
    "env": (0x0000000, 0x0040000),
    "idblock": (0x0040000, 0x0100000),
    "uboot": (0x0140000, 0x0100000),
    "boot_a": (0x0240000, 0x0400000),
    "boot_b": (0x0640000, 0x0400000),
    "rootfs_a": (0x0A40000, 0x0A00000),
    "rootfs_b": (0x1440000, 0x0A00000),
    "oem_a": (0x1E40000, 0x2000000),
    "oem_b": (0x3E40000, 0x2000000),
    "reserved": (0x5E40000, 0x01C0000),
    "misc": (0x6000000, 0x0040000),
    "userdata": (0x6040000, 0x1FC0000),
}
FACTORY_IMAGES = {
    "env": "env.img",
    "idblock": "idblock.img",
    "uboot": "uboot.img",
    "boot_a": "boot.img",
    "rootfs_a": "rootfs.img",
    "oem_a": "oem.img",
    "userdata": "userdata.img",
}
PARTITION_COMMAND = (
    'export RK_PARTITION_CMD_IN_ENV="256K(env),1M@256K(idblock),1M(uboot),'
    '4M(boot_a),4M(boot_b),10M(rootfs_a),10M(rootfs_b),32M(oem_a),'
    '32M(oem_b),1792K(reserved),256K(misc),-(userdata)"'
)
FILESYSTEM_CONFIG = (
    "export RK_PARTITION_FS_TYPE_CFG="
    "rootfs_a@IGNORE@squashfs,oem_a@/oem@squashfs,userdata@/userdata@ubifs"
)
OLD_PARTITION_COMMAND = (
    'export RK_PARTITION_CMD_IN_ENV="256K(env),1M@256K(idblock),1M(uboot),'
    '4M(boot),32M(rootfs),48M(oem),-(userdata)"'
)
OLD_FILESYSTEM_CONFIG = (
    "export RK_PARTITION_FS_TYPE_CFG="
    "rootfs@IGNORE@squashfs,oem@/oem@squashfs,userdata@/userdata@ubifs"
)
BOARD_CONFIG_RELATIVE_PATH = (
    "project/cfg/BoardConfig_IPC/"
    "BoardConfig-SPI_NAND-NONE-RV1106_IPC38_DEMO_V10-IPC-XWR60440.mk"
)
DEFCONFIG_RELATIVE_PATH = (
    "sysdrv/source/uboot/u-boot/configs/rv1106-XWR60440_defconfig"
)
ANDROID_AB_RELATIVE_PATH = "sysdrv/source/uboot/u-boot/common/android_ab.c"
AVB_AB_FLOW_RELATIVE_PATH = (
    "sysdrv/source/uboot/u-boot/lib/avb/libavb_ab/avb_ab_flow.c"
)
PATCH_TARGETS = [
    BOARD_CONFIG_RELATIVE_PATH,
    DEFCONFIG_RELATIVE_PATH,
    ANDROID_AB_RELATIVE_PATH,
    AVB_AB_FLOW_RELATIVE_PATH,
]


def read_ini(filename):
    config = configparser.ConfigParser(interpolation=None)
    with (IMAGE_DIR / filename).open(encoding="utf-8") as stream:
        config.read_file(stream)
    return config


def makefile_references_upgrade_ini(makefile_text):
    active_lines = [
        line.strip()
        for line in makefile_text.splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    return any("upgrade.ini" in line for line in active_lines)


def parse_git_patch(patch_text):
    changes = {}
    current_target = None
    in_hunk = False

    for line in patch_text.splitlines():
        if line.startswith("diff --git "):
            paths = shlex.split(line.removeprefix("diff --git "))
            if len(paths) != 2:
                raise ValueError(f"malformed diff header: {line}")
            current_target = tuple(paths)
            if current_target in changes:
                raise ValueError(f"duplicate diff target: {line}")
            changes[current_target] = {"added": [], "deleted": []}
            in_hunk = False
        elif line.startswith("@@"):
            if current_target is None:
                raise ValueError("hunk found before diff header")
            in_hunk = True
        elif in_hunk and line.startswith("+"):
            changes[current_target]["added"].append(line[1:])
        elif in_hunk and line.startswith("-"):
            changes[current_target]["deleted"].append(line[1:])

    return changes


def target_changes(changes, relative_path):
    target = (f"a/{relative_path}", f"b/{relative_path}")
    if target not in changes:
        raise AssertionError(f"missing patch target: {relative_path}")
    return changes[target]


def stripped(lines):
    return [line.strip() for line in lines]


class FullPhysicalLayoutTest(unittest.TestCase):
    def assert_full_layout(self, filename):
        config = read_ini(filename)
        self.assertEqual(config.sections(), ["global", *EXPECTED])
        self.assertEqual(int(config["global"]["flash_size"], 0), FLASH_SIZE)
        self.assertEqual(int(config["global"]["sector_size"], 0), ALIGNMENT)

        cursor = 0
        image_mappings = {}
        for name, (expected_start, expected_size) in EXPECTED.items():
            start = int(config[name]["start"], 0)
            size = int(config[name]["size"], 0)
            self.assertEqual((start, size), (expected_start, expected_size), name)
            self.assertEqual(start % ALIGNMENT, 0, name)
            self.assertEqual(size % ALIGNMENT, 0, name)
            self.assertEqual(start, cursor, name)
            cursor = start + size

            image_file = config[name].get("file", "").strip()
            if image_file:
                image_mappings[name] = image_file

        self.assertEqual(cursor, FLASH_SIZE)
        self.assertEqual(image_mappings, FACTORY_IMAGES)

    def test_partition_ini_locks_full_physical_layout(self):
        self.assert_full_layout("partition.ini")

    def test_merge_ini_locks_full_physical_layout(self):
        self.assert_full_layout("merge.ini")


class DeprecatedUpgradeInputTest(unittest.TestCase):
    def test_upgrade_ini_contains_only_generic_a_slot_payloads(self):
        config = read_ini("upgrade.ini")
        self.assertEqual(config.sections(), ["global", "boot", "rootfs", "oem"])
        self.assertEqual(int(config["global"]["flash_size"], 0), FLASH_SIZE)
        self.assertEqual(int(config["global"]["sector_size"], 0), ALIGNMENT)

        expected = {
            "boot": (0x0240000, 0x0400000, "boot.img"),
            "rootfs": (0x0A40000, 0x0A00000, "rootfs.img"),
            "oem": (0x1E40000, 0x2000000, "oem.img"),
        }
        actual = {
            name: (
                int(config[name]["start"], 0),
                int(config[name]["size"], 0),
                config[name]["file"],
            )
            for name in expected
        }
        self.assertEqual(actual, expected)

    def test_makefile_does_not_actively_package_deprecated_upgrade_input(self):
        self.assertFalse(
            makefile_references_upgrade_ini(
                (IMAGE_DIR / "Makefile").read_text(encoding="utf-8")
            )
        )

    def test_makefile_detector_rejects_any_active_upgrade_ini_command(self):
        makefile_text = """\
# alternate-tool --input ./upgrade.ini
\talternate-tool   --input=./upgrade.ini
"""
        self.assertTrue(makefile_references_upgrade_ini(makefile_text))

    def test_makefile_declares_all_and_clean_phony(self):
        makefile_lines = (IMAGE_DIR / "Makefile").read_text(encoding="utf-8").splitlines()
        self.assertIn(".PHONY: all clean", makefile_lines)

    def test_makefile_is_not_executable(self):
        self.assertEqual((IMAGE_DIR / "Makefile").stat().st_mode & 0o111, 0)


class BoardConfigPatchTest(unittest.TestCase):
    def assert_expected_layout_changes(self, added_lines, deleted_lines):
        self.assertEqual(added_lines, [PARTITION_COMMAND, FILESYSTEM_CONFIG])
        self.assertEqual(
            deleted_lines,
            [OLD_PARTITION_COMMAND, OLD_FILESYSTEM_CONFIG],
        )

    def test_patch_locks_partition_command_and_a_slot_build_inputs(self):
        self.assertTrue(PATCH_PATH.is_file(), f"missing patch: {PATCH_PATH}")
        patch_text = PATCH_PATH.read_text(encoding="utf-8")
        changes = parse_git_patch(patch_text)

        self.assertEqual(
            list(changes),
            [
                (f"a/{path}", f"b/{path}")
                for path in PATCH_TARGETS
            ],
        )
        board_changes = target_changes(changes, BOARD_CONFIG_RELATIVE_PATH)
        self.assert_expected_layout_changes(
            board_changes["added"], board_changes["deleted"]
        )

    def test_layout_change_guard_rejects_unrelated_exports(self):
        expected_added = [PARTITION_COMMAND, FILESYSTEM_CONFIG]
        expected_deleted = [OLD_PARTITION_COMMAND, OLD_FILESYSTEM_CONFIG]

        for added_lines, deleted_lines in (
            ([*expected_added, "export UNRELATED=new"], expected_deleted),
            (expected_added, [*expected_deleted, "export UNRELATED=old"]),
        ):
            with self.subTest(added=added_lines, deleted=deleted_lines):
                with self.assertRaises(AssertionError):
                    self.assert_expected_layout_changes(added_lines, deleted_lines)

    def test_git_patch_parser_ignores_misleading_comments_and_context(self):
        patch_text = f"""\
# claims to add: +{PARTITION_COMMAND}
diff --git a/project/example.mk b/project/example.mk
--- a/project/example.mk
+++ b/project/example.mk
@@ -1,2 +1,2 @@
 {PARTITION_COMMAND}
-export EXAMPLE=old
+export EXAMPLE=new
"""
        changes = parse_git_patch(patch_text)

        self.assertEqual(
            changes,
            {
                ("a/project/example.mk", "b/project/example.mk"): {
                    "added": ["export EXAMPLE=new"],
                    "deleted": ["export EXAMPLE=old"],
                }
            },
        )


class BootSelectionPatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.changes = parse_git_patch(PATCH_PATH.read_text(encoding="utf-8"))

    def test_libavb_invalid_metadata_defaults_to_a_only(self):
        change = target_changes(self.changes, AVB_AB_FLOW_RELATIVE_PATH)
        added = stripped(change["added"])
        deleted = stripped(change["deleted"])

        self.assertIn("data->slots[1].priority = 0;", added)
        self.assertIn("data->slots[1].tries_remaining = 0;", added)
        self.assertNotIn("data->slots[1].successful_boot = 0;", deleted)
        self.assertIn("data->slots[1].priority = AVB_AB_MAX_PRIORITY - 1;", deleted)
        self.assertIn(
            "data->slots[1].tries_remaining = AVB_AB_MAX_TRIES_REMAINING;",
            deleted,
        )
        self.assertFalse(any("slots[0]" in line for line in change["deleted"]))

    def test_android_invalid_metadata_defaults_to_a_only(self):
        change = target_changes(self.changes, ANDROID_AB_RELATIVE_PATH)
        added = stripped(change["added"])
        deleted = stripped(change["deleted"])

        self.assertIn("memset(abc->slot_info, 0, sizeof(abc->slot_info));", added)
        self.assertIn("abc->slot_info[0].priority = 15;", added)
        self.assertIn("abc->slot_info[0].tries_remaining = 7;", added)
        self.assertIn("abc->slot_info[0].successful_boot = 0;", added)
        self.assertTrue(any("android_slot_metadata metadata" in line for line in deleted))
        self.assertTrue(any("for (i = 0;" in line for line in deleted))
        self.assertFalse(any("android_boot_control_compute_crc" in line for line in added))
        self.assertFalse(any("android_boot_control_compute_crc" in line for line in deleted))

    def test_selected_slot_updates_bootarg_and_rootfs_partition_together(self):
        change = target_changes(self.changes, ANDROID_AB_RELATIVE_PATH)
        added = stripped(change["added"])

        self.assertIn("char slot_suffix[3] = {0};", added)
        self.assertIn("if (ab_get_slot_suffix(slot_suffix)) {", added)
        self.assertIn(
            'snprintf(slot_arg, sizeof(slot_arg), "androidboot.slot_suffix=%s", slot_suffix);',
            added,
        )
        self.assertEqual(added.count('env_update("bootargs", slot_arg);'), 1)
        self.assertIn(
            'snprintf(root_part_name, sizeof(root_part_name), "rootfs%s", slot_suffix);',
            added,
        )
        self.assertTrue(
            any(
                "part_get_info_by_name(dev_desc, root_part_name" in line
                for line in added
            )
        )
        self.assertFalse(any("system_a" in line or "system_b" in line for line in added))
        self.assertFalse(any("ANDROID_PARTITION_SYSTEM" in line for line in added))
        self.assertFalse(
            any(
                "ubi.mtd=%d root=/dev/ubiblock0_0" in line
                for line in change["deleted"]
            )
        )

    def test_defconfig_enables_ab_and_disables_legacy_update_command(self):
        change = target_changes(self.changes, DEFCONFIG_RELATIVE_PATH)
        added = stripped(change["added"])

        self.assertIn("CONFIG_AVB_LIBAVB_AB=y", added)
        self.assertIn('CONFIG_ROCKCHIP_CMD="ab_sd_update -"', added)
        self.assertIn("# CONFIG_CMD_SCRIPT_UPDATE is not set", added)
        self.assertNotIn('CONFIG_ROCKCHIP_CMD="sd_update -"', added)


class LinkMountSlotSelectionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.script = LINKMOUNT_PATH.read_text(encoding="utf-8")

    def test_script_maps_all_physical_partitions_to_exact_mtd_indices(self):
        physical_links = {
            name: int(index)
            for index, name in re.findall(r"ln -sf /dev/mtd(\d+) (\w+)", self.script)
        }
        self.assertEqual(
            physical_links,
            {
                "env": 0,
                "idblock": 1,
                "uboot": 2,
                "boot_a": 3,
                "boot_b": 4,
                "rootfs_a": 5,
                "rootfs_b": 6,
                "oem_a": 7,
                "oem_b": 8,
                "reserved": 9,
                "misc": 10,
                "userdata": 11,
            },
        )
        self.assertIn("mkdir -p /dev/block/by-name", self.script)

    def test_script_accepts_only_exact_slot_suffix_tokens_and_defaults_to_a(self):
        self.assertIn("slot_suffix=_a", self.script)
        self.assertIn("androidboot.slot_suffix=_a)", self.script)
        self.assertIn("androidboot.slot_suffix=_b)", self.script)
        self.assertRegex(self.script, r"for \w+ in \$\(cat /proc/cmdline\)")
        self.assertNotIn("androidboot.slot_suffix=*)", self.script)

    def test_logical_links_and_oem_mount_use_the_selected_slot(self):
        for name in ("boot", "rootfs", "oem"):
            self.assertIn(f'ln -sf "{name}${{slot_suffix}}" {name}', self.script)
        self.assertIn("mount_part oem /oem squashfs", self.script)


if __name__ == "__main__":
    unittest.main()
