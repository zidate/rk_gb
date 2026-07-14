# RV1106 A/B Upgrade and SPI NAND Write Protection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 RV1106G/XWR60440 的 128 MiB C5F1GM7RE SPI NAND 上实现 boot/rootfs/oem A/B、Linux 网络 OTA、U-Boot 独立文件 SD 升级和前 96 MiB 芯片块保护。

**Architecture:** Rockchip SDK 正式源码作为 U-Boot、kernel 和 `rk_ota` 的实现基线，修改结果以可重复应用的补丁保存在当前仓库；当前仓库负责应用层调用、A/B 挂载脚本和镜像打包。U-Boot 与 Linux 共用 `AvbABData` 语义和 A0=`0x00/0x2A` 保护协议，任何升级都先写非活动槽、恢复保护，最后更新 `misc`。

**Tech Stack:** Rockchip RV1106 vendor SDK、U-Boot C、Linux MTD/SPI NAND C、C/C++ `rk_ota`、BusyBox shell、Python `unittest`、GNU Make、UBI/squashfs。

---

## File map

当前仓库中新增或修改：

- Create: `vendor/rv1106_sdk_patches/README.md` — SDK 基线、补丁顺序和构建说明。
- Create: `vendor/rv1106_sdk_patches/apply.sh` — 向可写 SDK 树顺序应用补丁。
- Create: `vendor/rv1106_sdk_patches/0001-ab-layout.patch` — XWR60440 分区与 A/B 启动补丁。
- Create: `vendor/rv1106_sdk_patches/0002-uboot-sd-protection.patch` — U-Boot SD 升级器与 A0 保护补丁。
- Create: `vendor/rv1106_sdk_patches/0003-kernel-protection.patch` — Linux SPI NAND MTD lock/unlock 补丁。
- Create: `vendor/rv1106_sdk_patches/0004-rk-ota.patch` — Linux `rk_ota` 非活动槽写入补丁。
- Create: `tools/tests/test_rv1106_ab_layout.py` — 分区、镜像大小和打包静态测试。
- Create: `tools/tests/test_rv1106_sd_policy.py` — SD 文件组合与切槽策略测试。
- Modify: `packaging/Makefile` — 10 MiB rootfs、32 MiB oem、31.75 MiB userdata 和 A/B 输出。
- Modify: `packaging/image/merge.ini` — 工厂镜像 A 槽布局，B 保持空。
- Modify: `packaging/image/partition.ini` — 完整 128 MiB 物理布局。
- Modify: `packaging/image/upgrade.ini` — 删除旧单槽升级布局。
- Modify: `packaging/image/Makefile` — 生成网络 OTA tar 和 SD 独立镜像目录。
- Modify: `packaging/image/tools-sourcecode/packaging-raw/packaging.c` — 支持 A/B 分区名和无镜像的空 B 槽。
- Rebuild: `packaging/image/packaging-raw` — 使用修改后的源码重新生成工厂镜像工具。
- Modify: `packaging/rootfs_pub/etc/init.d/S20linkmount` — 根据 slot suffix 建立逻辑分区链接并挂载 oem。
- Create: `App/Update/AbUpdate.h` — 网络 A/B 升级入口。
- Create: `App/Update/AbUpdate.cpp` — 无 shell 拼接地执行 `/usr/bin/rk_ota`。
- Modify: `App/Protocol/ProtocolManager.cpp` — 将 `DG_update()` 切换为 `AbUpdateApply()`。
- Modify: `App/CMakeLists.txt` — 编译新的 A/B 升级包装器。
- Modify: `App/Protocol/gb28181/sdk_port/CMakeLists.txt` — 保持备用构建入口一致。

SDK 补丁覆盖的实际文件：

