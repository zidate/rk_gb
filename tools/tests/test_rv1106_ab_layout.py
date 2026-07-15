"""Lock the RV1106 XWR60440 SPI NAND A/B partition layout."""

import configparser
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
IMAGE_DIR = ROOT / "packaging" / "image"
PATCH_PATH = ROOT / "vendor" / "rv1106_sdk_patches" / "0001-ab-layout.patch"
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


def read_ini(filename):
    config = configparser.ConfigParser(interpolation=None)
    with (IMAGE_DIR / filename).open(encoding="utf-8") as stream:
        config.read_file(stream)
    return config


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
        active_lines = [
            line.strip()
            for line in (IMAGE_DIR / "Makefile").read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
        self.assertFalse(
            any("packaging-update ./upgrade.ini" in line for line in active_lines)
        )


class BoardConfigPatchTest(unittest.TestCase):
    def test_patch_locks_partition_command_and_a_slot_build_inputs(self):
        self.assertTrue(PATCH_PATH.is_file(), f"missing patch: {PATCH_PATH}")
        patch_text = PATCH_PATH.read_text(encoding="utf-8")
        self.assertIn(PARTITION_COMMAND, patch_text)
        self.assertIn("export RK_MISC=wipe_all-misc.img", patch_text)
        self.assertIn(FILESYSTEM_CONFIG, patch_text)
        self.assertNotIn("rootfs_b@", patch_text)
        self.assertNotIn("oem_b@", patch_text)
        self.assertNotIn("boot_b@", patch_text)


if __name__ == "__main__":
    unittest.main()
