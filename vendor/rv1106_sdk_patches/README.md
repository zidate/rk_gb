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