- `project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-NONE-RV1106_IPC38_DEMO_V10-IPC-XWR60440.mk`
- `sysdrv/source/uboot/u-boot/configs/rv1106-XWR60440_defconfig`
- `sysdrv/source/uboot/u-boot/common/android_ab.c`
- `sysdrv/source/uboot/u-boot/lib/avb/libavb_ab/avb_ab_flow.c`
- `sysdrv/source/uboot/u-boot/include/linux/mtd/spinand.h`
- `sysdrv/source/uboot/u-boot/drivers/mtd/nand/spi/core.c`
- `sysdrv/source/uboot/u-boot/drivers/mtd/nand/spi/chucun.c`
- `sysdrv/source/uboot/u-boot/cmd/Kconfig`
- `sysdrv/source/uboot/u-boot/cmd/Makefile`
- Create: `sysdrv/source/uboot/u-boot/cmd/ab_sd_update.c`
- `sysdrv/source/kernel/include/linux/mtd/spinand.h`
- `sysdrv/source/kernel/drivers/mtd/nand/spi/core.c`
- `sysdrv/source/kernel/drivers/mtd/nand/spi/chucun.c`
- `sysdrv/tools/board/rk_ota/src/bootloader.h`
- `sysdrv/tools/board/rk_ota/src/bootloader.cpp`
- `sysdrv/tools/board/rk_ota/src/main.cpp`
- `sysdrv/tools/board/rk_ota/Makefile`

`driver/uboot/`、`DG_sdupdate.c` 和 `sd_update.txt` 不修改。

### Task 1: Create the reproducible SDK patch workspace

**Files:**
- Create: `vendor/rv1106_sdk_patches/README.md`
- Create: `vendor/rv1106_sdk_patches/apply.sh`

- [ ] **Step 1: Create the patch application smoke test**

Create `tools/tests/test_rv1106_sdk_patchset.py`:

```python
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
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```bash
python3 -m unittest tools.tests.test_rv1106_sdk_patchset -v
```

Expected: FAIL because `vendor/rv1106_sdk_patches/series` does not exist.

- [ ] **Step 3: Create the patchset scaffold**

Create `vendor/rv1106_sdk_patches/series`:

```text
0001-ab-layout.patch
0002-uboot-sd-protection.patch
0003-kernel-protection.patch
0004-rk-ota.patch
```

Create executable `vendor/rv1106_sdk_patches/apply.sh`:

```sh
#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <RV1106_IPC_SDK-root>" >&2
    exit 2
fi

sdk_root=$(realpath "$1")
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

test -f "$sdk_root/project/build.sh"
while IFS= read -r patch_name; do
    test -n "$patch_name" || continue
    patch -d "$sdk_root" -p1 --forward < "$script_dir/$patch_name"
