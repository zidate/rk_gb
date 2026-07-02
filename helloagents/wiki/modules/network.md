# Network

## 目的
记录当前 RK IPC 工程的网络栈事实、DHCPv4 入口和 IPv6 SLAAC + DHCPv6 DNS 支持状态，作为后续双栈调试的基础。

## 模块概述
- **职责:** 说明以太网/Wi-Fi DHCP 客户端、内核 IPv6、BusyBox 网络 applet、应用层 IPv6 socket 宏和 RKIPC 网络 API 的当前状态
- **状态:** 🚧源码、rootfs applet 和 IPv6 kernel/boot 镜像已接入，待实机验证
- **最后更新:** 2026-07-02
- **代码真实来源:** `build.sh`、`driver/kernel/*/rv1106_defconfig`、`driver/busybox/.config`、`Middleware/libmpp/rkipc/common/network/network.c`、`Middleware/libmpp/rkipc/common/network/Rk_wifi.c`、`Middleware/libmpp/rkipc/common/network/ntp.c`、`App/Protocol/SocketCompat.h`、`App/Protocol/gb28181/*`、`third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/SDP/SdpUtil.cpp`、`App/RtspServer/src/net/*`、`App/RtspServer/src/xop/*`、`App/DM/DmClientService.cpp`

## 当前事实

- 默认构建口径为 `RV1106_DUAL_IPC + RC0240 + ATBM6062`。
- 外部完整 SDK 位于 `/home/jerry/lhy`；当前 IPv6 内核闭环使用 `/home/jerry/lhy/cmiot/RV1106_IPC_SDK.tar.gz` 解压后的 `project/build.sh lunch` 入口，选择 `9` 后实际指向 `BoardConfig-SPI_NAND-NONE-RV1106_IPC38_DEMO_V10-IPC-XWR60440.mk`。
- RC0240/RC0240_LGV10/RC0330_V20 的主 `rv1106_defconfig` 已打开 `CONFIG_IPV6=y`；`driver/rndis-kernel/rv1106_defconfig` 里仍存在历史 `CONFIG_IPV6=m`。
- SDK 侧 `lunch 9` 使用 `RK_KERNEL_DEFCONFIG=rv1106_defconfig` 与 `RK_KERNEL_DEFCONFIG_FRAGMENT=rv1106-ipc-XWR60440.config`；基础 defconfig 里 `CONFIG_IPV6` 关闭，但 fragment 已覆盖为 `CONFIG_IPV6=y`，最终 `objs_kernel/.config` 已验证为 `CONFIG_IPV6=y`。
- 工具链 uClibc 配置已启用 IPv6，但这不能替代目标内核 IPv6。
- BusyBox 已启用 `CONFIG_FEATURE_IPV6`、IPv4 `udhcpc`、`CONFIG_UDHCPC6`、`CONFIG_FEATURE_UDHCPC6_RFC3646`、`ip`/`ip addr`/`ip route` 和 `ping6`。
- `network.c::rk_network_ipv4_set()` 的 DHCP 路径会按接口启动 IPv4 `udhcpc`，随后启动 IPv6 SLAAC + DHCPv6 DNS。
- `network.c::rk_network_get_cable_state()` 在网线 link up 时会启动 IPv4 `udhcpc` 和 `rk_network_dhcpv6_start(name)`；link down 时会停止 `udhcpc6` 并清理 IPv6 地址。
- `packaging/rootfs_pub/bin/busybox` 已按 BusyBox 1.27.2 和 `driver/busybox/.config` 交叉重建，包含 `udhcpc6`、`ping6`、`ipaddr`、`iplink`、`iproute`；rootfs 已补齐 `ping6`、`ip*`、`udhcpc6` applet 链接。
- 当前打包 rootfs 没有 `dhcpcd`，Wi-Fi 应用逻辑已在 `Rk_wifi.c` 中收敛为 BusyBox `udhcpc` + `udhcpc6`：`wlan0` 启动/连接成功时开启 IPv6 sysctl、IPv4 DHCP 和 DHCPv6 DNS，断开/关闭时停止 `udhcpc6` 并清理 IPv6 地址。
- Wi-Fi IPv4 地址读取不能只依赖 `wpa_cli status ip_address`；`Rk_wifi.c::RK_wifi_running_getConnectionInfo()` 已增加 `SIOCGIFADDR` fallback，从内核接口状态读取 `wlan0` 地址。
- IPv6 不要求 Wi-Fi 芯片驱动单独修改；当前仓库固件包内驱动模块在 `packaging/oem_ipc/usr/ko/`，完整 SDK 驱动源码在 `/tmp/lhy_rv1106_sdk/RV1106_IPC_SDK/sysdrv/drv_ko/wifi/`，主要目录包括 `aic8800_netdrv/`、`atbm/`、`rtl8188ftv/`。
- 应用层业务 socket 增加 `RK_ENABLE_IPV6_SOCKET` 编译宏：默认 `OFF/0` 保持 IPv4-only 行为；开启后关键业务路径使用 `getaddrinfo(AF_UNSPEC)`、`sockaddr_storage` 和 IPv6 dual-stack listener。
- C++ 侧公共兼容层为 `App/Protocol/SocketCompat.h`，负责 `IPV6_V6ONLY=0`、IPv4-mapped IPv6 格式化/比较、`CopyAddressWithPort()` 等通用逻辑。
- 当前应用层 socket 宏已覆盖 GB28181 RTP/PS 发送、广播/对讲收发、GB SDK socket shim、GB SDK SDP 地址族、GB 本地回包地址选择、GAT1400 callback listener、RTSP TCP/UDP RTP、RTSP SDP 地址族、NTP UDP connect 和 DM/LwM2M UDP 地址族选择。

