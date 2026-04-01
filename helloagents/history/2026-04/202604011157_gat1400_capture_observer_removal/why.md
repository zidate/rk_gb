# 变更提案: 去掉 GAT1400 抓拍 observer 继承

## 需求背景
当前 `GAT1400ClientService` 通过继承 `media::GAT1400CaptureObserver` 并注册到 `GAT1400CaptureControl` 的方式，在抓拍事件入队时立即触发 `DrainPendingCaptureEvents()`。

这套写法有两个问题：
- 用户明确要求把 `class GAT1400ClientService : public media::GAT1400CaptureObserver` 去掉。
- 对当前嵌入式测试场景而言，`observer + 回调 + 注册/反注册` 这一层偏重，实际只需要保留“抓拍事件先进队列，1400 服务在合适时机显式消费”。

## 变更目标
1. 去掉 `GAT1400ClientService` 对 `media::GAT1400CaptureObserver` 的继承。
2. 删除 `GAT1400CaptureControl` 内部只被这一处使用的 observer 机制。
3. 保持现有抓拍桥接和 `/Data` Base64 上传主链路不变，只调整抓拍队列的触发时机。

## 核心场景

### 需求: 抓拍桥接最小化
**模块:** `GAT1400ClientService`、`GAT1400CaptureControl`

#### 场景: 抓拍事件入队后仍能被上传
编码侧 / 算法侧调用 `SubmitFaceCapture()` 或 `SubmitMotorCapture()`。
- 事件先进入进程内队列
- 1400 服务不再依赖 observer 回调
- 在注册成功或保活成功后显式执行 `DrainPendingCaptureEvents()`
- 继续按既有 `PostFaces/PostMotorVehicles/PostImages/PostVideoSlices/PostFiles` 链路上传

## 影响范围
- `rk_gb/App/Protocol/gat1400/GAT1400ClientService.*`
- `rk_gb/App/Media/GAT1400CaptureControl.*`
- `helloagents/wiki/modules/gat1400.md`
- `helloagents/wiki/api.md`
- `helloagents/wiki/data.md`
