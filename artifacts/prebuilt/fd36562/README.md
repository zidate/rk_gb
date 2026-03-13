预编译 `dgiot` 测试包

- 源码提交: `fd36562fbdf5e439dc8e8420ad072772394eecb2`
- 来源分支: `dev`
- 目标板型: `RC0240_LGV10`
- BLE 选项: `AIC8800DL`
- 交叉编译器: `arm-rockchip830-linux-uclibcgnueabihf-gcc/g++ 8.3.0`
- CMake: `4.2.3`，配置时附加 `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`

文件说明:

- `dgiot`: 预编译应用程序，ARM 32-bit，未 strip

校验:

- `sha256`: `8fb9e39b16936e9297c264753763ecfccf9fe839946790b9f76841e3b23392bd`

构建说明:

1. 先单独构建 `Middleware/libmpp`
2. 再构建根工程并重新链接 `dgiot`
3. 二进制输出自仓库内 `Bin/dgiot`

用途:

- 供测试同事直接在设备上替换应用程序验证
- 用于区分“本地编译环境问题”与“当前 Git 代码问题”