## DHCPv6 改造要点

- DHCPv6 地址获取必须与 IPv6 RA 一起考虑；默认网关通常来自 Router Advertisement，不应期望 DHCPv6 客户端单独下发默认网关。
- SLAAC + DHCPv6 DNS 表示地址和默认路由主要来自 RA/SLAAC，DHCPv6 只补 DNS、域名等参数；stateful DHCPv6 表示 IPv6 地址也由 DHCPv6 服务器分配，但默认路由仍通常依赖 RA。
- 面向当前设备的实施文档见 `rk_gb/doc/ipv6_slaac_dhcpv6_dns_support.md`。
- 当前实现沿 BusyBox 路线：启用 `CONFIG_UDHCPC6` 生成/安装 `udhcpc6` applet；它不是 `dhcpc6`。同时补齐 `ip` applet 或等价 `iproute2` 工具，并提供 DHCPv6 lease 脚本。
- 内核侧应在各业务板型 defconfig 中启用 `CONFIG_IPV6=y`，SPI NOR 小系统不建议依赖未验证的模块自动加载路径。
- 启动/热插拔逻辑需要同时覆盖开机路径、`rk_network_init()` 线程和应用侧手动 `udhcpc -i eth0` 路径，避免开机和网线重插行为不一致。

## 验证入口

| 目标 | 命令/观察 |
|------|-----------|
| 内核 IPv6 | `cat /proc/net/if_inet6`、`ls /proc/sys/net/ipv6/conf/eth0`、`ls /proc/sys/net/ipv6/conf/wlan0` |
| 客户端能力 | `busybox | grep -E 'udhcpc6|udhcpc|ping6|ipaddr|iproute'` |
| 地址 | `ip -6 addr show dev eth0`、`ip -6 addr show dev wlan0` 或 `/proc/net/if_inet6` |
| 默认路由 | `ip -6 route` 或 BusyBox `route -A inet6` |
| 抓包 | `tcpdump -i eth0 -nn 'icmp6 or udp port 546 or udp port 547'` |
| 应用 socket 宏 | `cmake ... -DRK_ENABLE_IPV6_SOCKET=ON` 后确认 GB28181/RTSP/NTP/GAT/DM 相关源文件进入构建 |

## 本地验证记录

