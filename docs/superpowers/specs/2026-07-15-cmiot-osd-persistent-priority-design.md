# cmiot OSD 持久化与平台优先级设计

## 背景

同一套 RV1106 固件需要通过运行时持久化配置选择国标（GB28181）或 cmiot 平台，两者不会同时运行。cmiot SDK 下发的 OSD 与设备本地 OSD 是两份独立配置：当前平台为 cmiot 且 `osdSwitch=1` 时，cmiot OSD 覆盖本地 OSD；`osdSwitch=0` 时恢复最新的本地 OSD。设备重启后必须保持这一状态。

最新远端提交 `6ad8b24` 已合入 cmiot SDK，并把 `CmiotOsdControl.*` 移到 `App/ChinaMobile/`。现有适配仍存在以下问题：

- `CmiotOsdControl` 通过面向国标适配的 `media::VideoOsdState` 解析和应用 cmiot OSD。
- cmiot 的 `0-10000` 坐标在适配层被转换为设备像素，破坏了平台配置原值。
- cmiot OSD 被写入本地 `CFG_OSD_TIME/CFG_OSD_TEXT`，无法保存两份独立配置。
- `CMIOT_CMD_SET_OSD` 只打印配置，没有应用或返回实际错误。
- `CmiotOsdControl.cpp` 迁移后没有加入 `CM_SRC`，调用接口会产生链接错误。
- `Main.cpp` 用 `s_bStartCmiot=true` 硬编码平台，并在 cmiot 分支后无条件死循环，使国标启动链路不可达。

## 目标

1. 用一项持久化运行时配置选择 `GB28181` 或 `CMIOT`，进程启动时只启动所选平台。
2. 独立持久化本地 OSD 与 cmiot OSD，重启后恢复正确的生效来源。
3. cmiot OSD 直接解析 `cmiotOSDInfo_t`，不经过 `VideoOsdState`。
4. cmiot 的 `x/y` 在解析和持久化阶段保持 `0-10000` 原值，只在最终调用 RK OSD 时换算为像素。
5. 打通 `demo_dev_config_callback()` 的 `CMIOT_CMD_SET_OSD` 设置闭环，并保留明确的失败返回。
6. 保持现有 GB28181 OSD 行为与外部协议兼容。

## 非目标

- 不支持进程运行期间热切换平台；修改平台配置后重启生效。
- 不同时启动 cmiot 与 GB28181 SDK。
- 不重构 RK RGN、字体渲染或 GB28181 协议解析。
- 不扩大 cmiot 文本容量；仍使用文本区域 1-5，保留区域 6/7。
- cmiot SDK 中 `mode=2` 的“国标模式”仅表示水印排版，不作为云平台选择依据。

## 平台选择

新增全局平台配置 `CFG_CLOUD_PLATFORM`，配置值使用显式枚举：

- `CLOUD_PLATFORM_GB28181 = 0`
- `CLOUD_PLATFORM_CMIOT = 1`

默认值为 `GB28181`，保持存量设备行为。cmiot 产品在出厂配置中明确设置为 `CMIOT`。配置在应用启动时读取一次；非法或缺失值回退到 `GB28181` 并记录错误日志。

正常启动流程按平台分支：

- `GB28181`：不启动 cmiot SDK，继续现有网络、DM、ProtocolManager、GB28181/GAT1400 启动链路。
- `CMIOT`：不启动 ProtocolManager 的 GB28181 链路，启动 cmiot SDK 及其媒体推流链路。

删除 `s_bStartCmiot` 硬编码和阻断国标路径的无条件死循环。平台切换需要重启，避免在本次改动中引入 SDK 注销、线程停止、推流重绑和资源回收风险。

## OSD 数据模型与持久化

保留现有本地配置：

- `CFG_OSD_TIME`
- `CFG_OSD_TEXT`

新增独立的 `CFG_CMIOT_OSD`。该配置使用可序列化、无裸指针的固定容量结构保存：

