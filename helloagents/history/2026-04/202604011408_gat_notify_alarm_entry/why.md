# 变更提案: 新增 ProtocolManager GAT 业务通知入口

## 需求背景
当前 1400 抓拍业务接线已经具备内部桥接能力，但调用面仍偏底层：业务侧需要直接接触 `media::GAT1400CaptureControl` 或拆开调用 `PostFaces/PostImages/...`。

同事希望这条链路和 `ProtocolManager::NotifyGbAlarm()` 一样，提供一个明确的协议层业务入口。算法或编码侧在产出抓拍结果后，只需要调用一个 `ProtocolManager` 方法，把结果交给 1400 模块处理。

## 变更目标
1. 新增 `ProtocolManager::NotifyGatAlarm()` 作为 1400 业务通知入口。
2. 调用方仍复用现有 `GAT1400CaptureEvent` 数据模型，不改 `/Data` Base64 上传主链路。
3. 1400 服务在“已启动且已注册”时优先直接尝试上传；未就绪或直接上传失败时，回退到现有内存队列，留待注册成功/保活成功后继续处理。

## 核心场景

### 需求: 算法/编码通过单一接口通知 1400
**模块:** `ProtocolManager`、`GAT1400ClientService`

#### 场景: 拍照结果产出后通知 1400 模块
算法或编码侧得到一条抓拍结果，包含人脸或机动车及关联图片/视频/文件。
- 调用方组装 `media::GAT1400CaptureEvent`
- 通过 `ProtocolManager::NotifyGatAlarm()` 交给协议模块
- 若 1400 当前已注册，服务立即尝试上传
- 若服务未就绪或即时上传失败，事件进入内存队列，等待后续注册成功/保活成功后处理

## 影响范围
- `rk_gb/App/Protocol/ProtocolManager.*`
- `rk_gb/App/Protocol/gat1400/GAT1400ClientService.*`
- `rk_gb/App/Media/GAT1400CaptureControl.h`
- `helloagents/wiki/modules/gat1400.md`
- `helloagents/wiki/api.md`
- `helloagents/wiki/data.md`
