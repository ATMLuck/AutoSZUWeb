# <center> AutoSZUWeb </center>


<p align="center">
  <img src="https://img.shields.io/badge/Platform-Windows-blue?logo=windows" alt="Platform">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-%23f34b7d?logo=c%2B%2B" alt="Language">
  <img src="https://img.shields.io/badge/License-MIT-green" alt="License">
  <img src="https://img.shields.io/badge/SZU-Dorm%20Network-orange" alt="SZU">
</p>

---

## 简介

**AutoSZUWeb** 是一个 Windows 后台常驻工具，专为深圳大学宿舍区校园网设计。

首次配置后，程序会静默常驻后台：在线时每 10 秒探测一次网络连通性，断开即判定离线并自动重新登录认证服务器，无需手动操作。程序自身会复制到 %AppData% 并注册开机自启，真正做到无感认证。

---

## 功能特性

- **后台常驻** — GUI 模式运行，无控制台窗口，静默驻留
- **断网自动重连** — 在线时每 10 秒探测网络状态，断开即判定离线并自动重新登录
- **开机自启** — 首次配置后自动复制到 %AppData% 并写入注册表启动项
- **首次弹窗反馈** — 首次启动时弹窗显示登录结果，后续静默重连
- **本地存储** — 账号密码加密保存在 %APPDATA%\AutoSZUWeb\setting.json，不上传第三方
- **单文件分发** — 静态编译，无需安装运行库

---

## 快速开始

### 环境要求

- Windows 操作系统
- 深圳大学校园网环境

### 方式一：直接使用（推荐）

从 [Releases](https://github.com/ATMluck/AutoSZUWeb/releases) 下载最新的 AutoSZUWeb.exe，双击运行即可。

### 方式二：从源码构建

```bash
# 克隆仓库
git clone https://github.com/ATMluck/AutoSZUWeb.git
cd AutoSZUWeb

# MinGW 一键编译
build.bat

# 或使用 CMake
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

---

## 使用说明

### 首次配置

1. **双击运行** AutoSZUWeb.exe，弹出提示框
2. 桌面自动生成 userdata.txt，用记事本打开
3. **编辑文件**：第一行学号/校园卡号，第二行统一身份认证密码

   ```
   2025123456
   your_password_here
   ```

4. **保存文件**，再次双击运行 AutoSZUWeb.exe
5. 程序读取配置、完成首次登录，弹窗显示认证结果
6. 程序自动复制自身到 %APPDATA%\AutoSZUWeb\并注册开机自启

> 桌面上的 userdata.txt 会在配置成功后自动删除。

### 日常运行

配置完成后无需任何操作。程序开机自启后常驻后台：

- 在线时每 **10 秒** 探测一次网络连通性，连通则无动作
- 探测到断开即判定离线，离线时每 **10 秒** 登录一次直到恢复
- 后台重连不弹窗，静默运行

---

### 生成的文件

| 路径 | 说明 |
|---|---|
| `%APPDATA%\AutoSZUWeb\setting.json` | 加密保存的账号密码（JSON 格式） |
| `%APPDATA%\AutoSZUWeb\AutoSZUWeb.exe` | 程序自身副本，用于开机自启 |
| `%APPDATA%\AutoSZUWeb\logs\auth_YYYY-MM-DD.log` | 程序连接日志 |
| `Desktop\userdata.txt` | 首次配置临时文件（配置后自动删除） |

### 修改密码 / 重新配置

删除 %APPDATA%\AutoSZUWeb\setting.json，重新运行程序即可重新走首次配置流程。

---

## 工作流程

```
首次启动                         常驻后台
   │                                │
   ├─ 生成 userdata.txt              ├─ DPAPI 解密 setting.json
   ├─ 用户填写账号密码                ├─ 在线态: 每 10s NetworkCheck
   ├─ DPAPI 加密 → setting.json      │     ├─ 连通 → 无动作
   ├─ Login() 弹窗反馈                │     └─ 断开 → 转离线态
   ├─ SetAutoStart()                  ├─ 离线态: 每 10s Login()
   │   ├─ 复制 exe 到 AppData          │     ├─ 成功 → 转在线态
   │   └─ 写入注册表 Run               │     └─ 失败 → 10s 后重试
   └─ 进入常驻循环 ───────────────────┘
```

---

## 技术栈

- **语言**: C++17
- **编译器**: MinGW-w64 (g++)，静态链接 + -mwindows
- **HTTP 库**: [libcurl](https://curl.se/libcurl/)（静态链接）
- **JSON 库**: [nlohmann/json](https://github.com/nlohmann/json)
- **网络检测**: Winsock2 TCP connect + select 超时，多目标公共 DNS（TCP 53）冗余探测
- **凭据加密**: Windows DPAPI (CryptProtectData / CryptUnprotectData) + Base64 编码
- **认证**: 教学/办公区 SRun（net.szu.edu.cn）+ 宿舍区 ePortal Dr.COM（172.30.255.42:801），SRun 优先、失败自动回退

---

## 加密说明

账号密码使用 **Windows DPAPI** 加密后以 Base64 字符串存入 JSON，非明文落盘。

- **密钥由 Windows 保管**，无需用户记忆额外密码
- **绑定 Windows 用户**：只有加密时的同一用户在同一台电脑上才能解密
- 正常修改 Windows 密码不影响解密；管理员强制重置密码会导致解密失败，此时删除 %APPDATA%\AutoSZUWeb\setting.json 重新配置即可

---

## 注意事项

- 本程序适用于**深圳大学宿舍区与教学/办公区**校园网认证（教学区 SRun / 宿舍区 ePortal），不适用于校外网络
- 凭据以 DPAPI 密文存储在本地 %APPDATA%\AutoSZUWeb\setting.json，在当前 Windows 用户下运行的恶意软件理论上可调用 DPAPI 解密，请注意电脑安全
- 登录请求通过内网 HTTP 发送至 172.30.255.42，不会经过外网
- 程序会复制自身到 AppData 并注册自启，卸载时删除注册表项 HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run\AutoSZUWeb 和 %APPDATA%\AutoSZUWeb\目录即可

---

<p align="center">
  <sub>Made with ❤️ for SZU students</sub>
</p>
