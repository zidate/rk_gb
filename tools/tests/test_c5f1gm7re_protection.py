"""Verify C5F1GM7x A0 block protection in both RV1106 SDK stacks."""

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
BASELINE = pathlib.Path(
    os.environ.get("RV1106_SDK_BASELINE", "/tmp/rk_dual_backup_ref/RV1106_IPC_SDK")
)

UBOOT_PREFIX = "sysdrv/source/uboot/u-boot"
KERNEL_PREFIX = "sysdrv/source/kernel"
RELATIVE_FILES = (
    "include/linux/mtd/spinand.h",
    "drivers/mtd/nand/spi/core.c",
    "drivers/mtd/nand/spi/chucun.c",
)
PATCH_CASES = {
    "0002-uboot-sd-protection.patch": UBOOT_PREFIX,
    "0003-kernel-protection.patch": KERNEL_PREFIX,
}
REQUIRED_HEADER_LINES = (
    "#define SPINAND_HAS_BLOCK_PROTECTION BIT(2)",
    "#define BL_ALL_UNLOCKED 0x00",
    "#define BL_LOWER_3_4_LOCKED 0x2a",
    "int spinand_get_block_lock(struct spinand_device *spinand, u8 *lock);",
    "int spinand_set_block_lock(struct spinand_device *spinand, u8 lock);",
)


def parse_git_patch(patch_text):
    changes = {}
    target = None
    old_header = None
    new_header = None
    in_hunk = False

    def validate_headers():
        if target is not None and (old_header, new_header) != target:
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
            if target is None or (old_header, new_header) != target:
                raise ValueError(f"hunk before matching file headers: {line}")
            in_hunk = True
        elif in_hunk and line.startswith("+"):
            changes[target]["added"].append(line[1:])
        elif in_hunk and line.startswith("-"):
            changes[target]["deleted"].append(line[1:])

    validate_headers()
    return changes


def apply_patch_to_clean_sources(patch_name, prefix):
    patch_path = PATCH_DIR / patch_name
    if not patch_path.is_file():
        raise AssertionError(f"missing patch: {patch_path}")

    tempdir = tempfile.TemporaryDirectory()
    sdk_root = pathlib.Path(tempdir.name)
    for relative in RELATIVE_FILES:
        source = BASELINE / prefix / relative
        destination = sdk_root / prefix / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    result = subprocess.run(
        ["patch", "-p1", "--fuzz=0", "--batch", "-i", str(patch_path)],
        cwd=sdk_root,
        capture_output=True,
        text=True,
    )
    output = result.stdout + result.stderr
    if result.returncode != 0:
        tempdir.cleanup()
        raise AssertionError(f"{patch_name} failed to apply:\n{output}")
    if re.search(r"\b(?:offset|fuzz)\b", output, re.IGNORECASE):
        tempdir.cleanup()
        raise AssertionError(f"{patch_name} applied non-exactly:\n{output}")
    if list(sdk_root.rglob("*.rej")):
        tempdir.cleanup()
        raise AssertionError(f"{patch_name} produced rejects")

    sources = {
        relative: (sdk_root / prefix / relative).read_text(encoding="utf-8")
        for relative in RELATIVE_FILES
    }
    return tempdir, sources


def spinand_info_block(source, model):
    match = re.search(
        rf'SPINAND_INFO\("{re.escape(model)}",(?P<body>.*?)SPINAND_ECCINFO\(',
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"missing SPINAND_INFO for {model}")
    return match.group("body")


class ProtectionPatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patch_changes = {}
        cls.sources = {}
        cls.tempdirs = []
        for patch_name, prefix in PATCH_CASES.items():
            patch_text = (PATCH_DIR / patch_name).read_text(encoding="utf-8")
            cls.patch_changes[patch_name] = parse_git_patch(patch_text)
            tempdir, sources = apply_patch_to_clean_sources(patch_name, prefix)
            cls.tempdirs.append(tempdir)
            cls.sources[prefix] = sources

    @classmethod
    def tearDownClass(cls):
        for tempdir in getattr(cls, "tempdirs", []):
            tempdir.cleanup()

    def test_patches_have_exact_sdk_root_relative_target_scope(self):
        for patch_name, prefix in PATCH_CASES.items():
            expected = [
                (f"a/{prefix}/{relative}", f"b/{prefix}/{relative}")
                for relative in RELATIVE_FILES
            ]
            self.assertEqual(list(self.patch_changes[patch_name]), expected)

    def test_both_headers_export_exact_protection_contract(self):
        for prefix in PATCH_CASES.values():
            header = self.sources[prefix]["include/linux/mtd/spinand.h"]
            for line in REQUIRED_HEADER_LINES:
                with self.subTest(prefix=prefix, line=line):
                    self.assertIn(line, header)

    def test_only_c5f1_parts_gain_block_protection_flag(self):
        models = ("C5F1GM7UE", "C5F1GM7RE", "C5F2GM7UE", "C5F2GM7RE")
        for prefix in PATCH_CASES.values():
            source = self.sources[prefix]["drivers/mtd/nand/spi/chucun.c"]
            self.assertEqual(source.count("SPINAND_HAS_BLOCK_PROTECTION"), 2)
            for model in models:
                block = spinand_info_block(source, model)
                expected = model.startswith("C5F1")
                self.assertEqual(
                    "SPINAND_HAS_BLOCK_PROTECTION" in block,
                    expected,
                    f"{prefix}: {model}",
                )

    def test_uboot_helpers_verify_a0_and_init_every_target(self):
        core = self.sources[UBOOT_PREFIX]["drivers/mtd/nand/spi/core.c"]
        self.assertIn(
            "int spinand_get_block_lock(struct spinand_device *spinand, u8 *lock)",
            core,
        )
        self.assertIn("return spinand_read_reg_op(spinand, REG_BLOCK_LOCK, lock);", core)
        self.assertIn(
            "int spinand_set_block_lock(struct spinand_device *spinand, u8 lock)",
            core,
        )
        self.assertIn("ret = spinand_write_reg_op(spinand, REG_BLOCK_LOCK, lock);", core)
        self.assertIn("ret = spinand_get_block_lock(spinand, &readback);", core)
        self.assertIn("return readback == lock ? 0 : -EIO;", core)
        self.assertIn(
            "static int spinand_lock_block(struct spinand_device *spinand, u8 lock)",
            core,
        )
        self.assertIn("for (i = 0; i < nand->memorg.ntargets; i++)", core)
        self.assertIn("spinand_lock_block(spinand, HWP_EN)", core)
        self.assertIn("spinand_set_block_lock(spinand, BL_ALL_UNLOCKED)", core)
        self.assertRegex(
            core,
            r"if \(spinand->flags & SPINAND_HAS_BLOCK_PROTECTION\)\s*\{?\s*"
            r"ret = spinand_set_block_lock\(spinand, BL_LOWER_3_4_LOCKED\);",
        )

    def test_kernel_helpers_callbacks_init_and_resume_enforce_exact_a0(self):
        core = self.sources[KERNEL_PREFIX]["drivers/mtd/nand/spi/core.c"]
        for signature in (
            "int spinand_get_block_lock(struct spinand_device *spinand, u8 *lock)",
            "int spinand_set_block_lock(struct spinand_device *spinand, u8 lock)",
            "static int spinand_mtd_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len)",
            "static int spinand_mtd_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len)",
            "static int spinand_mtd_is_locked(struct mtd_info *mtd, loff_t ofs, uint64_t len)",
        ):
            self.assertIn(signature, core)
        self.assertIn("return readback == lock ? 0 : -EIO;", core)
        self.assertIn(
            "if (ofs < 0 || len > mtd->size || ofs > mtd->size - len)", core
        )
        self.assertIn("spinand_set_block_lock(spinand, BL_LOWER_3_4_LOCKED)", core)
        self.assertIn("spinand_set_block_lock(spinand, BL_ALL_UNLOCKED)", core)
        self.assertIn("ret = spinand_get_block_lock(spinand, &lock);", core)
        self.assertIn("return lock == BL_LOWER_3_4_LOCKED ? 1 : 0;", core)
        self.assertIn("spinand_lock_block(spinand, HWP_EN)", core)
        self.assertRegex(
            core,
            r"if \(spinand->flags & SPINAND_HAS_BLOCK_PROTECTION\) \{\s*"
            r"mtd->_lock = spinand_mtd_lock;\s*"
            r"mtd->_unlock = spinand_mtd_unlock;\s*"
            r"mtd->_is_locked = spinand_mtd_is_locked;\s*}",
        )
        init = re.search(
            r"static int spinand_init\(struct spinand_device \*spinand\)"
            r".*?(?=\nstatic void spinand_cleanup)",
            core,
            re.DOTALL,
        ).group(0)
        self.assertLess(
            init.index("spinand->scratchbuf = kzalloc"),
            init.index("spinand_init_flash(spinand)"),
        )
        resume = re.search(
            r"static void spinand_mtd_resume\(.*?\n}\n", core, re.DOTALL
        ).group(0)
        self.assertIn("spinand_init_flash(spinand)", resume)

    def test_added_lines_do_not_touch_forbidden_register_policies(self):
        forbidden = ("REG_CFG", "0xb0", "BPL", "WP#")
        for patch_name, changes in self.patch_changes.items():
            added = "\n".join(
                line for change in changes.values() for line in change["added"]
            )
            for token in forbidden:
                with self.subTest(patch=patch_name, token=token):
                    self.assertNotIn(token, added)


if __name__ == "__main__":
    unittest.main()
