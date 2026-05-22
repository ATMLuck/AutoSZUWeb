# <center> AutoSZUWeb </center>


<p align="center">
  <img src="https://img.shields.io/badge/Platform-Windows-blue?logo=windows" alt="Platform">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-%23f34b7d?logo=c%2B%2B" alt="Language">
  <img src="https://img.shields.io/badge/License-MIT-green" alt="License">
  <img src="https://img.shields.io/badge/SZU-Dorm%20Network-orange" alt="SZU">
</p>

---

## 简介

**AutoSZUWeb** 是一个 Windows 平台下的宿舍区校园网自动登录工具，专为深圳大学学生设计。

每次开机或断网重连后，无需手动打开浏览器输入账号密码 — 程序会自动向后端的 Dr.COM 认证服务器发起登录请求，让你快速恢复网络连接。一次配置，长期使用。

> 支持开机自启动，真正做到无感登录。

---

## 功能特性

- **一键登录** — 运行即登录，无需打开浏览器
- **开机自启** — 配置后自动注册到 Windows 启动项，开机无需任何操作
- **本地存储** — 账号密码保存在 `%APPDATA%\autoWEB.json`，不上传任何第三方
- **轻量高效** — 单文件程序，静态编译，无需安装运行库
- **校园网专用** — 针对深大宿舍区 Dr.COM 认证协议定制
---

## 快速开始

### 环境要求

- Windows 操作系统
- Shenzhen University 宿舍区校园网环境

### 方式一：直接使用（推荐）

从 [Releases](https://github.com/ATMluck/AutoSZUWeb/releases) 下载最新的 `AutoSZUWeb.exe`，双击运行即可。

### 方式二：从源码构建

需要安装 [MSYS2](https://www.msys2.org/) 并配置 MinGW-w64 (g++) 工具链。

```bash
# 克隆仓库
git clone https://github.com/ATMluck/AutoSZUWeb.git
cd AutoSZUWeb

# 编译
build.bat

# 可执行文件在 build/AutoSZUWeb.exe
```

---

## 使用说明

### 首次配置

1. **双击运行** `AutoSZUWeb.exe`

2. 程序会弹出提示框，告知配置方法

3. **桌面上会自动生成 `userdata.txt` 文件**，用记事本打开

4. **编辑文件**，第一行填写学号/校园卡号，第二行填写统一身份认证密码：

   ```
   2023123456
   your_password_here
   ```

5. **保存文件**，再次双击运行 `AutoSZUWeb.exe`

6. 程序读取配置并完成登录，控制台将显示登录结果

> 配置成功后，桌面上的 `userdata.txt` 会自动删除，账号密码已保存至 `%APPDATA%\autoWEB.json`。

### 日常使用

配置完成后，每次需要登录校园网时运行一次程序即可。如果开启了开机自启（默认），程序会在开机时自动运行。

---

## 文件说明

| 路径 | 说明 |
|---|---|
| `AutoSZUWeb.cpp` | 主程序源码（~240 行） |
| `build.bat` | Windows 一键编译脚本 |
| `include/` | 第三方头文件（libcurl, nlohmann/json） |
| `lib/` | 静态库文件（curl, openssl, nghttp2 等） |
| `ico/` | 程序图标 |
| `ico.rc` | Windows 资源文件 |

### 生成的文件

| 路径 | 说明 |
|---|---|
| `%APPDATA%\autoWEB.json` | 保存的账号密码（JSON 格式） |
| `Desktop\userdata.txt` | 首次配置时的临时文件（配置后自动删除） |

---

## 技术栈

- **语言**: C++17
- **编译器**: MinGW-w64 (g++)
- **HTTP 库**: [libcurl](https://curl.se/libcurl/) (静态链接)
- **JSON 库**: [nlohmann/json](https://github.com/nlohmann/json)
- **认证协议**: Dr.COM 校园网认证

---

## 注意事项

- 本程序仅适用于**深圳大学宿舍区**校园网（Dr.COM 认证系统），不适用于教学区或校外网络
- 账号密码以明文形式存储在本地 `%APPDATA%\autoWEB.json`，请注意保护个人电脑安全
- 登录请求通过内网 HTTP 发送至认证服务器 (`172.30.255.42`)，不会经过外网
- 如更换密码，直接删除 `%APPDATA%\autoWEB.json` 后重新运行程序即可重新配置

---

<p align="center">
  <sub>Made with ❤️ for SZU students</sub>
</p>
