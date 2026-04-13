# 任务清单

- [√] 1. 新增一个最小复现测试，固定 `PackAlarmNotify()` 当前对旧 `DeviceID` 的失败行为
- [√] 2. 修改 `GB28181XmlParser.cpp`，让报警 XML `<DeviceID>` 优先使用 `m_local_code`
- [√] 3. 运行复现测试和目标文件级编译校验
- [√] 4. 同步 `helloagents` 文档并归档本次方案包

## 执行结果

- 最小复现测试 `/tmp/gb_alarm_notify_deviceid_test.cpp` 已先在旧逻辑上跑出失败：`m_local_code=35010101001320124935` 时，报警 XML 仍写入旧 `C0440326101000000010`。
- `third_party/platform_sdk_port/CommonLibSrc/GB28181SDK/include/XML/GB28181XmlParser.cpp` 已修改为在 `PackAlarmNotify()` 中优先使用 `m_local_code` 写 `notify.DeviceID`，把最终报警体设备号约束下沉到 XML 组包层。
- 修改后重新编译并执行最小复现测试已通过，确认 XML 中已包含 `35010101001320124935`，且不再包含 `C0440326101000000010`。
- 工程侧 `make -C /home/jerry/silver/rk_gb/cmake-build gb28181_client_sdk_port -j2` 无法直接执行，原因是当前 `cmake-build/Makefile` 固定引用了不存在的 `/opt/cmake/cmake-3.16.3-Linux-x86_64/bin/cmake`；本轮以成功的宿主机最小复现编译+运行作为主要验证依据。