done < "$script_dir/series"
```

Create `README.md` with the baseline archive path, the temporary writable SDK path `/tmp/rv1106-ab-sdk/RV1106_IPC_SDK`, the XWR60440 board config name, and the exact `apply.sh` command.

- [ ] **Step 4: Run the smoke test**

Run the same unittest command.

Expected: PASS.

- [ ] **Step 5: Commit the scaffold**

```bash
git add vendor/rv1106_sdk_patches tools/tests/test_rv1106_sdk_patchset.py
git commit -m "build: scaffold RV1106 SDK patchset"
```

### Task 2: Lock the A/B partition layout with tests

**Files:**
- Create: `tools/tests/test_rv1106_ab_layout.py`
- Modify: `packaging/image/partition.ini`
- Modify: `packaging/image/merge.ini`
- Modify: `packaging/image/upgrade.ini`
- Modify in SDK worktree: `project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-NONE-RV1106_IPC38_DEMO_V10-IPC-XWR60440.mk`
- Create patch: `vendor/rv1106_sdk_patches/0001-ab-layout.patch`

- [ ] **Step 1: Write the failing layout test**

The test must assert these byte ranges:

```python
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
```

Parse the INI sections with `configparser`, assert exact `start` and `size`, assert every start and size is divisible by `0x20000`, and assert the final end is `0x8000000`.

- [ ] **Step 2: Run the test and verify old single-slot INIs fail**

```bash
python3 -m unittest tools.tests.test_rv1106_ab_layout -v
```

Expected: FAIL at missing `boot_a`.

- [ ] **Step 3: Update all three INI files**

Use the exact `EXPECTED` starts and sizes. `merge.ini` and `partition.ini` include the full physical table but associate files only with `env/idblock/uboot/boot_a/rootfs_a/oem_a/userdata`; B sections have no image file so the factory result leaves B empty. Keep `upgrade.ini` as a deprecated compatibility input containing only boot/rootfs/oem, and remove every invocation of `packaging-update upgrade.ini` from the active Makefile path.

- [ ] **Step 4: Update the SDK BoardConfig**

Set:

```sh
export RK_PARTITION_CMD_IN_ENV="256K(env),1M@256K(idblock),1M(uboot),4M(boot_a),4M(boot_b),10M(rootfs_a),10M(rootfs_b),32M(oem_a),32M(oem_b),1792K(reserved),256K(misc),-(userdata)"
```

Keep `RK_MISC=wipe_all-misc.img`. Configure build inputs only for A images; do not duplicate A images into B during factory packing.

- [ ] **Step 5: Run the layout test**

Expected: PASS with 12 contiguous partitions and end `0x8000000`.

- [ ] **Step 6: Generate and commit the SDK layout patch**

Generate `0001-ab-layout.patch` relative to the SDK root, then run:

```bash
git add packaging/image tools/tests/test_rv1106_ab_layout.py vendor/rv1106_sdk_patches/0001-ab-layout.patch
git commit -m "feat: define RV1106 A/B flash layout"
```

### Task 3: Make invalid A/B metadata A-only and export the slot suffix

**Files:**
- Modify in SDK: `sysdrv/source/uboot/u-boot/lib/avb/libavb_ab/avb_ab_flow.c`
- Modify in SDK: `sysdrv/source/uboot/u-boot/common/android_ab.c`
- Modify in SDK: `sysdrv/source/uboot/u-boot/configs/rv1106-XWR60440_defconfig`
- Modify: `packaging/rootfs_pub/etc/init.d/S20linkmount`
- Extend patch: `vendor/rv1106_sdk_patches/0001-ab-layout.patch`

- [ ] **Step 1: Add failing static assertions to the layout test**

Assert the patched SDK text contains:

```c
data->slots[0].priority = AVB_AB_MAX_PRIORITY;
data->slots[0].tries_remaining = AVB_AB_MAX_TRIES_REMAINING;
data->slots[1].priority = 0;
data->slots[1].tries_remaining = 0;
data->slots[1].successful_boot = 0;
```

Also assert `CONFIG_AVB_LIBAVB_AB=y` and that `CONFIG_ROCKCHIP_CMD` names `ab_sd_update`, not `sd_update`.

- [ ] **Step 2: Run the test and verify it fails on SDK defaults**

Expected: FAIL because B currently gets priority 14 and seven tries.

- [ ] **Step 3: Implement A-only initialization**

Change both `avb_ab_data_init()` and `android_boot_control_default()` so invalid CRC or missing metadata yields A priority 15/tries 7 and B all zero. Keep CRC generation unchanged.

- [ ] **Step 4: Export the selected slot in bootargs**

After `ab_get_slot_suffix()` succeeds, add exactly one token:

```c
snprintf(slot_arg, sizeof(slot_arg), "androidboot.slot_suffix=%s", slot_suffix);
env_update("bootargs", slot_arg);
```

Resolve the root partition with `rootfs_a` or `rootfs_b`, not `system_a/system_b`, before generating `ubi.mtd=<index> root=/dev/ubiblock0_0`.

- [ ] **Step 5: Make Linux mount the matching OEM slot**

In `S20linkmount`, parse `_a` or `_b` from `/proc/cmdline`, create physical by-name links for all 12 partitions, then point the logical `oem` link at `oem_a` or `oem_b`. Default to `_a` only when the token is absent.

- [ ] **Step 6: Run tests and commit**

```bash
python3 -m unittest tools.tests.test_rv1106_ab_layout -v
git add packaging/rootfs_pub/etc/init.d/S20linkmount vendor/rv1106_sdk_patches/0001-ab-layout.patch tools/tests/test_rv1106_ab_layout.py
git commit -m "feat: select matching RV1106 rootfs and OEM slots"
```

### Task 4: Add C5F1GM7RE A0 protection to U-Boot and Linux

**Files:**
- Modify in SDK U-Boot: `include/linux/mtd/spinand.h`, `drivers/mtd/nand/spi/core.c`, `drivers/mtd/nand/spi/chucun.c`
- Modify in SDK kernel: `include/linux/mtd/spinand.h`, `drivers/mtd/nand/spi/core.c`, `drivers/mtd/nand/spi/chucun.c`
- Create patches: `0002-uboot-sd-protection.patch`, `0003-kernel-protection.patch`
- Create: `tools/tests/test_c5f1gm7re_protection.py`

- [ ] **Step 1: Write failing constant/API tests**

The Python test must assert both SDK headers define:

```c
#define SPINAND_HAS_BLOCK_PROTECTION BIT(2)
#define BL_ALL_UNLOCKED 0x00
#define BL_LOWER_3_4_LOCKED 0x2a
int spinand_get_block_lock(struct spinand_device *spinand, u8 *lock);
int spinand_set_block_lock(struct spinand_device *spinand, u8 lock);
```

It must also assert only CHUCUN `C5F1GM7UE/C5F1GM7RE` entries add `SPINAND_HAS_BLOCK_PROTECTION`.

- [ ] **Step 2: Run and verify failure**

```bash
python3 -m unittest tools.tests.test_c5f1gm7re_protection -v
```

Expected: FAIL because the lower-three-quarter constant and public APIs do not exist.

- [ ] **Step 3: Implement public feature-register helpers in both cores**

Wrap the existing private `spinand_read_reg_op()` and `spinand_write_reg_op()`:

```c
int spinand_get_block_lock(struct spinand_device *spinand, u8 *lock)
{
    return spinand_read_reg_op(spinand, REG_BLOCK_LOCK, lock);
}

