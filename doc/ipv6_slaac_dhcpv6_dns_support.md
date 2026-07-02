# RK/RV1106 IPC IPv6 支持实施文档

## 1. 目标和边界

目标设备为当前 RK/RV1106 IPC 固件，默认构建口径为：

```text
CHIP_TYPE=RV1106_DUAL_IPC
BOARD_TYPE=RC0240
BLE_TYPE=ATBM6062
```

本次目标是让设备支持 IPv4/IPv6 双栈，其中 IPv6 场景采用 `SLAAC + DHCPv6 DNS`：

- IPv6 地址由路由器 RA prefix 触发内核自动生成。
- IPv6 默认路由来自 RA。
- DNS、域搜索等附加参数通过 DHCPv6 获取。
- IPv4 DHCP/static 原行为保持可用。

本阶段不把 stateful DHCPv6 地址分配作为目标，也不实现 DHCPv6 Prefix Delegation。

需要特别区分两层能力：

- 内核 IPv6 能力：决定设备是否能创建 IPv6 地址、接收 RA、维护 IPv6 路由。
- 用户态工具能力：决定设备是否有 `udhcpc6`、`ip -6`、`ping6` 等命令来获取 DNS 和调试。

## 2. 当前实现状态

| 层级 | 状态 | 证据 |
|------|------|------|
| 业务板型 kernel defconfig | 已改为 `CONFIG_IPV6=y` | `driver/kernel/RC0240/rv1106_defconfig`、`driver/kernel/RC0240_LGV10/rv1106_defconfig`、`driver/kernel/RC0330_V20/rv1106_defconfig` |
| 当前打包 kernel 镜像 | 已用完整 SDK 重编并替换 | `packaging/image/boot.img`，MD5 `cde6decc2fc07dfd6870bfc877ae411a` |
| BusyBox 配置 | 已启用 IPv6、`udhcpc6`、`ip`、`ping6` | `driver/busybox/.config` |
| 打包 rootfs BusyBox | 已用 BusyBox 1.27.2 交叉重建并替换 | `packaging/rootfs_pub/bin/busybox` |
| rootfs applet 链接 | 已补 `udhcpc6`、`ping6`、`ip/ipaddr/iplink/iproute` | `packaging/rootfs_pub/usr/bin/udhcpc6`、`packaging/rootfs_pub/sbin/ip*` |
| 以太网应用逻辑 | 已在 link up/static/dhcp 路径启动 IPv6 sysctl + `udhcpc6` | `Middleware/libmpp/rkipc/common/network/network.c` |
| Wi-Fi 应用逻辑 | 已接入 BusyBox `udhcpc` + `udhcpc6` | `Middleware/libmpp/rkipc/common/network/Rk_wifi.c` |
| 应用层业务 socket | 已增加宏开关隔离 | `RK_ENABLE_IPV6_SOCKET`，默认关闭保持 IPv4，开启后关键 GB28181/RTSP/GAT/NTP/DM(LwM2M) socket 和 GB SDP 地址族支持 IPv6 |
| Wi-Fi 驱动 | 本次不需要修改 | 固件包内为 `packaging/oem_ipc/usr/ko/*`，完整 SDK 源码在 `sysdrv/drv_ko/wifi/` |
| 本地固件产物 | 已生成 | `Release/raw.bin`、`Release/linux.bin`、`Release/upgrade.bin` |
| 实机验证 | 未完成 | 需要上板确认 RA、DHCPv6 DNS、DNS 写入和业务联网 |

结论：

- rootfs、以太网/Wi-Fi 应用侧 DHCPv6 DNS 支持和 IPv6 kernel/boot 镜像已经进入本地打包镜像。
- 内核侧闭环使用 `/home/jerry/lhy/cmiot/RV1106_IPC_SDK.tar.gz` 解压出的完整 SDK 完成；`build.sh lunch` 选择 `9` 实际对应 `BoardConfig-SPI_NAND-NONE-RV1106_IPC38_DEMO_V10-IPC-XWR60440.mk`。
- SDK 最终 kernel `.config` 已验证包含 `CONFIG_IPV6=y`；真正运行闭环仍需要上板检查 `/proc/net/if_inet6`、RA 路由和业务联网。