- 2026-06-30: 新增 `tools/tests/network_ipv6_slaac_dhcpv6_regression.py`，静态检查内核/BusyBox 配置和 `network.c` DHCPv6 生命周期入口，已通过。
- 2026-06-30: 使用 `arm-rockchip830-linux-uclibcgnueabihf-gcc -fsyntax-only` 对 `network.c` 做交叉编译语法检查，已通过。
- 2026-07-01: `bash tools/issue_bot/build_verify.sh /home/jerry/silver/rk_gb` 的 Middleware 和主工程构建验证通过，日志目录为 `/tmp/rk_gb-build-verify.OD8rui/logs`。构建参数基线为禁用全局 `-o3/-O3`：小写 `-o3` 会被 GCC 解释为输出参数 `-o 3` 并导致编译失败；大写 `-O3` 虽可编译，但现场反馈编码启动会宕机。`tools/tests/middleware_cmake_flags_regression.py` 现同时检查 Middleware 和主工程 `flags.make`，防止两类参数回归。
- 2026-06-30: `packaging.7z` 可用 `python3 -m py7zr x packaging.7z .` 恢复完整 `packaging/`；不带输出目录时 `py7zr` 会触发 `NoneType.is_absolute` 异常。
- 2026-06-30: 从 BusyBox 官方源码包 `busybox-1.27.2.tar.bz2` 使用 `driver/busybox/.config` 和 `arm-rockchip830-linux-uclibcgnueabihf-` 交叉编译出新 `busybox`，替换到 `packaging/rootfs_pub/bin/busybox`，并补 `ping6`、`ip/ipaddr/iplink/iproute`、`udhcpc6` 链接；`tools/tests/network_ipv6_slaac_dhcpv6_regression.py` 已扩展为在 `packaging/` 存在时检查打包 busybox。
- 2026-07-01: `make -C packaging CROSS=/home/jerry/silver/RK/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-` 已重新生成 `Release/raw.bin`、`Release/linux.bin`、`Release/upgrade.bin`。普通沙箱下 `fakeroot -- chown 0:0` 会失败，需要提权沙箱执行打包。当时仓库没有 kernel 源码，`packaging/image/boot.img` 仍是 `packaging.7z` 里的预置内核镜像，尚不能证明目标内核已包含 `CONFIG_IPV6=y`。当时 MD5: `raw.bin=174d4c2c78ef9f6fe4d20b597ce642ba`，`linux.bin=455dac23ad862f31161950bd219e8e7a`，`upgrade.bin=455dac23ad862f31161950bd219e8e7a`，`Bin/dgiot=26c44cab9db95907951cef8c64cccdc2`，`oem_ipc/usr/bin/dgiot=0703e6978e1df7788134b7bf9c5fe365`。
- 2026-07-02: 用户补充完整 SDK 在 `/home/jerry/lhy`，编译配置入口为解压 SDK 后运行 `build.sh lunch` 并选择 `9`；实测 `lunch 9` 对应 `BoardConfig-SPI_NAND-NONE-RV1106_IPC38_DEMO_V10-IPC-XWR60440.mk`，最终 kernel `.config` 包含 `CONFIG_IPV6=y`。
- 2026-07-02: 在 `/tmp/lhy_rv1106_sdk/RV1106_IPC_SDK` 用 `project/build.sh kernel` 生成 IPv6 `boot.img` 并替换 `rk_gb/packaging/image/boot.img`；应用层 IPv6 socket 宏收尾后，重新执行 `make -C packaging CROSS=/home/jerry/silver/RK/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-` 生成 `Release/raw.bin`、`Release/linux.bin`、`Release/upgrade.bin`。当前 MD5: `boot.img=cde6decc2fc07dfd6870bfc877ae411a`，`raw.bin=6123c1ed925d0a99ab4b58e2a2dff9af`，`linux.bin=78ac36771b8d1a4d3da2c73dcecca8c1`，`upgrade.bin=78ac36771b8d1a4d3da2c73dcecca8c1`，`Bin/dgiot=a4c92b470caf3e7f1d8d96453de1fe00`，`oem_ipc/usr/bin/dgiot=eb020035257fc158480b5981fabd4662`。
- 2026-07-02: Wi-Fi 路线不改驱动，应用层 `Rk_wifi.c` 已接入 `wlan0` BusyBox `udhcpc` + `udhcpc6`，并为 `wpa_cli status` 缺少 `ip_address` 的情况增加 `SIOCGIFADDR` fallback；`python3 tools/tests/network_ipv6_slaac_dhcpv6_regression.py` 与 `bash tools/issue_bot/build_verify.sh /home/jerry/silver/rk_gb` 均已通过。
- 2026-07-02: 新增 `RK_ENABLE_IPV6_SOCKET` 应用层 socket 宏，默认关闭保持 IPv4；开启后 GB28181/RTSP/GAT/NTP/DM(LwM2M) 关键 socket 路径和 GB SDK SDP 地址族支持 IPv6。验证：`python3 tools/tests/network_ipv6_socket_macro_regression.py` 通过；默认构建 `bash tools/issue_bot/build_verify.sh /home/jerry/silver/rk_gb` 通过，日志 `/tmp/rk_gb-build-verify.91D0xE/logs`；宏开启交叉构建通过，构建目录 `/tmp/rk_gb-ipv6-socket-on.9dWpEd`。
- 2026-06-30: `doc/ipv6_slaac_dhcpv6_dns_support.md` 已补全为实施手册，覆盖 BusyBox/appet 机制、当前实现状态、构建命令、上板验收、故障定位、回滚方案和后续必须完成项；文档明确 BusyBox 1.27.2 `udhcpc6` 可能发送 IA_NA，严格 `SLAAC + DHCPv6 DNS` 仍需抓包确认或换支持 information-only 的 DHCPv6 客户端。

