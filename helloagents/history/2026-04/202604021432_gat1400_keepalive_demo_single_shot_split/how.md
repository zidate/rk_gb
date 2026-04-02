# 实现方案

## 代码调整
- 将原来的组合 demo 拆成 `SendKeepaliveDemoFaceResource()` 和 `SendKeepaliveDemoMotorVehicleResource()`。
- 用两个独立的 `atomic<bool>` 标记“本次服务启动后是否已尝试发送”，避免某一路失败后把整组 demo 在后续心跳里重新发送。
- 人脸 demo 使用两张图：
  - 全景图 `/mnt/sdcard/test.jpeg` -> `Type=14`
  - 人脸图 `/mnt/sdcard/face.jpeg` -> `Type=11`
- 机动车 demo 使用两张图：
  - 全景图 `/mnt/sdcard/test2.jpeg` -> `Type=01`
  - 车辆图 `/mnt/sdcard/motor.jpeg` -> `Type=09`

## 运行约束
- 两类全景图统一按 `1920x1080` 上报。
- 两类 demo 都仍在 `SendKeepaliveNow()` 成功后触发，但每次服务启动只会各尝试一次；若图片缺失或平台返回失败，只打印错误，不在后续心跳重试。