## 3. SLAAC、DHCPv6 DNS 和 stateful DHCPv6

### 3.1 SLAAC

SLAAC 依赖路由器发送 RA。设备收到 RA 后，由 Linux 内核完成：

- 生成 link-local IPv6 地址，例如 `fe80::xxxx/64`。
- 根据 RA prefix 生成全局或 ULA IPv6 地址。
- 根据 RA 添加默认 IPv6 路由。
- 维护地址生命周期。

典型结果：

```text
ip -6 addr show dev eth0
  inet6 fe80::xxxx/64 scope link
  inet6 2409:xxxx:xxxx:1:xxxx/64 scope global dynamic

ip -6 route
  default via fe80::router dev eth0 proto ra
```

### 3.2 DHCPv6 DNS

本方案中 DHCPv6 只用于获取 DNS 和域搜索列表。默认网关仍看 RA，不期望 DHCPv6 下发默认路由。

BusyBox 1.27.2 的 `udhcpc6` 支持 RFC3646：

```text
CONFIG_UDHCPC6=y
CONFIG_FEATURE_UDHCPC6_RFC3646=y
```

源码中 `dns` 和 `search` 选项会转换成脚本环境变量：

```text
dns=<ipv6 dns list>
search=<domain search list>
```

### 3.3 stateful DHCPv6

stateful DHCPv6 表示 IPv6 地址也由 DHCPv6 服务器分配。它和本次目标不同。

注意：BusyBox 1.27.2 的 `udhcpc6` 没有明显的 information-only 专用参数，默认 DHCPv6 流程会携带 IA_NA。当前接入的目标是让设备能通过 `udhcpc6` 获取 DNS；是否严格不申请 DHCPv6 地址，需要在目标网络上抓包确认。如果现场必须严格做到 information-only，建议改用支持信息请求模式的 DHCPv6 客户端，或按 BusyBox 版本能力补补丁。

## 4. BusyBox 说明

BusyBox 是嵌入式 Linux rootfs 中的基础命令集合。它通常是一个二进制文件，通过不同软链接表现为不同命令。

当前 rootfs 中的典型形态：

```text
/bin/busybox
/sbin/udhcpc -> ../bin/busybox
/usr/bin/udhcpc6 -> ../../bin/busybox
/sbin/ip -> ../bin/busybox
/bin/ping6 -> busybox
```

执行 `/usr/bin/udhcpc6` 时，实际运行的仍是 `/bin/busybox`，BusyBox 根据调用名选择 `udhcpc6` applet。

和 IPv6 相关的关键 applet：

| applet | 用途 |
|--------|------|
| `udhcpc` | DHCPv4 客户端 |
| `udhcpc6` | DHCPv6 客户端，本次用于 DHCPv6 DNS |
| `ip` / `ipaddr` / `iplink` / `iproute` | 查看 IPv6 地址、路由和链路 |
| `ping6` | IPv6 连通性测试 |
| `ifconfig` / `route` | 兼容旧脚本的网络配置命令 |

只修改 `driver/busybox/.config` 不会自动改变固件里的 `/bin/busybox`。必须重新编译 BusyBox，并替换到 rootfs，再补齐 applet 软链接。

## 5. 代码和配置改动

### 5.1 kernel defconfig

以下业务板型已打开 IPv6：

```text
driver/kernel/RC0240/rv1106_defconfig
driver/kernel/RC0240_LGV10/rv1106_defconfig
driver/kernel/RC0330_V20/rv1106_defconfig
```

关键配置：

```text
CONFIG_IPV6=y
```

SPI NOR 小系统建议使用 built-in `CONFIG_IPV6=y`，不要先做成模块。若改成 `m`，必须保证 `ipv6.ko` 在网络初始化前加载，否则 `/proc/sys/net/ipv6` 不存在，SLAAC 和 `udhcpc6` 都无法正常工作。

### 5.1.1 SDK kernel/boot.img 闭环记录

