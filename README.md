# <center> AutoSZUWeb </center>

<p align="center">
  <img src="https://img.shields.io/badge/Platform-Windows%20%7C%20macOS-blue" alt="Platform">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-%23f34b7d?logo=c%2B%2B" alt="Language">
  <img src="https://img.shields.io/badge/License-MIT-green" alt="License">
  <img src="https://img.shields.io/badge/SZU-Campus%20Network-orange" alt="SZU">
</p>

## 简介

AutoSZUWeb 是深圳大学校园网后台自动认证工具。配置账号后，程序会常驻后台，每 10 秒检查一次外网连通性；断网后自动尝试教学/办公区 SRun 认证，并在失败时回退到宿舍区 ePortal 认证。

## 平台支持

| 平台 | 状态 | 凭据保护 | 自启动 |
|---|---|---|---|
| Windows | 支持 | Windows DPAPI | 自复制到 `%APPDATA%` 并写入当前用户 Run 注册表 |
| macOS 11+ | 支持源码构建 | 用户登录钥匙串保存随机密钥 + AES-256-GCM | 用户级 LaunchAgent |

macOS 开发构建会生成后台 `.app`（`LSUIElement=true`，无 Dock 图标）。公开分发前仍需完成 Homebrew 动态依赖封装、Developer ID 签名和 Apple 公证，详见 [测试与发布方案](docs/testing.md)。

## 功能

- 后台常驻、断网自动重连；
- 教学/办公区 SRun + 宿舍区 ePortal 双认证；
- Windows 首次认证结果提示；macOS 启动认证结果仅写日志，后台静默运行；
- 本地加密保存凭据；
- 用户登录后自动启动；
- 单实例保护，避免 Finder 与自启动进程重复弹窗；
- 按日期保存认证日志；
- Windows/macOS 自动化构建与跨平台核心测试。

## Windows 使用

### 构建

```powershell
cmake -S . -B build/windows -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows --parallel
```

也可以运行 `build.bat`。

### 首次配置

1. 双击 `AutoSZUWeb.exe`；
2. 程序在桌面生成 `userdata.txt`；
3. 第一行填写校园卡号，第二行填写统一身份认证密码；
4. 保存后再次启动程序。

配置和日志位置：

```text
%APPDATA%\AutoSZUWeb\setting.json
%APPDATA%\AutoSZUWeb\logs\auth_YYYY-MM-DD.log
```

## macOS 使用

### 构建要求

- macOS 11 或更新版本；
- Homebrew；
- CMake、Ninja、Homebrew curl、OpenSSL 3、nlohmann-json。

```bash
brew install cmake ninja curl openssl@3 nlohmann-json
./build_macos.sh
```

开发产物：

```text
build/macos/AutoSZUWeb.app
```

生成包含第三方动态库的可安装 DMG：

```bash
brew install dylibbundler
./package_macos.sh
```

DMG 输出到 `dist/macos/`。无 Developer ID 时生成 ad-hoc 签名测试包，首次需在 Finder 中右键打开；正式无警告分发必须使用 Developer ID 签名并完成 Apple 公证，详见 [macOS 打包说明](docs/macos_packaging.md)。

建议先将 `.app` 放到最终目录（例如 `/Applications`）再首次运行。程序会创建：

```text
~/Library/Application Support/AutoSZUWeb/setting.json
~/Library/Application Support/AutoSZUWeb/logs/auth_YYYY-MM-DD.log
~/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist
```

首次访问桌面上的 `userdata.txt` 时，macOS 可能请求“桌面文件夹”权限，请选择允许。

## 工作流程

```text
首次启动
  ├─ 生成桌面 userdata.txt
  ├─ 用户填写账号和密码
  ├─ 平台凭据保护后写入 setting.json
  ├─ 注册当前用户自启动
  └─ 首次认证并进入常驻循环

常驻循环
  ├─ 在线：每 10 秒执行多目标 TCP 连通性探测
  └─ 离线：每 10 秒执行 SRun → ePortal 认证，成功后返回在线态
```

## 安全说明

- Windows 使用 DPAPI，密文绑定当前 Windows 用户；
- macOS 使用当前用户登录钥匙串保存 256 位随机密钥，配置密文采用随机 IV 的 AES-256-GCM；
- macOS 密钥保存在当前用户登录钥匙串中；正式分发版应完成应用签名，并在真机上复核钥匙串访问控制；
- ePortal 密码参数会进行 URL 编码；
- 凭据不会上传到第三方，但校园认证请求本身按照校园网协议发送给校内认证服务器；
- 与当前用户处于同一安全上下文的恶意程序仍可能调用平台凭据 API，应保证设备安全。

## 测试

```powershell
cmake -S . -B build/test -G "MinGW Makefiles" -DAUTOSZUWEB_BUILD_TESTS=ON
cmake --build build/test --parallel
ctest --test-dir build/test --output-on-failure
```

完整的 Windows 回归、macOS CI、Keychain、LaunchAgent、TCC、Gatekeeper、签名公证和校园网真机验收步骤见：

- [macOS 移植实现说明](docs/macos_port.md)
- [完整测试与发布方案](docs/testing.md)

## 卸载

Windows：删除当前用户 Run 注册表中的 `AutoSZUWeb`，再删除 `%APPDATA%\AutoSZUWeb`。

macOS：

```bash
launchctl bootout "gui/$(id -u)" "$HOME/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist" 2>/dev/null || true
rm -f "$HOME/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist"
rm -rf "$HOME/Library/Application Support/AutoSZUWeb"
```

最后删除 `AutoSZUWeb.app`。如需彻底清除凭据，还应在“钥匙串访问”中删除 `com.autoszuweb.AutoSZUWeb` 项。
