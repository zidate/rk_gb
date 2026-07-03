# GAT1400 IPv6 HTTP URL Support

## Context

- Project: RK/RV1106 IPC Linux application firmware.
- Module: GAT1400 client HTTP register, keepalive, upload, subscribe notification, and local subscribe server.
- Requirement: GAT1400 should support IPv6 while GB28181 remains IPv4-only by deployment policy.

## Findings

- `GAT1400ClientService::ConnectTcp()` already uses `getaddrinfo()` with `AF_UNSPEC`, so direct `server_ip` values can resolve/connect over IPv4 or IPv6.
- `GAT1400ClientService::StartServerLocked()` already follows the global `RK_ENABLE_IPV6_SOCKET` switch and creates an IPv6 dual-stack listen socket when enabled.
- The remaining gap was absolute URL handling for GAT1400 override URLs, such as subscribe notification URLs. The old parser split host/port at the last colon and did not normalize bracketed IPv6 literals.

## Fix Pattern

- Keep GAT1400 tied to global `RK_ENABLE_IPV6_SOCKET`, unlike GB28181's dedicated `RK_ENABLE_GB_IPV6_SOCKET=OFF` default.
- Parse bracketed IPv6 literal URLs as `http://[2001:db8::1]:port/path`.
- Store `RequestTarget.host` internally without brackets for `getaddrinfo()`.
- Format HTTP `Host` and digest auth URLs with brackets when the host is an IPv6 literal.

## Verification

- `python3 tools/tests/network_ipv6_socket_macro_regression.py`
- `python3 tools/tests/dm_lwm2m_regression.py`
- `python3 tools/tests/issue45_gat_enabled_regression.py`
- `python3 tools/tests/issue45_gat_online_status_regression.py`
- `python3 tools/tests/issue49_gat_notify_async_once_retry_regression.py`
- Host `g++ -std=c++11 -fsyntax-only` for `GAT1400ClientService.cpp` with `RK_ENABLE_IPV6_SOCKET=1` and `=0`.