完整 SDK 位于 `/home/jerry/lhy/cmiot/RV1106_IPC_SDK.tar.gz`。本次在 `/tmp/lhy_rv1106_sdk/RV1106_IPC_SDK/project` 执行：

```sh
printf '9\n' | ./build.sh lunch
./build.sh kernel
```

`lunch 9` 实际选择：

```text
BoardConfig_IPC/BoardConfig-SPI_NAND-NONE-RV1106_IPC38_DEMO_V10-IPC-XWR60440.mk
```

该配置使用：

```text
RK_KERNEL_DEFCONFIG=rv1106_defconfig
RK_KERNEL_DEFCONFIG_FRAGMENT=rv1106-ipc-XWR60440.config
RK_KERNEL_DTS=rv1106g-38x38-ipc-XWR60440-v10-spi-nand.dts
```

基础 `rv1106_defconfig` 中 `CONFIG_IPV6` 关闭，但 `rv1106-ipc-XWR60440.config` 覆盖为 `CONFIG_IPV6=y`。最终 `sysdrv/source/objs_kernel/.config` 已验证：

```text
CONFIG_IPV6=y
CONFIG_IPV6_SIT=y
CONFIG_IPV6_MROUTE=y
```

宿主未安装 `libssl-dev`、`device-tree-compiler` 且 sudo 需要密码时，本次采用非 root 临时依赖路径：将 `libssl-dev`、`libssl3` 用 `apt-get download` 下载到 `/tmp/lhy_debs` 并解包到 `/tmp/lhy_host_deps`；添加 `/tmp/lhy_host_tools/python -> /usr/bin/python3`；首次内核构建生成 `sysdrv/source/objs_kernel/scripts/dtc/dtc` 后，将该目录加入 `PATH` 重跑 `./build.sh kernel`。

### 5.2 BusyBox 配置

`driver/busybox/.config` 中已启用：

```text
CONFIG_FEATURE_IPV6=y
CONFIG_IP=y
CONFIG_IPADDR=y
CONFIG_IPLINK=y
CONFIG_IPROUTE=y
CONFIG_FEATURE_IP_ADDRESS=y
CONFIG_FEATURE_IP_LINK=y
CONFIG_FEATURE_IP_ROUTE=y
CONFIG_PING6=y
CONFIG_UDHCPC6=y
CONFIG_FEATURE_UDHCPC6_RFC3646=y
```

`CONFIG_UDHCPC6` 生成的 applet 名是 `udhcpc6`，不是 `dhcpc6`。

### 5.3 rootfs applet

`packaging/rootfs_pub/bin/busybox` 已从 BusyBox 1.27.2 重新交叉编译，当前二进制包含：

```text
udhcpc6
ping6
ipaddr
iplink
iproute
```

当前已补齐链接：

```text
packaging/rootfs_pub/bin/ping6 -> busybox
packaging/rootfs_pub/sbin/ip -> ../bin/busybox
packaging/rootfs_pub/sbin/ipaddr -> ../bin/busybox
packaging/rootfs_pub/sbin/iplink -> ../bin/busybox
packaging/rootfs_pub/sbin/iproute -> ../bin/busybox
packaging/rootfs_pub/usr/bin/udhcpc6 -> ../../bin/busybox
packaging/rootfs_pub/sbin/udhcpc6 -> ../bin/busybox
```

### 5.4 network.c 以太网逻辑

`Middleware/libmpp/rkipc/common/network/network.c` 已增加以下静态函数：

```text
rk_network_ipv6_conf_set()
rk_network_ipv6_enable()
rk_network_ipv6_flush()
rk_network_dhcpv6_stop()
rk_network_dhcpv6_start()
```

link up 或应用设置 IPv4 DHCP/static 时，会执行：

```text
disable_ipv6=0
forwarding=0
accept_ra=1
autoconf=1
udhcpc6 -i <ifname> -s <script> -p /var/run/udhcpc6.<ifname>.pid -b -q
```

link down 时会：

```text
kill /var/run/udhcpc6.<ifname>.pid
ip -6 addr flush dev <ifname>
ifconfig <ifname> 0.0.0.0
```

当前 `rk_network_dhcpv6_start()` 的脚本选择逻辑：

