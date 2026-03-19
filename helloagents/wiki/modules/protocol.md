# Protocol 模块

## 职责
- 管理 GB28181 与 GAT1400 生命周期。
- 处理 SIP/SDP 请求到本地媒体发送链路的映射。
- 统一输出协议日志与错误信息。

## 当前重点
- GB 注册与鉴权稳定性。
- GB 注册成功后的时间同步与设备本地时间持久化。
- GB 实时流在 UDP/TCP 模式下的媒体协商兼容性。
- GB `RecordInfo` 查询与本地录像检索结果映射。
- 1400 注册、保活与基础能力补齐。

## 注意事项
- TCP 实时流要区分 `setup:active` 与 `setup:passive` 方向。
- SIP `INVITE` 应答不要被同步媒体建链阻塞。
- GB 回放音频不能按原始 PCM 直接塞给 RTP PS；应与实时流保持一致，按 SDP/`gb.live.audio_codec` 输出 `G711A` 或 `G711U`。
- GB 回放/下载回调需要带 session 身份；否则旧存储线程收尾时的 EOS 可能误伤新回放 session，表现为 ACK 已到但媒体立即 `eos video=0 audio=0`。
- GB 回放依赖 MP4 demux 连续读到有效 ES；对“packet 已读到但当前未产出有效音视频载荷”的场景不能直接当 EOF，否则会在首包前提前结束回放。
- 当 GB 回放仍在 `play-start` 后立即 `eos` 时，要同时观察存储层日志：先确认索引是否装载成功、是否选中目标录像文件，再区分 open/read 失败还是协议发送失败。
