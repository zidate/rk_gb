# 技术设计: GAT1400 抓拍上传职责下沉到媒体桥接层

## 技术方案
### 核心技术
- 新增 `App/Media/GAT1400CaptureControl.*`
- 使用“内存队列 + 观察者通知”作为抓拍桥接模型
- `GAT1400ClientService` 注册为观察者并主动消费队列

### 实现要点
- `GAT1400CaptureControl`
  - 定义 `GAT1400CaptureEvent`
  - 提供 `SubmitFaceCapture()`、`SubmitMotorCapture()` 和通用 `Submit()`
  - 提供 `PopPending()`、`PendingCount()` 给消费侧主动拉取
  - 队列默认上限 `128`，超限丢弃最旧事件
- `GAT1400ClientService`
  - 继承 `media::GAT1400CaptureObserver`
  - 构造时注册观察者，析构时移除
  - 在 `OnGAT1400CaptureQueued()` 和 `Start()` 中调用 `DrainPendingCaptureEvents()`
  - `PostCaptureEvent()` 内按“对象 -> 图像 -> 视频 -> 文件”顺序复用现有上传接口
  - 当 `Face.SourceID/Motor.SourceID` 为空时，优先从关联 `ImageID/VideoID/FileID` 回填最小来源关系

## 架构决策 ADR
### ADR-20260328-01: 抓拍来源与 1400 协议上传通过媒体桥接层解耦
**上下文:** 1400 已有上传能力，但缺少独立的抓拍事件入口。
**决策:** 在 `App/Media` 新增 `GAT1400CaptureControl`，由编码侧 / 算法侧提交抓拍事件，1400 服务仅负责消费和上传。
**理由:** 让采集能力归设备/算法侧，协议模块只保留 1400 字段映射和传输职责。

### ADR-20260328-02: 抓拍桥接层先使用内存队列，不做 flash 持久化
**上下文:** 当前诉求是先把能力边界抽出来，并未要求补完整的断电恢复。
**决策:** 抓拍事件先只保存在进程内内存队列，由 `GAT1400ClientService` 启动后 drain。
**理由:** 最小实现即可满足“模块可能晚于参数/能力初始化”的解耦目标，同时避免把媒体事件和 `gat_upload` 失败补传文件队列混成一套语义。
**影响:** 进程重启后，尚未被 1400 消费的抓拍事件不会恢复；后续如果平台要求断电续传，可再扩独立持久化模型。

## 测试与部署
- 使用 `git diff --check` 检查补丁格式
- 使用 `g++ -fsyntax-only -std=gnu++11` 校验：
  - `App/Media/GAT1400CaptureControl.cpp`
  - `App/Protocol/gat1400/GAT1400ClientService.cpp`
