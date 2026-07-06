# GAT1400 IPv6, Plate Notify, and GB Independence

## Context

- Project: RK/RV1106 IPC Linux application firmware.
- Modules: `GAT1400ClientService`, `ProtocolManager`, `LocalConfigProvider`, Web config bridge, and `LowerGAT1400SDK`.
- Requirements:
  - GAT1400 local config supports IPv6 server address and port.
  - GAT1400 prefers IPv6 and falls back to IPv4.
  - GAT1400 TCP connect has explicit timeout behavior.
  - External modules can notify plate detection results through GAT1400.
  - GB28181 can be disabled while GAT1400 remains enabled.

## Findings

- `GatRegisterParam` previously only had `server_ip/server_port`, so no separate IPv6 endpoint could be configured from local INI or Web state.
- GAT1400 HTTP requests used one target endpoint. A dual-endpoint deployment needed deterministic IPv6-first target construction and fallback to IPv4 on request failure.
- After IPv6/IPv4 fallback was added, post-register requests also needed endpoint pinning. Otherwise a device could register through IPv4 and later send keepalive or uploads through IPv6 when both addresses point to different platform instances.
- TCP connect needed a bounded timeout instead of relying on blocking OS connect behavior.
- Plate fields already exist in `GAT_1400_Motor`: `HasPlate`, `PlateColor`, `PlateNo`, and related reliability fields. A new `GAT_1400_Plate` object would be non-standard for this codebase.
- `ProtocolManager::Start()` used to start GB runtime services before checking `gb_register.enabled`, so disabling GB28181 did not fully isolate GAT1400 startup.

## Fix Pattern

- Add `gat1400.ini` fields `server_ipv6/server_ipv6_port`, expose them through `GatRegisterParam`, Web config state, config validation, config logs, and reload diff.
- Build request targets as IPv6 first and IPv4 second during registration; absolute override URLs remain single-target by design.
- Store the IPv4/IPv6 family selected by successful registration and send keepalive, unregister, normal uploads, and replayed uploads to the corresponding configured endpoint until unregister, keepalive failure, stop, or a new registration selection.
- Implement nonblocking `ConnectWithTimeout()` using `O_NONBLOCK`, `select(fd + 1, ...)`, and `SO_ERROR`, then restore original fd flags.
- Add `NotifyGatPlateDetections()` and `LOWER_1400_NOTIFY_PLATEDETECTIONS()` by reusing `GAT_1400_Motor` and posting to `/VIID/MotorVehicles` through the same async queue policy as vehicle detection.
- Extract `StartGbRuntimeServices()`, `StopGbRuntimeServices()`, and `RestartGbRuntimeServices()`. Gate GB runtime startup/reload/restart by `gb_register.enabled`, while allowing the GAT1400 start path to continue when GB is disabled.

## Verification

- `python3 tools/tests/issue_gat_ipv6_endpoint_timeout_regression.py`
- `python3 tools/tests/issue_gat_active_endpoint_pinning_regression.py`
- `python3 tools/tests/issue_gat_plate_detection_notify_regression.py`
- `python3 tools/tests/issue_gat_gb_independent_lifecycle_regression.py`
- `python3 tools/tests/network_ipv6_socket_macro_regression.py`
- `python3 tools/tests/issue45_gat_enabled_regression.py`
- `python3 tools/tests/issue45_gat_online_status_regression.py`
- `python3 tools/tests/issue49_gat_notify_async_once_retry_regression.py`
- `python3 tools/tests/external_module_demos_doc_regression.py`
