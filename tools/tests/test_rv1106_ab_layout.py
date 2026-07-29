"""Lock the RV1106 XWR60440 SPI NAND A/B partition layout."""

import configparser
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
IMAGE_DIR = ROOT / "packaging" / "image"
PACKAGING_MAKEFILE = ROOT / "packaging" / "Makefile"
RAW_PACKER_SOURCE = (
    IMAGE_DIR / "tools-sourcecode" / "packaging-raw" / "packaging.c"
)
RAW_PACKER = IMAGE_DIR / "packaging-raw"
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
STANDARD_AVB_AB_FLOW_RELATIVE_PATH = (
    "sysdrv/source/uboot/u-boot/lib/avb/libavb_ab/avb_ab_flow.c"
)
RK_AVB_AB_RELATIVE_PATH = (
    "sysdrv/source/uboot/u-boot/lib/avb/rk_avb_user/rk_ab_ops_user.c"
)
PATCH_TARGETS = [
    BOARD_CONFIG_RELATIVE_PATH,
    DEFCONFIG_RELATIVE_PATH,
    ANDROID_AB_RELATIVE_PATH,
    RK_AVB_AB_RELATIVE_PATH,
]
SDK_BASELINE_ROOT = pathlib.Path(
    os.environ.get("RV1106_SDK_BASELINE", "/tmp/rk_dual_backup_ref/RV1106_IPC_SDK")
)
SDK_BASELINE_SHA256 = {
    BOARD_CONFIG_RELATIVE_PATH: "05a466237c9c0f8dc9a9769efb291e5ddb8c80a29e0066347806350ef81ed8d2",
    DEFCONFIG_RELATIVE_PATH: "e762bccd96ebd1be962fe845f5da24471c2301e4abc73ba0d236c7580bfea697",
    ANDROID_AB_RELATIVE_PATH: "95a848a050e0e85948f8ae5fc8f5a169c82aee101920e6dc77adca51f9675d73",
    RK_AVB_AB_RELATIVE_PATH: "5ed32179b47097121f127bdb1ee4f405f636b7a931047981cc1340adc75964e8",
}


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
    old_header_seen = False
    new_header_seen = False
    in_hunk = False

    def validate_file_headers():
        if current_target is not None and not (old_header_seen and new_header_seen):
            raise ValueError(f"missing file header for diff target: {current_target}")

    for line in patch_text.splitlines():
        if line.startswith("diff --git "):
            validate_file_headers()
            paths = shlex.split(line.removeprefix("diff --git "))
            if len(paths) != 2:
                raise ValueError(f"malformed diff header: {line}")
            current_target = tuple(paths)
            if current_target in changes:
                raise ValueError(f"duplicate diff target: {line}")
            changes[current_target] = {"added": [], "deleted": [], "context": []}
            old_header_seen = False
            new_header_seen = False
            in_hunk = False
        elif not in_hunk and line.startswith("--- "):
            if current_target is None or old_header_seen or new_header_seen:
                raise ValueError(f"unexpected old file header: {line}")
            paths = shlex.split(line.removeprefix("--- "))
            if paths != [current_target[0]]:
                raise ValueError(f"old file header does not match diff target: {line}")
            old_header_seen = True
        elif not in_hunk and line.startswith("+++ "):
            if current_target is None or not old_header_seen or new_header_seen:
                raise ValueError(f"unexpected new file header: {line}")
            paths = shlex.split(line.removeprefix("+++ "))
            if paths != [current_target[1]]:
                raise ValueError(f"new file header does not match diff target: {line}")
            new_header_seen = True
        elif line.startswith("@@"):
            if current_target is None:
                raise ValueError("hunk found before diff header")
            if not (old_header_seen and new_header_seen):
                raise ValueError(f"hunk found before complete file headers: {line}")
            in_hunk = True
        elif in_hunk and line.startswith("+"):
            changes[current_target]["added"].append(line[1:])
        elif in_hunk and line.startswith("-"):
            changes[current_target]["deleted"].append(line[1:])
        elif in_hunk and line.startswith(" "):
            changes[current_target]["context"].append(line[1:])

    validate_file_headers()
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