```sh
script=/usr/share/udhcpc6/default.script
[ -x "$script" ] || script=/usr/share/udhcpc/default.script
```

当前 rootfs 没有单独的 `/usr/share/udhcpc6/default.script`，会回退到 `/usr/share/udhcpc/default.script`。该脚本会处理 `dns` 和 `search` 环境变量并写入 `/etc/resolv.conf`，但它原本是 DHCPv4 脚本，也会尝试执行 IPv4 `ifconfig` 和 `route` 操作。上板验证时必须观察该脚本在 `udhcpc6` 事件中的实际输出，必要时应增加专用 `udhcpc6` 脚本。

### 5.5 Rk_wifi.c Wi-Fi 逻辑

IPv6 属于内核网络栈和用户态地址/DNS 配置能力，不要求 Wi-Fi 芯片驱动为 IPv6 单独改代码。当前需要改的是 `wlan0` 关联成功后的应用层 DHCP/SLAAC 生命周期。

`Middleware/libmpp/rkipc/common/network/Rk_wifi.c` 已增加：

```text
rk_wifi_dhcpv4_start()
rk_wifi_dhcpv4_stop()
rk_wifi_get_ipv4_address()
rk_wifi_ipv6_enable()
rk_wifi_ipv6_flush()
rk_wifi_dhcpv6_start()
rk_wifi_dhcpv6_stop()
```

当前打包 rootfs 没有 `dhcpcd`，但有 BusyBox `udhcpc` 和 `udhcpc6`。因此 Wi-Fi IPv4 DHCP 已从历史 `dhcpcd wlan0 -AL -t 0 &` 改为：

```text
udhcpc -i wlan0 -s /usr/share/udhcpc/default.script -p /var/run/udhcpc.wlan0.pid -b -q
```

Wi-Fi IPv6 DNS 使用：

```text
udhcpc6 -i wlan0 -s <script> -p /var/run/udhcpc6.wlan0.pid -b -q
```

`RK_wifi_enable()`、WPA connected 事件和手动连接成功路径都会启动 `wlan0` IPv4/IPv6 DHCP；WPA disconnected 和 Wi-Fi disable 路径会停止 `udhcpc6` 并清理 IPv6 地址。由于 `wpa_cli status` 不一定包含 `ip_address`，`RK_wifi_running_getConnectionInfo()` 增加了 `SIOCGIFADDR` fallback，从内核接口状态读取 `wlan0` IPv4 地址。

Wi-Fi 驱动位置：

```text
当前仓库打包模块:
packaging/oem_ipc/usr/ko/cfg80211.ko
packaging/oem_ipc/usr/ko/mac80211.ko
packaging/oem_ipc/usr/ko/aic8800dl/aic_load_fw.ko
packaging/oem_ipc/usr/ko/aic8800dl/aic8800_fdrv.ko
packaging/oem_ipc/usr/ko/aic8800dl/aic8800D80/*.bin

完整 SDK 驱动源码:
/tmp/lhy_rv1106_sdk/RV1106_IPC_SDK/sysdrv/drv_ko/wifi/aic8800_netdrv/
/tmp/lhy_rv1106_sdk/RV1106_IPC_SDK/sysdrv/drv_ko/wifi/atbm/
/tmp/lhy_rv1106_sdk/RV1106_IPC_SDK/sysdrv/drv_ko/wifi/rtl8188ftv/
```

### 5.6 应用层 socket 宏

业务协议是否使用 IPv6 socket 由编译宏控制：

```text
RK_ENABLE_IPV6_SOCKET=0  默认，保留 IPv4-only socket 行为
RK_ENABLE_IPV6_SOCKET=1  关键业务 socket 使用 IPv4/IPv6 双栈能力
```

启用方式：

```sh
cmake ... -DRK_ENABLE_IPV6_SOCKET=ON
```

宏关闭时，GB28181、RTSP、GAT1400 callback listener、NTP、DM/LwM2M 等路径保留原 `AF_INET/sockaddr_in` 分支。宏打开时，新增 `App/Protocol/SocketCompat.h` 作为 C++ 侧公共兼容层，使用：

