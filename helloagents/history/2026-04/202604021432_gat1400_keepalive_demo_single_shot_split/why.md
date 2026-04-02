# 需求说明

## 背景
- issue 41 于 2026-04-02 的最新评论指出，当前 keepalive demo 存在两个问题：图片加载失败日志和平台持续看到重复上报。
- 同时同事明确了新的联调素材路径：人脸 demo 使用 `/mnt/sdcard/test.jpeg` + `/mnt/sdcard/face.jpeg`，机动车 demo 使用 `/mnt/sdcard/test2.jpeg` + `/mnt/sdcard/motor.jpeg`。

## 目标
- 将 keepalive demo 拆成人脸、机动车两个独立函数。
- 两类 demo 都在设备启动后的保活成功路径各上报一次。
- 去掉失败后的自动回退重试，避免平台在后续心跳中持续看到重复事件。

## 影响范围
- `App/Protocol/gat1400/GAT1400ClientService.cpp/.h`
- `helloagents/wiki/modules/gat1400.md`
- `helloagents/CHANGELOG.md`
- `helloagents/history/index.md`
