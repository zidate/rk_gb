# 任务清单: 新增 ProtocolManager GAT 业务通知入口

目录: `helloagents/history/2026-04/202604011408_gat_notify_alarm_entry/`

---

## 1. 代码调整
- [√] 1.1 在 `rk_gb/App/Protocol/ProtocolManager.*` 中新增 `NotifyGatAlarm()` 入口，并转发到 1400 服务
- [√] 1.2 在 `rk_gb/App/Protocol/gat1400/GAT1400ClientService.*` 中新增 `NotifyCaptureEvent()`，实现“已注册直接尝试上传，失败或未就绪则入队”
- [√] 1.3 在 `rk_gb/App/Media/GAT1400CaptureControl.h` 中补充调用约束，明确业务侧优先走 `ProtocolManager::NotifyGatAlarm()`

## 2. 知识库同步
- [√] 2.1 更新 `helloagents/wiki/modules/gat1400.md`
- [√] 2.2 更新 `helloagents/wiki/api.md`
- [√] 2.3 更新 `helloagents/wiki/data.md`
- [√] 2.4 更新 `helloagents/CHANGELOG.md`
- [√] 2.5 更新 `helloagents/history/index.md`

## 3. 验证
- [√] 3.1 执行 `git diff --check`
- [√] 3.2 执行最小语法检查，覆盖 `ProtocolManager.cpp` 与 `GAT1400ClientService.cpp`

## 执行结果
- 已新增 `ProtocolManager::NotifyGatAlarm(const media::GAT1400CaptureEvent* event)`，供算法/编码侧以单一接口把抓拍结果交给 1400 模块。
- `GAT1400ClientService` 已新增 `NotifyCaptureEvent()`：当前若 1400 已启动且已注册，会直接尝试上传；若未就绪或直接上传失败，会回退到 `GAT1400CaptureControl` 内存队列，等待后续注册/保活成功后处理。
- `GAT1400CaptureControl` 继续保留为内部排队组件，但调用约束已调整为“业务侧优先走 `ProtocolManager::NotifyGatAlarm()`”。
- 已执行 `git diff --check`，并基于 `cmake-build/App/CMakeFiles/dgiot.dir/flags.make` 映射本地 include/define 后，用主机 `g++ -std=gnu++11 -fsyntax-only` 分别检查 `ProtocolManager.cpp` 与 `GAT1400ClientService.cpp`，本次改动范围内未发现语法错误。
