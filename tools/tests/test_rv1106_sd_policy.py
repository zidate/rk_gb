"""Verify the RV1106 U-Boot SD update policy and SPI NAND transaction."""

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
PLAN_BASELINE = pathlib.Path(
    os.environ.get("RV1106_SDK_PLAN_BASELINE", "/tmp/rk_plan_sdk/RV1106_IPC_SDK")
)
UBOOT = "sysdrv/source/uboot/u-boot"
BOARD_CONFIG = (
    "project/cfg/BoardConfig_IPC/"
    "BoardConfig-SPI_NAND-NONE-RV1106_IPC38_DEMO_V10-IPC-XWR60440.mk"
)
DEFCONFIG = f"{UBOOT}/configs/rv1106-XWR60440_defconfig"
ANDROID_AB = f"{UBOOT}/common/android_ab.c"
RK_AVB_AB = f"{UBOOT}/lib/avb/rk_avb_user/rk_ab_ops_user.c"
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
    RK_AVB_AB: (DUAL_BASELINE, "5ed32179b47097121f127bdb1ee4f405f636b7a931047981cc1340adc75964e8"),
    SPINAND_HEADER: (DUAL_BASELINE, "69a6b6146b4f0fa2a46e9fadb9a4e0c264d176ba22be7216469a5637fbbd6066"),
    SPINAND_CORE: (DUAL_BASELINE, "fa8ddbed4500fb0b4191cfa76f85d828bccd97a8c4496482ea8d47af9187380a"),
    CHUCUN: (DUAL_BASELINE, "649994b0727e29cbec1af1b0e21a1aab1f5df11257d3fc5586db9f1f19e52a3f"),
    CMD_KCONFIG: (PLAN_BASELINE, "344a72e300d59409b145ef8bb2fe698997fbf6eebb7b21d099d85d3301cd2a0f"),
    CMD_MAKEFILE: (PLAN_BASELINE, "c3550590185aa6591614318560c012b5bbdcc172048dcd56422e38b1e955f074"),
}
POST_SHA256 = {
    COMMAND: "6af579be7411602624e56956812487d3bd6b0833e6770e60bb1f5c0edd997857",
    CMD_KCONFIG: "835bf2ebc533dc5c4c359441d566076b1c85023c268811c3f41b9476a669ed9f",
    CMD_MAKEFILE: "89ed26b3e1eaea7914970174262cf0887f2a586ef473ad886cc58f0da7234d14",
    DEFCONFIG: "78289eee8165ad9c35d3c86e17356ee4b3f9eab8394c4cf84a01150ba10ef798",
}
TASK5_PROTECTION_CORE_SHA256 = (
    "a30b5e4ac4898469c0f074ff863dde623d24d1ea9a636f79107beea2badda823"
)
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