class OtaUpgradeInputTest(unittest.TestCase):
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

    def test_makefile_packages_ota_from_upgrade_input(self):
        self.assertTrue(
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
        self.assertTrue(
            any(line.startswith(".PHONY:") and "all" in line and "clean" in line
                for line in makefile_lines)
        )

    def test_makefile_is_not_executable(self):
        self.assertEqual((IMAGE_DIR / "Makefile").stat().st_mode & 0o111, 0)


class PackagingArtifactsTest(unittest.TestCase):
    def test_partition_sizes_and_outputs_are_exact(self):
        makefile = PACKAGING_MAKEFILE.read_text(encoding="utf-8")
        for assignment in (
            "ROOTFS_PART_SIZE := 10*0x100000",
            "OEM_PART_SIZE := 32*0x100000",
            "USERDATA_PART_SIZE := 0x1FC0000",
        ):
            self.assertIn(assignment, makefile)
        for image in ("rootfs", "oem", "userdata"):
            self.assertIn(f"/{image}.img", makefile)
        self.assertIn("RELEASE_DIR := $(ROOT)/Release", makefile)
        self.assertIn("$(RELEASE_DIR)/ota.bin", makefile)
        self.assertIn("$(RELEASE_DIR)/sd", makefile)
        self.assertNotIn("ota_ab.tar", makefile)
        self.assertNotIn("sd_update.txt", makefile)
        image_makefile = (IMAGE_DIR / "Makefile").read_text(encoding="utf-8")
        self.assertIn("./packaging-update ./upgrade.ini ./ota.bin", image_makefile)

    def test_raw_packer_declares_exact_ordered_sections(self):
        source = RAW_PACKER_SOURCE.read_text(encoding="utf-8")
        expected = (
            '"env", "idblock", "uboot", "boot_a", "boot_b",\n'
            '    "rootfs_a", "rootfs_b", "oem_a", "oem_b",\n'
            '    "reserved", "misc", "userdata",'
        )
        self.assertIn(expected, source)
        self.assertIn("0x8000000", source)
        self.assertIn("0x20000", source)

    def test_raw_packer_builds_empty_b_slots_as_ff(self):
        self.assertTrue(RAW_PACKER.is_file())
        with tempfile.TemporaryDirectory(prefix="rv1106-raw-pack-") as tempdir:
            work = pathlib.Path(tempdir)
            shutil.copy2(IMAGE_DIR / "partition.ini", work / "partition.ini")
            for name in FACTORY_IMAGES.values():
                (work / name).write_bytes(bytes([len(name) & 0xFF]))

            result = subprocess.run(
                [str(RAW_PACKER), "partition.ini", "raw.bin"],
                cwd=work,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            raw = work / "raw.bin"
            self.assertEqual(raw.stat().st_size, FLASH_SIZE)
            with raw.open("rb") as stream:
                for section in ("boot_b", "rootfs_b", "oem_b", "reserved", "misc"):
                    start, size = EXPECTED[section]
                    stream.seek(start)
                    self.assertEqual(stream.read(min(size, 4096)), b"\xff" * min(size, 4096))

    def test_raw_packer_rejects_invalid_layout_before_output(self):
        original = (IMAGE_DIR / "partition.ini").read_text(encoding="utf-8")
        invalid_layouts = (
            original.replace("start=0x640000", "start=0x640001"),
            original.replace("start=0x640000", "start=0x240000"),
            original.replace("[boot_b]\nstart=0x640000\nsize=0x400000\ntype=4\n", ""),
            original.replace("size=0x40000", "size=0x9000000", 1),
        )
        with tempfile.TemporaryDirectory(prefix="rv1106-raw-invalid-") as tempdir:
            work = pathlib.Path(tempdir)
            for index, content in enumerate(invalid_layouts):
                ini = work / f"invalid-{index}.ini"
                output = work / f"invalid-{index}.bin"
                ini.write_text(content, encoding="utf-8")
                result = subprocess.run(
                    [str(RAW_PACKER), str(ini), str(output)],
                    cwd=work,
                    capture_output=True,
                    text=True,
                )
                self.assertNotEqual(result.returncode, 0, index)
                self.assertFalse(output.exists(), index)


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
                    "context": [PARTITION_COMMAND],
                }
            },
        )

    def test_git_patch_parser_rejects_missing_or_mismatched_file_headers(self):
        malformed_patches = (
            """\
diff --git a/project/example.mk b/project/example.mk
--- a/project/other.mk
+++ b/project/example.mk
@@ -1 +1 @@
-old
+new
""",
            """\
diff --git a/project/example.mk b/project/example.mk
--- a/project/example.mk
@@ -1 +1 @@
-old
+new
""",
        )

        for patch_text in malformed_patches:
            with self.subTest(patch_text=patch_text):
                with self.assertRaises(ValueError):
                    parse_git_patch(patch_text)


class BootSelectionPatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.changes = parse_git_patch(PATCH_PATH.read_text(encoding="utf-8"))

    def test_linked_rockchip_avb_invalid_metadata_defaults_to_a_only(self):
        change = target_changes(self.changes, RK_AVB_AB_RELATIVE_PATH)
        added = stripped(change["added"])
        deleted = stripped(change["deleted"])
        context = stripped(change["context"])

        self.assertIn("data->slots[1].priority = 0;", added)
        self.assertIn("data->slots[1].tries_remaining = 0;", added)
        self.assertIn("data->slots[0].successful_boot = 0;", context)
        self.assertIn("data->slots[1].successful_boot = 0;", context)
        self.assertIn("data->slots[1].priority = AVB_AB_MAX_PRIORITY - 1;", deleted)
        self.assertIn(
            "data->slots[1].tries_remaining = AVB_AB_MAX_TRIES_REMAINING;",
            deleted,
        )
        self.assertFalse(any("slots[0]" in line for line in change["deleted"]))
        self.assertNotIn(
            (
                f"a/{STANDARD_AVB_AB_FLOW_RELATIVE_PATH}",
                f"b/{STANDARD_AVB_AB_FLOW_RELATIVE_PATH}",
            ),
            self.changes,
        )

    def test_patch_applies_without_fuzz_and_preserves_complete_a_only_defaults(self):
        missing = [
            relative_path
            for relative_path in PATCH_TARGETS
            if not (SDK_BASELINE_ROOT / relative_path).is_file()
        ]
        self.assertFalse(
            missing,
            f"missing pinned SDK sources below {SDK_BASELINE_ROOT}: {missing}",
        )
        for relative_path, expected_sha256 in SDK_BASELINE_SHA256.items():
            actual_sha256 = hashlib.sha256(
                (SDK_BASELINE_ROOT / relative_path).read_bytes()
            ).hexdigest()
            self.assertEqual(actual_sha256, expected_sha256, relative_path)

        with tempfile.TemporaryDirectory(prefix="rv1106-ab-layout-") as tempdir:
            sdk_root = pathlib.Path(tempdir)
            for relative_path in PATCH_TARGETS:
                source = SDK_BASELINE_ROOT / relative_path
                destination = sdk_root / relative_path
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)

            result = subprocess.run(
                [
                    "patch",
                    "--fuzz=0",
                    "-d",
                    str(sdk_root),
                    "-p1",
                    "--forward",
                ],
                input=PATCH_PATH.read_text(encoding="utf-8"),
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            avb_source = (sdk_root / RK_AVB_AB_RELATIVE_PATH).read_text(
                encoding="utf-8"
            )
            for assignment in (
                "data->slots[0].priority = AVB_AB_MAX_PRIORITY;",
                "data->slots[0].tries_remaining = AVB_AB_MAX_TRIES_REMAINING;",
                "data->slots[0].successful_boot = 0;",
                "data->slots[1].priority = 0;",
                "data->slots[1].tries_remaining = 0;",
                "data->slots[1].successful_boot = 0;",
            ):
                self.assertEqual(avb_source.count(assignment), 1)

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
        self.assertIn('if (env_update("bootargs", slot_arg)) {', added)
        self.assertIn(
            'printf("%s: Failed to update slot suffix in bootargs.\\n", __func__);',
            added,
        )
        self.assertNotIn('env_update("bootargs", slot_arg);', added)
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
        deleted = stripped(change["deleted"])

        self.assertEqual(
            added,
            [
                'CONFIG_ROCKCHIP_CMD="ab_sd_update -"',
                "CONFIG_AVB_LIBAVB_AB=y",
                "CONFIG_AVB_LIBAVB_USER=y",
                "CONFIG_ANDROID_AB=y",
                "CONFIG_PARTITION_UUIDS=y",
                "# CONFIG_CMD_SCRIPT_UPDATE is not set",
            ],
        )
        self.assertEqual(
            deleted,
            [
                'CONFIG_ROCKCHIP_CMD="sd_update -"',
                "CONFIG_CMD_SCRIPT_UPDATE=y",
            ],
        )


class LinkMountSlotSelectionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.script = LINKMOUNT_PATH.read_text(encoding="utf-8")

    def test_script_maps_all_physical_partitions_to_exact_mtd_indices(self):
        physical_links = {
            name: int(index)
            for index, name in re.findall(r"make_link /dev/mtd(\d+) (\w+)", self.script)
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
        self.assertIn('mkdir -p "$BY_NAME_DIR" || return 1', self.script)
        self.assertIn('cd "$BY_NAME_DIR" || return 1', self.script)

    def test_script_accepts_only_exact_slot_suffix_tokens_and_defaults_to_a(self):
        self.assertIn("CMDLINE_FILE=${CMDLINE_FILE:-/proc/cmdline}", self.script)
        self.assertIn("BY_NAME_DIR=${BY_NAME_DIR:-/dev/block/by-name}", self.script)
        self.assertIn("androidboot.slot_suffix=_a)", self.script)
        self.assertIn("androidboot.slot_suffix=_b)", self.script)
        self.assertIn("androidboot.slot_suffix=*)", self.script)

    def test_logical_links_and_oem_mount_use_the_selected_slot(self):
        for name in ("boot", "rootfs", "oem"):
            self.assertIn(f'make_link "{name}${{slot_suffix}}" {name}', self.script)
        self.assertIn('mount_part oem "$OEM_MOUNTPOINT" squashfs', self.script)

    def invoke_linkdev(self, cmdline, existing_file=None, existing_link=None):
        tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(tempdir.cleanup)
        root = pathlib.Path(tempdir.name)
        cmdline_path = root / "cmdline"
        by_name = root / "by-name"
        cmdline_path.write_text(cmdline, encoding="utf-8")

        if existing_file is not None or existing_link is not None:
            by_name.mkdir()
        if existing_file is not None:
            (by_name / existing_file).write_text("keep", encoding="utf-8")
        if existing_link is not None:
            name, target = existing_link
            (by_name / name).symlink_to(target)

        env = os.environ.copy()
        env["CMDLINE_FILE"] = str(cmdline_path)
        env["BY_NAME_DIR"] = str(by_name)
        result = subprocess.run(
            ["sh", str(LINKMOUNT_PATH), "linkdev"],
            capture_output=True,
            text=True,
            env=env,
        )
        return result, by_name

    def assert_exact_links(self, by_name, slot_suffix):
        expected = {
            "env": "/dev/mtd0",
            "idblock": "/dev/mtd1",
            "uboot": "/dev/mtd2",
            "boot_a": "/dev/mtd3",
            "boot_b": "/dev/mtd4",
            "rootfs_a": "/dev/mtd5",
            "rootfs_b": "/dev/mtd6",
            "oem_a": "/dev/mtd7",
            "oem_b": "/dev/mtd8",
            "reserved": "/dev/mtd9",
            "misc": "/dev/mtd10",
            "userdata": "/dev/mtd11",
            "boot": f"boot{slot_suffix}",
            "rootfs": f"rootfs{slot_suffix}",
            "oem": f"oem{slot_suffix}",
        }
        actual = {
            path.name: os.readlink(path)
            for path in by_name.iterdir()
            if path.is_symlink()
        }
        self.assertEqual(actual, expected)

    def test_linkdev_defaults_to_a_only_when_slot_key_is_absent(self):
        result, by_name = self.invoke_linkdev("console=ttyS2 rootwait\n")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_exact_links(by_name, "_a")

    def test_linkdev_selects_b_and_replaces_existing_symlink(self):
        result, by_name = self.invoke_linkdev(
            "console=ttyS2 androidboot.slot_suffix=_b rootwait\n",
            existing_link=("boot", "boot_a"),
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_exact_links(by_name, "_b")

    def test_linkdev_rejects_malformed_slot_suffix(self):
        result, _ = self.invoke_linkdev("androidboot.slot_suffix=invalid\n")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Invalid androidboot.slot_suffix", result.stderr)

    def test_linkdev_rejects_conflicting_slot_suffixes(self):
        result, _ = self.invoke_linkdev(
            "androidboot.slot_suffix=_a androidboot.slot_suffix=_b\n"
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Conflicting androidboot.slot_suffix", result.stderr)

    def test_linkdev_rejects_existing_non_symlink_destination(self):
        result, by_name = self.invoke_linkdev("console=ttyS2\n", existing_file="boot")

        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((by_name / "boot").read_text(encoding="utf-8"), "keep")

    def test_start_and_cli_branches_propagate_failures_and_quote_output(self):
        for command in (
            "linkdev || exit 1",
            "mount_part rootfs IGNORE squashfs || exit 1",
            'mount_part oem "$OEM_MOUNTPOINT" squashfs || exit 1',
            'mount_part userdata "$USERDATA_MOUNTPOINT" ubifs || exit 1',
        ):
            self.assertIn(command, self.script)
        self.assertIn("printf '%s\\n' \"stop $0 finished\"", self.script)
        self.assertIn("printf 'Usage: %s {start|linkdev|stop}\\n' \"$0\" >&2", self.script)

    def test_start_reaches_oem_and_userdata_after_spi_nand_rootfs_probe(self):
        with tempfile.TemporaryDirectory(prefix="rv1106-linkmount-start-") as tempdir:
            root = pathlib.Path(tempdir)
            cmdline_path = root / "cmdline"
            by_name = root / "by-name"
            oem_mountpoint = root / "oem"
            userdata_mountpoint = root / "userdata"
            stub_bin = root / "bin"
            cmdline_path.write_text("androidboot.slot_suffix=_a\n", encoding="utf-8")
            oem_mountpoint.mkdir()
            userdata_mountpoint.mkdir()
            stub_bin.mkdir()

            stubs = {
                "mountpoint": "#!/bin/sh\nprintf '/dev/ubiblock5_0 / squashfs rw 0 0\\n'\n",
                "realpath": """#!/bin/sh
case "$1" in
    */oem) printf '/dev/mtd7\\n' ;;
    */userdata) printf '/dev/mtd11\\n' ;;
    *) exit 1 ;;
esac
""",
                "mount": """#!/bin/sh
printf '/dev/ubiblock7_0 on oem type squashfs\\n'
printf '/dev/ubi11_0 on userdata type ubifs\\n'
""",
            }
            for name, content in stubs.items():
                path = stub_bin / name
                path.write_text(content, encoding="utf-8")
                path.chmod(0o755)

            env = os.environ.copy()
            env.update(
                {
                    "CMDLINE_FILE": str(cmdline_path),
                    "BY_NAME_DIR": str(by_name),
                    "OEM_MOUNTPOINT": str(oem_mountpoint),
                    "USERDATA_MOUNTPOINT": str(userdata_mountpoint),
                    "PATH": f"{stub_bin}:{env['PATH']}",
                }
            )
            result = subprocess.run(
                ["sh", str(LINKMOUNT_PATH), "start"],
                capture_output=True,
                text=True,
                env=env,
            )

            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("rootfs mount on /dev/ubiblock5_0", result.stdout)
            self.assertIn("oem has been mounted", result.stdout)
            self.assertIn("userdata has been mounted", result.stdout)


if __name__ == "__main__":
    unittest.main()
