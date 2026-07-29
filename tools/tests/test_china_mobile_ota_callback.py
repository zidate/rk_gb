"""Verify the China Mobile callback drives the unified ota.bin flow."""

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
CALLBACK = ROOT / "App/ChinaMobile/demo_callback.cpp"
DOWNLOADER = ROOT / "App/Update/OtaDownload.cpp"


class ChinaMobileOtaCallbackTest(unittest.TestCase):
    def test_callback_is_async_serialized_and_uses_unified_wrapper(self):
        source = CALLBACK.read_text(errors="ignore")
        start = source.index("cmiotUpgradeInfo_t g_upgradeInfoFw")
        end = source.index("static int hostname_to_ip", start)
        upgrade = source[start:end]

        self.assertIn("pthread_create(&thread", upgrade)
        self.assertIn("g_upgradeBusy", upgrade)
        self.assertIn("OtaDownloadFile(task->info.url", upgrade)
        self.assertIn('AbUpdateApply(kCmiotOtaPackagePath, true)', upgrade)
        self.assertIn('kCmiotOtaPackagePath[] = "/tmp/ota.bin"', upgrade)
        self.assertNotIn("system(", upgrade)

    def test_callback_reports_download_and_install_lifecycle(self):
        source = CALLBACK.read_text(errors="ignore")
        for status in (
            "CMIOT_UPGRADE_STATUS_START_DOWNLOAD",
            "CMIOT_UPGRADE_STATUS_DOWNLOADING",
            "CMIOT_UPGRADE_STATUS_DOWNLOAD_COMPLETE",
            "CMIOT_UPGRADE_STATUS_DOWNLOAD_FAILED",
            "CMIOT_UPGRADE_STATUS_START_INSTALL",
            "CMIOT_UPGRADE_STATUS_INSTALLING",
            "CMIOT_UPGRADE_STATUS_INSTALL_COMPLETE",
            "CMIOT_UPGRADE_STATUS_INSTALL_FAILED",
        ):
            self.assertIn(status, source)
        self.assertIn("cmiot_report_upgrade_step(&report)", source)

    def test_downloader_uses_exec_arguments_and_md5_before_rename(self):
        source = DOWNLOADER.read_text()
        self.assertIn('const_cast<char *>("--")', source)
        self.assertIn("execvp(arguments[0], arguments)", source)
        self.assertNotIn("system(", source)
        digest = source.index("CalculateFileMd5(temporary_path")
        publish = source.index("rename(temporary_path, final_path)")
        self.assertLess(digest, publish)


if __name__ == "__main__":
    unittest.main()
