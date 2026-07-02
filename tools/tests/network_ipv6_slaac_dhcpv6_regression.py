#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

KERNEL_DEFCONFIGS = [
    ROOT / "driver/kernel/RC0240/rv1106_defconfig",
    ROOT / "driver/kernel/RC0240_LGV10/rv1106_defconfig",
    ROOT / "driver/kernel/RC0330_V20/rv1106_defconfig",
]
BUSYBOX_CONFIG = ROOT / "driver/busybox/.config"
NETWORK_SOURCE = ROOT / "Middleware/libmpp/rkipc/common/network/network.c"
WIFI_SOURCE = ROOT / "Middleware/libmpp/rkipc/common/network/Rk_wifi.c"
PACKAGED_BUSYBOX = ROOT / "packaging/rootfs_pub/bin/busybox"
PACKAGED_APPLET_LINKS = [
    ROOT / "packaging/rootfs_pub/bin/ping6",
    ROOT / "packaging/rootfs_pub/sbin/ip",
    ROOT / "packaging/rootfs_pub/sbin/ipaddr",
    ROOT / "packaging/rootfs_pub/sbin/iplink",
    ROOT / "packaging/rootfs_pub/sbin/iproute",
    ROOT / "packaging/rootfs_pub/usr/bin/udhcpc6",
]


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig", errors="ignore")


def read_bytes(path: Path) -> bytes:
    return path.read_bytes()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    for config in KERNEL_DEFCONFIGS:
        text = read_text(config)
        require(
            "CONFIG_IPV6=y" in text,
            f"{config.relative_to(ROOT)} should build IPv6 into the business-board kernel.",
        )
        require(
            "# CONFIG_IPV6 is not set" not in text,
            f"{config.relative_to(ROOT)} should not leave IPv6 disabled.",
        )

    busybox = read_text(BUSYBOX_CONFIG)
    require(
        "CONFIG_FEATURE_IPV6=y" in busybox,
        "BusyBox should keep IPv6-capable networking applets enabled.",
    )
    require(
        "CONFIG_UDHCPC6=y" in busybox,
        "BusyBox should build the udhcpc6 DHCPv6 client applet.",
    )
    require(
        "CONFIG_FEATURE_UDHCPC6_RFC3646=y" in busybox,
        "BusyBox udhcpc6 should support RFC3646 DNS options for DHCPv6 DNS.",
    )

    network = read_text(NETWORK_SOURCE)
    for symbol in (
        "rk_network_ipv6_enable",
        "rk_network_dhcpv6_start",
        "rk_network_dhcpv6_stop",
    ):
        require(
            symbol in network,
            f"network.c should provide {symbol}() for SLAAC + DHCPv6 DNS lifecycle.",
        )
    require(
        "rk_network_dhcpv6_start(name);" in network,
        "Ethernet link-up handling should start DHCPv6 DNS for the active interface.",
    )
    require(
        "rk_network_dhcpv6_stop(name);" in network,
        "Ethernet link-down handling should stop DHCPv6 DNS for the active interface.",
    )
    require(
        "disable_ipv6" in network and "accept_ra" in network and "autoconf" in network,
        "network.c should enable IPv6 SLAAC sysctls before starting udhcpc6.",
    )

    wifi = read_text(WIFI_SOURCE)
    for symbol in (
        "rk_wifi_dhcpv4_start",
        "rk_wifi_dhcpv4_stop",
        "rk_wifi_get_ipv4_address",
        "rk_wifi_ipv6_enable",
        "rk_wifi_dhcpv6_start",
        "rk_wifi_dhcpv6_stop",
    ):
        require(
            symbol in wifi,
            f"Rk_wifi.c should provide {symbol}() for WLAN SLAAC + DHCPv6 DNS lifecycle.",
        )
    require(
        "udhcpc -i %s" in wifi and "/var/run/udhcpc.%s.pid" in wifi,
        "Wi-Fi IPv4 DHCP should use packaged BusyBox udhcpc with a wlan0 pid file.",
    )
    require(
        "dhcpcd wlan0" not in wifi,
        "Wi-Fi code should not depend on dhcpcd because the packaged rootfs does not include it.",
    )
    require(
        "SIOCGIFADDR" in wifi and "rk_wifi_get_ipv4_address(\"wlan0\"" in wifi,
        "Wi-Fi connection info should fall back to the wlan0 kernel IPv4 address.",
    )
    require(
        "rk_wifi_dhcpv6_start(\"wlan0\");" in wifi,
        "Wi-Fi connection handling should start DHCPv6 DNS for wlan0.",
    )
    require(
        "rk_wifi_dhcpv6_stop(\"wlan0\");" in wifi,
        "Wi-Fi disconnect/disable handling should stop DHCPv6 DNS for wlan0.",
    )
    require(
        "ip -6 addr flush dev %s" in wifi,
        "Rk_wifi.c should flush stale IPv6 addresses when wlan0 disconnects.",
    )

    if PACKAGED_BUSYBOX.exists():
        busybox_image = read_bytes(PACKAGED_BUSYBOX)
        for applet in (b"udhcpc6", b"ping6", b"ipaddr", b"iplink", b"iproute"):
            require(
                applet in busybox_image,
                f"packaged busybox should contain {applet.decode()} applet support.",
            )
        for link in PACKAGED_APPLET_LINKS:
            require(
                link.is_symlink(),
                f"{link.relative_to(ROOT)} should be an applet symlink into busybox.",
            )

    print("PASS: IPv6 SLAAC + DHCPv6 DNS static integration points are enforced")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        raise SystemExit(1)
