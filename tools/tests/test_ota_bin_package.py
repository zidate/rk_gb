"""Verify the RV1106 ota.bin host packager and target parser."""

import pathlib
import struct
import subprocess
import tempfile
import textwrap
import unittest
import zlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
PACKAGER_SOURCE = (
    ROOT / "packaging/image/tools-sourcecode/packaging-update/packaging.c"
)
OTA_PARSER = ROOT / "App/Update/OtaPackage.cpp"
OTA_INCLUDE = ROOT / "App/Update"
BUILD_SCRIPT = ROOT / "build.sh"
PACKAGING_MAKEFILE = ROOT / "packaging/Makefile"
MAIN_SOURCE = ROOT / "App/Main.cpp"
ENV_VERIFIER = ROOT / "tools/verify_uboot_env.py"

IMAGES = (
    ("boot", "boot.img", 4, 0x0240000, 0x0400000, b"boot-data"),
    ("rootfs", "rootfs.img", 5, 0x0A40000, 0x0A00000, b"rootfs-data"),
    ("oem", "oem.img", 6, 0x1E40000, 0x2000000, b"oem-data"),
)


def write_ini(path: pathlib.Path) -> None:
    sections = ["[global]", "flash_size=0x8000000", "sector_size=0x20000"]
    for section, filename, _, start, limit, _ in IMAGES:
        sections.extend(
            [
                f"[{section}]",
                f"start=0x{start:X}",
                f"size=0x{limit:X}",
                "type=0",
                f"file={filename}",
            ]
        )
    path.write_text("\n".join(sections) + "\n")


def parse_package(package: bytes):
    platform = package[:20].rstrip(b"\0")
    magic, expected_crc, payload_size = struct.unpack(">III", package[20:32])
    payload = package[32:]
    entries = []
    offset = 0
    while offset < len(payload):
        image_type, image_size, start = struct.unpack(">III", payload[offset : offset + 12])
        offset += 12
        data = payload[offset : offset + image_size]
        offset += image_size
        entries.append((image_type, image_size, start, data))
    return platform, magic, expected_crc, payload_size, payload, entries