```text
getaddrinfo(AF_UNSPEC)
sockaddr_storage
IPV6_V6ONLY=0 dual-stack listener
IPv4-mapped IPv6 地址格式化/比较
```

当前已覆盖的应用层路径：

```text
App/Protocol/gb28181/GB28181RtpPsSender.cpp
App/Protocol/gb28181/GB28181BroadcastBridge.cpp
App/Protocol/gb28181/GB28181ListenBridge.cpp
App/Protocol/gb28181/sdk_port/NetSocketSdkShim.cpp
third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/SDP/SdpUtil.cpp
App/Protocol/ProtocolManager.cpp
App/Protocol/gat1400/GAT1400ClientService.cpp
App/RtspServer/src/net/SocketUtil.cpp
App/RtspServer/src/net/TcpSocket.cpp
App/RtspServer/src/xop/RtpConnection.cpp
App/RtspServer/src/xop/MediaSession.cpp
Middleware/libmpp/rkipc/common/network/ntp.c
App/DM/DmClientService.cpp
```

同时处理的协议层地址声明：

- GB28181 广播应答 SDP 在本地地址为 IPv6 时输出 `IN IP6`。
- GB28181 SDK `CSdpUtil::ToString()` 在本地地址为 IPv6 时输出 `IN IP6`。
- RTSP SDP 在本地地址为 IPv6 时输出 `IN IP6`。

注意：本轮只补齐当前工程实际接入的 GB SDK SDP 生成路径和业务 socket 路径；更深层的第三方 SIP/eXosip 内部 IPv6 细节没有做无差别重构，后续应以平台联调证据再决定是否继续改。

## 6. 建议补齐的 DHCPv6 专用脚本

为了避免 DHCPv6 调用 DHCPv4 脚本时执行无意义的 IPv4 配置，建议后续增加：

```text
packaging/rootfs_pub/usr/share/udhcpc6/default.script
```

建议脚本逻辑：

```sh
#!/bin/sh

RESOLV_CONF="/etc/resolv.conf"
[ -e "$RESOLV_CONF" ] || touch "$RESOLV_CONF"

case "$1" in
	deconfig|leasefail|nak)
		tmp=$(mktemp)
		grep -vE "# ${interface}\.v6$" "$RESOLV_CONF" > "$tmp"
		cat "$tmp" > "$RESOLV_CONF"
		rm -f "$tmp"
		;;
	bound|renew)
		tmp=$(mktemp)
		grep -vE "# ${interface}\.v6$" "$RESOLV_CONF" > "$tmp"
		cat "$tmp" > "$RESOLV_CONF"
		rm -f "$tmp"

		[ -n "$search" ] && echo "search $search # ${interface}.v6" >> "$RESOLV_CONF"
		for i in $dns; do
			echo "nameserver $i # ${interface}.v6" >> "$RESOLV_CONF"
		done
		;;
esac

exit 0
```

首次上板时也可以先用调试脚本替代：

```sh
#!/bin/sh
env | sort > /tmp/udhcpc6.${interface}.${1}.env
exit 0
```

确认 `dns`、`search` 变量名和事件名后，再固化正式脚本。

## 7. 本地构建步骤

### 7.1 应用和中间件构建验证

完整 CMake 验证命令：

```sh
bash tools/issue_bot/build_verify.sh /home/jerry/silver/rk_gb
```

已通过，最近日志目录：

```text
/tmp/rk_gb-build-verify.91D0xE/logs
```

`RK_ENABLE_IPV6_SOCKET=ON` 也已单独完成 Middleware + App 交叉构建验证，构建目录：

```text
/tmp/rk_gb-ipv6-socket-on.9dWpEd
```

构建参数注意：

- `Middleware/CMakeLists.txt` 的历史小写 `-o3` 不是 GCC 优化参数，GCC 会按 `-o 3` 解析，和 CMake 自动生成的对象输出参数冲突。
- 顶层 `CMakeLists.txt` 的大写 `-O3` 虽然能让工程编译通过，但现场反馈编码启动会宕机，当前也不能作为全局构建参数使用。
- 当前基线是不启用全局 `-O3/-o3`；若后续某个单独目标需要优化，必须先证明编码链路运行稳定，再按目标级别局部打开。
- `tools/tests/middleware_cmake_flags_regression.py` 会检查 Middleware 和主工程生成的 `flags.make`，防止 `-o3` 或 `-O3` 回归。

