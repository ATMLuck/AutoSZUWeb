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


## 功能

- 后台常驻、断网自动重连；
- 教学/办公区 SRun + 宿舍区 ePortal 双认证；
- 命令行手动认证：`AutoSZUWeb 卡号 密码` 认证一次后退出；
- 后台自动认证开关：`--auto true|false` 写入 `setting.json` 的 `AutoAuth` 字段，运行中的实例约 10 秒内生效；
- 一键卸载：`AutoSZUWeb --uninstall` 清除自启动、配置、日志、凭据密钥与程序本体；
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

### 命令行

```powershell
# 手动认证一次（成功退出码 0，失败 1；不写配置、不影响自启动与后台实例）
.\AutoSZUWeb.exe 卡号 密码

# 开启/关闭后台自动认证（写入 setting.json 的 AutoAuth 字段，默认开启；
# 关闭后常驻进程不再自动认证，运行中的实例约 10 秒内应用；手动认证不受影响）
.\AutoSZUWeb.exe --auto false

# 从本机完全卸载（自启动、配置、日志、密钥与程序本体）
.\AutoSZUWeb.exe --uninstall

# 查看用法
.\AutoSZUWeb.exe --help
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

DMG 输出到 `dist/macos/`。无 Developer ID 时生成 ad-hoc 签名测试包，首次需在设置中手动放行；

建议先将 `.app` 放到最终目录（例如 `/Applications`）再首次运行。程序会创建：

```text
~/Library/Application Support/AutoSZUWeb/setting.json
~/Library/Application Support/AutoSZUWeb/logs/auth_YYYY-MM-DD.log
~/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist
```

首次访问桌面上的 `userdata.txt` 时，macOS 可能请求“桌面文件夹”权限，请选择允许。

### 命令行

```bash
# 手动认证一次（成功退出码 0，失败 1）
AutoSZUWeb.app/Contents/MacOS/AutoSZUWeb 卡号 密码

# 开启/关闭后台自动认证（同 Windows）
AutoSZUWeb.app/Contents/MacOS/AutoSZUWeb --auto false

# 从本机完全卸载
AutoSZUWeb.app/Contents/MacOS/AutoSZUWeb --uninstall
```

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
- 命令行手动认证的密码以明文传递，会留在 Shell 历史与进程列表中，仅建议手动临时使用；
- 凭据不会上传到第三方，但校园认证请求本身按照校园网协议发送给校内认证服务器；
- 与当前用户处于同一安全上下文的恶意程序仍可能调用平台凭据 API，应保证设备安全。

## 测试

```powershell
cmake -S . -B build/test -G "MinGW Makefiles" -DAUTOSZUWEB_BUILD_TESTS=ON
cmake --build build/test --parallel
ctest --test-dir build/test --output-on-failure
```

## 卸载

推荐使用命令行一键卸载，执行后会先弹窗确认，确认后清除自启动注册、配置与日志、凭据密钥以及程序本体：

```powershell
.\AutoSZUWeb.exe --uninstall
```

```bash
AutoSZUWeb.app/Contents/MacOS/AutoSZUWeb --uninstall
```

若仍有后台实例占用文件，程序会在结果中列出未删除项，请结束 AutoSZUWeb 进程或重启后手动删除。注意：从哪个程序本体执行卸载，就会删除哪个本体；正在运行的文件由系统在本程序退出后延迟删除。

也可以手动卸载。Windows：删除当前用户 Run 注册表中的 `AutoSZUWeb`，再删除 `%APPDATA%\AutoSZUWeb`（含自启动副本 `AutoSZUWeb.exe`）与本程序本体。

macOS：

```bash
launchctl bootout "gui/$(id -u)" "$HOME/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist" 2>/dev/null || true
rm -f "$HOME/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist"
rm -rf "$HOME/Library/Application Support/AutoSZUWeb"
```

最后删除 `AutoSZUWeb.app`。如需彻底清除凭据，还应在“钥匙串访问”中删除 `com.autoszuweb.AutoSZUWeb` 项。
