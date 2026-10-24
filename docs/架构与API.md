# AutoSZUWeb 架构与 API 说明

> 本文档面向开发者与维护者，覆盖项目的**完整功能清单、总体架构、构建体系、核心流程、认证协议、安全设计与全部公共模块的 API 参考**。
> 安装、日常使用、更新与卸载等操作指引见 [使用指南.md](使用指南.md)。

## 目录

- [1. 项目概述](#1-项目概述)
- [2. 完整功能清单](#2-完整功能清单)
- [3. 总体架构](#3-总体架构)
- [4. 核心运行流程](#4-核心运行流程)
- [5. 认证协议实现](#5-认证协议实现)
- [6. 凭据安全设计](#6-凭据安全设计)
- [7. 模块 API 参考](#7-模块-api-参考)
- [8. 平台差异对照](#8-平台差异对照)
- [9. 测试体系](#9-测试体系)
- [10. 持续集成（CI）](#10-持续集成ci)
- [11. 扩展指南](#11-扩展指南)

## 1. 项目概述

AutoSZUWeb 是深圳大学校园网后台自动认证工具：配置一次校园卡号与统一身份认证密码后常驻后台，周期性探测外网连通性，断网时自动完成教学/办公区 SRun 认证，失败则回退宿舍区 ePortal 认证。

技术栈与约束：

| 项 | 内容 |
|---|---|
| 语言标准 | C++17（`CXX_EXTENSIONS OFF`，无编译器扩展） |
| 构建系统 | CMake ≥ 3.20 |
| 第三方库 | libcurl（HTTP 客户端）、OpenSSL（MD5/SHA1/HMAC/AES-256-GCM）、nlohmann/json（JSON） |
| 发布平台 | Windows（MinGW UCRT64，GUI 子系统单文件 exe）、macOS 11+（arm64 / x86_64，.app bundle） |
| Linux | 仅作为开发期逻辑测试桩（`build.sh`），不产出发布二进制 |
| 许可 | MIT |

## 2. 完整功能清单

| # | 功能 | 行为说明 |
|---|---|---|
| 1 | 后台常驻自动认证 | 无参数启动后进入 `Online`/`Offline` 状态机，10 秒一轮；启动时立即认证一次 |
| 2 | 双认证链路回退 | 每次认证先尝试教学/办公区 SRun（`net.szu.edu.cn`），失败回退宿舍区 ePortal（`172.30.255.42:801`）；失败消息合并两个阶段的原因 |
| 3 | 外网连通性探测 | 对三个公共 DNS（119.29.29.29 / 223.5.5.5 / 114.114.114.114）的 TCP 53 端口探测，任一可达即在线；每端点默认 2000 ms 超时 |
| 4 | 首次配置向导 | 桌面 `userdata.txt` 模板生成 → 读取前两行（卡号/密码）→ 加密入库 → 删除明文文件；模板含提示文本，未替换即报错 |
| 5 | 凭据本地加密 | Windows DPAPI（绑定当前用户）；macOS 登录钥匙串保存 256 位随机密钥 + AES-256-GCM（随机 IV）加密配置；Linux 测试桩使用固定密钥 |
| 6 | 开机自启动（含自修复） | Windows：自复制到 `%APPDATA%\AutoSZUWeb\` 并写 HKCU Run 注册表值；macOS：写用户级 LaunchAgent plist。每次启动都重新注册，允许用户移动 .app 后路径自动更新 |
| 7 | 后台自动认证开关 | `setting.json` 的 `AutoAuth` 字段（默认 `true`，缺省/损坏回退为开）；常驻实例每个周期重读该字段，运行中修改约 10 秒内生效，关闭时完全静默（无任何网络请求） |
| 8 | 命令行接口 | `卡号 密码` 手动认证一次、`--auto true\|false` 修改开关、`--uninstall` 完全卸载、`--help`/`-h` 帮助；未知参数退出码 2；Windows 下宽字符 argv 转 UTF-8、GUI 子系统自动附加控制台 |
| 9 | 手动认证模式 | 认证一次即退出：不写配置、不注册自启动、跳过单实例守卫；结果输出到控制台，无控制台时回退弹窗 |
| 10 | 一键完全卸载 | `--uninstall`：确认弹窗 → 摘自启动 → 删共享数据（数据目录 + 桌面 userdata.txt）→ 平台善后（Windows 延迟自删运行中的 exe / macOS 钥匙串 + .app + bootout）→ 残留清单报告 |
| 11 | 单实例保护 | Windows 命名互斥体 / POSIX `flock`；锁路径 `<数据目录>/AutoSZUWeb.instance.lock`；`--auto` 用临时守卫探测常驻实例是否在运行 |
| 12 | 认证日志 | 按天追加 `<数据目录>/logs/auth_YYYY-MM-DD.log`，记录时间、认证方式、结果、IP 与原因；macOS 另有 launchd 标准输出日志 |
| 13 | 平台差异化 UI | Windows `MessageBoxW` 弹窗（含首次认证结果）；macOS 自绘 `NSPanel` 模态面板（LSUIElement 后台应用，弹窗时临时切换激活策略）；Linux 桩输出 stdout |
| 14 | 旧配置自动迁移 | Windows 上若发现早期版本的 `%APPDATA%\autoWEB.json`，自动复制为 `setting.json` 并删除旧文件 |
| 15 | CI 自动构建 | GitHub Actions：Windows（UCRT64）+ macOS arm64（macos-15）+ macOS x86_64（macos-15-intel）三矩阵，含测试、app bundle 校验与自包含 DMG 打包 |

## 3. 总体架构

### 3.1 分层结构

```text
┌──────────────────────────────────────────────────────────────────┐
│ AutoSZUWeb（可执行程序 / .app）                                   │
│   src/AutoSZUWeb.cpp   main：CLI 分发、首次配置向导、常驻主循环    │
│   src/web.cpp          Login：SRun → ePortal 回退编排、日志、弹窗  │
│   src/srun.cpp         SRun（深澜）协议实现                       │
├──────────────────────────────────────────────────────────────────┤
│ AutoSZUWebCore（静态库；测试直接链接，逻辑可全平台覆盖）           │
│   纯逻辑：cli / config / runtime / uninstall / user_data          │
│   跨平台：sys.cpp(WriteAuthLog 等) / platform_support             │
│   内部共享：internal/credential_crypto（Base64 + AES-256-GCM）     │
├──────────────────────────────────────────────────────────────────┤
│ 平台实现层（同名钩子、按平台择一编译）                             │
│   src/windows/  sys.cpp  ui.cpp  file_path.cpp  network.cpp      │
│                 single_instance.cpp                               │
│   src/macos/    sys.cpp  ui.mm  file_path.cpp                    │
│   src/posix/    network.cpp  single_instance.cpp（macOS/Linux 共用）│
│   src/linux/    sys.cpp  ui.cpp  file_path.cpp（测试桩）          │
├──────────────────────────────────────────────────────────────────┤
│ AutoSZUWebTests（全平台逻辑测试）/ AutoSZUWebMacUITests（macOS）   │
└──────────────────────────────────────────────────────────────────┘
```

设计原则：

- **核心与平台分离**：业务逻辑（解析、配置、状态机、卸载编排）是纯函数/纯逻辑，放在 Core 静态库，不依赖任何平台头文件，可在 Windows、macOS、Linux 三平台完整测试；
- **平台钩子集中在 `sys.h` 等接口之后**：每个平台目录提供同名实现，CMake 按平台择一编译；新增平台只需实现一组钩子；
- **入口极薄**：`main` 只做分发与流程编排，所有可测逻辑下沉到 Core。

### 3.2 目录结构

```text
AutoSZUWeb/
├── CMakeLists.txt               # 构建定义
├── README.md                    # 项目简介
├── docs/
│   ├── 使用指南.md               # 安装 / 使用 / 更新 / 卸载
│   └── 架构与API.md              # 本文档
├── include/                     # 公共头文件（Core 对外 API）
│   ├── app_ui.h                 # 跨平台消息框与 UI 策略
│   ├── cli.h                    # 命令行解析（纯函数）
│   ├── config.h                 # setting.json AutoAuth 读写
│   ├── file_path.h              # 数据根目录 / 桌面 / 配置路径
│   ├── network.h                # 连通性探测与本机 IP
│   ├── platform_support.h       # XML 转义、LaunchAgent plist 生成
│   ├── runtime.h                # 监控状态机
│   ├── single_instance.h        # SingleInstanceGuard
│   ├── srun.h                   # SRun 协议入口
│   ├── sys.h                    # 平台钩子（自启动/凭据/日志/CLI 支持）
│   ├── uninstall.h              # 卸载编排
│   ├── user_data.h              # userdata.txt 生命周期
│   └── web.h                    # Login 认证入口
├── src/
│   ├── AutoSZUWeb.cpp           # main + 首次配置 + 常驻主循环
│   ├── cli.cpp                  # Cli::Parse / PrintUsage
│   ├── config.cpp               # AutoAuth 字段读写（原子写）
│   ├── runtime.cpp              # RunIteration / RunMonitorLoop
│   ├── sys.cpp                  # WriteAuthLog / GetCredentialError（跨平台部分）
│   ├── uninstall.cpp            # Uninstall::RemoveAll / RemoveSharedData
│   ├── user_data.cpp            # userdata.txt 模板 / 读取 / 删除
│   ├── platform_support.cpp     # XmlEscape / BuildLaunchAgentPlist
│   ├── web.cpp                  # Login 编排（SRun → ePortal）
│   ├── srun.cpp                 # SRun 协议（challenge → 加密载荷 → 登录）
│   ├── internal/
│   │   ├── credential_crypto.h  # 内部共享：Base64 + AES-256-GCM + 错误状态
│   │   └── credential_crypto.cpp
│   ├── windows/                 # Win32：DPAPI、注册表、MessageBox、延迟删除…
│   ├── macos/                   # Cocoa/Security：NSPanel、钥匙串、LaunchAgent…
│   ├── posix/                   # macOS/Linux 共用：BSD socket 探测、flock
│   └── linux/                   # 测试桩：stdout UI、固定测试密钥
├── tests/
│   ├── test_main.cpp            # 全平台逻辑与平台测试
│   └── mac_ui_test.mm           # macOS NSPanel 冒烟测试
├── packaging/entitlements.plist # codesign entitlements
├── .github/workflows/build.yml  # CI 三矩阵
├── build.bat                    # Windows 一键构建 + 测试
├── build_macos.sh               # macOS 构建 .app + 测试
├── build.sh                     # Linux 测试桩构建
├── package_macos.sh             # DMG 打包（dylibbundler / 签名 / 可选公证）
├── Info.plist.in                # .app 元数据模板（LSUIElement）
├── lib/                         # Windows 预编译静态库（libcurl/OpenSSL 全家桶）
├── res/ + ico/                  # Windows 图标资源
└── dist/                        # 打包输出（不入库）
```

### 3.3 CMake 目标与构建选项

| 目标 | 平台 | 说明 |
|---|---|---|
| `AutoSZUWebCore` | 全部 | 静态库：纯逻辑模块 + 平台钩子实现。链接 `OpenSSL::Crypto`；找到 `nlohmann_json` 配置包时以 PUBLIC 链接（MSYS2 无配置包，头文件在编译器默认包含路径）。Windows 另链 `ws2_32 crypt32 bcrypt shell32`；macOS 另链 `Security`、`Cocoa` 框架 |
| `AutoSZUWeb` | 仅 WIN32 / APPLE | 可执行程序。Windows：windres 编译图标资源、`-static -mwindows`（静态单文件、GUI 子系统）、链接 `lib/` 下 curl 全家桶；macOS：`MACOSX_BUNDLE`（`Info.plist.in` 模板，`LSUIElement=true`、最低 11.0） |
| `AutoSZUWebTests` | 全部 | 链接 Core 的测试程序 |
| `AutoSZUWebMacUITests` | 仅 APPLE | `src/macos/ui.mm` 的 NSPanel 冒烟测试 |

选项：`AUTOSZUWEB_BUILD_TESTS`（默认 `ON`）。Linux 仅在开启测试时允许配置（`UNIX AND AUTOSZUWEB_BUILD_TESTS` 分支），否则 CMake 直接报错——Linux 不产出发布二进制。

### 3.4 第三方依赖策略

- **Windows**：静态链接产出单文件 exe。依赖仓库 `lib/` 下的预编译 MinGW 静态库：`libcurl`、`ssl`、`crypto`、`nghttp2`、`nghttp3`、`ngtcp2`、`ngtcp2_crypto_libressl`、`ssh2`、`z`、`zstd`、`brotli`、`psl`，外加系统库 `ws2_32 crypt32 bcrypt wldap32 iphlpapi secur32 shell32`。Core 的 OpenSSL 在 `lib/libcrypto.a` 存在时直接使用，否则 `find_package(OpenSSL)`；
- **macOS**：动态链接 Homebrew 的 `curl`、`openssl@3`、`nlohmann-json`（构建脚本以 `CMAKE_PREFIX_PATH` 指定）。分发时 `package_macos.sh` 用 `dylibbundler` 把非系统动态库收进 `.app/Contents/Frameworks` 并改写引用，做依赖自包含校验（拒绝残留 Homebrew 路径、校验 `@loader_path`/`@rpath` 全部可解析），嵌套签名后打出 DMG；
- **nlohmann/json**：Core 中 `find_package(nlohmann_json CONFIG QUIET)`——MSYS2 环境没有 CMake 配置包，头文件位于默认包含路径，因此"找到才链接"是兼容写法。

## 4. 核心运行流程

### 4.1 main 分发

`src/AutoSZUWeb.cpp` 的 `main` 以纯函数解析 + 显式分发组织全部入口：

```text
GetUtf8Arguments(argc, argv)     # Windows：宽字符 argv → UTF-8；其余平台透传
        │
Cli::Parse(args)                 # 纯函数，无副作用
        │
        ├─ Help         → PrepareCliConsole + PrintUsage                    → exit 0
        ├─ UsageError   → stderr 提示 + PrintUsage                          → exit 2
        ├─ Uninstall    → RunUninstall：确认弹窗 → Uninstall::RemoveAll     → exit 0/1
        ├─ OneShotAuth  → RunOneShotAuth：Login 一次，控制台/弹窗输出结果    → exit 0/1
        ├─ SetAutoAuth  → RunSetAutoAuth：写 AutoAuth + 探测常驻实例         → exit 0/1
        └─ NormalStartup
              ├─ GetConfigPath()（确保数据目录存在；Windows 含旧配置迁移）
              ├─ SingleInstanceGuard 守卫（非主实例 → 静默 exit 0）
              └─ setting.json 存在？
                    ├─ 是 → RunMainLoop()      常驻模式
                    └─ 否 → ConfigureNewUser() 首次配置
```

要点：

- **Help / UsageError 依赖控制台**：`PrepareCliConsole()` 返回 false（Windows 双击启动无控制台）时不输出任何内容，仅返回退出码——CLI 功能面向终端使用；
- **OneShotAuth / SetAutoAuth / Uninstall 均跳过单实例守卫**：可与常驻实例并存；输出优先走控制台，无控制台时回退 `AppUI::ShowMessage` 弹窗；
- **SetAutoAuth 的常驻探测**：构造一个临时 `SingleInstanceGuard` 试探锁文件——抢不到锁说明常驻实例在运行，据此提示"约 10 秒内生效"。

### 4.2 首次配置（ConfigureNewUser）

```text
桌面 userdata.txt 不存在？
├─ 是 → 提示弹窗（可取消，取消即退出）
│       → UserData::CreateTemplate 生成两行提示文本的模板
│       → 程序结束（等待用户填写）
└─ 否 → UserData::ReadCredentials 读前两行（卡号 / 密码）
        ├─ 校验：两行非空、不以提示文本 '(' 开头、剥离 '\r'
        → EncryptStr 加密账号与密码（失败即弹窗退出，不落盘）
        → 写 setting.json.tmp → 原子 rename 为 setting.json（失败时删除旧文件重试一次）
        → UserData::RemoveFile 删除桌面 userdata.txt（失败仅提示，因含明文）
        → RunMainLoop()
```

### 4.3 常驻状态机与 AutoAuth 门控

`RunMainLoop` 读取并解密凭据后，把认证与探测封装为回调交给 `Runtime::RunMonitorLoop`（默认 10 秒周期）。`AutoAuth` 开关不进入状态机本身，而是在回调闭包内每个周期重读字段实现门控：

```cpp
const bool autoAuthEnabled = Config::ReadAutoAuthEnabled(loginPath);
const bool online = autoAuthEnabled && Login(account, password);   // 启动认证

callbacks.login = [&] {
    return Config::ReadAutoAuthEnabled(loginPath) && Login(account, password);
};
callbacks.networkCheck = [&] {
    // 关闭时视为在线保持静默；重新开启后恢复真实探测
    return !Config::ReadAutoAuthEnabled(loginPath) || NetworkCheck();
};
callbacks.sleep = [](std::chrono::milliseconds d) { std::this_thread::sleep_for(d); };
callbacks.shouldStop = [] { return false; };   // 常驻，永不主动退出

Runtime::RunMonitorLoop(online ? ConnectionState::Online : ConnectionState::Offline,
                        callbacks);
```

门控语义（`AutoAuth=false` 时**完全静默**，不发任何网络请求）：

| 当前状态 | `AutoAuth = true` | `AutoAuth = false` |
|---|---|---|
| `Online` | `networkCheck()` 真实探测，失败转 `Offline` | 短路返回 true，保持 `Online` |
| `Offline` | `login()` 真实认证，成功转 `Online` | 短路返回 false，保持 `Offline` |

重新开启后的恢复路径：`Online` 态下一周期做真实探测，若实际离线转入 `Offline` 并在随后周期认证；`Offline` 态下一周期直接认证。

### 4.4 首启弹窗（FirstBoot）与 UI 策略

`AutoSZUWeb.cpp` 匿名命名空间持有 `FirstBoot` 标志，`web.cpp` 通过外部声明 `bool IsFirstBoot();` 读取。`Login` 成功/失败后仅在 `IsFirstBoot() && AppUI::ShouldShowAuthenticationResult()` 时弹窗展示结果——Windows 保留首次认证结果弹窗（`ShouldShowAuthenticationResult()` 返回 `true`），macOS 后台运行仅写日志（返回 `false`）。OneShot 手动认证模式在入口处置 `FirstBoot = false`，抑制弹窗改用控制台输出。

## 5. 认证协议实现

### 5.1 教学区 SRun（`src/srun.cpp`）

对 `https://net.szu.edu.cn`（深澜 SRun 门户）的 challenge 认证，全流程复用一个 curl 句柄并启用内存 Cookie 引擎：

1. **发现 ac_id**：GET 首页，扫描 `ac_id=` 后的数字；失败回退 `"1"`；
2. **获取挑战**：`GET /cgi-bin/get_challenge?callback=_&username=<账号>&ip=` → JSONP 解析出 `challenge` 与 `client_ip`；
3. **构造加密载荷**：
   - `password_md5 = HMAC-MD5(key=challenge, msg=密码)`，十六进制；
   - `info = "{SRBX1}" + SrunBase64(XEncode(JSON{username, password, ip, acid, enc_ver:"srun_bx1"}, key=challenge))`，其中 `XEncode` 为 XXTEA 兼容实现（小端分词、可选追加原始长度），`SrunBase64` 把标准 Base64 字母表重映射到 SRun 自定义字母表 `LVoJPiCN2R8G90yg+hmFHuacZ1OWMnrsSTXkYpUq/3dlbfKwv6xztjI7DeBE45QA`；
   - `chksum = SHA1(challenge‖账号‖challenge‖password_md5‖challenge‖ac_id‖challenge‖client_ip‖challenge‖"200"‖challenge‖"1"‖challenge‖info)`（challenge 逐段前置拼接）；
4. **提交登录**：`GET /cgi-bin/srun_portal?callback=_&action=login&username=…&password={MD5}<password_md5>&info=…&chksum=…&ac_id=…&ip=…&n=200&type=1&os=Windows&name=Windows&double_stack=0`；
5. **成功判定**：`error=="ok"` ‖ `res=="ok"` ‖ `st` 为 `1`/`true`/`"1"`。

实现约束：TLS 证书校验关闭（校园自签环境）；连接超时 5 s、总超时 12 s；请求带 Chrome UA。

### 5.2 宿舍区 ePortal（`src/web.cpp` 的 `LoginDormitory`）

`GET http://172.30.255.42:801/eportal/portal/login?user_account=%2C0%2C<URL编码账号>&user_password=<URL编码密码>`。账号密码均经 `curl_easy_escape` 编码；响应形如 `(...)` 包裹的 JSON，剥壳解析后判定成功：`result == 0` ‖ `msg` 含“认证成功”。连接超时 5 s、总超时 12 s。

### 5.3 连通性探测与本机 IP（`include/network.h`）

- `NetworkCheck`：依次对 `{119.29.29.29:53, 223.5.5.5:53, 114.114.114.114:53}` 做 TCP 连接探测，任一成功即在线；
- `GetLocalIp`：创建 UDP socket 并 `connect 8.8.8.8:53`（不发包，仅让系统选择路由），再 `getsockname` 取本机出口 IPv4；失败返回空串，仅用于成功日志记录；
- Windows 实现（`src/windows/network.cpp`）用 Winsock 并处理 `WSAStartup`；macOS/Linux 共用 `src/posix/network.cpp` 的 BSD socket 实现。

### 5.4 认证日志（`src/sys.cpp` 的 `WriteAuthLog`）

追加写入 `<数据目录>/logs/auth_YYYY-MM-DD.log`，格式：

```text
[YYYY-MM-DD HH:MM:SS] 方式=<method> 结果=成功|失败 [IP=<ip>] [详情=<note>|原因=<reason>]
```

`method` 取值：`SRun(教学区)` / `ePortal(宿舍区)` / `SRun→ePortal`（双失败）/ `初始化`（curl 初始化失败）。时间取本地时间（Windows 用 `GetLocalTime`，其余 `localtime_r`）。

## 6. 凭据安全设计

| 平台 | 方案 | 密文格式（存于 setting.json） |
|---|---|---|
| Windows | DPAPI：`CryptProtectData` / `CryptUnprotectData`（`CRYPTPROTECT_UI_FORBIDDEN`，描述串 `AutoSZUWeb`），密文绑定当前 Windows 用户，无独立密钥存储 | Base64(DPAPI 密文) |
| macOS | 登录钥匙串保存 32 字节随机密钥（`kSecClassGenericPassword`，service `com.autoszuweb.AutoSZUWeb`，account `credential-encryption-key`；首次自动生成，`SecItemDuplicateItem` 竞态时回退重读）；加密用随机 IV 的 AES-256-GCM | `v1:` + Base64(IV ‖ 密文 ‖ GCM tag) |
| Linux（桩） | 固定 32 字节测试密钥（`AutoSZUWeb-Linux-Test-Key-000001`），仅用于逻辑测试 | 同 macOS 格式 |

AES-GCM 与 Base64 的单份实现放在 Core 内部共享模块 `src/internal/credential_crypto`（`CredentialCrypto` 命名空间），供各平台 `sys.cpp` 的 `EncryptStr`/`DecryptStr` 复用，并维护 `thread_local` 错误状态（`SetError`/`ClearError`/`GetError`，经 `GetCredentialError()` 对外查询）。

安全边界与已知限制：

- 凭据不上传任何第三方；认证请求仅按校园网协议发给校内认证服务器；
- ePortal 的密码参数做 URL 编码；SRun 请求中的密码只以 HMAC-MD5 / XXTEA 加密形态出现；
- 与当前用户同安全上下文的恶意程序仍可调用 DPAPI / 钥匙串 API 解密，设备安全是前提；
- 命令行手动认证的密码为明文（Shell 历史 / 进程列表可见），仅面向手动临时使用；
- macOS 正式分发应完成 Developer ID 签名并复核钥匙串访问控制。

## 7. 模块 API 参考

以下为 `include/` 全部公共模块（及内部共享模块）的接口语义。除特别说明外，字符串均为 UTF-8，路径为 `std::filesystem::path`。

### 7.1 Cli — 命令行解析（`include/cli.h`）

```cpp
namespace Cli
{
    enum class Action {
        NormalStartup,  // 无参数：后台常驻
        OneShotAuth,    // 卡号 密码：手动认证一次
        SetAutoAuth,    // --auto true|false
        Uninstall,      // --uninstall
        Help,           // --help / -h
        UsageError      // 未知或多余参数
    };

    struct Parsed {
        Action action = Action::NormalStartup;
        std::string account;        // OneShotAuth 时有效
        std::string password;       // OneShotAuth 时有效
        bool autoAuthEnabled = false; // SetAutoAuth 时有效
    };

    Parsed Parse(const std::vector<std::string>& args); // 不含程序名，纯函数
    void PrintUsage(std::ostream& output);
}
```

解析规则（`src/cli.cpp`）：

| 参数 | 结果 |
|---|---|
| （空） | `NormalStartup` |
| `--uninstall` | `Uninstall` |
| `--help` / `-h` | `Help` |
| 其他单个参数 | `UsageError` |
| `--auto` + `true`/`false`（不区分大小写） | `SetAutoAuth`（`autoAuthEnabled` 置位） |
| `--auto` + 其他 | `UsageError` |
| 两个均不以 `-` 开头的参数 | `OneShotAuth`（卡号、密码） |
| 其余两参数及以上 | `UsageError` |

使用示例：

```cpp
const Cli::Parsed parsed = Cli::Parse(GetUtf8Arguments(argc, argv));
if (parsed.action == Cli::Action::UsageError)
    return 2;
```

### 7.2 Config — AutoAuth 字段读写（`include/config.h`）

```cpp
namespace Config
{
    // 读 setting.json 的 AutoAuth；文件缺失 / 字段缺失 / 解析失败一律返回 true（默认开启，兼容旧配置）。
    bool ReadAutoAuthEnabled(const std::filesystem::path& configPath);

    // 设置 AutoAuth 并原子写回（tmp + rename，失败回退 remove+rename），保留其他字段。
    // 配置文件不存在或 JSON 格式错误时返回 false，error 说明原因。
    bool WriteAutoAuthEnabled(const std::filesystem::path& configPath,
                              bool enabled, std::string& error);
}
```

常驻实例的动态开关依赖 `ReadAutoAuthEnabled` 每周期重读（见 4.3）。写回使用 `dump(2)` 缩进格式。

### 7.3 Uninstall — 卸载编排（`include/uninstall.h`）

```cpp
namespace Uninstall
{
    // 删除共享数据：数据目录（setting.json、logs/、实例锁、自启动副本）逐项
    // remove_all，被占用条目记入 leftovers；随后删除桌面 userdata.txt。
    void RemoveSharedData(std::vector<std::filesystem::path>& leftovers);

    // 完全卸载编排：
    //   RemoveAutoStart() → RemoveSharedData() → RemovePlatformResidue()
    //   → leftovers 非空则汇总"未能删除（可能被占用）"并返回 false。
    bool RemoveAll(std::string& error);
}
```

顺序保证先摘自启动：即使后续步骤失败，也不会残留开机自启。逐项删除（而非整目录 `remove_all`）是为了把被占用文件（如运行中的 exe 副本）精确记入 leftovers，交给平台善后（Windows 延迟删除）。

### 7.4 Sys — 平台钩子（`include/sys.h`）

Core 定义接口，`src/windows|macos|linux/sys.cpp` 提供实现；跨平台部分（`WriteAuthLog`、`GetCredentialError`）直接在 `src/sys.cpp`。

```cpp
void SetAutoStart();               // 注册当前用户登录自启
void RemoveAutoStart();            // 移除自启动注册
bool RemoveCredentialKey(std::string& error);   // 删除平台凭据密钥
bool RemovePlatformResidue(std::vector<fs::path>& leftovers, std::string& error);
std::vector<std::string> GetUtf8Arguments(int argc, char* argv[]); // UTF-8 参数（不含程序名）
bool PrepareCliConsole();          // CLI 模式控制台准备
std::string EncryptStr(const std::string& plaintext);   // 平台凭据加密
std::string DecryptStr(const std::string& ciphertext);  // 平台凭据解密
std::string GetCredentialError();  // 最近一次凭据操作失败原因
void WriteAuthLog(const std::string& method, bool success,
                  const std::string& deviceIp, const std::string& message);
```

各钩子的平台行为：

| 钩子 | Windows | macOS | Linux（桩） |
|---|---|---|---|
| `SetAutoStart` | 复制自身到 `%APPDATA%\AutoSZUWeb\AutoSZUWeb.exe`，写 HKCU `Run` 值 `AutoSZUWeb` 指向副本 | 重写 `~/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist`（RunAtLoad，stdout/stderr 重定向到 logs/），权限 600 | 空实现 |
| `RemoveAutoStart` | `RegDeleteValueW`（`ERROR_FILE_NOT_FOUND` 视为成功） | 删除 plist（尊重 `AUTOSZUWEB_LAUNCH_AGENT_DIR` 覆盖） | 空实现 |
| `RemoveCredentialKey` | 无独立密钥，恒成功 | `SecItemDelete` 钥匙串条目（`errSecItemNotFound` 视为成功） | 恒成功 |
| `RemovePlatformResidue` | 删遗留 `%APPDATA%\autoWEB.json`；对运行中 exe 安排延迟删除（`cmd /c ping -n 3 … & del`，`CREATE_NO_WINDOW`；失败回退 `MoveFileExW(MOVEFILE_DELAY_UNTIL_REBOOT)`）并从 leftovers 移除；处理 AppData 自启动副本 | `getppid() != 1` 时 `launchctl bootout gui/<uid>/<label>`（终止 launchd 实例，自身是 launchd 子进程则跳过避免自杀）→ 删钥匙串 → 删 `.app` 包（路径含 `.app/Contents/MacOS/` 时定位包根） | 空实现 |
| `GetUtf8Arguments` | `GetCommandLineW` + `CommandLineToArgvW` + `WideCharToMultiByte(CP_UTF8)`（main 的 ANSI argv 中文会乱码） | argv 本身即 UTF-8，透传 | 透传 |
| `PrepareCliConsole` | stdout 句柄为管道/文件（重定向，Git Bash 场景）→ true；否则 `AttachConsole(ATTACH_PARENT_PROCESS)` + `freopen("CONOUT$")`（cmd/PowerShell）→ true；均失败（双击启动）→ false | 恒 true | 恒 true |
| `EncryptStr` / `DecryptStr` | DPAPI（见第 6 节） | 钥匙串密钥 + AES-256-GCM | 固定测试密钥 + AES-256-GCM |

### 7.5 Web / Srun — 认证入口（`include/web.h`、`include/srun.h`）

```cpp
// web.h
bool Login(const std::string& account, const std::string& password);
bool Login(const std::string& account, const std::string& password, std::string& message);

// srun.h
bool SrunLogin(const std::string& account, const std::string& password, std::string& message);
```

`Login` 的编排（`src/web.cpp`）：

1. `curl_global_init`（进程内一次，失败即失败）；
2. 先 `SrunLogin`（教学/办公区）；成功 → `message` 为成功信息，方式记 `SRun(教学区)`；
3. 失败 → `LoginDormitory`（宿舍区 ePortal）；成功方式记 `ePortal(宿舍区)`，双失败方式记 `SRun→ePortal` 且 `message` 合并两个阶段原因（`"教学区认证失败: …\n宿舍区认证失败: …"`）；
4. 每次调用写认证日志（成功附 `GetLocalIp()`）；
5. 首启（`IsFirstBoot()`）且平台允许（`ShouldShowAuthenticationResult()`）时弹窗展示结果。

2 参重载转发 3 参版本，行为一致。协议细节见第 5 节。

### 7.6 Network — 连通性探测（`include/network.h`）

```cpp
bool ProbeTcpEndpoint(const std::string& ip, int port, int timeoutMs = 2000); // 单端点 TCP 探测
bool NetworkCheck(int timeoutMs = 2000);  // 多目标任一可达即在线
std::string GetLocalIp();                 // 系统路由选择的出口 IPv4，失败为空
```

### 7.7 Runtime — 监控状态机（`include/runtime.h`）

```cpp
namespace Runtime
{
    enum class ConnectionState { Online, Offline };

    struct MonitorCallbacks {
        std::function<bool()> login;        // 必填：离线态认证，true → Online
        std::function<bool()> networkCheck; // 必填：在线态探测，false → Offline
        std::function<void(std::chrono::milliseconds)> sleep; // 必填
        std::function<bool()> shouldStop;   // 必填
    };

    ConnectionState RunIteration(ConnectionState state, const MonitorCallbacks& callbacks);
    void RunMonitorLoop(ConnectionState initialState, const MonitorCallbacks& callbacks,
                        std::chrono::milliseconds interval = std::chrono::seconds(10));
}
```

- `RunIteration` 是单步纯逻辑（回调为空抛 `std::invalid_argument`）：`Online → networkCheck() ? Online : Offline`；`Offline → login() ? Online : Offline`；
- `RunMonitorLoop`：循环 `RunIteration` + `sleep(interval)`，直到 `shouldStop()` 返回 true；`sleep`/`shouldStop` 为空抛异常；
- 状态机本身不感知 AutoAuth、不感知认证协议——门控与协议全部由回调闭包注入（见 4.3），这是 Core 可全平台单测的关键。

使用示例：

```cpp
Runtime::MonitorCallbacks cb;
cb.login        = [&] { return Login(account, password); };
cb.networkCheck = [] { return NetworkCheck(); };
cb.sleep        = [](std::chrono::milliseconds d) { std::this_thread::sleep_for(d); };
cb.shouldStop   = [] { return false; };
Runtime::RunMonitorLoop(Runtime::ConnectionState::Online, cb);
```

### 7.8 SingleInstance — 单实例守卫（`include/single_instance.h`）

```cpp
namespace Runtime
{
    class SingleInstanceGuard
    {
    public:
        explicit SingleInstanceGuard(const std::filesystem::path& lockPath);
        ~SingleInstanceGuard();                       // 释放锁
        // 不可拷贝、不可移动
        bool IsPrimary() const;   // true = 抢到锁（首个实例）
    };
}
```

实现：Windows（`src/windows/single_instance.cpp`）以锁文件绝对路径派生名称的**命名互斥体**（`CreateMutexW`，跨进程、无残留文件句柄问题）；macOS/Linux（`src/posix/single_instance.cpp`）对锁文件 `open(O_RDWR|O_CREAT, 0600)` 后 `flock(LOCK_EX|LOCK_NB)`，锁文件内容仅写入持有者 PID 作诊断（`flock` 而非文件存在性持有锁）。两种实现的锁路径语义一致：`<数据目录>/AutoSZUWeb.instance.lock`。

### 7.9 UserData — userdata.txt 生命周期（`include/user_data.h`）

```cpp
namespace UserData
{
    bool CreateTemplate(const fs::path& path, std::string& error); // 生成两行提示文本模板
    bool ReadCredentials(const fs::path& path, std::string& account,
                         std::string& password, std::string& error); // 读前两行
    bool RemoveFile(const fs::path& path, std::string& error);     // 删除配置临时文件
}
```

`ReadCredentials` 的校验：两行均存在、非空、不以 `(` 开头（提示文本未替换）、剥离行尾 `\r`。返回前保证文件已关闭（避免 Windows 句柄占用）。

### 7.10 FilePath — 路径解析（`include/file_path.h`）

```cpp
fs::path GetUsersFolderPath();  // 数据根目录
fs::path GetDesktopPath();      // 用户桌面
fs::path GetConfigPath();       // <数据根>/AutoSZUWeb/setting.json（确保目录存在）
```

| 平台 | 数据根目录 | 桌面 |
|---|---|---|
| Windows | `%APPDATA%`（`CSIDL_APPDATA`） | `CSIDL_DESKTOP` |
| macOS | `~/Library/Application Support` | `~/Desktop` |
| Linux（桩） | `~/.local/share` | `~/Desktop` |

测试覆盖：`AUTOSZUWEB_DATA_ROOT` 与 `AUTOSZUWEB_DESKTOP_ROOT` 环境变量分别覆盖数据根与桌面路径。Windows 的 `GetConfigPath` 额外做旧版 `autoWEB.json` → `setting.json` 的一次性迁移（存在旧文件且新文件不存在时复制并删除旧文件）。

### 7.11 AppUI — 跨平台消息框（`include/app_ui.h`）

```cpp
namespace AppUI
{
    enum class Button { Ok, Cancel };

    Button ShowMessage(const std::string& text, const std::string& title = "提示",
                       bool allowCancel = false);
    bool ShouldShowAuthenticationResult();  // 首次认证结果是否弹窗
}
```

| 平台 | `ShowMessage` 实现 | `ShouldShowAuthenticationResult` |
|---|---|---|
| Windows | `MessageBoxW`（UTF-8 → UTF-16）；仅 `IDOK` 映射 `Ok`，调用失败映射 `Cancel`（失败安全，避免误确认破坏性对话框） | `true` |
| macOS | `src/macos/ui.mm`：自绘 `NSPanel` 模态面板（标题/正文/按钮的 `NSStackView` 布局），弹窗期间临时切换 `NSApplicationActivationPolicyRegular` 并激活，结束恢复；关闭按钮与 ESC 等同取消 | `false` |
| Linux（桩） | 输出 `[标题] 文本` 到 stdout；`AUTOSZUWEB_UI_RESPONSE=cancel` 模拟取消（测试用） | `false` |

### 7.12 PlatformSupport — 无副作用工具（`include/platform_support.h`）

```cpp
namespace PlatformSupport
{
    std::string XmlEscape(const std::string& value);   // XML 实体转义
    std::string BuildLaunchAgentPlist(const std::string& executablePath,
                                      const std::string& stdoutPath,
                                      const std::string& stderrPath);
}
```

`BuildLaunchAgentPlist` 生成 macOS LaunchAgent plist 全文：Label `com.autoszuweb.AutoSZUWeb`、`RunAtLoad=true`、`KeepAlive=false`、`LimitLoadToSessionType=Aqua`、可执行文件路径与标准输出/错误重定向均经 `XmlEscape`。

### 7.13 CredentialCrypto — 内部共享加密模块（`src/internal/credential_crypto.h`）

> 非公共 API（不入 `include/`），供各平台 `sys.cpp` 复用，保证加解密实现单份。

```cpp
namespace CredentialCrypto
{
    constexpr size_t kAesKeySize = 32;

    std::string Base64Encode(const unsigned char* data, size_t length);
    std::vector<unsigned char> Base64Decode(const std::string& encoded);

    // 密文格式 "v1:" + Base64(iv || ciphertext || tag)；失败返回空串并记录错误
    std::string EncryptAesGcm(const std::vector<unsigned char>& key, const std::string& plaintext);
    std::vector<unsigned char> DecryptAesGcm(const std::vector<unsigned char>& key,
                                             const std::string& encoded);

    // thread_local 错误状态
    void SetError(const std::string& message);
    void ClearError();
    const std::string& GetError();
}
```

AES-256-GCM 基于 OpenSSL EVP；密钥长度恒为 32 字节（`kAesKeySize`），IV 随机生成并随密文存储。

## 8. 平台差异对照

| 维度 | Windows | macOS | Linux（测试桩） |
|---|---|---|---|
| 产物 | GUI 子系统单文件 exe（`-static -mwindows`，含图标） | `.app` bundle（`LSUIElement`，无 Dock 图标）+ 可选 DMG | 仅测试二进制 |
| 数据目录 | `%APPDATA%\AutoSZUWeb\` | `~/Library/Application Support/AutoSZUWeb/` | `~/.local/share/AutoSZUWeb/` |
| 自启动 | HKCU Run 注册表值 + `%APPDATA%` 自启动副本 | 用户级 LaunchAgent plist（RunAtLoad） | 无 |
| 凭据保护 | DPAPI（绑定用户，无独立密钥） | 钥匙串密钥 + AES-256-GCM | 固定测试密钥 |
| 消息框 | `MessageBoxW` | 自绘 `NSPanel` 模态 | stdout |
| 首次认证弹窗 | 显示 | 不显示（仅日志） | 不显示 |
| 网络 | Winsock（`src/windows/network.cpp`） | BSD socket（`src/posix/network.cpp`） | 同 macOS（posix 共用） |
| 单实例 | 命名互斥体 | `flock` 锁文件 | 同 macOS（posix 共用） |
| argv 编码 | ANSI → 需宽字符 API 转 UTF-8 | 原生 UTF-8 | 原生 UTF-8 |
| CLI 控制台 | 句柄检测 + `AttachConsole` | 恒可用 | 恒可用 |
| 卸载善后 | 延迟自删运行中 exe（ping+del / 重启删除） | `launchctl bootout` + 钥匙串 + `.app` 删除 | 无 |
| 旧配置迁移 | `autoWEB.json` → `setting.json` | 无 | 无 |

## 9. 测试体系

测试入口 `tests/test_main.cpp`（链接 `AutoSZUWebCore`，三平台可跑），逐项断言输出 `[PASS]/[FAIL]`，任一失败进程返回非零。测试组：

| 测试组 | 覆盖 |
|---|---|
| `TestEncryption` | 各平台 `EncryptStr`/`DecryptStr` 往返、错误状态 |
| `TestCli` | 全部解析规则、用法文本、`GetUtf8Arguments` 往返、`PrepareCliConsole` |
| `TestAuthenticationResultUiPolicy` | `ShouldShowAuthenticationResult` 平台策略 |
| `TestUserDataFileLifecycle` | userdata.txt 模板生成 → 读取校验（空行/提示文本/`\r`）→ 删除 |
| `TestPathsAndLogging` | 路径解析 + `WriteAuthLog` 落盘格式 |
| `TestUninstallSharedData` | 伪造数据目录与桌面文件 → `Uninstall::RemoveAll` → 验证删除 |
| `TestAutoAuthConfig` | AutoAuth 读写往返、字段保留、默认值、损坏文件拒绝写入、无 `.tmp` 残留 |
| `TestLaunchAgentPlist` | plist 生成与 XML 转义 |
| `TestSingleInstanceGuard` | 同进程内守卫互斥语义 |
| `TestRuntimeStateMachine` | `RunIteration`/`RunMonitorLoop` 状态转移与回调校验 |
| `TestLoopbackProbe` | `ProbeTcpEndpoint` 对本机监听端口探测 |
| macOS 专属块 | `SetAutoStart` → `RemoveAutoStart` 往返；钥匙串测试（设置 `AUTOSZUWEB_KEYCHAIN_SERVICE` 时运行：删钥后旧密文不可解、密钥自动重建） |

测试用环境变量（隔离真实系统状态）：

| 变量 | 作用 | 平台 |
|---|---|---|
| `AUTOSZUWEB_DATA_ROOT` | 覆盖数据根目录（配置/日志/锁文件重定向到临时目录） | 全部 |
| `AUTOSZUWEB_DESKTOP_ROOT` | 覆盖桌面路径（userdata.txt 重定向） | 全部 |
| `AUTOSZUWEB_KEYCHAIN_SERVICE` | 覆盖钥匙串 service 名（CI 用独立 keychain + 独立 service，避免污染真实凭据；本机未设置则跳过钥匙串测试） | macOS |
| `AUTOSZUWEB_LAUNCH_AGENT_DIR` | 覆盖 LaunchAgent 目录（plist 写入测试目录） | macOS |
| `AUTOSZUWEB_UI_RESPONSE` | 设为 `cancel` 时桩 UI 返回取消（测试取消路径） | Linux 桩 |

运行方式：

```bash
# Windows（MSYS2 UCRT64）
build.bat                        # 或手动 cmake -DAUTOSZUWEB_BUILD_TESTS=ON + ctest

# macOS
./build_macos.sh                 # 构建 + ctest（含 AutoSZUWebMacUITests）

# Linux / WSL
./build.sh                       # 平台桩逻辑测试
```

`tests/mac_ui_test.mm`（目标 `AutoSZUWebMacUITests`）为 macOS NSPanel 的冒烟测试，15 秒超时。

## 10. 持续集成（CI）

`.github/workflows/build.yml`，push 与 pull_request 触发，三个 job（`fail-fast: false`）：

| Job | Runner | 步骤要点 |
|---|---|---|
| `windows` | `windows-latest` + MSYS2 UCRT64 | 安装 mingw-w64-ucrt-x86_64 全套依赖 → Ninja 构建 + ctest → 上传 `AutoSZUWeb.exe` 产物 |
| `macos (arm64)` | `macos-15` | brew 依赖 → 构建 + ctest → app bundle 校验 → 打 DMG → 上传产物 |
| `macos (x86_64)` | `macos-15-intel` | 同上（Intel 镜像） |

macOS job 的关键设计：

- **钥匙串隔离**：创建专用测试 keychain 并设为默认，`AUTOSZUWEB_KEYCHAIN_SERVICE` 按 `run_id.run_attempt.arch` 唯一命名，测试后校验条目存在再删除 keychain（`if: always()` 兜底清理）；
- **LaunchAgent 重定向**：`AUTOSZUWEB_LAUNCH_AGENT_DIR` 指向 runner 临时目录，不碰真实 `~/Library/LaunchAgents`；
- **bundle 校验**：可执行位、`lipo -info` 断言架构、`plutil -lint` 校验 plist、断言 `LSUIElement=true`；
- **DMG 打包**：调 `package_macos.sh`（`APP_SOURCE` 指向 CI 构建产物）产出 ad-hoc 签名的自包含测试 DMG。

Runner 选型备注：`macos-13`（Intel）已于 2025-12 退役，`macos-14` 将于 2026-11-02 退役，因此 arm64 使用 `macos-15`、Intel 使用 `macos-15-intel`。

## 11. 扩展指南

**新增平台钩子**（如新增一个自启动/凭据相关的系统能力）：

1. 在 `include/sys.h`（或对应模块头）声明接口，注释写清全部平台的语义约定；
2. 在 `src/windows/sys.cpp`、`src/macos/sys.cpp` 实现真实行为，`src/linux/sys.cpp` 提供桩；
3. 为可无副作用验证的部分补测试（利用第 9 节的环境变量重定向）；写真实注册表/钥匙串的操作不在单测中执行，靠 CI 与手动冒烟覆盖。

**新增平台**（参照 Linux 桩模式）：

1. 新建 `src/<platform>/`：`sys.cpp`（凭据/自启动钩子）、`ui.cpp`（消息框）、`file_path.cpp`（路径）；网络与单实例优先复用 `src/posix/`；
2. `CMakeLists.txt` 增加 `elseif` 分支（Linux 分支即模板：仅测试构建放行）；
3. 跑通 `AutoSZUWebTests` 全部用例。

**修改认证协议**：教学区改 `src/srun.cpp`（challenge 流程/载荷构造），宿舍区改 `src/web.cpp` 的 `LoginDormitory`；编排与日志在 `Login`。协议成功判定集中在 `PortalSuccess` 与 `LoginDormitory` 的 JSON 解析处。

**发布打包**：macOS 走 `package_macos.sh`（设置 `MACOS_SIGN_IDENTITY` 为 Developer ID 可切换正式签名 + 公证流程，见脚本尾部 notarization 段）；Windows 产物即 `build*/AutoSZUWeb.exe`。
