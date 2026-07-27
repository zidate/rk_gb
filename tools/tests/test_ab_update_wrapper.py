"""Build and exercise the application rk_ota execv wrapper."""

import pathlib
import subprocess
import tempfile
import textwrap
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
UPDATE_DIR = ROOT / "App/Update"
SOURCE = UPDATE_DIR / "AbUpdate.cpp"
PROTOCOL = ROOT / "App/Protocol/ProtocolManager.cpp"
CMAKE_FILES = (
    ROOT / "App/CMakeLists.txt",
    ROOT / "App/Protocol/gb28181/sdk_port/CMakeLists.txt",
)


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

static jmp_buf child_jump;
static int mode;
static int exec_called;
static int exec_args_ok;
static int exit_status;
static int wait_status;
static const char *expected_package;

extern "C" pid_t FakeFork(void) {
    return mode == 0 ? 0 : 42;
}

extern "C" int FakeExecv(const char *path, char *const argv[]) {
    char expected_tar[512];
    snprintf(expected_tar, sizeof(expected_tar), "--tar_path=%s", expected_package);
    exec_called++;
    exec_args_ok = !strcmp(path, "/oem/usr/bin/rk_ota") &&
                   !strcmp(argv[0], "/oem/usr/bin/rk_ota") &&
                   !strcmp(argv[1], "--misc=update") &&
                   !strcmp(argv[2], expected_tar) &&
                   !strcmp(argv[3], "--partition=all") &&
                   !strcmp(argv[4], "--reboot") && argv[5] == NULL &&
                   strstr(path, "/bin/sh") == NULL;
    return -1;
}

extern "C" pid_t FakeWaitpid(pid_t pid, int *status, int options) {
    if (pid != 42 || options != 0)
        return -1;
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
    expected_package = package;
    if (setjmp(child_jump) == 0)
        (void)AbUpdateApply(package, true);
    return exec_called == 1 && exec_args_ok && exit_status == 127 ? 0 : 1;
}

int main(void) {
    if (check_child("/tmp/upgrade.tar"))
        return 1;
    if (check_child("/tmp/a;touch /tmp/not-a-command"))
        return 2;
    mode = 1;
    wait_status = 0;
    if (AbUpdateApply("/tmp/upgrade.tar", true) != 0)
        return 3;
    wait_status = 7 << 8;
    if (AbUpdateApply("/tmp/upgrade.tar", true) == 0)
        return 4;
    return 0;
}
"""


class AbUpdateWrapperTest(unittest.TestCase):
    def test_execv_argv_and_exit_status(self):
        self.assertTrue(SOURCE.is_file())
        with tempfile.TemporaryDirectory() as tempdir:
            temp = pathlib.Path(tempdir)
            fake_header = temp / "fake_seam.h"
            harness = temp / "harness.cpp"
            binary = temp / "test_ab_update"
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
                    str(SOURCE),
                    str(harness),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_wrapper_has_no_shell_execution(self):
        source = SOURCE.read_text()
        self.assertIn("fork()", source)
        self.assertIn("execv", source)
        self.assertIn("waitpid", source)
        self.assertNotIn("system(", source)
        self.assertNotIn("/bin/sh", source)

    def test_protocol_and_both_builds_use_only_ab_wrapper(self):
        protocol = PROTOCOL.read_text()
        self.assertIn('#include "Update/AbUpdate.h"', protocol)
        self.assertIn('kGbUpgradePackagePath = "/tmp/upgrade.tar"', protocol)
        self.assertIn('kGbUpgradePackageTempPath = "/tmp/upgrade.tar.download"', protocol)
        apply_thread = protocol[
            protocol.index("void* ProtocolManager::GbUpgradeApplyThread") :
            protocol.index("ProtocolManager::ProtocolManager()")
        ]
        self.assertIn("AbUpdateApply(kGbUpgradePackagePath, true)", apply_thread)
        self.assertNotIn("DG_update", apply_thread)
        self.assertNotIn("reboot -f", apply_thread)
        self.assertIn("CreateDetachedThread", protocol)
        self.assertNotIn("gb_upgrade_firmware", protocol)
        launch = protocol[
            protocol.index("std::unique_ptr<GbUpgradeThreadContext> threadCtx") :
            protocol.index("threadCtx.release()")
        ]
        self.assertNotIn("//", launch)
        self.assertIn("ProtocolManager::GbUpgradeApplyThread", launch)
        for cmake in CMAKE_FILES:
            text = cmake.read_text()
            self.assertIn("set(Update Update/AbUpdate.cpp)", text)
            self.assertNotIn("set(Update Update/update.cpp)", text)


if __name__ == "__main__":
    unittest.main()