### 7.2 BusyBox 重建

本地使用 BusyBox 1.27.2 源码：

```text
/tmp/rk_gb_busybox_ipv6/busybox-1.27.2
```

构建步骤：

```sh
mkdir -p /tmp/rk_gb_busybox_ipv6
cd /tmp/rk_gb_busybox_ipv6
wget https://busybox.net/downloads/busybox-1.27.2.tar.bz2
tar xf busybox-1.27.2.tar.bz2
cp /home/jerry/silver/rk_gb/driver/busybox/.config busybox-1.27.2/.config
cd busybox-1.27.2
PATH="/home/jerry/silver/RK/arm-rockchip830-linux-uclibcgnueabihf/bin:$PATH" \
	make ARCH=arm CROSS_COMPILE=arm-rockchip830-linux-uclibcgnueabihf- oldconfig
PATH="/home/jerry/silver/RK/arm-rockchip830-linux-uclibcgnueabihf/bin:$PATH" \
	make ARCH=arm CROSS_COMPILE=arm-rockchip830-linux-uclibcgnueabihf- -j4 busybox
```

验证：

```sh
strings busybox | grep -E '^(udhcpc6|ping6|ipaddr|iplink|iproute)$'
file busybox
```

替换 rootfs：

```sh
cp /tmp/rk_gb_busybox_ipv6/busybox-1.27.2/busybox \
	/home/jerry/silver/rk_gb/packaging/rootfs_pub/bin/busybox
```

补链接：

```sh
ln -sf busybox packaging/rootfs_pub/bin/ping6
ln -sf ../bin/busybox packaging/rootfs_pub/sbin/ip
ln -sf ../bin/busybox packaging/rootfs_pub/sbin/ipaddr
ln -sf ../bin/busybox packaging/rootfs_pub/sbin/iplink
ln -sf ../bin/busybox packaging/rootfs_pub/sbin/iproute
ln -sf ../../bin/busybox packaging/rootfs_pub/usr/bin/udhcpc6
ln -sf ../bin/busybox packaging/rootfs_pub/sbin/udhcpc6
```

### 7.3 packaging 恢复

当前仓库有 `packaging.7z`。恢复命令：

```sh
python3 -m py7zr x packaging.7z .
```

注意：不要省略输出目录参数。`python3 -m py7zr x packaging.7z` 在当前环境会触发 `NoneType.is_absolute` 异常并留下半成品。

### 7.4 固件打包

普通沙箱下 `fakeroot -- chown 0:0` 会失败，打包需要提权沙箱或正常宿主环境。

打包命令：

```sh
make -C packaging CROSS=/home/jerry/silver/RK/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-
```

产物：

```text
Release/raw.bin
Release/linux.bin
Release/upgrade.bin
```

最近本地生成结果（应用层 IPv6 socket 宏收尾后，默认 `RK_ENABLE_IPV6_SOCKET=0`）：

```text
Release/raw.bin      128M  md5 6123c1ed925d0a99ab4b58e2a2dff9af
Release/linux.bin     18M  md5 78ac36771b8d1a4d3da2c73dcecca8c1
Release/upgrade.bin   18M  md5 78ac36771b8d1a4d3da2c73dcecca8c1
Bin/dgiot                   md5 a4c92b470caf3e7f1d8d96453de1fe00
packaging/oem_ipc/usr/bin/dgiot md5 eb020035257fc158480b5981fabd4662
```

## 8. 上板验收

### 8.1 启动后基础检查

```sh
cat /proc/net/if_inet6
ls /proc/sys/net/ipv6/conf/eth0
cat /proc/sys/net/ipv6/conf/eth0/disable_ipv6
cat /proc/sys/net/ipv6/conf/eth0/accept_ra
cat /proc/sys/net/ipv6/conf/eth0/autoconf
cat /proc/sys/net/ipv6/conf/eth0/forwarding
```

