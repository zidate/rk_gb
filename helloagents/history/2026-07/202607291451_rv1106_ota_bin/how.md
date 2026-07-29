# 技术设计: RV1106 ota.bin 升级包

## 技术方案
### 方案取舍
- **采用:** 保持旧 `packaging-update` 容器的 32 字节包头、12 字节镜像头、网络字节序、4 字节 `0xFF` 对齐和标准 CRC32；重写小型严格 packager/parser。
- **修正:** 不照搬旧工具对固定 section 的宽松解析和错误返回逻辑；容器镜像类型固定为 boot=4、rootfs=5、oem=6，不复用 `upgrade.ini` 中的物理分区 type。
- **拒绝/暂缓:** 暂不修改厂商 `rk_ota`，避免扩大 A/B 写槽和写保护逻辑变更范围。

### 核心技术
- Host C99 packager
- Target C++11 parser、CRC32 和最小 USTAR writer
- POSIX `fork/execv/waitpid` 调用 `rk_ota`

### 实现要点
- 校验 platform、magic、包长和 payload CRC 后才创建升级 tar。
- 只接受类型 4/5/6 各一次，地址固定为 A 槽逻辑地址，镜像不得超过 4/10/32 MiB。
- 临时文件使用 `mkstemp`，tar 成员名由程序固定，不使用包内字符串或 shell。

## 架构设计
```mermaid
flowchart LR
    I[boot/rootfs/oem images] --> P[packaging-update]
    P --> B[Release/ota.bin]
    B --> V[OtaPackage validator]
    V --> T[temporary USTAR]
    T --> R[rk_ota]
    R --> S[inactive A/B slot]
```

## 架构决策 ADR
### ADR-20260729-ota-adapter: 在应用层适配 ota.bin 到 rk_ota tar
**上下文:** 交付格式要求使用旧二进制容器，但已验证的厂商 A/B updater 只接收 tar。
**决策:** 应用严格解析 `ota.bin` 并生成临时 USTAR，再调用原 `rk_ota`。
**理由:** 保留厂商写槽、校验、写保护和槽切换事务，格式边界集中在可测试的应用模块。
**替代方案:** 直接修改 `rk_ota` → 拒绝原因: 增大 vendor patch 和设备端回归范围。
**影响:** 应用升级前增加一次顺序读和一次临时 tar 写入，需要 `/tmp` 有约一份镜像集合的空间。

## 安全与性能
- **安全:** 固定成员映射、严格整数边界、CRC/长度/类型去重校验、`mkstemp`、`execv`，禁止 `system()`。
- **性能:** 全程流式 CRC 和拷贝，内存缓冲固定，不加载整包。

## 测试与部署
- **测试:** host packager 格式、CRC、损坏包拒绝、parser 转 tar、wrapper argv、构建路径静态约束。
- **部署:** 交叉编译应用；设备端以 `/tmp/test_ota` + `/mnt/sdcard/ota.bin` 验证。
