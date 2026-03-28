# 任务清单: 收口 GB28181 协议散落硬编码

目录: `helloagents/history/2026-03/202603281209_gb_protocol_constants_cleanup/`

---

## 1. 协议硬编码热点收口
- [√] 1.1 新增 `GB28181ProtocolConstants.h`，集中默认设备 ID、注册/保活缺省值、分辨率摘要、音频 payload/mime、`ConfigType/CmdType`、`FrameMirror` 映射
- [√] 1.2 在 `ProtocolManager.cpp` 中移除重复的音频 codec 映射、`FrameMirror` 映射和查询默认设备 ID 回退逻辑
- [√] 1.3 在 `GB28181XmlParser.cpp` 中将 `ConfigType/CmdType` 与 `FrameMirror` 编解码改为复用共享常量/helper
- [√] 1.4 在 `GB28181BroadcastBridge.cpp` 中将广播音频 payload/mime/SDP 协议字符串改为复用共享常量/helper

## 2. 知识库同步
- [√] 2.1 更新 `helloagents/wiki/modules/gb28181.md`
- [√] 2.2 更新 `helloagents/CHANGELOG.md`
- [√] 2.3 更新 `helloagents/history/index.md`
- [√] 2.4 新增本次历史留痕目录与 `why/how/task`

## 3. 验证
- [√] 3.1 执行 `git diff --check`
- [√] 3.2 执行三处目标源文件的 `g++ -fsyntax-only`
- [√] 3.3 记录 `GB28181XmlParser.cpp` 仍存在历史空字符字面量告警，未在本次一并扩改
