# 任务清单: 去掉 GAT1400 抓拍 observer 继承

目录: `helloagents/history/2026-04/202604011157_gat1400_capture_observer_removal/`

---

## 1. 代码调整
- [√] 1.1 在 `rk_gb/App/Protocol/gat1400/GAT1400ClientService.*` 中去掉 `media::GAT1400CaptureObserver` 继承、回调声明和注册逻辑
- [√] 1.2 在 `rk_gb/App/Media/GAT1400CaptureControl.*` 中删除只被 1400 服务使用的 observer 机制
- [√] 1.3 在 `rk_gb/App/Protocol/gat1400/GAT1400ClientService.cpp` 中把抓拍队列消费时机收口到注册成功和保活成功路径

## 2. 知识库同步
- [√] 2.1 更新 `helloagents/wiki/modules/gat1400.md`
- [√] 2.2 更新 `helloagents/wiki/api.md`
- [√] 2.3 更新 `helloagents/wiki/data.md`
- [√] 2.4 更新 `helloagents/CHANGELOG.md`
- [√] 2.5 更新 `helloagents/history/index.md`

## 3. 验证
- [√] 3.1 执行 `git diff --check`
- [√] 3.2 执行最小语法检查，覆盖 `GAT1400ClientService.cpp` 与 `GAT1400CaptureControl.cpp`

## 执行结果
- `GAT1400ClientService` 已去掉对 `media::GAT1400CaptureObserver` 的继承，构造/析构中的 observer 注册逻辑和 `OnGAT1400CaptureQueued()` 回调也已删除。
- `GAT1400CaptureControl` 已收敛为纯内存抓拍队列，只保留 `Submit/PopPending/PendingCount`，不再维护 observer 列表和入队通知。
- 抓拍事件现改为在 `RegisterNow()` 和 `SendKeepaliveNow()` 成功后由 `GAT1400ClientService` 显式执行 `DrainPendingCaptureEvents()`；现有 `/Data` Base64 上传链路未改动。
- 已执行 `git diff --check`，并基于 `cmake-build/App/CMakeFiles/dgiot.dir/flags.make` 映射本地 include/define 后，用主机 `g++ -std=gnu++11 -fsyntax-only` 分别检查 `GAT1400ClientService.cpp` 与 `GAT1400CaptureControl.cpp`，本次改动范围内未发现语法错误。