int spinand_set_block_lock(struct spinand_device *spinand, u8 lock)
{
    u8 actual;
    int ret = spinand_write_reg_op(spinand, REG_BLOCK_LOCK, lock);

    if (ret)
        return ret;
    ret = spinand_read_reg_op(spinand, REG_BLOCK_LOCK, &actual);
    if (ret)
        return ret;
    return actual == lock ? 0 : -EIO;
}
```

- [ ] **Step 4: Preserve protection at initialization**

After the vendor core's existing all-unlocked initialization, test `SPINAND_HAS_BLOCK_PROTECTION`; for C5F1GM7RE set and verify `BL_LOWER_3_4_LOCKED`. Do this in both U-Boot and Linux so every boot/resume normalizes A0 to `0x2A`.

- [ ] **Step 5: Wire Linux standard MTD lock ioctls**

For protected CHUCUN devices set:

```c
mtd->_lock = spinand_mtd_lock;
mtd->_unlock = spinand_mtd_unlock;
mtd->_is_locked = spinand_mtd_is_locked;
```

`_unlock` writes `0x00`; `_lock` writes `0x2A`; `_is_locked` reads A0 and returns 1 only for `0x2A`. Reject ranges extending beyond the master device. Do not modify B0, QE, BRWD, or BPL.

- [ ] **Step 6: Run static tests and compile both trees**

```bash
python3 -m unittest tools.tests.test_c5f1gm7re_protection -v
./build.sh uboot
./build.sh kernel
```

Expected: test PASS; `uboot.img` and kernel `boot.img` build successfully with no undefined MTD callbacks.

- [ ] **Step 7: Generate patches and commit**

```bash
git add vendor/rv1106_sdk_patches/0002-uboot-sd-protection.patch vendor/rv1106_sdk_patches/0003-kernel-protection.patch tools/tests/test_c5f1gm7re_protection.py
git commit -m "feat: add C5F1GM7RE block protection control"
```

### Task 5: Implement U-Boot SD file policy before flash I/O

**Files:**
- Create in SDK: `sysdrv/source/uboot/u-boot/cmd/ab_sd_update.c`
- Modify in SDK: `cmd/Kconfig`, `cmd/Makefile`, `configs/rv1106-XWR60440_defconfig`
- Create: `tools/tests/test_rv1106_sd_policy.py`
- Extend patch: `0002-uboot-sd-protection.patch`

- [ ] **Step 1: Write the failing policy test**

Test all 16 file-presence masks. The required rule is:

```python
BOOT = 1 << 0
ROOTFS = 1 << 1
OEM = 1 << 2
UBOOT = 1 << 3
SLOT_TRIO = BOOT | ROOTFS | OEM

