# Changelog

本文件记录项目的重要变更。

## [Unreleased]

### 修复
- 修正 GB28181 实时流在 TCP 协商场景下的 SIP/媒体建立时序。
- 修复 GB 注册时间同步仅 dry-run 不落地的问题，并补齐录像文件列表查询响应。
- 修复 LG 存储分支录像时间搜索误用索引导致的查询结果错乱。
- 修复 GB 回放音频发送逻辑，改为按 SDP/`gb.live.audio_codec` 将回放 PCM 编码为 G711A/G711U 后再送入 RTP PS。
- 修复 GB 回放回调缺少 session 身份导致旧存储线程 EOS 可能误清空新回放 session 的竞态，并增加 `stale eos ignored` 诊断日志。
- 修复 MP4 回放 demux 将“读到 packet 但未产出有效 ES 数据”误判为 EOF 的问题，避免录像回放在首包前直接 `eos video=0 audio=0`。

### 新增
- 增加 issue bot 本机定时巡检脚本、Codex 修复器包装脚本与 cron 安装脚本，默认基于 `silver` 分支在隔离仓库中执行 triage / repair。

### 优化
- 优化 issue bot 本机定时任务的可执行文件发现逻辑，自动写入 `CODEX_BIN` / `GH_BIN` 绝对路径，降低 cron 环境缺少 PATH 时的失败率。
- 静默忽略 `SIGPIPE`，并移除 GB live 音频 ES、PS 输出、RTP 发送统计的周期性刷屏日志。