class OtaBinPackageTest(unittest.TestCase):
    def test_build_outputs_only_ota_bin_for_network_upgrade(self):
        build_script = BUILD_SCRIPT.read_text()
        packaging_makefile = PACKAGING_MAKEFILE.read_text()
        self.assertIn("Release/ota.bin", build_script)
        self.assertNotIn("upgrade.tar.gz", build_script)
        self.assertIn("$(RELEASE_DIR)/ota.bin", packaging_makefile)
        self.assertNotIn("ota_ab.tar", packaging_makefile)
        main_source = MAIN_SOURCE.read_text(encoding="utf-8-sig", errors="ignore")
        self.assertIn('/mnt/sdcard/ota.bin', main_source)
        self.assertNotIn('/mnt/sdcard/upgrade.tar.gz', main_source)

    def test_sd_release_requires_fresh_sdk_fixed_images_and_six_image_set(self):
        build_script = BUILD_SCRIPT.read_text()
        packaging_makefile = PACKAGING_MAKEFILE.read_text()
        six_images = "env.img idblock.img uboot.img boot.img rootfs.img oem.img"

        self.assertIn(f"SD_IMAGE_NAMES=({six_images})", build_script)
        self.assertIn('RV1106_SDK_DIR is required', build_script)
        self.assertIn('SDK_IMAGE_DIR=$RV1106_SDK_DIR/output/image', build_script)
        self.assertIn('SDK_ENV_IMAGE=$SDK_IMAGE_DIR/env.img', build_script)
        self.assertIn('tools/verify_uboot_env.py', build_script)
        self.assertIn(f"SD_IMAGES := {six_images}", packaging_makefile)
        self.assertIn("cp $(SD_IMAGE_PATHS) $(RELEASE_DIR)/sd/", packaging_makefile)
        self.assertIn("stat -c %s $(IMAGE_DIR)/env.img", packaging_makefile)
        self.assertNotIn(
            "cp $(IMAGE_DIR)/uboot.img $(IMAGE_DIR)/boot.img",
            packaging_makefile,
        )

    def test_env_verifier_checks_crc_size_and_required_ab_values(self):
        self.assertTrue(ENV_VERIFIER.is_file())

        def build_env(entries):
            payload = b"\0".join(entries) + b"\0\0"
            payload += b"\0" * (256 * 1024 - 4 - len(payload))
            return struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF) + payload

        complete_entries = (
            b"mtdparts=mtdparts=spi-nand0:256K(env),1M(idblock),1M(uboot),"
            b"4M(boot_a),4M(boot_b),256K(misc),-(userdata)",
            b"sys_bootargs= rootfstype=squashfs",
            b"sd_parts=mmcblk0:16K@512(env)",
        )
        with tempfile.TemporaryDirectory() as tempdir:
            temp = pathlib.Path(tempdir)
            valid = temp / "valid.img"
            valid.write_bytes(build_env(complete_entries))
            subprocess.run(["python3", str(ENV_VERIFIER), str(valid)], check=True)

            bad_crc = temp / "bad-crc.img"
            corrupted = bytearray(valid.read_bytes())
            corrupted[-1] ^= 1
            bad_crc.write_bytes(corrupted)

            missing = temp / "missing.img"
            missing.write_bytes(build_env(complete_entries[:-1]))

            bad_size = temp / "bad-size.img"
            bad_size.write_bytes(valid.read_bytes()[:-1])

            for invalid in (bad_crc, missing, bad_size):
                with self.subTest(invalid=invalid.name):
                    result = subprocess.run(
                        ["python3", str(ENV_VERIFIER), str(invalid)],
                        capture_output=True,
                        text=True,
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("verify_uboot_env:", result.stderr)

    def build_fixture(self, temp: pathlib.Path):
        packager = temp / "packaging-update"
        subprocess.run(
            [
                "gcc",
                "-std=c99",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-D_FILE_OFFSET_BITS=64",
                str(PACKAGER_SOURCE),
                "-o",
                str(packager),
            ],
            check=True,
        )
        write_ini(temp / "upgrade.ini")
        for _, filename, _, _, _, content in IMAGES:
            (temp / filename).write_bytes(content)
        subprocess.run(
            [str(packager), "upgrade.ini", "ota.bin"], cwd=temp, check=True
        )
        return packager, temp / "ota.bin"

    def test_packager_emits_compatible_headers_crc_and_padding(self):
        with tempfile.TemporaryDirectory() as tempdir:
            temp = pathlib.Path(tempdir)
            _, package_path = self.build_fixture(temp)
            platform, magic, expected_crc, payload_size, payload, entries = parse_package(
                package_path.read_bytes()
            )
            self.assertEqual(platform, b"rv1106")
            self.assertEqual(magic, 0xABCD1234)
            self.assertEqual(payload_size, len(payload))
            self.assertEqual(expected_crc, zlib.crc32(payload) & 0xFFFFFFFF)
            self.assertEqual([entry[0] for entry in entries], [4, 5, 6])
            for entry, image in zip(entries, IMAGES):
                _, image_size, start, data = entry
                self.assertEqual(start, image[3])
                self.assertEqual(image_size % 4, 0)
                self.assertEqual(data[: len(image[5])], image[5])
                self.assertEqual(data[len(image[5]) :], b"\xff" * ((-len(image[5])) % 4))

    def test_parser_extracts_valid_package_and_rejects_corruption(self):
        harness_source = r"""
            #include "OtaPackage.h"
            int main(int argc, char **argv) {
                return argc == 3 && OtaPackageExtractImages(argv[1], argv[2]) == 0 ? 0 : 1;
            }
        """
        with tempfile.TemporaryDirectory() as tempdir:
            temp = pathlib.Path(tempdir)
            _, package_path = self.build_fixture(temp)
            harness = temp / "parser_harness.cpp"
            binary = temp / "parser_harness"
            output = temp / "images"
            output.mkdir()
            harness.write_text(textwrap.dedent(harness_source))
            subprocess.run(
                [
                    "g++",
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(OTA_INCLUDE),
                    str(OTA_PARSER),
                    str(harness),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            result = subprocess.run(
                [str(binary), str(package_path), str(output)],
                capture_output=True,
                text=True,
                check=True,
            )
            self.assertEqual(result.stdout, "")
            self.assertEqual(
                sorted(path.name for path in output.iterdir()),
                ["boot.img", "oem.img", "rootfs.img"],
            )
            for _, filename, _, _, _, content in IMAGES:
                extracted = (output / filename).read_bytes()
                self.assertEqual(extracted[: len(content)], content)

            original = bytearray(package_path.read_bytes())
            header_offsets = []
            offset = 32
            while offset < len(original):
                header_offsets.append(offset)
                image_size = struct.unpack_from(">I", original, offset + 4)[0]
                offset += 12 + image_size

            def refresh_payload_fields(package):
                payload = package[32:]
                struct.pack_into(">I", package, 24, zlib.crc32(payload) & 0xFFFFFFFF)
                struct.pack_into(">I", package, 28, len(payload))

            cases = {}
            bad_magic = bytearray(original)
            struct.pack_into(">I", bad_magic, 20, 0)
            cases["magic"] = bad_magic

            bad_size = bytearray(original)
            struct.pack_into(">I", bad_size, 28, len(bad_size) - 33)
            cases["package-size"] = bad_size

            bad_crc = bytearray(original)
            bad_crc[-1] ^= 0x01
            cases["crc"] = bad_crc

            unknown_type = bytearray(original)
            struct.pack_into(">I", unknown_type, header_offsets[0], 99)
            refresh_payload_fields(unknown_type)
            cases["unknown-type"] = unknown_type

            duplicate_type = bytearray(original)
            struct.pack_into(">I", duplicate_type, header_offsets[1], 4)
            refresh_payload_fields(duplicate_type)
            cases["duplicate-type"] = duplicate_type

            bad_start = bytearray(original)
            struct.pack_into(">I", bad_start, header_offsets[0] + 8, 0)
            refresh_payload_fields(bad_start)
            cases["start-address"] = bad_start

            zero_image = bytearray(original)
            struct.pack_into(">I", zero_image, header_offsets[0] + 4, 0)
            refresh_payload_fields(zero_image)
            cases["zero-image"] = zero_image

            missing_image = bytearray(original[: header_offsets[2]])
            refresh_payload_fields(missing_image)
            cases["missing-image"] = missing_image

            cases["truncated"] = bytearray(original[:-1])

            for name, contents in cases.items():
                with self.subTest(name=name):
                    bad_path = temp / f"bad-{name}.bin"
                    bad_path.write_bytes(contents)
                    for _, filename, _, _, _, _ in IMAGES:
                        (output / filename).write_bytes(b"stale")
                    rejected = subprocess.run(
                        [str(binary), str(bad_path), str(output)],
                        capture_output=True,
                        text=True,
                    )
                    self.assertNotEqual(rejected.returncode, 0)
                    self.assertEqual(list(output.iterdir()), [])

    def test_packager_rejects_oversized_image(self):
        with tempfile.TemporaryDirectory() as tempdir:
            temp = pathlib.Path(tempdir)
            packager, _ = self.build_fixture(temp)
            with (temp / "boot.img").open("wb") as boot:
                boot.truncate(0x0400001)
            result = subprocess.run(
                [str(packager), "upgrade.ini", "oversized.bin"], cwd=temp
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((temp / "oversized.bin").exists())


if __name__ == "__main__":
    unittest.main()