def should_switch(mask):
    return (mask & SLOT_TRIO) == SLOT_TRIO
```

Assert U-Boot alone never switches, every one/two-slot-image combination never switches, and the complete trio switches whether or not U-Boot is present.

- [ ] **Step 2: Run and verify failure**

Expected: FAIL because the SDK command source and symbols do not exist.

- [ ] **Step 3: Add a pure policy function to `ab_sd_update.c`**

```c
#define AB_SD_BOOT   BIT(0)
#define AB_SD_ROOTFS BIT(1)
#define AB_SD_OEM    BIT(2)
#define AB_SD_UBOOT  BIT(3)
#define AB_SD_SLOT_TRIO (AB_SD_BOOT | AB_SD_ROOTFS | AB_SD_OEM)

static bool ab_sd_should_switch(u32 present)
{
    return (present & AB_SD_SLOT_TRIO) == AB_SD_SLOT_TRIO;
}
```

Represent the four files in a fixed table with maximum sizes 1, 4, 10 and 32 MiB. Use `fs_set_blk_dev("mmc", "1", FS_TYPE_FAT)`, `fs_exists()` and `fs_size()`.

- [ ] **Step 4: Determine the target slot at runtime**

Call `ab_get_slot_suffix()`. Map `_a` to target suffix `_b`, `_b` to `_a`, and invalid metadata to `_a` after the A-only initializer repairs `misc`. Construct `boot_<slot>`, `rootfs_<slot>` and `oem_<slot>` names; U-Boot always maps to `uboot`.

- [ ] **Step 5: Register and auto-run the command**

Add `CONFIG_CMD_AB_SD_UPDATE`; compile `ab_sd_update.o`; register `U_BOOT_CMD(ab_sd_update, 1, 1, do_ab_sd_update, ...)`. Set:

```text
CONFIG_ROCKCHIP_CMD="ab_sd_update -"
CONFIG_CMD_AB_SD_UPDATE=y
# CONFIG_CMD_SCRIPT_UPDATE is not set
```

- [ ] **Step 6: Run policy tests and build U-Boot**

Expected: all 16 policy cases PASS and U-Boot links the new command.

- [ ] **Step 7: Commit**

```bash
git add tools/tests/test_rv1106_sd_policy.py vendor/rv1106_sdk_patches/0002-uboot-sd-protection.patch
git commit -m "feat: add RV1106 U-Boot SD update policy"
```

### Task 6: Implement bad-block-aware U-Boot SD writes

**Files:**
- Modify in SDK: `cmd/ab_sd_update.c`
- Extend patch: `0002-uboot-sd-protection.patch`

- [ ] **Step 1: Add source-level failure tests**

Assert the command contains checks for `mtd_block_isbad`, `mtd_erase`, `mtd_write_oob`, `mtd_read`, `spinand_set_block_lock`, and `avb_ab_mark_slot_active`, and asserts `avb_ab_mark_slot_active` appears after relocking.

- [ ] **Step 2: Implement chunked FAT reads**

Allocate one eraseblock-sized buffer. Read each file with `fs_read(filename, addr, file_offset, chunk_len, &actual)`. Do not load a complete image into RAM.

- [ ] **Step 3: Implement partition writes**

Use `mtd_probe_devices()` and `get_mtd_device_nm(partition_name)`. Erase good blocks only, skip bad blocks with `mtd_block_isbad()`, write page-aligned data with `mtd_write_oob()`, and ensure the logical image fits within the available good blocks. Read back each chunk and compare before advancing.

- [ ] **Step 4: Implement the ordered transaction**

The function order must be:

```c
spinand_set_block_lock(spinand, BL_ALL_UNLOCKED);
write_present_slot_images();
if (uboot_present)
    write_uboot_last();
spinand_set_block_lock(spinand, BL_LOWER_3_4_LOCKED);
if (ab_sd_should_switch(present))
    avb_ab_mark_slot_active(ab_ops, target_slot);
