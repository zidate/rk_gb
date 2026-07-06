#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAT_HEADER = ROOT / "App/Protocol/gat1400/GAT1400ClientService.h"
GAT_SOURCE = ROOT / "App/Protocol/gat1400/GAT1400ClientService.cpp"


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
    header = read_text(GAT_HEADER)
    source = read_text(GAT_SOURCE)

    for snippet in (
        "bool m_active_endpoint_valid;",
        "std::string m_active_endpoint_scheme;",
        "std::string m_active_endpoint_host;",
        "int m_active_endpoint_port;",
    ):
        require(snippet in header, f"GAT1400ClientService should store registered endpoint: {snippet}")

    require(
        "bool useActiveEndpoint = true" in header,
        "ExecuteRequest should default to active endpoint pinning after registration.",
    )

    build_targets = extract_function(source, "bool BuildRequestTargets")
    require("activeEndpointValid" in build_targets, "BuildRequestTargets should accept an active endpoint selector.")
    require(
        build_targets.find("activeEndpointValid") < build_targets.find("cfg.gat_register.server_ipv6"),
        "Active endpoint should be used before rebuilding IPv6/IPv4 fallback targets.",
    )

    execute_body = extract_function(source, "int GAT1400ClientService::ExecuteRequest")
    for snippet in (
        "useActiveEndpoint",
        "m_active_endpoint_valid",
        "m_active_endpoint_scheme",
        "m_active_endpoint_host",
        "m_active_endpoint_port",
        "selectedEndpointScheme",
        "selectedEndpointHost",
        "selectedEndpointPort",
    ):
        require(snippet in execute_body, f"ExecuteRequest should pin and report active endpoint detail: {snippet}")

    register_body = extract_function(source, "int GAT1400ClientService::RegisterNow")
    require(
        "false," in register_body and "&selectedEndpointScheme" in register_body,
        "RegisterNow should disable active endpoint pinning and capture the actually registered endpoint.",
    )
    require(
        "m_active_endpoint_valid = true;" in register_body,
        "RegisterNow should store the selected endpoint after a successful ResponseStatus.",
    )
    require(
        "FormatHttpHost(selectedEndpointHost, selectedEndpointPort)" in register_body,
        "RegisterNow success log should print the actual endpoint, not always the IPv4 config.",
    )

    unregister_body = extract_function(source, "int GAT1400ClientService::UnregisterNow")
    require(
        "m_active_endpoint_valid = false;" in unregister_body,
        "UnregisterNow should clear the active endpoint after logout.",
    )

    heartbeat_body = extract_function(source, "void GAT1400ClientService::HeartbeatLoop")
    require(
        "m_active_endpoint_valid = false;" in heartbeat_body,
        "Heartbeat failure should clear active endpoint before re-register fallback selection.",
    )

    print("PASS: GAT1400 pins post-register requests to the actual registered endpoint")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
