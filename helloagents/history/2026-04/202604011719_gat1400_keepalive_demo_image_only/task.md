# 任务清单: 修正 keepalive demo 仅发送图片

目录: `helloagents/history/2026-04/202604011719_gat1400_keepalive_demo_image_only/`

---

## 1. 代码调整
- [√] 1.1 在 `rk_gb/App/Protocol/gat1400/GAT1400ClientService.cpp` 中删除 keepalive demo 的视频读取与视频事件构造
- [√] 1.2 保持 keepalive demo 继续通过 `NotifyCaptureEvent()` 发送人脸 + 图片事件，并保留 5 次上限

## 2. 知识库同步
- [√] 2.1 更新 `helloagents/wiki/modules/gat1400.md`
- [√] 2.2 更新 `helloagents/CHANGELOG.md`
- [√] 2.3 更新 `helloagents/history/index.md`

## 3. 验证
- [√] 3.1 执行 `git diff --check`
- [√] 3.2 执行最小语法检查，覆盖 `GAT1400ClientService.cpp`

## 执行结果
- keepalive demo 已从“人脸 + 图片 + 视频”收口为“人脸 + 图片”；`SendKeepaliveDemoCaptureEvent()` 中不再读取本地视频文件，也不再构造 `GAT_1400_VideoSliceSet` 或向 `event.video_slice_list` 填充内容。
- demo 仍保留 `NotifyCaptureEvent()` 调用方式、`FACE_DETECT_EVENT` 事件类型和“每次服务启动最多触发 `5` 次”的约束。
- 当前知识库中的运行态说明已同步改为“只发图片”。
- 已执行 `git diff --check`，并基于 `cmake-build/App/CMakeFiles/dgiot.dir/flags.make` 映射本地 include/define 后，用主机 `g++ -std=gnu++11 -fsyntax-only` 检查 `GAT1400ClientService.cpp`；本次改动未引入新的语法错误。当前仍会看到现存的 `GB_ID_LEN` 宏重复定义 warning，但不是本次修正引入的问题。
