# 任务清单: 按 issue 41 最新评论调整 1400 保活 demo

目录: `helloagents/history/2026-04/202604011703_gat1400_keepalive_demo_notify_capture/`

---

## 1. 代码调整
- [√] 1.1 在 `rk_gb/App/Protocol/gat1400/GAT1400ClientService.*` 中移除旧 `SendKeepaliveDemoUploadOnce()` 及其“一次性”状态位
- [√] 1.2 新增基于 `NotifyCaptureEvent()` 的保活 demo 事件构造逻辑，内容包含人脸、图片、视频
- [√] 1.3 在保活成功路径里触发 demo，并限制总共发送 5 次

## 2. 知识库同步
- [√] 2.1 更新 `helloagents/wiki/modules/gat1400.md`
- [√] 2.2 更新 `helloagents/CHANGELOG.md`
- [√] 2.3 更新 `helloagents/history/index.md`

## 3. 验证
- [√] 3.1 执行 `git diff --check`
- [√] 3.2 执行最小语法检查，覆盖 `GAT1400ClientService.cpp`

## 执行结果
- 旧的 `SendKeepaliveDemoUploadOnce()` 与 `m_keepalive_demo_upload_attempted` 已删除；保活 demo 现统一收口为 `SendKeepaliveDemoCaptureEvent()`。
- 新 demo 会在保活成功后构造 `media::GAT1400CaptureEvent`，并强制通过 `NotifyCaptureEvent()` 进入 1400 上传主链；事件类型按最新评论要求收口为 `FACE_DETECT_EVENT`，同时关联本地图片 `/mnt/sdcard/test.jpeg` 和本地视频 `/mnt/sdcard/DCIM/2026/03/24/20260324174542-20260324174845.mp4`。
- demo 触发次数现改为“每次服务启动后最多 `5` 次”；单次构造或通知失败时会回退计数，不影响保活主链，也允许后续保活继续重试。
- 已执行 `git diff --check`，并基于 `cmake-build/App/CMakeFiles/dgiot.dir/flags.make` 映射本地 include/define 后，用主机 `g++ -std=gnu++11 -fsyntax-only` 检查 `GAT1400ClientService.cpp`；本次改动未引入新的语法错误。当前仍会看到现存的 `GB_ID_LEN` 宏重复定义 warning，但不是本次 demo 调整引入的问题。