- `valid` 与 `osd_switch`
- `mode`
- 日期状态、格式、12/24 小时制、星期开关
- 自定义日期位置和国标排版边距
- 字号与颜色原值
- 最多 5 条自定义文本及各自位置
- 国标排版下最多合计 5 条辖区/附加文本及其边距

`cmiotOSDInfo_t` 中的文本指针只在回调期间有效。保存时必须校验计数与指针，再深拷贝到固定数组；持久化数据不得保存 SDK 指针。查询接口从持久化结构重建输出，继续遵循调用方提供缓冲区和容量的约定。

## 生效优先级

| 当前平台 | cmiot `osdSwitch` | 生效配置 |
|---|---:|---|
| `GB28181` | 任意 | 本地/国标 `CFG_OSD_TIME` 与 `CFG_OSD_TEXT` |
| `CMIOT` | `0` | 本地 `CFG_OSD_TIME` 与 `CFG_OSD_TEXT` |
| `CMIOT` | `1` | `CFG_CMIOT_OSD` |

历史 cmiot 配置可以在切换到国标后继续保存，但国标平台不得读取它作为生效配置。切回 cmiot 并重启时，如果持久化的 `osdSwitch=1`，恢复 cmiot OSD；否则应用最新本地 OSD。

cmiot 覆盖生效期间，本地 OSD 配置回调仍更新和持久化本地配置，但不得刷新硬件 OSD。这样关闭 cmiot OSD 后可以恢复用户最近一次本地设置，而不是进入 cmiot 前的旧快照。

## 模块边界与数据流

### cmiot 设置

1. `demo_dev_config_callback(CMIOT_CMD_SET_OSD)` 检查 `input`，调用 `cmiot_osd_set_config()`。
2. `CmiotOsdControl` 校验 `osdSwitch`、模式、文本计数/指针、坐标、格式和颜色，直接解析 `cmiotOSDInfo_t`。
3. 将完整 cmiot 配置深拷贝并写入 `CFG_CMIOT_OSD`。
4. 若当前平台不是 `CMIOT`，只保存配置，不改变画面。
5. 若当前平台为 `CMIOT`：
   - `osdSwitch=1`：应用 cmiot 配置；
   - `osdSwitch=0`：重新读取并应用本地 `CFG_OSD_TIME/TEXT`。
6. 回调返回实际结果；失败时输出命令、阶段和返回码。

### 启动恢复

1. ConfigManager 加载平台、本地 OSD 和 cmiot OSD 持久化配置。
2. 媒体 OSD/RGN 初始化完成后，根据平台和上述优先级选择唯一生效配置。
3. cmiot 平台在 SDK 下发新配置前即可恢复上次 cmiot OSD；国标平台始终忽略 cmiot OSD。

### 本地或国标设置

1. 现有 `VideoOsdControl` 和 `CFG_OSD_TIME/TEXT` 路径保持不变。
2. 配置始终落盘。
3. 仅当当前生效来源不是 cmiot 覆盖时，`AVManager` 才把本地配置刷新到硬件。

## 坐标与 RK OSD 边界

外部国标接口和 cmiot SDK 都使用 `0-10000` 坐标，但它们的协议解析相互独立。`CmiotOsdControl` 只校验并原样保存 cmiot 坐标，不调用 `ScaleCoordinate*ToDevice()`，也不构造 `VideoOsdState`；超出范围的坐标直接拒绝，不通过钳制改变平台原值。

最终调用 `gb_rkipc_osd_time_set()`、`gb_rkipc_osd_text_set()` 前，根据主码流分辨率将归一化坐标换算为像素。国标现有换算继续由 `VideoOsdControl` 负责；cmiot 换算放在 cmiot 到 RK OSD 的最终应用函数中。对齐边距到坐标的计算同样只属于最终排版阶段，持久化配置保留 SDK 原值。

## 并发与失败处理