```

Any failure before `avb_ab_mark_slot_active()` returns failure with `misc` unchanged. Always attempt relock before returning.

- [ ] **Step 5: Build and inspect symbols**

```bash
./build.sh uboot
arm-rockchip830-linux-uclibcgnueabihf-nm sysdrv/source/uboot/u-boot/u-boot | grep ab_sd_update
```

Expected: U-Boot build succeeds and the command/policy symbols are present in the ELF or map output used by this SDK.

- [ ] **Step 6: Commit**

```bash
git add vendor/rv1106_sdk_patches/0002-uboot-sd-protection.patch tools/tests/test_rv1106_sd_policy.py
git commit -m "feat: write SD images through SPI NAND MTD"
```

### Task 7: Adapt `rk_ota` to boot/rootfs/oem only

**Files:**
- Modify in SDK: `sysdrv/tools/board/rk_ota/src/bootloader.h`
- Modify in SDK: `sysdrv/tools/board/rk_ota/src/bootloader.cpp`
- Modify in SDK: `sysdrv/tools/board/rk_ota/src/main.cpp`
- Modify in SDK: `sysdrv/tools/board/rk_ota/Makefile`
- Create patch: `0004-rk-ota.patch`

- [ ] **Step 1: Add a host policy test**

Assert network OTA accepts exactly `boot.img`, `rootfs.img`, `oem.img`; rejects `uboot.img`; maps current A to `_b` and current B to `_a`; and calls slot activation only after relock.

- [ ] **Step 2: Replace SDK partition constants**

Use:

```c
#define AB_BOOT_NAME "boot"
#define AB_ROOTFS_NAME "rootfs"
#define AB_OEM_NAME "oem"
```

Remove `AB_UBOOT_NAME` from the `all` path and require the complete trio before any flash write.

- [ ] **Step 3: Replace shell-based MTD writing**

Remove `system("flash_erase ...")` and `system("nandwrite ...")`. Open the MTD character device, call `MEMUNLOCK`, use the existing `mtdutils` bad-block-aware writer, fsync, and perform MD5 readback. After all three partitions pass, call `MEMLOCK` and `MEMISLOCKED` to confirm protection.

- [ ] **Step 4: Keep `misc` last**

Only after `MEMISLOCKED` reports protected call `setSlotActivity()`. If any image, verification, relock or metadata write fails, return nonzero and do not reboot.

- [ ] **Step 5: Build `rk_ota`**

```bash
make -C sysdrv/tools/board/rk_ota clean all
```

Expected: `out/usr/bin/rk_ota` exists and `rk_ota --help` lists only boot/rootfs/oem/all, with all meaning the complete trio.

- [ ] **Step 6: Commit the patch**

```bash
git add vendor/rv1106_sdk_patches/0004-rk-ota.patch
git commit -m "feat: adapt rk_ota for RV1106 A/B slots"
```

### Task 8: Integrate network OTA into the application without shell injection

**Files:**
- Create: `App/Update/AbUpdate.h`
- Create: `App/Update/AbUpdate.cpp`
- Modify: `App/Protocol/ProtocolManager.cpp`
- Modify: `App/CMakeLists.txt`
- Modify: `App/Protocol/gb28181/sdk_port/CMakeLists.txt`
- Create: `tools/tests/test_ab_update_wrapper.py`

- [ ] **Step 1: Write a failing wrapper test**

The test compiles `AbUpdate.cpp` with a fake `execv` seam and asserts the argv array is exactly:

```text
/usr/bin/rk_ota
--misc=update
--tar_path=/tmp/upgrade.tar
--partition=all
--reboot
```

It also passes a path containing shell metacharacters and asserts no `/bin/sh` process is invoked.

- [ ] **Step 2: Implement `AbUpdateApply()`**

Use `fork()`, `execv()` and `waitpid()`; return 0 only for a normal child exit code 0. Do not use `system()` or concatenate a command string.

Header:

```c
#ifndef APP_UPDATE_AB_UPDATE_H
#define APP_UPDATE_AB_UPDATE_H
int AbUpdateApply(const char *package_path, bool reboot_after_success);
#endif
```

- [ ] **Step 3: Replace the old call site**

Change `ProtocolManager::GbUpgradeApplyThread()` from `DG_update()` to `AbUpdateApply()`. Change `kGbUpgradePackagePath` to `/tmp/upgrade.tar` and its temporary path to `/tmp/upgrade.tar.download`. Remove its unconditional `reboot -f`; `rk_ota --reboot` owns reboot only after a successful transaction.

- [ ] **Step 4: Change both CMake source lists**

Replace `Update/update.cpp` with `Update/AbUpdate.cpp`. Leave the old source file unmodified but uncompiled.

- [ ] **Step 5: Run tests and build the application**

```bash
python3 -m unittest tools.tests.test_ab_update_wrapper -v
./build.sh
```

Expected: wrapper tests PASS and `Bin/dgiot` links without `DG_update` references.

- [ ] **Step 6: Commit**

```bash
git add App/Update App/Protocol/ProtocolManager.cpp App/CMakeLists.txt App/Protocol/gb28181/sdk_port/CMakeLists.txt tools/tests/test_ab_update_wrapper.py
git commit -m "feat: route network upgrades through rk_ota"
```

### Task 9: Generate A/B UBI images and delivery artifacts

**Files:**
- Modify: `packaging/Makefile`
- Modify: `packaging/image/Makefile`
- Modify: `packaging/image/merge.ini`
- Modify: `packaging/image/partition.ini`
- Modify: `packaging/image/upgrade.ini`
- Modify: `packaging/image/tools-sourcecode/packaging-raw/packaging.c`
- Rebuild: `packaging/image/packaging-raw`
- Extend: `tools/tests/test_rv1106_ab_layout.py`

- [ ] **Step 1: Add failing artifact assertions**

Assert packaging uses:

```text
rootfs size = 10*0x100000
oem size = 32*0x100000
userdata size = 0x1FC0000
```

Assert expected outputs are `Release/ota_ab.tar` and directory `Release/sd/` containing independent `uboot.img`, `boot.img`, `rootfs.img`, `oem.img`.

- [ ] **Step 2: Update UBI generation**

Use the `rootfs.img/oem.img/userdata.img` symlinks generated by `mkfs_ubi.sh` rather than hard-coded truncated filenames. Fail the make target when an image exceeds its partition size.

- [ ] **Step 3: Make the raw packer understand empty B slots**

Replace the old fixed six-partition parser with this ordered section table:

```c
static const char *const partition_sections[] = {
    "env", "idblock", "uboot", "boot_a", "boot_b",
    "rootfs_a", "rootfs_b", "oem_a", "oem_b",
    "reserved", "misc", "userdata",
};
```

Require `start` and `size` for every section. Treat a missing or empty `file` key as an erased partition: seek/pad the output with `0xff` for its declared size without opening an image. Reject overlapping sections, non-128-KiB alignment, files larger than partitions, and any final size other than `0x8000000`. Rebuild `packaging-raw` from `tools-sourcecode/packaging-raw/Makefile` before using it.

```bash
make -C packaging/image/tools-sourcecode/packaging-raw clean all
cp packaging/image/tools-sourcecode/packaging-raw/packaging-raw packaging/image/packaging-raw
```

- [ ] **Step 4: Produce network and SD outputs**

Create `ota_ab.tar` containing only boot/rootfs/oem. Copy independent images to `Release/sd/`; U-Boot is present only in the SD directory. Do not copy `sd_update.txt`.

- [ ] **Step 5: Run packaging tests and build**

```bash
python3 -m unittest tools.tests.test_rv1106_ab_layout -v
make -C packaging CROSS=/home/jerry/silver/RK/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-
tar -tf Release/ota_ab.tar
```

Expected tar listing, exactly:

```text
boot.img
rootfs.img
oem.img
```

- [ ] **Step 6: Commit**

```bash
git add packaging/image/tools-sourcecode/packaging-raw/packaging.c packaging/image/packaging-raw packaging tools/tests/test_rv1106_ab_layout.py
git commit -m "build: package RV1106 A/B OTA and SD images"
```

### Task 10: Verify the complete SDK patchset and builds

**Files:**
- Modify: `vendor/rv1106_sdk_patches/README.md`
- Update: `helloagents/wiki/modules/rv1106_ab_upgrade.md`

- [ ] **Step 1: Extract a clean writable SDK tree**

```bash
rm -rf /tmp/rv1106-ab-sdk
mkdir -p /tmp/rv1106-ab-sdk
tar -xzf /home/jerry/lhy/cmiot/RV1106_IPC_SDK.tar.gz -C /tmp/rv1106-ab-sdk
```

The archive has a known damaged tail. Accept tar exit 2 only after verifying the BoardConfig, U-Boot, kernel and `rk_ota` files listed in the file map all exist.

- [ ] **Step 2: Apply all patches**

```bash
vendor/rv1106_sdk_patches/apply.sh /tmp/rv1106-ab-sdk/RV1106_IPC_SDK
```

Expected: all four patches apply without offsets, rejects or reversed-patch warnings.

- [ ] **Step 3: Run the complete host test suite**

```bash
python3 -m unittest \
  tools.tests.test_rv1106_sdk_patchset \
  tools.tests.test_rv1106_ab_layout \
  tools.tests.test_rv1106_sd_policy \
  tools.tests.test_c5f1gm7re_protection \
  tools.tests.test_ab_update_wrapper -v
