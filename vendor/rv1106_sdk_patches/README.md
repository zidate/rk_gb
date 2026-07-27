# RV1106 SDK patch set

This directory contains the ordered, replayable patch series for the RV1106G / XWR60440 vendor SDK. The `series` file is the authoritative patch application order; `apply.sh` reads it from top to bottom and applies each listed patch to a writable SDK tree.

The vendor SDK baseline archive is:

`/home/jerry/lhy/cmiot/RV1106_IPC_SDK.tar.gz`

Use this temporary writable SDK tree:

`/tmp/rv1106-ab-sdk/RV1106_IPC_SDK`

The XWR60440 board configuration is:

`BoardConfig-SPI_NAND-NONE-RV1106_IPC38_DEMO_V10-IPC-XWR60440.mk`

From the repository root, apply the patch series with:

```sh
vendor/rv1106_sdk_patches/apply.sh /tmp/rv1106-ab-sdk/RV1106_IPC_SDK
```

## Complete source snapshots

`source/` contains the final versions of all 18 SDK files changed by the patch
series, preserving their SDK-relative paths. Prefer the ordered patch series
for reviewable integration. If a colleague's SDK has local context changes and
`patch`/`git apply` cannot apply cleanly, compare and copy the matching files
from `source/` instead of using files from one of the intermediate delivery
archives.

The snapshots cover the board layout, environment generation, U-Boot A/B and
SD updater, U-Boot/Linux C5F1 block protection, and Linux `rk_ota` paths. They
do not include generated images or toolchain binaries.

## Build rk_ota

Run the following commands from a complete `RV1106_IPC_SDK` tree after applying
the patch series or copying the final source snapshots:

```sh
cd /path/to/RV1106_IPC_SDK
source envsetup.sh
./build.sh lunch
# Select the XWR60440 RV1106 SPI-NAND board configuration.
make -C sysdrv/tools/board/rk_ota distclean all
```

If the SDK board/toolchain environment is already configured, only the final
`make` command is required. The output is:

```text
sysdrv/tools/board/rk_ota/out/usr/bin/rk_ota
```

Verify and install it with:

```sh
file sysdrv/tools/board/rk_ota/out/usr/bin/rk_ota
sha256sum sysdrv/tools/board/rk_ota/out/usr/bin/rk_ota
cp sysdrv/tools/board/rk_ota/out/usr/bin/rk_ota /path/to/rootfs/usr/bin/rk_ota
```

The target Makefile builds the SDK OpenSSL/Zlib dependencies first. The result
must be the ARM EABI5/uClibc binary; do not replace it with a host build.

## Build verification

The first seven patches were applied in `series` order to a temporary SDK tree
with the XWR60440 board configuration selected, and the affected targets were
built with the vendor toolchain. Patch `0008` was then checked against both
environment build entry points. The normal `./build.sh env` path generates
`.env.txt` in `project/build.sh`; direct `make -C sysdrv env` uses
`sysdrv/Makefile`. The archive did not contain the ARM toolchain, so the
env-only verification used a `gcc --version` placeholder solely to pass the
script's global compiler gate; `./build.sh env` then ran the real partition
parser, `build_env()`, and SDK host `mkenvimage`:

```sh
cd /tmp/rv1106-task10-sdk/RV1106_IPC_SDK/project
./build.sh clean uboot
./build.sh uboot
./build.sh env
./build.sh kernel
make -C ../sysdrv/tools/board/rk_ota distclean all
```

Patch `0009` fixes the 2026-07-24 board log where all six SD images validated
but fixed-image writing stopped at `MTD partition env not found (err = -19)`.
Fixed images now use the SPI NAND master with bounded physical ranges: env
`0x000000+0x040000`, idblock `0x040000+0x100000`, and U-Boot
`0x140000+0x100000`. Slot images still use their named inactive-slot MTD
partitions. The FAT chunk and verify buffers use `ARCH_DMA_MINALIGN`-aligned
allocation, eliminating `FAT: Misaligned buffer address`. Slot activation is
still deferred until all requested writes, UBI initialization, protection
restore, and verification succeed.

`0008` changes generated environment data, so an old `env.img` is not
interchangeable with the fixed U-Boot. Rebuild both artifacts. Because fixed
images are written after UBI initialization, use staged cards: with the current
new U-Boot, put only the rebuilt `env.img` on the card and reboot after it is
written; then put `boot.img`, `rootfs.img`, and `oem.img` on the card for the A/B
upgrade. If the board still runs a U-Boot without `env.img` support, add an
earlier stage containing only the new `uboot.img` and reboot before the
`env.img` stage. Do not mix the A/B trio into either fixed-image stage.

The host used for this verification needed `dtc`/`fdtget`, a `python` command
that invokes Python 3, and OpenSSL development headers. These are host build
dependencies only and are not part of this patch series.

Verification on 2026-07-15, with the linked Rockchip A/B initializer
correction rebuilt on 2026-07-16, the SD FAT/DOS compatibility fix rebuilt on
2026-07-22, and the U-Boot MTD parser plus temporary-unlock/UBI initialization
fixes rebuilt on 2026-07-23,
produced:

