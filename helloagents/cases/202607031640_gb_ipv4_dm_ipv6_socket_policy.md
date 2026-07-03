# GB IPv4 Policy With DM IPv6 Socket Support

## Context

- Project: RK/RV1106 IPC Linux application firmware.
- User-visible issue: DM platform access needed IPv6 support while IPv4 continued to work.
- Deployment clarification: GB28181 access is numeric-IP based and remains IPv4 in this deployment.

## Root Cause

The DM dual-stack fix enabled the global `RK_ENABLE_IPV6_SOCKET` build option by default so one firmware can support both IPv4 and IPv6 DM/LwM2M sockets. Several GB28181 media and SDP paths also used that same global option, which could make GB media/listen/broadcast/SDP behavior follow the DM socket policy.

The GB SIP control path was already IPv4-only through `AF_INET` usage in `third_party/platform_sdk_port/CommonLibSrc/SipSDK/common/SipEventManager.cpp`, matching the deployment requirement.

## Fix Pattern

- Keep `RK_ENABLE_IPV6_SOCKET=ON` for DM/LwM2M dual-stack firmware behavior.
- Add `RK_ENABLE_GB_IPV6_SOCKET`, default `OFF`, for GB28181 media/SDP paths.
- Ensure release `build.sh` passes `-DRK_ENABLE_GB_IPV6_SOCKET=OFF`.
- Keep GB SIP control path unchanged and IPv4-only.

## Verification

- `python3 tools/tests/network_ipv6_socket_macro_regression.py`
- `python3 tools/tests/dm_lwm2m_regression.py`
- `bash -n build.sh`
- `git diff --check`
- Host `g++ -std=c++11 -fsyntax-only` checks for touched GB files with `RK_ENABLE_GB_IPV6_SOCKET=0` and `=1`.
