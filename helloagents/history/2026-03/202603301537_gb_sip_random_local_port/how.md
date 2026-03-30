# 技术设计: GB28181 SIP 本地监听端口随机化

## 技术方案

### 核心技术
- C++ 现有协议栈最小改动
- `ProtocolManager -> GB28181ClientSDK -> SipSDK -> eXosip` 监听/注册链路梳理
- 临时 socket `bind(0)` 预取随机端口 + 运行态端口回写

### 实现要点
- `ProtocolManager` 启动 GB28181 SDK 时，把本地 SIP 绑定端口改为 `0`，不再复用 `gb_register.server_port`。
- `SipEventManager::StartSipListen()` 在请求随机端口时，先用临时 socket `bind(0)` 取回一个可用本地端口，再沿用现有 `eXosip_listen_addr(... actualLocalPort ...)` 建立正式监听，并把实际端口写入 `m_local_port`，避免后续 `ClientInfo` 仍保存 `0`。
- 为保持现有 eXosip 事件处理和线程模型不变，不采用 `eXosip_set_socket()` 接管预绑定 socket，而是只在“本地端口为 0”时切到“预取随机端口 -> 正常 `eXosip_listen_addr()` 监听”分支。
- `CSipClientImpl::Start/Register` 在进入构包和日志前，使用 `SipEventManager` 已缓存的实际本地端口刷新 `m_client_info->LocalPort`。
- `CSipEventManager::Register()` 再次兜底同步 `client_info->LocalPort = m_local_port`，确保 `SetFromeAndTo()` 的 `from/contact` 与真实监听端口一致。

## 架构设计
- 无新增模块、无第三方依赖变化。
- 本次仅修正 GB28181 SIP 客户端运行时“本地监听端口”和“远端服务器端口”被错误耦合的问题。

## 安全与性能
- **安全:** 不涉及密码、鉴权策略、远端地址选择、媒体链路或设备控制逻辑；只调整本地 SIP socket 绑定行为。
- **性能:** 仅在 SIP 监听启动时增加一次本地 socket 绑定和端口查询，不影响运行期主要性能。

## 测试与部署
- **测试:**  
  1. 全仓搜索确认 `gb_register.server_port` 仍只作为远端平台端口使用，不再参与本地 SIP 监听绑定。  
  2. 运行最小静态检查，确认修改后无格式错误、无明显编译语法问题。  
  3. 检查日志与构包路径，确认 `SipClientImpl/Register` 使用的是运行态实际本地端口。  
- **部署:** 无额外部署动作；按现有 `dgiot` 构建与联调流程验证即可。

## 验证结果
- `git diff --check` 通过，未引入格式错误或尾随空白。
- 使用 `cmake-build/App/CMakeFiles/*/flags.make` 中的既有交叉编译参数，修正旧源码树绝对路径与错误的 `-o3` 后，对以下文件执行 `arm-rockchip830-linux-uclibcgnueabihf-g++ -fsyntax-only`，均通过：
  - `rk_gb/App/Protocol/ProtocolManager.cpp`
  - `rk_gb/third_party/platform_sdk_port/CommonLibSrc/SipSDK/common/SipEventManager.cpp`
  - `rk_gb/third_party/platform_sdk_port/CommonLibSrc/SipSDK/client/SipClientImpl.cpp`
- 现有 `rk_gb/cmake-build` 的 `CMakeCache.txt` 和 `Makefile` 仍绑定旧机器路径 `/home/lhy/...` 与失效的 `/opt/cmake/cmake-3.16.3-Linux-x86_64/bin/cmake`，因此未直接复用该目录完成整仓 `dgiot` 重编。
