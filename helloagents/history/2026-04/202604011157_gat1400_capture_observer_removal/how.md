# 技术设计: 去掉 GAT1400 抓拍 observer 继承

## 技术方案
- `GAT1400ClientService` 改为普通类，不再继承 `media::GAT1400CaptureObserver`。
- 删除 `OnGAT1400CaptureQueued()`、构造/析构中的 `AddObserver/RemoveObserver()`。
- `GAT1400CaptureControl` 只保留线程安全队列能力，移除 observer 类、observer 列表和入队通知。
- 抓拍队列的新消费时机收口到现有成功路径：
  - `RegisterNow()` 成功后显式 `DrainPendingCaptureEvents()`
  - `SendKeepaliveNow()` 成功后显式 `DrainPendingCaptureEvents()`

## 实现要点
- 不改 `PostCaptureEvent()` 内部“先结构化对象，再图片/视频/文件”的上传顺序。
- 不改现有 `/Data` Base64 上传逻辑。
- 启动路径不再额外手动 drain，避免初次注册成功时重复消费同一批内存队列。

## 风险与缓解
- **风险:** 去掉 observer 后，抓拍事件不会在入队瞬间立刻触发上传。
- **缓解:** 依赖注册成功和保活成功两个稳定时机显式 drain，避免事件无人消费。
