# Changelog

本文件记录项目的重要变更。

## [Unreleased]

### 修复
- 修正 GB28181 实时流在 TCP 协商场景下的 SIP/媒体建立时序。

### 新增
- 增加 issue bot 本机定时巡检脚本与 cron 安装脚本，默认基于 `silver` 分支在隔离仓库中执行 triage / repair。
