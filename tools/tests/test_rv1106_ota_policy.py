"""Verify the RV1106 rk_ota A/B network-update policy."""

import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCHES = tuple(
    ROOT / "vendor/rv1106_sdk_patches" / name
    for name in ("0004-rk-ota.patch", "0007-rk-ota-ubi-init.patch")
)
SDK_BASELINE = pathlib.Path(
    os.environ.get(
        "RV1106_SDK_BASELINE", "/tmp/rk_dual_backup_ref/RV1106_IPC_SDK"
    )
)
RK_OTA = pathlib.Path("sysdrv/tools/board/rk_ota")
TARGETS = (
    RK_OTA / "src/bootloader.h",
    RK_OTA / "src/bootloader.cpp",
    RK_OTA / "src/main.cpp",
    RK_OTA / "Makefile",
)


def c_function(source, name):
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*\)\s*\{{", source, re.DOTALL)
    if match is None:
        raise AssertionError(f"missing function: {name}")
    start = match.start()
    brace = source.index("{", start)
    depth = 0
    for offset in range(brace, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if depth == 0:
                return source[start : offset + 1]
    raise AssertionError(f"unterminated function: {name}")


class RkOtaPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for patch in PATCHES:
            if not patch.is_file():
                raise AssertionError(f"missing patch: {patch}")
        cls.tempdir = tempfile.TemporaryDirectory()
        cls.sdk_root = pathlib.Path(cls.tempdir.name) / "RV1106_IPC_SDK"
        for relative in TARGETS:
            source = SDK_BASELINE / relative
            if not source.is_file():
                raise AssertionError(f"missing SDK baseline file: {source}")
            target = cls.sdk_root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)

        for patch in PATCHES:
            result = subprocess.run(
                ["patch", "--fuzz=0", "-p1", "-i", str(patch)],
                cwd=cls.sdk_root,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            if result.returncode:
                raise AssertionError(f"{patch.name} failed to apply:\n{result.stdout}")

        cls.header = (cls.sdk_root / TARGETS[0]).read_text()
        cls.bootloader = (cls.sdk_root / TARGETS[1]).read_text()
        cls.main = (cls.sdk_root / TARGETS[2]).read_text()

    @classmethod
    def tearDownClass(cls):
        cls.tempdir.cleanup()

    def test_patch_is_limited_to_rk_ota_sources(self):
        targets = []
        for patch in PATCHES:
            targets.extend(
                re.findall(
                    r"^diff --git a/(\S+) b/(\S+)$",
                    patch.read_text(),
                    re.MULTILINE,
                )
            )
        self.assertTrue(targets)
        self.assertEqual({left for left, right in targets}, {right for left, right in targets})
        self.assertLessEqual({pathlib.Path(left) for left, _ in targets}, set(TARGETS))

    def test_network_ota_names_only_boot_rootfs_and_oem(self):
        self.assertIn('#define AB_BOOT_NAME "boot"', self.header)
        self.assertIn('#define AB_ROOTFS_NAME "rootfs"', self.header)
        self.assertIn('#define AB_OEM_NAME "oem"', self.header)
        self.assertNotIn("AB_UBOOT_NAME", self.header + self.bootloader)
        self.assertNotIn('AB_ROOTFS_NAME "system"', self.header)
        writer = c_function(self.bootloader, "flash_write")
        for image in ("env.img", "idblock.img", "uboot.img"):
            self.assertIn(f'"%s/{image}"', writer)
        self.assertIn("supports only boot.img, rootfs.img and oem.img", writer)
        self.assertIn("--partition=<boot/rootfs/oem/all>", self.main)
        self.assertNotIn("--partition=<uboot", self.main)
        self.assertRegex(
            self.main,
            r"case '\?':\s+LOGE\(\"Invalid command argument\\n\"\);\s+"
            r"return RK_UPGRADE_ERR;",
        )

    def test_all_requires_complete_trio_before_unlock(self):
        writer = c_function(self.bootloader, "flash_write")
        unlock = writer.index("MEMUNLOCK")
        for image in (
            "src_boot_path",
            "src_rootfs_path",
            "src_oem_path",
            "src_env_path",
            "src_idblock_path",
            "src_uboot_path",
        ):
            check = writer.index(f"access({image}")
            self.assertLess(check, unlock)
        self.assertIn('strcmp(partition, "all")', writer)
        self.assertNotIn("extra_partition", self.header + self.bootloader + self.main)

    def test_mtd_write_uses_existing_bad_block_aware_writer_without_shell_tools(self):
        writer = c_function(self.bootloader, "mtd_write")
        for symbol in (
            "mtd_scan_partitions",
            "mtd_find_partition_by_name",
            "mtd_write_partition",
            "mtd_write_data",
            "mtd_erase_blocks",
            "mtd_write_close",
        ):
            self.assertIn(symbol, writer)
        self.assertNotIn("flash_erase", writer)
        self.assertNotIn("nandwrite", writer)
        self.assertNotIn("system(", writer)

    def test_transaction_relocks_and_verifies_before_misc_switch(self):
        writer = c_function(self.bootloader, "flash_write")
        update = c_function(self.bootloader, "miscUpdate")
        unlock = writer.index("MEMUNLOCK")
        writes = writer.index("flash_write_partition", unlock)
        sync = writer.index("fsync", writes)
        lock = writer.index("MEMLOCK", sync)
        is_locked = writer.index("MEMISLOCKED", lock)
        self.assertLess(unlock, writes)
        self.assertLess(writes, sync)
        ubi_prepare = writer.index("prepare_ubi_partition", sync)
        self.assertLess(sync, ubi_prepare)
        self.assertLess(ubi_prepare, lock)
        self.assertLess(lock, is_locked)
        self.assertIn("goto relock", writer[unlock:lock])

        transaction = update.index("flash_write(")
        activate = update.index("setSlotActivity()")
        self.assertLess(transaction, activate)
        self.assertIn("return -1", update[transaction:activate])

    def test_ubi_autoresize_is_consumed_before_relock(self):
        prepare = c_function(self.bootloader, "prepare_ubi_partition")
        writer = c_function(self.bootloader, "flash_write")

        for snippet in (
            '#include <mtd/ubi-user.h>',
            'open("/dev/ubi_ctrl", O_RDWR)',
            "request.ubi_num = UBI_DEV_NUM_AUTO;",
            "request.mtd_num = partition->device_index;",
            "ioctl(control_fd, UBI_IOCATT, &request)",
            "ioctl(control_fd, UBI_IOCDET, &ubi_num)",
        ):
            self.assertIn(snippet, self.bootloader)
        self.assertIn("mtd_find_partition_by_name", prepare)
        self.assertIn("write_rootfs", writer)
        self.assertIn("write_oem", writer)
        initialize = writer.index("prepare_ubi_partition", writer.index("fsync"))
        relock = writer.index("MEMLOCK", initialize)
        self.assertLess(initialize, relock)

    def test_target_slot_is_always_the_inactive_suffix(self):
        writer = c_function(self.bootloader, "flash_write")
        self.assertIn("slot == 0", writer)
        self.assertIn("? 'b' : 'a'", writer)
        for name in ("AB_BOOT_NAME", "AB_ROOTFS_NAME", "AB_OEM_NAME"):
            self.assertIn(name, writer)


if __name__ == "__main__":
    unittest.main()
