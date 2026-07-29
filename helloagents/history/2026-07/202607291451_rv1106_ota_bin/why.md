# 变更提案: RV1106 ota.bin 升级包

> 后续演进：`202607291546_ota_direct_images` 已取消临时 USTAR，改为校验后事务性发布三镜像并由 `rk_ota` 直接读取 `/tmp`；以下内容为当时的历史决策。

## 需求背景
当前构建链路把 `boot.img/rootfs.img/oem.img` 直接归档为 `upgrade.tar.gz`，应用再把 tar 交给 `rk_ota`。交付格式需要切换为历史 `packaging-update` 使用的带平台、CRC、镜像类型和地址信息的 `ota.bin`，同时保留底层 `rk_ota` 的 A/B 槽写入事务。

## 独立品审
- **原始想法:** 参考 `packaging.7z` 中 `packaging-update` 的方式生成并解析 `ota.bin`。
- **品审结论:** 修正后接受。
- **证据:** 旧格式使用 32 字节包头、12 字节镜像头、网络字节序和 payload CRC32；当前 `rk_ota` 只接受包含三镜像的 tar。
- **关键假设:** `boot/rootfs/oem` 镜像尾部增加最多 3 字节 `0xFF` 对齐不会改变分区升级语义，当前镜像通常已按 4 字节对齐。
- **替代方案:** 直接修改厂商 `rk_ota` 解析二进制包会扩大 SDK 补丁面；本次采用应用层严格解析后生成临时 tar 的适配方式。

## 变更内容
1. 新增格式兼容但边界检查更严格的 host `packaging-update`。
2. 构建产物从 `upgrade.tar.gz/ota_ab.tar` 统一切换为 `Release/ota.bin`。
3. 应用校验 `ota.bin` 的平台、magic、长度、CRC、三镜像类型/地址/大小，再生成仅供 `rk_ota` 使用的临时 tar。
4. GB 下载路径和 `Main.cpp` 调试触发路径切换到 `ota.bin`。

## 影响范围
- **模块:** Packaging、Update、Protocol、Main demo
- **文件:** `packaging/*`、`build.sh`、`App/Update/*`、`App/Protocol/ProtocolManager.cpp`、`App/Main.cpp`、两个 App CMake 入口
- **API:** `AbUpdateApply()` 入参语义由 tar 路径改为 `ota.bin` 路径
- **数据:** OTA 文件容器格式改变，A/B 分区布局不变

## 核心场景

### 需求: 生成 ota.bin
**模块:** Packaging
构建完成 boot/rootfs/oem 三个镜像后生成格式兼容的 `Release/ota.bin`。

#### 场景: 正常打包
三个镜像存在且不超过分区上限。
- 生成平台为 `rv1106`、magic 为 `0xABCD1234` 的包。
- payload CRC32 和长度字段与文件内容一致。

### 需求: 解析并执行 ota.bin
**模块:** Update
应用接收本地或 GB 下载的 `ota.bin`。

#### 场景: 合法包升级
包中恰好包含 boot/rootfs/oem 三种镜像且元数据合法。
- 安全生成临时 tar 并通过 `execv` 交给 `rk_ota`。
- `rk_ota` 完成后清理临时文件。

#### 场景: 非法包拒绝
平台、magic、CRC、长度、类型、地址或大小任一不合法。
- 在调用 `rk_ota` 前拒绝升级。
- 不执行 shell，不允许通过文件名注入命令。

### 需求: OTA 调试入口
**模块:** Main
设备检测 `/tmp/test_ota` 调试触发文件。

#### 场景: SD 卡调试升级
SD 卡中存在 `/mnt/sdcard/ota.bin`。
- 触发文件只消费一次。
- 调用统一的 `AbUpdateApply()` 路径并输出结果日志。

## 风险评估
- **风险:** 旧格式没有记录对齐前的原始长度；解析后的临时 tar 可能保留最多 3 字节 `0xFF`。
- **缓解:** packager 限制为 4 字节对齐且解析器执行分区大小校验；自动化测试覆盖格式损坏和三镜像边界。
