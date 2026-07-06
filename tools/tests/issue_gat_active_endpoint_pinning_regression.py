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
        "bool useActiveEndpoint",
        "selectedEndpointScheme",
        "selectedEndpointHost",
        "selectedEndpointPort",
        "m_active_endpoint_valid",
        "m_active_endpoint_scheme",
        "m_active_endpoint_host",
        "m_active_endpoint_port",
    ):
        require(snippet not in header, f"active endpoint selection should not expand ExecuteRequest state: {snippet}")

    require(
        "int m_active_endpoint_family;" in header,
        "GAT1400ClientService should only store the currently registered endpoint family.",
    )
    require(
        "int endpoint_family;" in header,
        "HttpResponse should carry the endpoint family used by this request without changing ExecuteRequest parameters.",
    )

    build_targets = extract_function(source, "bool BuildRequestTargets")
    require("activeEndpointFamily" in build_targets, "BuildRequestTargets should accept only an active endpoint family.")
    require(
        "m_active_endpoint_host" not in build_targets,
        "BuildRequestTargets should not pin a copied host; it should reuse configured endpoints.",
    )
    require(
        build_targets.find("activeEndpointFamily") < build_targets.find("cfg.gat_register.server_ipv6"),
        "Active endpoint family should be applied before IPv6/IPv4 fallback ordering.",
    )

    execute_body = extract_function(source, "int GAT1400ClientService::ExecuteRequest")
    for snippet in (
        "selectedEndpointScheme",
        "selectedEndpointHost",
        "selectedEndpointPort",
    ):
        require(snippet not in execute_body, f"ExecuteRequest should keep the old send interface: {snippet}")
    require(
        "m_active_endpoint_family" in execute_body,
        "ExecuteRequest should choose targets from the current registered endpoint family.",
    )
    require(
        "isRegisterRequest" in execute_body and "!isRegisterRequest" in execute_body,
        "Register requests should bypass the current endpoint family while other requests keep using it.",
    )
    require(
        "response.endpoint_family" in execute_body,
        "ExecuteRequest should record the family of the target that actually succeeded in the response.",
    )

    register_body = extract_function(source, "int GAT1400ClientService::RegisterNow")
    require(
        "selectedEndpoint" not in register_body,
        "RegisterNow should not receive endpoint detail from ExecuteRequest parameters.",
    )
    require(
        register_body.find("m_active_endpoint_family = kEndpointUnspecified;") > register_body.find("const int reqRet = ExecuteRequest"),
        "RegisterNow should not clear the current endpoint family before issuing the register request.",
    )
    require(
        "m_active_endpoint_family = response.endpoint_family;" in register_body,
        "RegisterNow should store the family that the successful register request used.",
    )

    unregister_body = extract_function(source, "int GAT1400ClientService::UnregisterNow")
    require(
        "m_active_endpoint_family = kEndpointUnspecified;" in unregister_body,
        "UnregisterNow should clear the active endpoint family after logout.",
    )

    heartbeat_body = extract_function(source, "void GAT1400ClientService::HeartbeatLoop")
    require(
        "m_active_endpoint_family = kEndpointUnspecified;" in heartbeat_body,
        "Heartbeat failure should clear active endpoint family before re-register fallback selection.",
    )

    print("PASS: GAT1400 sends follow-up requests to the currently connected endpoint family without changing send interface")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
