#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    crypto_cpp = ROOT / "App/DM/DmCrypto.cpp"
    crypto_h = ROOT / "App/DM/DmCrypto.h"
    if not crypto_cpp.exists() or not crypto_h.exists():
        print("FAIL: 缺少 DM 加密实现文件")
        return 1

    test_source = r'''
#include "DM/DmCrypto.h"
#include <iostream>
#include <string>

int main() {
    std::string encrypted;
    int ret = dm::DmEncryptValue("a1adfef9ed014286b7f7a314ee978f15", "123456", encrypted);
    if (ret != 0) {
        std::cerr << "encrypt ret=" << ret << "\n";
        return 2;
    }
    if (encrypted != "maC2/b2Vi517QalT6Ebeyg==") {
        std::cerr << "encrypted=" << encrypted << "\n";
        return 3;
    }
    std::cout << "PASS: DM AES sample matches FAQ\n";
    return 0;
}
'''

    with tempfile.TemporaryDirectory(prefix="dm_crypto_test_") as tmp:
        source = Path(tmp) / "dm_crypto_test.cpp"
        binary = Path(tmp) / "dm_crypto_test"
        source.write_text(test_source, encoding="utf-8")
        crypto_lib = Path("/usr/lib/x86_64-linux-gnu/libcrypto.so.3")
        crypto_arg = str(crypto_lib) if crypto_lib.exists() else "-lcrypto"
        cmd = [
            "g++",
            "-std=c++11",
            "-I",
            str(ROOT / "App"),
            "-I",
            str(ROOT / "Include"),
            str(source),
            str(crypto_cpp),
            crypto_arg,
            "-o",
            str(binary),
        ]
        build = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if build.returncode != 0:
            print("FAIL: DM 加密 host 编译失败")
            print(build.stdout)
            return build.returncode
        run = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        print(run.stdout, end="")
        return run.returncode


if __name__ == "__main__":
    raise SystemExit(main())
