"""校验 RV1106 U-Boot env.img 的大小、CRC 和 A/B 必需环境项。"""

import argparse
import pathlib
import struct
import sys
import zlib


ENV_IMAGE_SIZE = 256 * 1024
REQUIRED_VALUES = (
    b"mtdparts=mtdparts=spi-nand0:",
    b"boot_a",
    b"boot_b",
    b"(misc)",
    b"sys_bootargs=",
    b"sd_parts=",
)


def validate_env_image(path: pathlib.Path) -> None:
    if not path.is_file():
        raise ValueError(f"not a regular file: {path}")
    image = path.read_bytes()
    if len(image) != ENV_IMAGE_SIZE:
        raise ValueError(
            f"unexpected size {len(image)}, expected {ENV_IMAGE_SIZE}: {path}"
        )

    stored_crc = struct.unpack_from("<I", image)[0]
    calculated_crc = zlib.crc32(image[4:]) & 0xFFFFFFFF
    if stored_crc != calculated_crc:
        raise ValueError(
            f"CRC mismatch stored=0x{stored_crc:08x} "
            f"calculated=0x{calculated_crc:08x}: {path}"
        )

    payload = image[4:]
    missing = [
        value.decode("ascii") for value in REQUIRED_VALUES if value not in payload
    ]
    if missing:
        raise ValueError(f"missing required values {', '.join(missing)}: {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("env_image", type=pathlib.Path)
    args = parser.parse_args()
    try:
        validate_env_image(args.env_image)
    except (OSError, ValueError) as error:
        print(f"verify_uboot_env: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
