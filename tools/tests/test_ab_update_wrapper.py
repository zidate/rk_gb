"""Build and exercise the ota.bin parser and rk_ota execv wrapper."""

import pathlib
import shutil
import struct
import subprocess
import tempfile
import textwrap
import unittest
import zlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
UPDATE_DIR = ROOT / "App/Update"
SOURCES = (UPDATE_DIR / "AbUpdate.cpp", UPDATE_DIR / "OtaPackage.cpp")
PROTOCOL = ROOT / "App/Protocol/ProtocolManager.cpp"
CMAKE_FILES = (
    ROOT / "App/CMakeLists.txt",
    ROOT / "App/Protocol/gb28181/sdk_port/CMakeLists.txt",
)


def build_ota(path: pathlib.Path) -> None:
    payload = bytearray()
    for image_type, start, content in (
        (4, 0x0240000, b"boot"),
        (5, 0x0A40000, b"rootfs"),
        (6, 0x1E40000, b"oem"),
    ):
        aligned = content + b"\xff" * ((-len(content)) % 4)
        payload.extend(struct.pack(">III", image_type, len(aligned), start))
        payload.extend(aligned)
    header = b"rv1106".ljust(20, b"\0") + struct.pack(
        ">III", 0xABCD1234, zlib.crc32(payload) & 0xFFFFFFFF, len(payload)
    )
    path.write_bytes(header + payload)


FAKE_HEADER = r"""
#include <sys/types.h>
extern "C" pid_t FakeFork(void);
extern "C" int FakeExecv(const char *, char *const []);
extern "C" pid_t FakeWaitpid(pid_t, int *, int);
extern "C" void FakeExit(int);
#define AB_UPDATE_FORK FakeFork
#define AB_UPDATE_EXECV FakeExecv
#define AB_UPDATE_WAITPID FakeWaitpid
#define AB_UPDATE_EXIT FakeExit
"""


HARNESS = r"""
#include "AbUpdate.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static jmp_buf child_jump;
static int mode;
static int exec_called;
static int exec_args_ok;
static int exit_status;
static int wait_status;
static int images_present_at_wait;

static int file_starts_with(const char *path, const char *expected, size_t size) {
    FILE *file = fopen(path, "rb");
    char buffer[16] = {0};
    if (file == NULL)
        return 0;
    const int ok = fread(buffer, 1, size, file) == size &&
                   !memcmp(buffer, expected, size);
    fclose(file);
    return ok;
}

static int images_are_ready(void) {
    return file_starts_with("/tmp/boot.img", "boot", 4) &&
           file_starts_with("/tmp/rootfs.img", "rootfs", 6) &&
           file_starts_with("/tmp/oem.img", "oem", 3);
}

static int images_are_removed(void) {
    return access("/tmp/boot.img", F_OK) != 0 &&
           access("/tmp/rootfs.img", F_OK) != 0 &&
           access("/tmp/oem.img", F_OK) != 0;
}

extern "C" pid_t FakeFork(void) {
    return mode == 0 ? 0 : 42;
}

extern "C" int FakeExecv(const char *path, char *const argv[]) {
    exec_called++;
    exec_args_ok = !strcmp(path, "/oem/usr/bin/rk_ota") &&
                   !strcmp(argv[0], "/oem/usr/bin/rk_ota") &&
                   !strcmp(argv[1], "--misc=update") &&
                   !strcmp(argv[2], "--save_dir=/tmp") &&
                   !strcmp(argv[3], "--partition=all") &&
                   !strcmp(argv[4], "--reboot") && argv[5] == NULL &&
                   strstr(path, "/bin/sh") == NULL && images_are_ready();
    return -1;
}

extern "C" pid_t FakeWaitpid(pid_t pid, int *status, int options) {
    if (pid != 42 || options != 0)
        return -1;
    images_present_at_wait = images_are_ready();
    *status = wait_status;
    return pid;
}

extern "C" void FakeExit(int status) {
    exit_status = status;
    longjmp(child_jump, 1);
}

static int check_child(const char *package) {
    mode = 0;
    exec_called = 0;
    exec_args_ok = 0;
    exit_status = 0;
    if (setjmp(child_jump) == 0)
        (void)AbUpdateApply(package, true);
    return exec_called == 1 && exec_args_ok && exit_status == 127 && images_are_removed() ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc != 3)
        return 10;
    if (check_child(argv[1]))
        return 1;
    if (check_child(argv[2]))
        return 2;
    mode = 1;
    wait_status = 0;
    images_present_at_wait = 0;
    if (AbUpdateApply(argv[1], true) != 0 || !images_present_at_wait || !images_are_removed())
        return 3;
    wait_status = 7 << 8;
    images_present_at_wait = 0;
    if (AbUpdateApply(argv[1], true) == 0 || !images_present_at_wait || !images_are_removed())
        return 4;
    return 0;
}
"""


class AbUpdateWrapperTest(unittest.TestCase):
    def test_execv_argv_and_exit_status(self):
        for source in SOURCES:
            self.assertTrue(source.is_file())
        with tempfile.TemporaryDirectory() as tempdir:
            temp = pathlib.Path(tempdir)
            fake_header = temp / "fake_seam.h"
            harness = temp / "harness.cpp"
            binary = temp / "test_ab_update"
            package = temp / "ota.bin"
            malicious = temp / "ota;touch-not-a-command.bin"
            build_ota(package)
            shutil.copy2(package, malicious)
            fake_header.write_text(textwrap.dedent(FAKE_HEADER))
            harness.write_text(textwrap.dedent(HARNESS))
            subprocess.run(
                [
                    "g++",
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(UPDATE_DIR),
                    "-include",
                    str(fake_header),
                    *(str(source) for source in SOURCES),
                    str(harness),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary), str(package), str(malicious)], check=True)

    def test_wrapper_has_no_shell_execution(self):
        source = SOURCES[0].read_text()
        self.assertIn("AB_UPDATE_FORK()", source)
        self.assertIn("AB_UPDATE_EXECV", source)
        self.assertIn("AB_UPDATE_WAITPID", source)
        self.assertNotIn("system(", source)
        self.assertNotIn("/bin/sh", source)

    def test_protocol_and_both_builds_use_ota_wrapper(self):
        protocol = PROTOCOL.read_text()
        self.assertIn('#include "Update/AbUpdate.h"', protocol)
        self.assertIn('kGbUpgradePackagePath = "/tmp/ota.bin"', protocol)
        self.assertIn('kGbUpgradePackageTempPath = "/tmp/ota.bin.download"', protocol)
        apply_thread = protocol[
            protocol.index("void* ProtocolManager::GbUpgradeApplyThread") :
            protocol.index("ProtocolManager::ProtocolManager()")
        ]
        self.assertIn("AbUpdateApply(kGbUpgradePackagePath, true)", apply_thread)
        self.assertNotIn("DG_update", apply_thread)
        self.assertNotIn("reboot -f", apply_thread)
        for cmake in CMAKE_FILES:
            text = cmake.read_text()
            self.assertIn("Update/AbUpdate.cpp", text)
            self.assertIn("Update/OtaDownload.cpp", text)
            self.assertIn("Update/OtaPackage.cpp", text)


if __name__ == "__main__":
    unittest.main()
