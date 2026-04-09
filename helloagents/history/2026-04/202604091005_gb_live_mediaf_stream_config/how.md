# 技术设计

## 实现范围
- 文件: `App/Protocol/ProtocolManager.cpp`
- 文档: `helloagents/wiki/modules/gb28181.md`

## 方案
1. 在 `ProtocolManager.cpp` 增加轻量 `f=` 解析 helper，只解析视频段中的编码格式、分辨率、帧率、码率类型、码率。
2. live 点播仍只通过 `ResolveGbStreamNumber()` 确定目标码流，不新增另一套选流规则。
3. 基于 `media::QueryVideoEncodeStreamState()` 读取目标码流运行态，若 `f=` 里的参数和当前运行态一致则跳过，否则调用现有 `media::ApplyVideoEncodeStreamConfig()` 接口下发。
4. 当前不扩展 `App/Media/VideoEncodeControl.*` 的具体动态重配实现，只把协议层入口、比较和日志先接通。
5. 在 live 请求日志与 sender 初始化日志中补充 `f=` 和目标流号，便于联调判断是否已经命中指定码流。

## 风险与规避
- 风险: 当前编码参数接口未必覆盖 `f=` 的所有字段或都能动态生效。
  - 规避: 本轮只承诺协议层入口和日志打通，不承诺新增底层编码器实现。
- 风险: 平台 `f=` 只给部分字段，可能只触发部分参数比较。
  - 规避: 继续沿用现有“有值才比较/下发”的增量配置语义。

## ADR
- ADR-20260409-mediaf-live-stream-config
  - 决策: 本轮不做“按 `f=` 匹配主/子码流”，改为“按既有码流选择结果，把 `f=` 中的视频参数通过现有编码配置接口下发到该路码流”。
  - 原因: 更符合当前 demo/嵌入式程序的复杂度约束，也更贴近平台已经显式指定码流的联调语义。
