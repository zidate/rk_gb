# 技术设计: OTA 直接镜像解析与移动回调升级

## 设备端解析
- 先校验平台、magic、总长、CRC、类型唯一性、地址和分区上限。
- 每个镜像先写入输出目录内的 `mkstemp` 文件，完成后 `fsync`。
- 三个临时文件全部成功后才原子改名为 `boot.img/rootfs.img/oem.img`；提交失败时清理整套文件。
- `AbUpdateApply` 串行化事务，执行 `rk_ota --misc=update --save_dir=/tmp --partition=all`，等待完成后清理三镜像。

## rk_ota
- `--tar_path` 存在时保持原有解包兼容路径。
- 仅提供 `--save_dir` 时跳过 tar 解包，直接调用现有 A/B `flash_write` 事务。
- 两个参数均未提供时拒绝升级。

## 中国移动回调
- 有界校验并复制 URL、MD5、版本信息。
- 互斥拒绝并发升级，创建后台线程执行下载、MD5 校验和安装。
- 下载直接使用已静态链接的 libcurl 7.88.1 easy API，仅允许 HTTP/HTTPS 及同协议重定向，不依赖固件中的 `curl` 可执行文件；临时文件校验通过后原子改名。
- 依次上报下载和安装状态；失败路径上报对应失败状态并清理文件。

## 验证
- Host 单测验证镜像内容、损坏包拒绝、无半套文件、wrapper 参数与清理。
- 静态/host 测试验证 `rk_ota` 目录模式和移动回调状态流。
- 执行全量 `tools/tests`、严格编译、交叉构建及 `git diff --check`。
- libcurl 修正后再次完成 84 项回归、ARM GNU 8.3.0 严格语法检查，并用现有 libcurl/mbedTLS/libavutil 静态库生成 ARM/uClibc ELF，确认下载相关符号全部解析。