| Artifact | Size (bytes) | SHA-256 |
|---|---:|---|
| `sysdrv/source/uboot/u-boot/uboot.img` | 262144 | `1c783213ba3807597d7df9d66d9869ab1b3e516a88b876ef2c450121de274e81` |
| `sysdrv/source/uboot/u-boot/rv1106_idblock_v1.15.102.img` | 176128 | `40463c554c5a14ae612307518cec71e1a69a611ba059896a8a133039ca8e0c20` |
| `output/image/env.img` | 262144 | `dfdcd3d0ec875806f7ee489e2cd933249c83064585974dabbe4d15a75ca5ad3f` |
| `sysdrv/source/objs_kernel/boot.img` | 2287104 | `eaddad612f01b9346daa713127658e127f1f3f924e3a368147cc4b723615083a` |
| `sysdrv/source/objs_kernel/vmlinux` | 121839252 | `1acf5bbd11f2718acca7fc623da58584f02cae5fe52c3a4fa1f1522439acd0af` |
| `sysdrv/tools/board/rk_ota/out/usr/bin/rk_ota` | 51708 | `497d715349358f7992bdfd1fdf5681c3f3defb10509e0578316349ea40635c49` |

The `0009` rebuild on 2026-07-24 completed `project/build.sh uboot` with the
XWR60440 configuration. Its installed `uboot.img` is 262144 bytes with
SHA-256 `32790494096bb2beb49705b457ca8cc45471bb0c9212e3e63d8bae40ac11afa3`;
`idblock.img` is 176128 bytes with SHA-256
`96ddec15feb836652771515bd7ffc0fbdaac22d6533853a52a140dd6a8d26abe`.
The linked ELF contains the fixed-image success and next-boot slot activation
messages. Thirty-five RV1106 patch/policy tests pass.

The follow-up `0010` OEM-trigger rebuild also completed `project/build.sh
uboot`: `uboot.img` is 262144 bytes with SHA-256
`1c03166030dd12b53bb16f86886c34a8668040a6b30fc3b6f96bad0fee0d7691`, and
`idblock.img` is 176128 bytes with SHA-256
`c7a64bb787e915d0b0568973e0371cd99fa9da99e28fc38ede09f9ba6062a187`.
The focused `0010` policy subset contains 36 tests; the complete A/B,
protection, packaging, SD, `rk_ota`, patchset, application-wrapper, and source
snapshot coverage suite contains 76 tests and passes.

The U-Boot ELF contains `ab_sd_update`, `blk_get_devnum_by_type`,
`spinand_set_block_lock`,
`avb_ab_mark_slot_active`, and the libavb user operations. In this Rockchip
SDK, `CONFIG_AVB_LIBAVB_AB` links `rk_avb_user/rk_ab_ops_user.o`; its invalid
metadata initializer now makes slot A bootable and slot B unbootable. The kernel
`vmlinux` contains `spinand_get_block_lock`, `spinand_set_block_lock`, and
the MTD lock/unlock handlers. `rk_ota` is a 32-bit ARM EABI5 uClibc binary.
The SD updater temporarily forces `mmc 1` to `PART_TYPE_DOS` while probing and
reading FAT files, then restores the original descriptor type on every exit.
It accepts `env.img`, `idblock.img`, `uboot.img`, `boot.img`, `rootfs.img`, and
`oem.img`. The SPI NAND is unlocked only for the write/verify transaction. If
rootfs or oem is written, U-Boot attaches and detaches that UBI partition while
the NAND is still unlocked so the autoresize flag is consumed before restoring
A0=`0x2A` (Lower 3/4, first 96 MiB protected). The Linux OTA path uses the same
ordering through `UBI_IOCATT`/`UBI_IOCDET`, but explicitly rejects env,
idblock, and U-Boot images; its kernel payload remains named `boot.img` because
that image contains the kernel and DTB.
The updater intentionally has no persistent completion marker. If the SD card
and images remain present after a successful boot, the next boot can start
another update toward the then-inactive slot; remove the card or its images
after observing `Activated A/B slot ... for next boot`.

Patch `0010` changes the slot-switch trigger: after a successful transaction,
the presence of `oem.img` is sufficient to activate the target A/B slot. This
also supports an OEM-only SD update; without `oem.img`, fixed images or other
partial content are written without changing the active slot. Any write, UBI,
verification, or protection-restore failure still prevents activation.
The board defconfig supplies `mtdids=spi-nand0=spi-nand0`; the legacy U-Boot
`ubi part` command requires this device mapping in addition to `mtdparts`.
The SDK's `env` target writes an environment variable named `mtdparts`, so its
value must itself begin with `mtdparts=` for the legacy parser. The normal
`./build.sh env` command does not enter `sysdrv/Makefile`'s `env` recipe: it
sets the first `.env.txt` line in `project/build.sh::parse_partition_file()` and
then calls `mkenvimage` from `build_env()`. Patch
`0008-uboot-env-mtdparts-prefix.patch` therefore changes both build entry
points to emit
`mtdparts=mtdparts=spi-nand0:...`; the doubled spelling is intentional (the
first prefix is the environment variable name, the second is the parser's
required value prefix). It also replaces the stale `rk-nand` device name with
the `spi-nand0` name used by Linux and `CONFIG_MTDIDS_DEFAULT`.
The generated SPI NAND `mtdparts` string intentionally retains the SDK's
human-readable binary suffixes, for example
`mtdparts=spi-nand0:256K(env),1M@256K(idblock),...`. Patch
`0005-uboot-mtdparts-unit-suffix.patch` extends U-Boot's
`drivers/mtd/mtdpart.c` parser to consume upper- and lower-case `K`, `M`, and
`G` suffixes for both partition sizes and offsets. The helper starts from
`simple_strtoull()` because this SDK's `ustrtoull()` already scales suffixes;
using it as the helper input would double-scale every unit. Patch `0008` changes
only the SPI NAND environment assignment in `project/build.sh`; the partition
list itself and bootargs preserve the same `K/M` form used by the vendor
partition configuration.

The baseline archive is damaged and a full extraction ends with tar exit code
2. For this verification, the required `project` and `sysdrv` trees were
extracted successfully; every patch target, relevant Makefile, and all three
verified builds were present. Replace the archive before relying on unrelated
SDK components not covered by this patch set.