预期：

```text
disable_ipv6 = 0
accept_ra    = 1
autoconf     = 1
forwarding   = 0
```

如果 `/proc/sys/net/ipv6` 不存在，优先判断 kernel 未启用 IPv6 或 `ipv6.ko` 未加载。

### 8.2 BusyBox applet 检查

```sh
busybox | grep -E 'udhcpc6|ping6|ip$'
which udhcpc6
which ip
which ping6
udhcpc6 --help
```

预期：

- 能看到 `udhcpc6`。
- `which udhcpc6` 指向 `/usr/bin/udhcpc6` 或 `/sbin/udhcpc6`。
- `ip -6 addr`、`ip -6 route` 可运行。

### 8.3 地址和路由检查

```sh
ifconfig eth0
ip -6 addr show dev eth0
ip -6 route
cat /etc/resolv.conf
```

必须满足：

- `eth0` 至少有 link-local IPv6 地址。
- 在有 RA prefix 的网络中，`eth0` 有非 `fe80::/10` 的全局地址或 ULA 地址。
- 有 IPv6 默认路由，来源应是 RA。
- `/etc/resolv.conf` 中出现 IPv6 DNS。

### 8.4 抓包

路由器侧或设备侧抓包：

```sh
tcpdump -i eth0 -nn 'icmp6 or udp port 546 or udp port 547'
```

预期：

- ICMPv6 RS/RA：type 133/134。
- DHCPv6 UDP：client 546，server 547。
- DHCPv6 Reply 中包含 DNS 相关 option。

如果现场要求严格 `SLAAC + DHCPv6 DNS`，需要确认 `udhcpc6` 是否发送 IA_NA，服务器是否下发地址，以及设备最终是否使用 DHCPv6 地址。BusyBox 1.27.2 默认流程可能包含 IA_NA，这一点不能只靠配置判断。

### 8.5 连通性检查

```sh
ping6 <router-link-local>%eth0
ping6 <ipv6-dns-address>
ping6 240c::6666
```

如果业务需要域名访问：

```sh
nslookup <domain>
```

当前 BusyBox 配置为 `# CONFIG_NSLOOKUP is not set`。如果需要直接在设备侧验证域名解析，需要临时启用 `nslookup`，或通过业务日志、`/etc/resolv.conf` 和抓包确认 DNS 行为。

### 8.6 热插拔回归

执行：

1. 上电插网线。
2. 拔网线。
3. 等待 5 秒。
4. 重新插网线。
5. 重复 3 次。

每次检查：

```sh
ps | grep -E 'udhcpc|udhcpc6'
ip -6 addr show dev eth0
ip -6 route
cat /etc/resolv.conf
```

预期：

- 不出现多个残留的同接口 `udhcpc6`。
- 重新插线后 IPv4/IPv6 均恢复。
- `/etc/resolv.conf` 不被反复清空导致 DNS 丢失。

## 9. 故障定位

### 9.1 没有 `/proc/sys/net/ipv6`

可能原因：

- kernel 没有启用 `CONFIG_IPV6=y`。
- 当前烧录的 `boot.img` 不是新内核。
- 如果 IPv6 做成模块，`ipv6.ko` 未加载。

检查：

```sh
cat /proc/net/if_inet6
lsmod | grep ipv6
dmesg | grep -i ipv6
```

处理：

- 在完整 SDK/sysdrv 中按业务板型 defconfig 重编 kernel。
- 确认打包时替换了 `packaging/image/boot.img`。

### 9.2 有 link-local 地址但没有全局地址

可能原因：

- 路由器没有发送 RA prefix。
- `accept_ra` 或 `autoconf` 没打开。
- 设备被配置成 forwarding，Linux 不接受 RA。

检查：

```sh
cat /proc/sys/net/ipv6/conf/eth0/accept_ra
cat /proc/sys/net/ipv6/conf/eth0/autoconf
cat /proc/sys/net/ipv6/conf/eth0/forwarding
tcpdump -i eth0 -nn 'icmp6'
```

处理：

