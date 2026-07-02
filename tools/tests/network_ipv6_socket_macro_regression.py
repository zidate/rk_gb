#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

TOP_CMAKE = ROOT / "CMakeLists.txt"
MIDDLEWARE_CMAKE = ROOT / "Middleware/CMakeLists.txt"
SOCKET_COMPAT = ROOT / "App/Protocol/SocketCompat.h"
GB_RTP = ROOT / "App/Protocol/gb28181/GB28181RtpPsSender.cpp"
GB_BROADCAST = ROOT / "App/Protocol/gb28181/GB28181BroadcastBridge.cpp"
GB_LISTEN_H = ROOT / "App/Protocol/gb28181/GB28181ListenBridge.h"
GB_LISTEN = ROOT / "App/Protocol/gb28181/GB28181ListenBridge.cpp"
GB_NET_SHIM = ROOT / "App/Protocol/gb28181/sdk_port/NetSocketSdkShim.cpp"
GB_SDP_UTIL = ROOT / "third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/SDP/SdpUtil.cpp"
PROTOCOL_MANAGER = ROOT / "App/Protocol/ProtocolManager.cpp"
DM_CLIENT = ROOT / "App/DM/DmClientService.cpp"
RTSP_SOCKET_UTIL_H = ROOT / "App/RtspServer/src/net/SocketUtil.h"
RTSP_SOCKET_UTIL = ROOT / "App/RtspServer/src/net/SocketUtil.cpp"
RTSP_TCP_SOCKET = ROOT / "App/RtspServer/src/net/TcpSocket.cpp"
RTSP_RTP_H = ROOT / "App/RtspServer/src/xop/RtpConnection.h"
RTSP_RTP = ROOT / "App/RtspServer/src/xop/RtpConnection.cpp"
RTSP_MEDIA_SESSION = ROOT / "App/RtspServer/src/xop/MediaSession.cpp"
NTP = ROOT / "Middleware/libmpp/rkipc/common/network/ntp.c"


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig", errors="ignore")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    for cmake in (TOP_CMAKE, MIDDLEWARE_CMAKE):
        text = read_text(cmake)
        rel = cmake.relative_to(ROOT)
        require(
            "option(RK_ENABLE_IPV6_SOCKET" in text,
            f"{rel} should expose the RK_ENABLE_IPV6_SOCKET build switch.",
        )
        require(
            "-DRK_ENABLE_IPV6_SOCKET=" in text,
            f"{rel} should pass RK_ENABLE_IPV6_SOCKET to C/C++ sources.",
        )

    compat = read_text(SOCKET_COMPAT)
    for token in (
        "#ifndef RK_ENABLE_IPV6_SOCKET",
        "sockaddr_storage",
        "getaddrinfo",
        "IPV6_V6ONLY",
        "AF_UNSPEC",
        "IN6_IS_ADDR_V4MAPPED",
        "AddressMatchesText",
        "CopyAddressWithPort",
    ):
        require(token in compat, f"SocketCompat.h should provide {token} support.")

    gb_rtp = read_text(GB_RTP)
    for token in (
        "RK_ENABLE_IPV6_SOCKET",
        "SocketCompat.h",
        "sockaddr_storage remote_addr",
        "remote_addr_len",
        "ResolveEndpoint",
        "CreateSocket",
        "BuildAnyEndpoint",
        "SockaddrToString",
    ):
        require(token in gb_rtp, f"GB28181 RTP/PS sender should use {token}.")
    require(
        "socket(AF_INET, sockType, 0)" in gb_rtp,
        "GB28181 RTP/PS sender should keep an IPv4-only branch when RK_ENABLE_IPV6_SOCKET=0.",
    )

    gb_broadcast = read_text(GB_BROADCAST)
    for token in (
        "RK_ENABLE_IPV6_SOCKET",
        "SocketCompat.h",
        "sockaddr_storage remote_addr",
        "ResolveEndpoint",
        "BuildAnyEndpoint",
        "AddressMatchesText",
        "IN IP6",
    ):
        require(token in gb_broadcast, f"GB28181 broadcast bridge should use {token}.")
    require(
        "socket(AF_INET, sockType, 0)" in gb_broadcast,
        "GB28181 broadcast bridge should keep an IPv4-only branch when RK_ENABLE_IPV6_SOCKET=0.",
    )

    gb_listen_h = read_text(GB_LISTEN_H)
    require(
        "sockaddr_storage m_remote_addr" in gb_listen_h and "socklen_t m_remote_addr_len" in gb_listen_h,
        "GB28181 listen bridge should store IPv4/IPv6 remote endpoints.",
    )

    gb_listen = read_text(GB_LISTEN)
    for token in (
        "RK_ENABLE_IPV6_SOCKET",
        "SocketCompat.h",
        "ResolveEndpoint",
        "CreateSocket",
        "remote_addr_len",
    ):
        require(token in gb_listen, f"GB28181 listen bridge should use {token}.")
    require(
        "socket(AF_INET, sockType, 0)" in gb_listen,
        "GB28181 listen bridge should keep an IPv4-only branch when RK_ENABLE_IPV6_SOCKET=0.",
    )

    gb_net_shim = read_text(GB_NET_SHIM)
    for token in ("RK_ENABLE_IPV6_SOCKET", "SocketCompat.h", "ResolveEndpoint", "CreateSocket"):
        require(token in gb_net_shim, f"GB socket shim should use {token}.")
    require(
        "socket(AF_INET, SOCK_STREAM, 0)" in gb_net_shim,
        "GB socket shim should keep an IPv4-only branch when RK_ENABLE_IPV6_SOCKET=0.",
    )

    gb_sdp = read_text(GB_SDP_UTIL)
    for token in (
        "RK_ENABLE_IPV6_SOCKET",
        "ResolveSdpAddrType",
        "\"IP6\"",
        "sdp.SetOrigin",
        "conn.SetAddrType",
    ):
        require(token in gb_sdp, f"GB SDK SDP generation should use {token}.")

    protocol_manager = read_text(PROTOCOL_MANAGER)
    for token in (
        "SocketCompat.h",
        "RK_ENABLE_IPV6_SOCKET",
        "ResolveEndpoint",
        "SockaddrToString",
    ):
        require(token in protocol_manager, f"ProtocolManager local IP resolution should use {token}.")

    dm_client = read_text(DM_CLIENT)
    for token in (
        "RK_ENABLE_IPV6_SOCKET",
        "ResolveDmAddressFamily",
        "hints.ai_family = AF_UNSPEC",
        "int family = AF_INET",
        "data.addressFamily = ResolveDmAddressFamily",
    ):
        require(token in dm_client, f"DM/LwM2M client should use {token}.")
    require(
        "return AF_UNSPEC;" not in dm_client,
        "DM/LwM2M client should resolve to a concrete address family before creating the Wakaama socket.",
    )

    socket_util_h = read_text(RTSP_SOCKET_UTIL_H)
    require(
        "sockaddr_storage" in socket_util_h,
        "RTSP SocketUtil.h should expose sockaddr_storage peer helpers.",
    )

    socket_util = read_text(RTSP_SOCKET_UTIL)
    for token in (
        "RK_ENABLE_IPV6_SOCKET",
        "SocketCompat.h",
        "sockaddr_storage",
        "ResolveEndpoint",
        "MapIpv4ToIpv6",
    ):
        require(token in socket_util, f"RTSP SocketUtil.cpp should use {token}.")
    require(
        "socket(AF_INET, SOCK_STREAM, 0)" in read_text(RTSP_TCP_SOCKET),
        "RTSP TcpSocket should keep an IPv4-only create path when RK_ENABLE_IPV6_SOCKET=0.",
    )

    rtsp_rtp_h = read_text(RTSP_RTP_H)
    require(
        "sockaddr_storage peer_addr_" in rtsp_rtp_h and "peer_rtp_addr_len_" in rtsp_rtp_h,
        "RTSP RTP connection should store IPv4/IPv6 peer RTP endpoints.",
    )
    rtsp_rtp = read_text(RTSP_RTP)
    for token in ("RK_ENABLE_IPV6_SOCKET", "SocketCompat.h", "CopyAddressWithPort", "peer_rtp_addr_len_"):
        require(token in rtsp_rtp, f"RTSP RTP connection should use {token}.")

    media_session = read_text(RTSP_MEDIA_SESSION)
    require(
        "IN IP6" in media_session and "RK_ENABLE_IPV6_SOCKET" in media_session,
        "RTSP SDP generation should emit IP6 when the local address is IPv6 and the macro is enabled.",
    )

    ntp = read_text(NTP)
    for token in ("RK_ENABLE_IPV6_SOCKET", "getaddrinfo", "AF_UNSPEC", "sockaddr_storage"):
        require(token in ntp, f"NTP client should use {token} when IPv6 socket support is enabled.")
    require(
        "socket(AF_INET, SOCK_DGRAM, 0)" in ntp,
        "NTP client should keep an IPv4-only branch when RK_ENABLE_IPV6_SOCKET=0.",
    )

    print("PASS: IPv6 socket macro integration points are enforced")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
