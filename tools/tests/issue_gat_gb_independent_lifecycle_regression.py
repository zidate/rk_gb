#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROTOCOL_MANAGER = ROOT / "App/Protocol/ProtocolManager.cpp"


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
    protocol_manager = read_text(PROTOCOL_MANAGER)
    start_body = extract_function(protocol_manager, "int ProtocolManager::Start()")
    reload_body = extract_function(protocol_manager, "int ProtocolManager::ReloadExternalConfig()")

    require("StartGbRuntimeServices" in protocol_manager, "GB runtime helper should be explicit.")
    require("StopGbRuntimeServices" in protocol_manager, "GB runtime cleanup helper should be explicit.")
    require(
        start_body.find("if (m_cfg.gb_register.enabled != 0)") < start_body.find("StartGbRuntimeServices"),
        "ProtocolManager::Start should only start GB runtime services when GB28181 is enabled.",
    )
    require(
        start_body.find("StartGbRuntimeServices") < start_body.find("StartGbClientLifecycle"),
        "GB client lifecycle should start after GB runtime services.",
    )
    require(
        start_body.find("skip gb runtime services because config disabled") >= 0,
        "ProtocolManager::Start should log skipped GB runtime services when GB28181 is disabled.",
    )
    require(
        start_body.find("m_gat_client->Start") > start_body.find("skip gb runtime services because config disabled"),
        "GAT1400 start path should remain reachable after GB28181 is skipped.",
    )

    require(
        "if (m_cfg.gb_register.enabled != 0)" in reload_body and "RestartGbRuntimeServices" in protocol_manager,
        "ReloadExternalConfig should gate GB runtime restarts by gb_register.enabled.",
    )
    require(
        "StopGbRuntimeServices();" in reload_body,
        "ReloadExternalConfig should stop GB runtime services when GB28181 is disabled.",
    )

    print("PASS: GB28181 runtime services are gated independently from GAT1400")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