```sh
echo 0 > /proc/sys/net/ipv6/conf/eth0/forwarding
echo 1 > /proc/sys/net/ipv6/conf/eth0/accept_ra
echo 1 > /proc/sys/net/ipv6/conf/eth0/autoconf
```

### 9.3 有 IPv6 地址但没有 IPv6 默认路由

可能原因：

- RA 中没有默认路由信息。
- `accept_ra` 设置不正确。
- 路由器策略只发 prefix，不发 default route。

检查：

```sh
ip -6 route
tcpdump -i eth0 -nn 'icmp6 and ip6[40] == 134'
```

### 9.4 `udhcpc6` 不存在

可能原因：

- BusyBox 配置改了，但 rootfs 没替换新 busybox。
- applet 软链接没创建。

检查：

```sh
busybox | grep udhcpc6
ls -l /usr/bin/udhcpc6 /sbin/udhcpc6
strings /bin/busybox | grep udhcpc6
```

处理：

- 重编 BusyBox。
- 替换 `/bin/busybox`。
- 补 applet 软链接。

### 9.5 DHCPv6 有报文但 `/etc/resolv.conf` 没 IPv6 DNS

可能原因：

- DHCPv6 server 没下发 RFC3646 DNS option。
- `udhcpc6` 没请求 `dns/search`。
- 脚本没有正确处理 `dns/search` 环境变量。
- IPv4 DHCP 脚本后运行，覆盖了 resolv.conf。

检查：

```sh
udhcpc6 -i eth0 -s /tmp/udhcpc6.debug -f -v
cat /tmp/udhcpc6.eth0.*.env
cat /etc/resolv.conf
```

临时调试脚本：

```sh
cat >/tmp/udhcpc6.debug <<'EOF'
#!/bin/sh
env | sort > /tmp/udhcpc6.${interface}.${1}.env
exit 0
EOF
chmod +x /tmp/udhcpc6.debug
```

## 10. 回滚方案

如果上板后 IPv6 改动影响 IPv4 或主业务，可以按以下顺序回滚：

1. 在 `network.c` 中停止调用 `rk_network_dhcpv6_start()` 和 `rk_network_dhcpv6_stop()`。
2. 保留 BusyBox 新二进制不一定影响 IPv4；若怀疑 BusyBox 兼容性，可恢复 `packaging.7z` 中的旧 `packaging/rootfs_pub/bin/busybox`。
3. kernel defconfig 的 `CONFIG_IPV6=y` 通常不会破坏 IPv4；若镜像空间或启动异常，再回退内核配置。
4. 重新执行 `make -C packaging ...` 生成 Release 镜像。

## 11. 已验证命令

本地已通过：

```sh
python3 tools/tests/network_ipv6_slaac_dhcpv6_regression.py
python3 tools/tests/middleware_cmake_flags_regression.py
git diff --check
bash tools/issue_bot/build_verify.sh /home/jerry/silver/rk_gb
```

本地已生成：

```text
Release/raw.bin
Release/linux.bin
Release/upgrade.bin
```

2026-07-02 重新生成的关键 MD5：

```text
packaging/image/boot.img = cde6decc2fc07dfd6870bfc877ae411a
Release/raw.bin          = 6123c1ed925d0a99ab4b58e2a2dff9af
Release/linux.bin        = 78ac36771b8d1a4d3da2c73dcecca8c1
Release/upgrade.bin      = 78ac36771b8d1a4d3da2c73dcecca8c1
```

## 12. 后续必须完成

1. 上板确认 `/proc/sys/net/ipv6`、`/proc/net/if_inet6`、RA 默认路由。
2. 抓包确认 DHCPv6 是否满足现场定义的 `SLAAC + DHCPv6 DNS`，特别是 BusyBox 1.27.2 是否发送 IA_NA。
3. 增加专用 `/usr/share/udhcpc6/default.script`，避免 DHCPv6 复用 DHCPv4 脚本。
4. 做 IPv4-only、IPv6-only、双栈、拔插网线、重启后的回归。
5. 上板验证 Wi-Fi 路线：`wlan0` 是否能获取 IPv4、SLAAC IPv6、RA 默认路由和 DHCPv6 DNS。
