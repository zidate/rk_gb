# 任务清单: GAT1400 抓拍上传职责下沉到媒体桥接层

目录: `helloagents/history/2026-03/202603281234_gat1400_capture_media_boundary/`

---

## 1. 媒体桥接层
- [√] 1.1 新增 `App/Media/GAT1400CaptureControl.h/.cpp`，定义抓拍事件模型、内存队列和观察者接口
- [√] 1.2 提供 `SubmitFaceCapture()`、`SubmitMotorCapture()` 和通用 `Submit()` 入口，供编码侧 / 算法侧投递抓拍
- [√] 1.3 更新构建脚本，将 `GAT1400CaptureControl.cpp` 纳入主工程和 `sdk_port` 的 `Media_SRC`

## 2. 1400 消费链路
- [√] 2.1 让 `GAT1400ClientService` 继承抓拍观察者，并在构造 / 析构时管理注册
- [√] 2.2 新增 `DrainPendingCaptureEvents()`，在“服务启动后”和“收到新抓拍通知时”主动消费队列
- [√] 2.3 新增 `PostCaptureEvent()`，按“结构化对象 -> 图片 / 视频 / 文件”顺序复用现有 `Post*` 上报链路

## 3. 知识库更新
- [√] 3.1 更新 `helloagents/wiki/modules/gat1400.md`
- [√] 3.2 更新 `helloagents/wiki/api.md`
- [√] 3.3 更新 `helloagents/wiki/data.md`
- [√] 3.4 更新 `helloagents/CHANGELOG.md`
- [√] 3.5 更新 `helloagents/history/index.md`
- [√] 3.6 补充 `GAT1400CaptureControl` 的调用时机、最小字段要求和接入示例

## 4. 验证
- [√] 4.1 执行 `git diff --check`
- [√] 4.2 执行最小语法编译验证
> 备注: 已使用 `g++ -fsyntax-only -std=gnu++11` 分别对 `App/Media/GAT1400CaptureControl.cpp` 与 `App/Protocol/gat1400/GAT1400ClientService.cpp` 做语法级编译验证。

---

## 任务状态符号
- `[ ]` 待执行
- `[√]` 已完成
- `[X]` 执行失败
- `[-]` 已跳过
- `[?]` 待确认
