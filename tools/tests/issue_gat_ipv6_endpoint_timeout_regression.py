#!/usr/bin/env python3
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]

PROTOCOL_CONFIG = ROOT / "App/Protocol/config/ProtocolExternalConfig.h"
LOCAL_PROVIDER = ROOT / "App/Protocol/config/LocalConfigProvider.cpp"
GAT_CLIENT = ROOT / "App/Protocol/gat1400/GAT1400ClientService.cpp"
WEB_HEADER = ROOT / "App/WebServer/web_server.h"
WEB_SOURCE = ROOT / "App/WebServer/web_server.c"
MAIN_SOURCE = ROOT / "App/Main.cpp"


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig", errors="ignore")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def extract_function(text: str, marker: str) -> str:
    start = text.find(marker)
    require(start >= 0, f"missing function marker: {marker}")
    brace = text.find("{", start)
    require(brace >= 0, f"missing function body: {marker}")
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:index + 1]
    raise AssertionError(f"unterminated function: {marker}")


def main() -> int:
    protocol_config = read_text(PROTOCOL_CONFIG)
    local_provider = read_text(LOCAL_PROVIDER)
    gat_client = read_text(GAT_CLIENT)
    web_header = read_text(WEB_HEADER)
    web_source = read_text(WEB_SOURCE)
    main_source = read_text(MAIN_SOURCE)

    gat_struct = protocol_config.split("struct GatRegisterParam", 1)[1].split("};", 1)[0]
    require("std::string server_ipv6;" in gat_struct, "GatRegisterParam should expose server_ipv6.")
    require("int server_ipv6_port;" in gat_struct, "GatRegisterParam should expose server_ipv6_port.")
    require("server_ipv6_port(0)" in gat_struct, "GatRegisterParam should default server_ipv6_port to 0.")

    for snippet in (
        'ReadIniString(ini, kLocalGatConfigSection, "server_ipv6", path, out.server_ipv6);',
        'ReadIniInt(ini, kLocalGatConfigSection, "server_ipv6_port", path, out.server_ipv6_port);',
        'fprintf(fp, "server_ipv6=%s\\n", param.server_ipv6.c_str());',
        'fprintf(fp, "server_ipv6_port=%d\\n", param.server_ipv6_port);',
        'gat_register_ipv6_endpoint',
    ):
        require(snippet in local_provider, f"LocalConfigProvider.cpp missing IPv6 config snippet: {snippet}")

    for snippet in (
        "std::vector<RequestTarget>",
        "BuildRequestTargets",
        "cfg.gat_register.server_ipv6",
        "cfg.gat_register.server_ipv6_port",
        "O_NONBLOCK",
        "select(fd + 1",
        "SO_ERROR",
        "fcntl(fd, F_SETFL",
    ):
        require(snippet in gat_client, f"GAT1400ClientService.cpp missing IPv6/fallback/timeout snippet: {snippet}")

    body = extract_function(gat_client, "bool BuildRequestTargets")
    ipv6_pos = body.find("cfg.gat_register.server_ipv6")
    ipv4_pos = body.find("cfg.gat_register.server_ip);", ipv6_pos + 1)
    require(
        ipv6_pos >= 0 and ipv4_pos >= 0 and ipv6_pos < ipv4_pos,
        "Configured GAT1400 endpoints should add IPv6 before IPv4.",
    )

    require("char gat1400_ipv6[64];" in web_header, "Web device_state_t should hold full GAT1400 IPv6 text.")
    require("char gat1400_ipv6_port[8];" in web_header, "Web device_state_t should hold GAT1400 IPv6 port.")
    for snippet in (
        'name=\'gat1400_ipv6\'',
        'name=\'gat1400_ipv6_port\'',
        "UrlDecodeInPlace",
        "gat1400_ipv6=",
        "gat1400_ipv6_port=",
    ):
        require(snippet in web_source, f"web_server.c missing GAT1400 IPv6 form handling snippet: {snippet}")

    for snippet in (
        "gatParam.server_ipv6 = state->gat1400_ipv6;",
        "gatParam.server_ipv6_port = atoi(state->gat1400_ipv6_port);",
        "verify_status.gat1400_ipv6",
        "verify_status.gat1400_ipv6_port",
    ):
        require(snippet in main_source, f"Main.cpp missing GAT1400 IPv6 state bridge snippet: {snippet}")

    print("PASS: GAT1400 IPv6 endpoint config, IPv6-first fallback, and connect timeout are wired")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
