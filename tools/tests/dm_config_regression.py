#!/usr/bin/env python3
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    config_cpp = ROOT / "App/DM/DmConfig.cpp"
    config_h = ROOT / "App/DM/DmConfig.h"
    if not config_cpp.exists() or not config_h.exists():
        print("FAIL: missing DM config implementation")
        return 1

    test_source = r'''
#include "DM/DmConfig.h"
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        return 10;
    }

    const std::string path = argv[1];
    {
        std::ofstream out(path.c_str());
        out << "[dm]\n";
        out << "enabled=1\n";
        out << "server_uri=coap://m.fxltsbl.com:5683\n";
        out << "local_port=0\n";
        out << "lifetime_sec=86400\n";
        out << "short_server_id=123\n";
        out << "startup_retry_interval_sec=60\n";
        out << "brand=brandA\n";
        out << "model=modelA\n";
        out << "app_key=appKeyA\n";
        out << "sdk_version=***\n";
        out << "api_version=4.0.1\n";
        out << "api_type=I\n";
        out << "template_id=templateA\n";
        out << "imei1=123456789012345\n";
        out << "imei2=***\n";
        out << "secret=secretA\n";
    }

    dm::DmConfig cfg;
    int ret = dm::GetDmConfig(cfg, path);
    if (ret != 0 || cfg.enabled != 1) {
        std::cerr << "initial get failed ret=" << ret << " enabled=" << cfg.enabled << "\n";
        return 2;
    }

    cfg.server_uri = "coap://b.fxltsbl.com:5683";
    cfg.device_values["sn"] = "SN001";
    ret = dm::SetDmConfig(cfg, path);
    if (ret != 0) {
        std::cerr << "SetDmConfig ret=" << ret << "\n";
        return 3;
    }

    dm::DmConfig after;
    ret = dm::GetDmConfig(after, path);
    if (ret != 0) {
        std::cerr << "reload ret=" << ret << "\n";
        return 4;
    }
    if (after.enabled != 1) {
        std::cerr << "enabled changed to " << after.enabled << "\n";
        return 5;
    }
    if (after.server_uri != "coap://b.fxltsbl.com:5683") {
        std::cerr << "server_uri=" << after.server_uri << "\n";
        return 6;
    }
    if (after.device_values["sn"] != "SN001") {
        std::cerr << "device_sn=" << after.device_values["sn"] << "\n";
        return 7;
    }

    ret = dm::SaveDmServerUri("coap://m.fxltsbl.com:5683", path);
    if (ret != 0) {
        std::cerr << "SaveDmServerUri ret=" << ret << "\n";
        return 8;
    }

    dm::DmConfig invalid = after;
    invalid.server_uri = "http://invalid.example.com";
    ret = dm::SetDmConfig(invalid, path);
    if (ret == 0) {
        std::cerr << "SetDmConfig accepted invalid enabled config\n";
        return 9;
    }

    dm::DmConfig unchanged;
    ret = dm::GetDmConfig(unchanged, path);
    if (ret != 0 || unchanged.server_uri != "coap://m.fxltsbl.com:5683") {
        std::cerr << "invalid set changed config ret=" << ret << " server_uri=" << unchanged.server_uri << "\n";
        return 10;
    }

    std::cout << "PASS: DM config get/set keeps enabled flag and rejects invalid active config\n";
    return 0;
}
'''

    with tempfile.TemporaryDirectory(prefix="dm_config_test_") as tmp:
        source = Path(tmp) / "dm_config_test.cpp"
        binary = Path(tmp) / "dm_config_test"
        ini = Path(tmp) / "dm.ini"
        source.write_text(test_source, encoding="utf-8")
        cmd = [
            "g++",
            "-std=c++11",
            "-I",
            str(ROOT / "App"),
            str(source),
            str(config_cpp),
            "-o",
            str(binary),
        ]
        build = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if build.returncode != 0:
            print("FAIL: DM config host build failed")
            print(build.stdout)
            return build.returncode
        run = subprocess.run([str(binary), str(ini)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        print(run.stdout, end="")
        return run.returncode


if __name__ == "__main__":
    raise SystemExit(main())