```

Expected: all tests PASS.

- [ ] **Step 4: Build SDK U-Boot, kernel, rootfs and rk_ota**

From the patched SDK root select the XWR60440 BoardConfig, then run:

```bash
./build.sh uboot
./build.sh kernel
make -C sysdrv/tools/board/rk_ota clean all
```

Expected: new U-Boot, boot image and `rk_ota` binary are produced without warnings promoted to errors.

- [ ] **Step 5: Verify forbidden paths are untouched**

```bash
git status --short driver/uboot
git diff -- driver/uboot
rg -n "sd_update\.txt|DG_sdupdate" vendor/rv1106_sdk_patches packaging App/Update
```

Expected: no `driver/uboot` changes; references to forbidden mechanisms appear only in documentation explaining their exclusion.

- [ ] **Step 6: Update knowledge and commit**

Record build commands, produced hashes, A0 API names, partition indices and any SDK-version-specific deviations in `helloagents/wiki/modules/rv1106_ab_upgrade.md`.

```bash
git add vendor/rv1106_sdk_patches/README.md helloagents/wiki/modules/rv1106_ab_upgrade.md
git commit -m "docs: record RV1106 A/B build and verification"
```

### Task 11: Hardware acceptance and power-cut validation

**Files:**
- Create: `helloagents/cases/20260714_rv1106_ab_powercut_validation.md`

- [ ] **Step 1: Validate partition discovery**

Capture U-Boot `mtd list`, Linux `/proc/mtd`, `/proc/cmdline`, mounted rootfs and `/oem`. Expected partition starts and sizes match the design; slot suffix, rootfs and OEM all name the same slot.

- [ ] **Step 2: Validate normal protection**

Read A0 in U-Boot and Linux; expected `0x2A`. Attempt PROGRAM/ERASE in a protected disposable block and expect P_FAIL/E_FAIL. Write and erase a test block in userdata and expect success.

- [ ] **Step 3: Validate SD file combinations**

Run U-Boot SD tests for U-Boot-only, each single slot image, each two-image combination, full trio, and trio plus U-Boot. Confirm only the full trio changes `misc`.

- [ ] **Step 4: Validate network A-to-B and B-to-A**

For each direction, capture pre/post `rk_ota --misc=display`, partition hashes, A0 values and boot logs. Expected target tries are 7 and source remains bootable.

- [ ] **Step 5: Validate power loss before misc switch**

Cut power during boot, rootfs and OEM writes separately. On every restart confirm the previous slot boots, A0 returns to `0x2A`, and rerunning the upgrade rewrites the target from offset zero.

- [ ] **Step 6: Validate corrupt misc fallback**

Corrupt only the 32-byte A/B metadata record, reboot, and confirm A priority 15/tries 7 while B is priority 0/tries 0.

- [ ] **Step 7: Record the single-U-Boot limitation without destructive testing**

Do not deliberately cut power while erasing U-Boot on the only board. Record that single-copy U-Boot power-loss safety is not claimed and requires a recoverable fixture before destructive validation.

- [ ] **Step 8: Commit validation evidence**

```bash
git add helloagents/cases/20260714_rv1106_ab_powercut_validation.md
git commit -m "test: document RV1106 A/B hardware validation"
```