## 依赖
- `rk_gb/build.sh`
- `rk_gb/driver/kernel/RC0240/rv1106_defconfig`
- `rk_gb/driver/kernel/RC0240_LGV10/rv1106_defconfig`
- `rk_gb/driver/kernel/RC0330_V20/rv1106_defconfig`
- `rk_gb/driver/rndis-kernel/rv1106_defconfig`
- `rk_gb/driver/busybox/.config`
- `rk_gb/Middleware/libmpp/rkipc/common/network/network.c`
- `rk_gb/Middleware/libmpp/rkipc/common/network/Rk_wifi.c`
- `rk_gb/App/Protocol/SocketCompat.h`
- `rk_gb/App/Protocol/gb28181/GB28181RtpPsSender.cpp`
- `rk_gb/App/Protocol/gb28181/GB28181BroadcastBridge.cpp`
- `rk_gb/App/Protocol/gb28181/GB28181ListenBridge.cpp`
- `rk_gb/App/Protocol/gb28181/sdk_port/NetSocketSdkShim.cpp`
- `rk_gb/third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/SDP/SdpUtil.cpp`
- `rk_gb/App/RtspServer/src/net/SocketUtil.cpp`
- `rk_gb/App/RtspServer/src/xop/RtpConnection.cpp`
- `rk_gb/App/Protocol/gat1400/GAT1400ClientService.cpp`
- `rk_gb/Middleware/libmpp/rkipc/common/network/ntp.c`
- `rk_gb/App/DM/DmClientService.cpp`
- `rk_gb/App/Main.cpp`
- `rk_gb/tools/tests/network_ipv6_slaac_dhcpv6_regression.py`
- `rk_gb/tools/tests/network_ipv6_socket_macro_regression.py`
- `rk_gb/doc/ipv6_slaac_dhcpv6_dns_support.md`
- `/home/jerry/lhy` 外部完整 SDK

## 变更历史
- 2026-07-02: 增加应用层 `RK_ENABLE_IPV6_SOCKET` 宏和 socket 兼容层，覆盖 GB28181/RTSP/GAT/NTP/DM(LwM2M) 关键业务路径与 GB SDK SDP 地址族；默认关闭保持 IPv4，开启后支持 IPv6 socket。
- 2026-07-02: 补齐 Wi-Fi 应用层 IPv4/IPv6 DHCP 生命周期，记录 Wi-Fi 驱动模块与完整 SDK 驱动源码位置；结论是 IPv6 支持不需要改 Wi-Fi 芯片驱动。
- 2026-07-02: 记录完整 SDK 位置和 `build.sh lunch` 选 `9` 的内核重编入口，并完成 IPv6 boot.img 生成、替换和 Release 固件重打包。
- 2026-07-01: 更新构建参数基线：禁用全局 `-o3/-O3`，其中 `-O3` 已确认存在编码启动宕机现场风险；重新完成构建、打包和 MD5 记录。
- 2026-06-30: 接入 IPv6 SLAAC + DHCPv6 DNS 源码和配置，补充本地验证与构建阻塞记录。
- 2026-06-30: 恢复 packaging、重建带 `udhcpc6/ip/ping6` 的 BusyBox rootfs，并生成本地 Release 固件镜像；内核镜像仍需在完整 SDK/sysdrv 中按 defconfig 重编。
- 2026-06-30: 补全 IPv6 实施文档为构建和上板验收手册，新增 BusyBox 说明、`udhcpc6` IA_NA 风险、专用 DHCPv6 脚本建议和回滚路径。
- 2026-06-30: 记录 DHCPv6 支持缺口和最小接入方向。