def c_function(source, name):
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*\)\s*\{{", source, re.DOTALL)
    if match is None:
        raise AssertionError(f"missing C function: {name}")
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
    raise AssertionError(f"unterminated C function: {name}")


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

        for patch_name in (
            "0001-ab-layout.patch",
            "0002-uboot-sd-protection.patch",
            "0006-uboot-sd-ubi-init.patch",
            "0009-uboot-sd-fixed-raw-offset.patch",
            "0010-uboot-sd-oem-switch.patch",
        ):
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

    def test_all_sixty_four_masks_switch_when_oem_is_present(self):
        self.assertIsNotNone(self.command)
        bits = {
            name: int(bit)
            for name, bit in re.findall(
                r"#define AB_SD_(ENV|IDBLOCK|UBOOT|BOOT|ROOTFS|OEM) BIT\((\d)\)",
                self.command,
            )
        }
        self.assertEqual(
            bits,
            {"ENV": 0, "IDBLOCK": 1, "UBOOT": 2, "BOOT": 3, "ROOTFS": 4, "OEM": 5},
        )
        self.assertIn(
            "#define AB_SD_SLOT_SWITCH_TRIGGER AB_SD_OEM",
            self.command,
        )
        self.assertIn(
            "return (present & AB_SD_SLOT_SWITCH_TRIGGER) != 0;",
            self.command,
        )
        for present in range(64):
            expected = bool(present & 0x20)
            actual = bool(present & 0x20)
            with self.subTest(present=present):
                self.assertEqual(actual, expected)
        self.assertIn(
            'printf("SD update %s; target slot %s; present mask 0x%x\\n",',
            self.command,
        )
        self.assertIn('"contains oem"', self.command)

    def test_fixed_table_has_exact_files_bits_and_overflow_safe_maxima(self):
        self.assertIn("#define AB_SD_KIB(n) ((loff_t)(n) * 1024)", self.command)
        self.assertIn("#define AB_SD_MIB(n) ((loff_t)(n) * 1024 * 1024)", self.command)
        self.assertIn("#define AB_SD_ENV_OFFSET 0", self.command)
        self.assertIn("#define AB_SD_IDBLOCK_OFFSET AB_SD_KIB(256)", self.command)
        self.assertIn(
            "#define AB_SD_UBOOT_OFFSET (AB_SD_IDBLOCK_OFFSET + AB_SD_MIB(1))",
            self.command,
        )
        expected_rows = (
            '{ "env.img", "env", AB_SD_ENV, AB_SD_KIB(256), AB_SD_ENV_OFFSET, false },',
            'AB_SD_IDBLOCK_OFFSET, false },',
            'AB_SD_UBOOT_OFFSET, false },',
            '{ "boot.img", "boot", AB_SD_BOOT, AB_SD_MIB(4), 0, true },',
            '{ "rootfs.img", "rootfs", AB_SD_ROOTFS, AB_SD_MIB(10), 0, true },',
            '{ "oem.img", "oem", AB_SD_OEM, AB_SD_MIB(32), 0, true },',
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

    def test_sd_scan_forces_dos_partition_parsing_and_restores_descriptor(self):
        command = c_function(self.command, "do_ab_sd_update")
        for snippet in (
            "#include <part.h>",
            "struct blk_desc *desc;",
            "desc = blk_get_devnum_by_type(IF_TYPE_MMC, 1);",
            "saved_part_type = desc->part_type;",
            "desc->part_type = PART_TYPE_DOS;",
            "out_restore:",
            "desc->part_type = saved_part_type;",
        ):
            self.assertIn(snippet, self.command)

        force_dos = command.index("desc->part_type = PART_TYPE_DOS;")
        restore = command.index("desc->part_type = saved_part_type;")
        self.assertLess(force_dos, command.index('fs_set_blk_dev("mmc", "1"'))
        self.assertLess(command.index("ab_sd_run_transaction("), restore)
        self.assertNotIn("return ", command[force_dos:restore])
        self.assertEqual(command[force_dos:restore].count("goto out_restore;"), 4)

    def test_fs_lifecycle_reselects_fat_before_exists_and_size(self):
        select_fat = 'fs_set_blk_dev("mmc", "1", FS_TYPE_FAT)'
        command = c_function(self.command, "do_ab_sd_update")
        loop_start = command.index(
            "for (i = 0; i < ARRAY_SIZE(ab_sd_images); i++)"
        )
        loop_end = command.index('\n\tprintf("SD update', loop_start)
        loop = command[loop_start:loop_end]

        self.assertNotIn(select_fat, command[:loop_start])
        self.assertEqual(loop.count(select_fat), 2)
        self.assertIn(
            "fs_exists() closes the filesystem and resets fs_type", loop
        )
        self.assertIn("Failed to select SD FAT before probing %s", loop)
        self.assertIn("Failed to select SD FAT before sizing %s", loop)

        first_select = loop.index(select_fat)
        exists = loop.index("exists = fs_exists(image->filename);")
        absence = loop.index("if (!exists)")
        second_select = loop.index(select_fat, first_select + 1)
        size = loop.index("ret = fs_size(image->filename, &size);")
        self.assertLess(first_select, exists)
        self.assertLess(exists, absence)
        self.assertLess(absence, second_select)
        self.assertLess(second_select, size)
        self.assertRegex(
            loop[second_select:size],
            r"(?s)fs_set_blk_dev\([^;]+;\s+if \(ret\) \{.*"
            r"ret = CMD_RET_FAILURE;\s+goto out_restore;\s+\}\s+$",
        )

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

    def test_command_is_wired_without_legacy_update_or_reboot(self):
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
        self.assertIn("CONFIG_CMD_UBI=y", defconfig)
        self.assertIn(
            'CONFIG_MTDIDS_DEFAULT="spi-nand0=spi-nand0"', defconfig
        )
        self.assertIn("# CONFIG_CMD_UBIFS is not set", defconfig)
        self.assertIn('CONFIG_ROCKCHIP_CMD="ab_sd_update -"', defconfig)
        self.assertIn("# CONFIG_CMD_SCRIPT_UPDATE is not set", defconfig)
        forbidden = (
            "sd_update.txt",
            "DG_sdupdate",
            "mtd_block_markbad",
            "run_command(\"reset",
            "do_reset(",
        )
        for symbol in forbidden:
            self.assertNotIn(symbol.casefold(), self.command.casefold())

    def test_task6_uses_only_existing_mtd_apis_and_preserves_detection(self):
        for header in (
            "#include <malloc.h>",
            "#include <mapmem.h>",
            "#include <mtd.h>",
            "#include <linux/mtd/mtd.h>",
            "#include <linux/mtd/spinand.h>",
            "#include <android_avb/avb_ops_user.h>",
            "#include <android_avb/avb_ab_flow.h>",
        ):
            self.assertIn(header, self.command)

        allowed_mtd_calls = {
            "mtd_block_isbad",
            "mtd_erase",
            "mtd_probe_devices",
            "mtd_read",
            "mtd_to_spinand",
            "mtd_write_oob",
        }
        actual_mtd_calls = set(re.findall(r"\b(mtd_[a-z0-9_]+)\s*\(", self.command))
        self.assertEqual(actual_mtd_calls, allowed_mtd_calls)
        self.assertNotRegex(self.command, r"\bmtd_write\s*\(")
        self.assertNotIn("mtd_block_markbad", self.command)

        core_change = self.changes[(f"a/{SPINAND_CORE}", f"b/{SPINAND_CORE}")]
        core_delta = "\n".join(core_change["added"] + core_change["deleted"])
        self.assertNotIn("mtd_block_isbad", core_delta)
        self.assertNotIn("mtd_block_markbad", core_delta)
        self.assertEqual(
            sha256(self.sdk_root / SPINAND_CORE), TASK5_PROTECTION_CORE_SHA256
        )

    def test_bad_block_capacity_and_writer_use_existing_detector_only(self):
        capacity = c_function(self.command, "ab_sd_good_capacity")
        writer = c_function(self.command, "ab_sd_write_image")

        self.assertEqual(self.command.count("mtd_block_isbad("), 2)
        self.assertEqual(capacity.count("mtd_block_isbad("), 1)
        self.assertEqual(writer.count("mtd_block_isbad("), 1)
        for function in (capacity, writer):
            self.assertIn("if (bad < 0)", function)
            self.assertIn("if (bad > 0)", function)
        self.assertIn("loff_t range_end = range_start + range_size;", capacity)
        self.assertIn("for (offset = range_start; offset < range_end;", capacity)
        self.assertIn("capacity += mtd->erasesize;", capacity)
        self.assertNotIn("mtd_block_markbad", capacity + writer)

    def test_writer_erases_all_good_blocks_and_verifies_page_writes(self):
        writer = c_function(self.command, "ab_sd_write_image")

        for snippet in (
            'mtd_name = image->slot_scoped ? target : "spi-nand0";',
            "mtd = get_mtd_device_nm(mtd_name);",
            "IS_ERR_OR_NULL(mtd)",
            "range_start = image->slot_scoped ? 0 : image->raw_offset;",
            "range_size = image->slot_scoped ? mtd->size : image->max_size;",
            "range_size > mtd->size - range_start",
            "!IS_ALIGNED(range_start, mtd->erasesize)",
            "!IS_ALIGNED(range_size, mtd->erasesize)",
            "range_end = range_start + range_size;",
            "ab_sd_good_capacity(mtd, range_start, range_size, &good_capacity)",
            "required = ALIGN((u64)image_size, mtd->writesize);",
            "if (required > good_capacity)",
            "for (offset = range_start; offset < range_end;",
            "erase.mtd = mtd;",
            "erase.addr = offset;",
            "erase.len = mtd->erasesize;",
            "ret = mtd_erase(mtd, &erase);",
            "if (file_offset >= image_size)",
            "chunk_len = min_t(loff_t, image_size - file_offset,",
            'fs_set_blk_dev("mmc", "1", FS_TYPE_FAT)',
            "map_to_sysmem(eraseblock_buf)",
            "file_offset, chunk_len, &actread",
            "actread != chunk_len",
            "page_len = ALIGN(chunk_len, mtd->writesize);",
            "memset(eraseblock_buf + chunk_len, 0xff, page_len - chunk_len);",
            "ops.mode = MTD_OPS_AUTO_OOB;",
            "ops.len = mtd->writesize;",
            "ops.datbuf = eraseblock_buf + page_offset;",
            "ret = mtd_write_oob(mtd, offset + page_offset, &ops);",
            "ops.retlen != mtd->writesize",
            "ret = mtd_read(mtd, offset + page_offset, mtd->writesize,",
            "retlen != mtd->writesize",
            "memcmp(verify_buf, eraseblock_buf + page_offset, mtd->writesize)",
            'printf("Wrote and verified %s to %s\\n", image->filename, target);',
            "put_mtd_device(mtd);",
        ):
            self.assertIn(snippet, writer)

        erase = writer.index("ret = mtd_erase(mtd, &erase);")
        trailing = writer.index("if (file_offset >= image_size)")
        read = writer.index("ret = fs_read(")
        write = writer.index("ret = mtd_write_oob(")
        verify = writer.index("ret = mtd_read(")
        compare = writer.index("memcmp(")
        self.assertLess(erase, trailing)
        self.assertLess(trailing, read)
        self.assertLess(read, write)
        self.assertLess(write, verify)
        self.assertLess(verify, compare)

    def test_transaction_owns_two_buffers_and_orders_relock_before_activation(self):
        transaction = c_function(self.command, "ab_sd_run_transaction")
        slot_writer = c_function(self.command, "ab_sd_write_present_slot_images")
        fixed_writer = c_function(self.command, "ab_sd_write_present_fixed_images")
        activation = c_function(self.command, "ab_sd_activate_slot")

        self.assertEqual(
            self.command.count("memalign(ARCH_DMA_MINALIGN, master->erasesize)"), 1
        )
        self.assertEqual(
            self.command.count("memalign(ARCH_DMA_MINALIGN, master->writesize)"), 1
        )
        self.assertNotIn("malloc(master->", self.command)
        for snippet in (
            "mtd_probe_devices();",
            'master = get_mtd_device_nm("spi-nand0");',
            "IS_ERR_OR_NULL(master)",
            "spinand = mtd_to_spinand(master);",
            "eraseblock_buf = memalign(ARCH_DMA_MINALIGN, master->erasesize);",
            "verify_buf = memalign(ARCH_DMA_MINALIGN, master->writesize);",
            "free(eraseblock_buf);",
            "free(verify_buf);",
            "put_mtd_device(master);",
        ):
            self.assertIn(snippet, transaction)

        unlock = transaction.index(
            "spinand_set_block_lock(spinand, BL_ALL_UNLOCKED)"
        )
        slot_write = transaction.index("ab_sd_write_present_slot_images(")
        ubi_prepare = transaction.index("ab_sd_prepare_present_ubi_images(")
        fixed_write = transaction.index("ab_sd_write_present_fixed_images(")
        relock_label = transaction.index("relock:")
        relock = transaction.index(
            "spinand_set_block_lock(spinand, BL_LOWER_3_4_LOCKED)"
        )
        failed = transaction.index("if (ret)", relock)
        activate = transaction.index("if (ab_sd_should_switch(present))")
        self.assertLess(unlock, slot_write)
        self.assertLess(slot_write, ubi_prepare)
        self.assertLess(ubi_prepare, fixed_write)
        self.assertLess(fixed_write, relock_label)
        self.assertLess(relock_label, relock)
        self.assertLess(relock, failed)
        self.assertLess(failed, activate)
        self.assertEqual(transaction[unlock:relock_label].count("goto relock;"), 4)
        self.assertRegex(
            transaction,
            r"if \(ab_sd_should_switch\(present\)\)\s+"
            r"ret = ab_sd_activate_slot\(target_suffix\);",
        )

        ordered_images = [
            slot_writer.index(f"&ab_sd_images[{index}]")
            for index in (
                "AB_SD_IMAGE_BOOT",
                "AB_SD_IMAGE_ROOTFS",
                "AB_SD_IMAGE_OEM",
            )
        ]
        self.assertEqual(ordered_images, sorted(ordered_images))
        self.assertNotIn("AB_SD_IMAGE_ENV", slot_writer)
        self.assertIn(
            "AB_SD_IMAGE_ENV,\n\t\tAB_SD_IMAGE_UBOOT,\n\t\tAB_SD_IMAGE_IDBLOCK,",
            fixed_writer,
        )
        for snippet in (
            "ops = avb_ops_user_new();",
            "avb_ab_mark_slot_active(ops->ab_ops, slot_number)",
            "avb_ops_user_free(ops);",
            'slot_number = !strcmp(target_suffix, "_b") ? 1 : 0;',
            'printf("Activated A/B slot %s for next boot\\n", target_suffix);',
        ):
            self.assertIn(snippet, activation)

    def test_ubi_autoresize_is_consumed_while_nand_is_unlocked(self):
        transaction = c_function(self.command, "ab_sd_run_transaction")
        prepare = c_function(self.command, "ab_sd_prepare_ubi_image")
        prepare_present = c_function(self.command, "ab_sd_prepare_present_ubi_images")

        self.assertIn('snprintf(command, sizeof(command), "ubi part %s", target);', prepare)
        self.assertIn('run_command("ubi detach", 0)', prepare)
        self.assertIn("AB_SD_IMAGE_ROOTFS", prepare_present)
        self.assertIn("AB_SD_IMAGE_OEM", prepare_present)
        unlock = transaction.index("BL_ALL_UNLOCKED")
        initialize = transaction.index("ab_sd_prepare_present_ubi_images(")
        relock = transaction.index("BL_LOWER_3_4_LOCKED")
        self.assertLess(unlock, initialize)
        self.assertLess(initialize, relock)

    def test_scan_persists_sizes_and_starts_one_transaction(self):
        command = c_function(self.command, "do_ab_sd_update")
        for snippet in (
            "loff_t image_sizes[ARRAY_SIZE(ab_sd_images)] = { 0 };",
            "image_sizes[i] = size;",
            "present |= image->present_bit;",
            "ret = ab_sd_run_transaction(present, image_sizes, target_suffix);",
        ):
            self.assertIn(snippet, command)


if __name__ == "__main__":
    unittest.main()
