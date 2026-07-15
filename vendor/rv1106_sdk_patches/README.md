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

## Build verification

The four patches were applied in `series` order to a temporary SDK tree with
the XWR60440 board configuration selected. The affected targets were then
built with the vendor toolchain:

```sh
cd /tmp/rv1106-task10-sdk/RV1106_IPC_SDK/project
./build.sh uboot
./build.sh kernel
make -C ../sysdrv/tools/board/rk_ota distclean all
```

The host used for this verification needed `dtc`/`fdtget`, a `python` command
that invokes Python 3, and OpenSSL development headers. These are host build
dependencies only and are not part of this patch series.

Verification on 2026-07-15 produced:

| Artifact | Size (bytes) | SHA-256 |
|---|---:|---|
| `sysdrv/source/uboot/u-boot/uboot.img` | 262144 | `9c7188fbd4c08d60e6634c90f663e383d4d0960543fc089d1a2086268f5c2eb8` |
| `sysdrv/source/uboot/u-boot/rv1106_idblock_v1.15.102.img` | 176128 | `69f41f5aa22c59b230509e6675f182249d0cdd68aeced7fd9c42310d308317b5` |
| `sysdrv/source/objs_kernel/boot.img` | 2287104 | `8585d73c86766d2ee5abf512e4fd8ba5b0a30621004c64c809280491bd48ba85` |
| `sysdrv/source/objs_kernel/vmlinux` | 121839252 | `1acf5bbd11f2718acca7fc623da58584f02cae5fe52c3a4fa1f1522439acd0af` |
| `sysdrv/tools/board/rk_ota/out/usr/bin/rk_ota` | 51664 | `82c8ecf642bfad94db5fe6bb22a34e9fc3a32ecd4a5ac896249b439879f428e4` |

The U-Boot ELF contains `ab_sd_update`, `spinand_set_block_lock`,
`avb_ab_mark_slot_active`, and the libavb user operations. The kernel
`vmlinux` contains `spinand_get_block_lock`, `spinand_set_block_lock`, and
the MTD lock/unlock handlers. `rk_ota` is a 32-bit ARM EABI5 uClibc binary.

The baseline archive is damaged and a full extraction ends with tar exit code
2. For this verification, the required `project` and `sysdrv` trees were
extracted successfully; every patch target, relevant Makefile, and all three
verified builds were present. Replace the archive before relying on unrelated
SDK components not covered by this patch set.
