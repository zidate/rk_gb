# CMIOT OSD 去除云平台选择依赖设计

## 背景

当前实现为了让同一固件在 GB28181 与 CMIOT 间运行时选择，引入了 `CloudPlatformType`、`CFG_CLOUD_PLATFORM` 和 `CloudPlatformControl`。这部分平台启动策略超出了本次 OSD 需求范围，并使 `CmiotOsdControl` 通过 `GetCloudPlatform()` 决定是否加载、覆盖或恢复 OSD。

本次修改恢复 CMIOT SDK 接入完成时的原有平台启动方式，不再由新增的云平台配置控制启动。OSD 配置选择只由 CMIOT 持久化配置中的 `osdSwitch` 决定。

## 目标

1. 删除新增的云平台选择机制及其配置、构建、测试和文档引用。
2. `Main.cpp` 恢复使用原有 `s_bStartCmiot` 启动路径，不设计新的平台启动策略。
3. CMIOT OSD 与平台类型完全解耦。
4. OSD 启动和运行时配置切换都遵循同一规则：
   - `osdSwitch == 1`：应用持久化的 CMIOT OSD。
   - `osdSwitch == 0`：应用当前缓存的本地 OSD。
5. 保留 CMIOT OSD 与本地 OSD 的独立持久化，以及 `0-10000` 坐标只在 RK 媒体边界换算的现有行为。

## 非目标

- 不改变 `s_bStartCmiot` 的来源或含义。
- 不重新设计 CMIOT、GB28181、GAT1400 或 DM 的启动关系。
- 不改变 CMIOT SDK 的 OSD 数据结构、回调接口和错误码传播。
- 不修改本地/国标 OSD 的配置格式。

## 设计

### 1. 恢复原平台启动方式

以提交 `6ad8b24` 的 CMIOT SDK 接入代码为启动行为基线：`Main.cpp` 继续使用 `s_bStartCmiot` 判断是否调用 `cmiot_start()`，删除 `CloudPlatformType`、`GetCloudPlatform()` 和 `CLOUD_PLATFORM_*` 分支。

在原有 CMIOT 启动分支中保留 `cmiot_osd_initialize()`。初始化失败只记录返回码；`cmiot_start()` 保持原有调用方式，不新增平台策略。

### 2. 删除云平台选择模块

删除以下新增内容：

- `App/Manager/CloudPlatformControl.cpp`
- `App/Manager/CloudPlatformControl.h`
- `CFG_CLOUD_PLATFORM` 及其配置名称映射
- `CMediaDefaultConfig::setCloudPlatform()` 及默认配置
- `App/CMakeLists.txt` 中的 `CloudPlatformControl.cpp`
- `tools/tests/cloud_platform_config_regression.py`
- `tools/tests/cloud_platform_startup_regression.py`

保留 `CFG_CMIOT_OSD`、`CMediaDefaultConfig::setCmiotOsd()` 和全部 CMIOT OSD 持久化数据。

### 3. OSD 仅根据 osdSwitch 决策

`CmiotOsdControl.cpp` 删除 `Manager/CloudPlatformControl.h` 和三个平台判断点。

- `ApplyEffectiveConfigLocked()`：直接按 `config.osdSwitch` 选择 `ApplyStoredCmiotOsd()` 或 `g_AVManager.ApplyLocalOsdConfig()`。
- `cmiot_osd_initialize()`：配置加载成功且有效时调用统一的生效逻辑；`osdSwitch=0` 时明确恢复本地 OSD，而不是仅返回成功。
- `cmiot_osd_is_override_active()`：只根据已加载配置的 `valid && osdSwitch` 返回覆盖状态。
- `cmiot_osd_set_config()`：继续在同一互斥锁内完成深拷贝、持久化和硬件应用，并传播实际错误码。

### 4. 数据流

```text
CMIOT_CMD_SET_OSD / 启动恢复
              |
              v
       CFG_CMIOT_OSD 持久化
              |
              v
         读取 osdSwitch
          /          \
        1              0
       /                \
CMIOT OSD          最新本地 OSD
       \                /
        RK OSD 媒体接口
```

本地 OSD 在 CMIOT 覆盖期间仍更新 `AVManager` 缓存，但不刷新硬件；`osdSwitch` 关闭后恢复时间 OSD 和全部文本槽。

## 测试策略

先修改回归测试并确认其在旧实现上因平台依赖仍存在而失败，再修改生产代码使其通过。

1. 更新 `cmiot_osd_adapter_regression.py`：禁止 `CloudPlatformControl`、`CloudPlatformType`、`GetCloudPlatform` 和 `CLOUD_PLATFORM_*` 出现在 CMIOT OSD 实现中。
2. 更新 `cmiot_osd_priority_regression.py`：断言启动和运行时都只按 `osdSwitch` 选择配置，并确认 `Main.cpp` 使用原有 `s_bStartCmiot` 路径。
3. 删除两项云平台配置/启动回归测试。
4. 重新运行其余 7 项聚焦 OSD 回归。
5. 对 `Main.cpp`、`CmiotOsdControl.cpp`、`AVManager.cpp` 和 `MediaDefaultConfig.cpp` 做宿主机 `g++ -fsyntax-only` 检查。
6. 运行 `git diff --check`。

完整交叉链接仍受 `Lib/Device/ChinaMobile` 下厂商静态库是否齐备约束；若缺失，只报告环境缺口，不宣称整机应用构建通过。

## 风险与控制

- **启动行为误改**：以 `6ad8b24` 的 `s_bStartCmiot` 路径为基线，只移除后来新增的平台选择代码。
- **关闭 CMIOT OSD 后画面残留**：初始化和运行时都复用 `ApplyEffectiveConfigLocked()`，确保 `osdSwitch=0` 明确恢复本地全部槽位。
- **配置误删**：只删除 `CFG_CLOUD_PLATFORM`，保留 `CFG_CMIOT_OSD` 和本地 OSD 配置。
- **工作区污染**：提交时只暂存本设计和后续明确列出的实现文件，不包含现有未提交内容。

## 验收标准

- 全项目生产代码不存在 `CloudPlatformType`、`CFG_CLOUD_PLATFORM`、`CloudPlatformControl`、`GetCloudPlatform` 和 `CLOUD_PLATFORM_*`。
- `Main.cpp` 使用 `s_bStartCmiot` 保持原有 CMIOT 启动方式。
- CMIOT OSD 模块只依据 `osdSwitch` 选择 CMIOT 或本地 OSD。
- CMIOT OSD 独立持久化、坐标边界、回调错误传播和本地 OSD 恢复行为保持有效。
- 聚焦回归、语法检查和空白检查通过，或明确记录外部静态库造成的验证缺口。