- 使用一把 cmiot OSD 状态锁保护持久化内存副本、覆盖状态和硬件应用顺序。
- 设置新配置时先完成校验和深拷贝，再持久化，最后切换生效画面；非法输入不得破坏上一份有效配置。
- 持久化失败时返回失败，并保留上一份有效配置和画面。
- 硬件应用失败时返回失败并记录阶段；持久化配置保留，便于重启或后续重试，但不得把失败伪装成成功。
- 文本总数超过 5 时拒绝输入，不静默截断；空文本按隐藏处理。
- `osdSwitch=0` 只持久化关闭状态并恢复本地配置，其他字段不参与有效性判断；已有 cmiot OSD 内容可以保留，但在再次收到有效的开启配置前不生效。

## 预计改动范围

- `App/Main.cpp`：读取平台配置并选择唯一启动分支。
- `App/CMakeLists.txt`：编译 `CmiotOsdControl.cpp`，保持 cmiot SDK 链接完整。
- `App/ChinaMobile/demo_callback.cpp`：实现 `CMIOT_CMD_SET_OSD` 调用与错误返回。
- `App/ChinaMobile/CmiotOsdControl.cpp/.h`：直接解析、持久化、查询、启动恢复和 cmiot OSD 应用。
- `App/Media/AVManager.cpp/.h`：本地配置保存与硬件刷新解耦，cmiot 覆盖时抑制本地刷新。
- `App/Media/MediaDefaultConfig.cpp/.h`：增加平台与 cmiot OSD 默认配置。
- `Include/ExchangeAL/CommExchange.h`、`ExchangeKind.h` 及对应交换实现：增加平台/cmiot OSD 可序列化配置类型和配置键。
- `tools/tests/`：更新 cmiot 静态回归测试并增加平台/优先级覆盖。
- `helloagents/wiki/modules/rk_media_pipeline.md`：记录平台选择与 OSD 生效规则。

实际实施时只修改完成上述行为所需的文件；若现有 ConfigManager 能直接安全持久化 JSON，则避免引入多余的交换结构。

## 验证方案

### 静态与单元回归

- `CmiotOsdControl.cpp` 不包含或调用 `VideoOsdState`、`ApplyVideoOsdConfig`、`QueryVideoOsdState`。
- cmiot 解析/保存路径不包含坐标到设备像素的换算。
- `CMIOT_CMD_SET_OSD` 调用 `cmiot_osd_set_config()` 并传播错误。
- CMake 源列表包含 `ChinaMobile/CmiotOsdControl.cpp`。
- 平台枚举只允许 GB28181/CMIOT，缺省和非法值回退 GB28181。

### 构建

- 使用项目既有 RV1106 交叉编译命令编译应用。
- 检查 cmiot SDK、CmiotOsdControl 和 GB28181 相关符号无编译/链接回归。

### 设备验证

1. GB28181 平台启动：只启动国标链路，历史 cmiot OSD 不覆盖画面。
2. CMIOT 平台、`osdSwitch=1`：下发自定义/国标排版 OSD，坐标、字体、颜色和文本生效。
3. cmiot 覆盖期间修改本地 OSD：本地配置落盘但画面保持 cmiot OSD。
4. 下发 `osdSwitch=0`：立即恢复最新本地 OSD。
5. 在 cmiot 覆盖开启和关闭两种状态下重启，验证恢复来源正确。
6. 在 GB28181 与 CMIOT 之间修改平台配置并重启，验证只启动目标平台且 OSD 来源符合优先级表。
7. 输入空指针、非法模式、越界坐标、超量文本和无效颜色，验证返回失败且旧画面/配置不被破坏。

## 成功标准

- 单固件可通过持久化配置在重启后选择且只运行一个平台。
- 国标平台不受 cmiot 历史配置影响。
- cmiot OSD 与本地 OSD 独立持久化，覆盖、关闭恢复和重启恢复行为符合优先级表。
- cmiot 协议解析不依赖 `VideoOsdState`，`x/y` 原值保持到最终 RK OSD 应用边界。
- SDK 回调、构建链接、静态回归和可执行的设备验证步骤均闭环。
