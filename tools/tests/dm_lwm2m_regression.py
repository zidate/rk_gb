#!/usr/bin/env python3
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore")


def require(text: str, pattern: str, message: str) -> None:
    if re.search(pattern, text, re.MULTILINE | re.DOTALL) is None:
        raise AssertionError(message)


def require_file(path: Path) -> str:
    if not path.exists():
        raise AssertionError(f"缺少文件: {path.relative_to(ROOT)}")
    return read(path)


def main() -> int:
    cmake = read(ROOT / "App/CMakeLists.txt")
    main_cpp = read(ROOT / "App/Main.cpp")
    config = require_file(ROOT / "App/DM/DmConfig.h") + require_file(ROOT / "App/DM/DmConfig.cpp")
    crypto = require_file(ROOT / "App/DM/DmCrypto.h") + require_file(ROOT / "App/DM/DmCrypto.cpp")
    client = require_file(ROOT / "App/DM/DmClientService.h") + require_file(ROOT / "App/DM/DmClientService.cpp")
    objects = require_file(ROOT / "App/DM/DmLwm2mObjects.h") + require_file(ROOT / "App/DM/DmLwm2mObjects.cpp")

    require(cmake, r"third_party/wakaama", "CMake 应 vendoring Wakaama 源码。")
    require(cmake, r"DM/DmClientService\.cpp", "CMake 应编译 DM 独立服务。")
    require(cmake, r"target_link_libraries\([^)]*wakaama_static", "主程序应链接 Wakaama 静态库。")

    require(main_cpp, r"#include\s+\"DM/DmClientService\.h\"", "Main.cpp 应包含 DM 独立服务。")
    require(main_cpp, r"DmClientService::Instance\(\)\.Start\(\)", "主流程应独立启动 DM。")
    require(main_cpp, r"DmClientService::Instance\(\)\.Stop\(\)", "退出/升级流程应停止 DM。")
    require(main_cpp, r"RestartDmClientService", "网络变化时应触发 DM 独立重启。")
    if "ProtocolManager" in client:
        raise AssertionError("DM 服务不应依赖 ProtocolManager。")

    require(config, r'kDmConfigFile\s*=\s*"/userdata/conf/Config/DM/dm\.ini"', "DM 参数应从独立 dm.ini 读取。")
    for key in [
        "brand",
        "model",
        "app_key",
        "template_id",
        "secret",
        "imei1",
        "imei2",
        "server_uri",
    ]:
        require(config, rf'"{key}"', f"dm.ini 缺少配置键: {key}")
    require(config, r"BuildDmEndpoint", "应提供 endpoint 拼接函数。")
    require(config, r'"\|\|"', "endpoint 应使用 DM 文档要求的 || 分隔符。")

    require(crypto, r"DmEncryptValue", "应提供 DM 字段加密函数。")
    require(crypto, r"SHA256", "DM 加密应先做 SHA256(key)。")
    require(crypto, r"AES-256-CBC|EVP_aes_256_cbc|AES256", "DM 加密应使用 AES-256-CBC。")
    require(crypto, r"maC2/b2Vi517QalT6Ebeyg==", "代码或测试应包含 DM FAQ AES 样例。")

    require(objects, r"668", "应实现 DM 配置对象 668。")
    require(objects, r"669", "应实现 DM 设备信息对象 669。")
    require(objects, r"fieldConfig", "对象 668 应处理 fieldConfig。")
    require(objects, r"ruleConfig", "对象 668 应处理 ruleConfig。")
    require(objects, r"addressConfig", "对象 668 应处理 addressConfig。")
    require(objects, r"deviceInfo", "对象 669 应返回 deviceInfo。")
    require(objects, r"batteryCapacityCurr", "心跳字段应支持 batteryCapacityCurr。")
    require(client, r"lwm2m_update_registration\([^;]+,\s*true\s*\)", "心跳 Update 应携带 ObjectLinks payload。")

    print("PASS: DM LwM2M integration regression checks")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
