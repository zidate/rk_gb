# DM IPv6 连接失败 case

## 现象
- `debug.log` 中 DM 客户端启动后反复打印 `Opening LwM2M connection to b.fxltsbl.com:5683`，未见注册成功或 `STATE_READY` 后的心跳更新日志。
- 同一份日志里以太网 DHCPv4 成功，`eth0` 获得 IPv4 地址。
- `ifconfig` 只显示 `eth0` 有 `fe80::.../64 Scope:Link`，没有 global/ULA IPv6 地址。

## 结论
- 日志证据首先指向设备侧没有可用于公网 DM 平台的 IPv6 地址或路由。`fe80::/64` 是链路本地地址，不能直接访问公网 AAAA 地址。
- `b.fxltsbl.com` 解析同时有 A 和 AAAA 记录，因此不能按“域名没有 IPv6”解释该现象。
- 旧 DM 实现还有一个编译侧前提：`rk_gb/CMakeLists.txt` 的 `RK_ENABLE_IPV6_SOCKET` 默认 `OFF`，`build.sh` 未传 `-DRK_ENABLE_IPV6_SOCKET=ON`。未打开时，`DmClientService.cpp` 会强制使用 `AF_INET`，即使设备网络后来具备 IPv6，也不会走 IPv6 DM 连接。
- 旧实现即使打开 `RK_ENABLE_IPV6_SOCKET`，也会先解析出一个 concrete address family 再创建 Wakaama socket；如果 DNS/策略选到 IPv6 而板端 IPv6 不可达，就不会继续回退 IPv4。
- 已修正为一个固件运行时双栈：构建默认打开 `RK_ENABLE_IPV6_SOCKET`；DM 传 `AF_UNSPEC` 给 Wakaama；Wakaama UDP 优先创建 IPv6 dual-stack socket，远端候选逐个 probe，IPv6 失败后可继续尝试 IPv4，IPv4 远端映射为 `::ffff:a.b.c.d` 后通过同一个 dual-stack socket 发送。
- 运行时仍需要板端网络拿到可路由 IPv6 地址/默认路由/DNS，才能真正走 IPv6；否则应自动落到 IPv4。

## 证据
- `debug.log:700`：`IPv6: ADDRCONF(NETDEV_CHANGE): eth0: link becomes ready`，只说明 IPv6 栈随链路 ready。
- `debug.log:807-808`：DHCPv4 脚本添加 IPv4 路由/DNS。
- `debug.log:1098-1100`：`eth0` 有 IPv4 地址和 `fe80::... Scope:Link`，没有 global IPv6。
- `debug.log:905-916`、`debug.log:1064-1111`：DM 反复打开 `b.fxltsbl.com:5683`。
- 旧证据：`rk_gb/App/DM/DmClientService.cpp:86-122` 曾在 `RK_ENABLE_IPV6_SOCKET` 打开时先用 `getaddrinfo(AF_UNSPEC)` 决定单一 IPv4/IPv6；关闭时固定返回 `AF_INET`。
- 修正后证据：`rk_gb/App/DM/DmClientService.cpp` 使用 `GetDmAddressFamilyPreference()`，宏打开时返回 `AF_UNSPEC`，不再提前锁定单一 DNS 地址族。
- 修正后证据：`rk_gb/third_party/wakaama/transport/udp/connection.c` 使用 `create_dual_stack_socket()`、`IPV6_V6ONLY=0`、`map_ipv4_to_ipv6()` 和逐候选 `connect()` probe 实现 IPv6/IPv4 fallback。
- 修正后证据：`rk_gb/CMakeLists.txt`、`rk_gb/Middleware/CMakeLists.txt` 和 `rk_gb/build.sh` 默认开启 `RK_ENABLE_IPV6_SOCKET`。

## 建议验证
1. 板端确认 IPv6 网络：`ip -6 addr show dev eth0` 应有非 `fe80::/64` 地址；`ip -6 route` 应有 `default via ...`。
2. 板端确认 DNS：`getent ahostsv6 b.fxltsbl.com` 或等价工具应解析出 AAAA。
3. 板端确认可达性：用 `ping6`/`nc -6 -u`/抓包验证能访问 DM 的 IPv6 `5683/udp`。
4. 应用构建侧用干净目录重配，确认 CMake cache 中 `RK_ENABLE_IPV6_SOCKET=ON`。
5. 若重构建后仍失败，查看新增的 `[DM] remote getaddrinfo failed` / `[DM] probe connect failed` 日志，区分 DNS、IPv6 route、IPv4 fallback 或 UDP 5683 被拦截。
