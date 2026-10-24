# AutoSZUWeb 使用指南

> 本文档面向使用者，覆盖项目的**安装、首次配置、日常使用、更新与卸载**。
> 完整功能清单、项目架构与代码级 API 说明见 [架构与API.md](架构与API.md)。

## 目录

- [1. 项目简介](#1-项目简介)
- [2. 系统要求](#2-系统要求)
- [3. 安装](#3-安装)
- [4. 日常使用](#4-日常使用)
- [5. 更新](#5-更新)
- [6. 卸载](#6-卸载)
- [7. 常见问题](#7-常见问题)

## 1. 项目简介

AutoSZUWeb 是深圳大学校园网后台自动认证工具。完成一次账号配置后，程序常驻后台，每 10 秒检查一次外网连通性；检测到断网后自动尝试认证——先走教学/办公区 SRun 认证（`net.szu.edu.cn`），失败时回退到宿舍区 ePortal 认证（`172.30.255.42`）。

核心特性：

- 全程后台运行，无需人工干预；账号密码本地加密保存；
- 命令行手动认证（`AutoSZUWeb 卡号 密码`）、后台自动认证开关（`--auto`）与一键完全卸载（`--uninstall`）；
- 开机自启动，并在每次启动时自动修复自启动配置；
- 支持 Windows 与 macOS（Apple Silicon / Intel）。

## 2. 系统要求

| 项目 | Windows | macOS |
|---|---|---|
| 操作系统 | Windows 10 及以上（x64） | macOS 11 及以上 |
| 芯片 | x64 | Apple Silicon（arm64）或 Intel（x86_64） |
| 运行依赖 | 无（静态链接，单文件 exe） | 无（DMG 内已捆绑全部依赖库） |
| 网络 | 可访问校园网认证服务器 | 同左 |

## 3. 安装

### 3.1 获取程序

| 平台 | 构建产物 | 内容 |
|---|---|---|
| Windows | `AutoSZUWeb-Windows` | `AutoSZUWeb.exe` 单文件 |
| macOS（Apple Silicon） | `AutoSZUWeb-macOS-arm64-test-dmg` | `AutoSZUWeb-<版本>-macOS-arm64.dmg` |
| macOS（Intel） | `AutoSZUWeb-macOS-x86_64-test-dmg` | `AutoSZUWeb-<版本>-macOS-x86_64.dmg` |

可在 GitHub Releases 或 Actions 构建产物（Artifacts）中下载。Mac 按芯片选择：M 系列选 arm64，Intel 机型选 x86_64。也可以从源码自行构建，见 [3.5 节](#35-从源码构建可选)。

### 3.2 Windows 安装

1. 将 `AutoSZUWeb.exe` 放到一个**固定目录**（例如 `D:\Tools\AutoSZUWeb\`），建议不要放在“下载”或临时目录；
2. 双击运行，程序弹出提示并生成桌面 `userdata.txt` 模板后自动退出；
3. 按 [3.4 节](#34-首次配置流程) 完成首次配置，再次双击运行即完成安装并进入后台常驻。

安装完成后，程序将配置与日志保存在 `%APPDATA%\AutoSZUWeb\`，并把自身副本注册为开机自启（机制见 [4.1 节](#41-后台自动认证默认模式)）。自启动不再依赖原始 exe 的位置，但建议保留它，便于日后更新与执行 `--uninstall`。

### 3.3 macOS 安装

1. 双击挂载 DMG，将 `AutoSZUWeb.app` 拖入「应用程序」。**建议一步放到最终目录**——程序按当前运行路径注册自启动（移动后再次启动会自动刷新注册）；
2. 首次启动：在「应用程序」中**右键 → 打开 → 再点“打开”**放行。测试包为 ad-hoc 签名，Gatekeeper 需手动放行一次；
3. 首次启动可能请求「桌面文件夹」访问权限（用于生成 `userdata.txt`），请选择允许；
4. 按 [3.4 节](#34-首次配置流程) 完成首次配置。程序为后台应用（不显示 Dock 图标），配置完成后即常驻后台。

### 3.4 首次配置流程

各平台配置方式一致，通过桌面的 `userdata.txt` 完成：

1. 首次启动时，程序在桌面生成 `userdata.txt`，内容为两行提示文本，随后退出；
2. 打开该文件：**第一行**整行替换为校园卡号，**第二行**整行替换为统一身份认证平台密码，保存（不要保留括号提示文本）；
3. 再次启动程序：读取账号密码 → 本地加密写入配置文件 → 删除桌面 `userdata.txt` → 注册开机自启 → 立即执行一次认证；
4. Windows 会弹出首次认证结果对话框；macOS 不弹窗，认证结果仅写入日志。

配置完成后，凭据以密文保存在：

```text
Windows:  %APPDATA%\AutoSZUWeb\setting.json
macOS:    ~/Library/Application Support/AutoSZUWeb/setting.json
```

### 3.5 从源码构建（可选）

**Windows**（MSYS2 UCRT64 环境，需安装 `mingw-w64-ucrt-x86_64-` 前缀的 cmake、gcc、ninja、curl、openssl、nlohmann-json）：

```powershell
cmake -S . -B build/windows -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows --parallel
```

或直接运行 `build.bat`（配置 + 构建 + 测试一条龙）。产物：`build\windows\AutoSZUWeb.exe`。Windows 版静态链接，依赖仓库 `lib/` 目录下的预编译库（libcurl、OpenSSL 等），详见[架构文档](架构与API.md)。

**macOS**（Xcode Command Line Tools + Homebrew）：

```bash
brew install cmake ninja curl openssl@3 nlohmann-json
./build_macos.sh              # 构建 .app 并运行测试
# 生成自包含 DMG（可选，分发用）：
brew install dylibbundler
./package_macos.sh            # 输出到 dist/macos/
```

无 Developer ID 证书时 `package_macos.sh` 生成 ad-hoc 签名的测试包。

## 4. 日常使用

### 4.1 后台自动认证（默认模式）

无参数启动即进入常驻模式（开机自启后同样进入该模式）：

- 启动时立即认证一次，随后进入监控循环；
- **在线**：每 10 秒做一次外网连通性探测（对 `119.29.29.29` / `223.5.5.5` / `114.114.114.114` 的 TCP 53 端口，任一可达即视为在线）；
- **离线**：每 10 秒自动认证一次（教学区 SRun 优先，失败回退宿舍区 ePortal），成功后回到在线态；
- 全程无窗口。同一台机器同时只运行一个实例（单实例保护），重复启动会静默退出。

自启动机制：

| 平台 | 机制 | 说明 |
|---|---|---|
| Windows | HKCU `...\CurrentVersion\Run` 注册表值 `AutoSZUWeb` | 指向 `%APPDATA%\AutoSZUWeb\AutoSZUWeb.exe` 副本；每次启动自动刷新副本与注册 |
| macOS | 用户级 LaunchAgent `~/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist` | 登录时启动（RunAtLoad）；每次启动按当前 .app 路径自动重写 |

### 4.2 命令行用法

| 命令 | 作用 | 退出码 |
|---|---|---|
| `AutoSZUWeb` | 后台常驻模式（默认） | 0 |
| `AutoSZUWeb 卡号 密码` | 手动认证一次后退出。不写配置、不注册自启动、不影响后台实例 | 成功 0 / 失败 1 |
| `AutoSZUWeb --auto true`（或 `false`） | 开启/关闭后台自动认证，写入 `setting.json` 的 `AutoAuth` 字段；运行中的实例约 10 秒内生效 | 成功 0 / 失败 1 |
| `AutoSZUWeb --uninstall` | 从本机完全卸载，见[第 6 节](#6-卸载) | 完成 0 / 有残留 1 |
| `AutoSZUWeb --help` 或 `-h` | 打印用法 | 0 |
| 其他参数组合 | 用法错误（输出到 stderr） | 2 |

**Windows**（cmd / PowerShell / Git Bash 均可）：

```powershell
.\AutoSZUWeb.exe 卡号 密码
.\AutoSZUWeb.exe --auto false
.\AutoSZUWeb.exe --uninstall
.\AutoSZUWeb.exe --help
```

**macOS**：

```bash
/Applications/AutoSZUWeb.app/Contents/MacOS/AutoSZUWeb 卡号 密码
/Applications/AutoSZUWeb.app/Contents/MacOS/AutoSZUWeb --auto false
```

补充说明：

- `--auto` 的 `true` / `false` 不区分大小写；该命令跳过单实例守卫，可与运行中的常驻实例并存；
- `--auto` 要求已完成首次配置（`setting.json` 存在且格式正确），否则提示“尚未完成首次配置”并返回 1；
- Windows 版为 GUI 子系统程序：在终端中运行时会自动附加到控制台输出结果；若无控制台（如通过资源管理器以参数方式启动），结果改为弹窗显示。`--help` 与用法错误在无控制台时没有可见输出，请在终端中使用命令行功能；
- 手动认证的密码以明文出现在命令行中，会留在 Shell 历史与进程列表里，仅建议在本人操作的机器上临时使用（详见[第 7 节](#7-常见问题)）。

### 4.3 配置文件说明

`setting.json` 由程序管理，字段如下：

| 字段 | 类型 | 说明 |
|---|---|---|
| `Account` | string | 校园卡号（平台加密后的密文） |
| `Password` | string | 统一身份认证密码（密文） |
| `AutoAuth` | bool（可选） | 是否后台自动认证，默认 `true`；`--auto` 修改的就是此字段 |

- 密文格式：Windows 为 DPAPI 密文的 Base64；macOS 为 `v1:` 前缀 + Base64（IV ‖ 密文 ‖ GCM 校验标签）；
- `AutoAuth` 字段缺失或解析失败时一律视为 `true`（与旧版本配置兼容）；
- 也可以直接手工编辑 `AutoAuth` 的布尔值，运行中的常驻实例约 10 秒内会读取到新值并应用。

### 4.4 日志

认证日志按日期保存：

```text
Windows:  %APPDATA%\AutoSZUWeb\logs\auth_YYYY-MM-DD.log
macOS:    ~/Library/Application Support/AutoSZUWeb/logs/auth_YYYY-MM-DD.log
```

每行格式示例：

```text
[2026-10-24 08:30:00] 方式=SRun(教学区) 结果=成功 IP=172.30.x.x
[2026-10-24 08:31:00] 方式=SRun→ePortal 结果=失败 原因=教学区: 密码错误; 宿舍区: 密码错误
```

macOS 另有 `logs/launchd.out.log` 与 `logs/launchd.err.log`（LaunchAgent 的标准输出/错误）。

## 5. 更新

更新**不会丢失任何数据**：配置（`setting.json`）、日志与凭据密钥全部保留，无需重新配置。前提是先退出运行中的常驻实例——否则单实例保护会让新实例静默退出，看起来像“更新没生效”。

### 5.1 Windows 更新步骤

1. 退出常驻实例：任务管理器中结束 `AutoSZUWeb.exe`，或执行：

   ```powershell
   taskkill /IM AutoSZUWeb.exe /F
   ```

2. 用新版 `AutoSZUWeb.exe` 覆盖固定目录中的旧版（如 `D:\Tools\AutoSZUWeb\AutoSZUWeb.exe`）；
3. 双击运行一次：程序启动时自动把新版复制到 `%APPDATA%\AutoSZUWeb\AutoSZUWeb.exe` 并刷新注册表自启动，随即认证并进入常驻。

也可以把新版放到任意位置运行一次，效果相同（自启动副本同样会刷新），但建议保持固定的原始位置，便于日后更新与 `--uninstall`。

### 5.2 macOS 更新步骤

1. 退出常驻实例：

   ```bash
   launchctl bootout "gui/$(id -u)" "$HOME/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist"
   ```

   或在活动监视器中退出 AutoSZUWeb 进程；
2. 将新版 `AutoSZUWeb.app` 拖入「应用程序」替换旧版（POSIX 允许替换正在运行的程序，但旧实例会继续运行旧代码直到退出，因此建议先退出再替换）；
3. 右键打开一次新版程序：启动时会按新路径自动重写 LaunchAgent 配置并进入常驻。当前登录会话的自启动在下次登录时自动恢复，也可手动立即挂载：

   ```bash
   launchctl bootstrap "gui/$(id -u)" "$HOME/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist"
   ```

4. 若新版签名发生变化，Gatekeeper 可能要求重新放行（右键 → 打开）。

### 5.3 从源码更新

```bash
git pull

# Windows
build.bat

# macOS
./build_macos.sh && ./package_macos.sh
```

构建完成后按 5.1 / 5.2 替换运行。

## 6. 卸载

### 6.1 一键卸载（推荐）

```powershell
# Windows
.\AutoSZUWeb.exe --uninstall
```

```bash
# macOS
/Applications/AutoSZUWeb.app/Contents/MacOS/AutoSZUWeb --uninstall
```

执行流程：

1. 弹出确认对话框，列出将删除的内容，可取消（取消时退出码 0，不做任何删除）；
2. 确认后按顺序清理：
   - **开机自启动注册**：Windows 的 Run 注册表值 / macOS 的 LaunchAgent plist；
   - **共享数据**：数据目录（`setting.json`、`logs\`、实例锁文件、自启动副本 exe）与桌面 `userdata.txt`；
   - **平台痕迹与程序本体**：
     - Windows：删除早期版本遗留的 `%APPDATA%\autoWEB.json`、AppData 自启动副本，以及正在运行的 exe 本体——运行中的文件在本程序退出后由系统延迟删除（失败时安排重启后删除）；
     - macOS：卸载 LaunchAgent 任务（终止 launchd 拉起的后台实例）、删除钥匙串中的凭据密钥、删除 `.app` 包本体；
3. 报告结果：全部删除完成退出码 0；仍有残留时退出码 1，并列出未删除项（提示结束 AutoSZUWeb 进程或重启后手动删除）。

注意：**从哪个程序本体执行卸载，就会删除哪个本体**。建议从固定的安装位置运行；从构建目录运行会删除构建产物（这也是预期行为）。

### 6.2 手动卸载（Windows）

1. 删除自启动注册：

   ```powershell
   Remove-ItemProperty -Path "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" -Name "AutoSZUWeb" -ErrorAction SilentlyContinue
   ```

2. 删除数据目录 `%APPDATA%\AutoSZUWeb`（含配置、日志与自启动副本 exe）；
3. 删除程序本体 exe；
4. 桌面若仍有 `userdata.txt`，一并删除。

### 6.3 手动卸载（macOS）

```bash
launchctl bootout "gui/$(id -u)" "$HOME/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist" 2>/dev/null || true
rm -f "$HOME/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist"
rm -rf "$HOME/Library/Application Support/AutoSZUWeb"
rm -rf "/Applications/AutoSZUWeb.app"
```

如需彻底清除凭据密钥，在「钥匙串访问」中删除 `com.autoszuweb.AutoSZUWeb` 条目（类别“密码”，账户名 `credential-encryption-key`）。桌面若有 `userdata.txt` 一并删除。

> 手动卸载不会删除凭据密钥以外的隐藏内容；Windows 的 DPAPI 密文随 `setting.json` 删除即净。

## 7. 常见问题

**如何修改账号或密码？**
删除 `setting.json`（路径见 4.3）后重新启动程序，会重新走 `userdata.txt` 配置流程。学校修改密码后，可先用 `AutoSZUWeb 卡号 新密码` 验证能否认证成功，再重新配置。

**如何临时停用自动认证（不卸载）？**
执行 `AutoSZUWeb --auto false`。常驻实例约 10 秒内停止自动认证并保持静默（不再发送任何认证或探测请求）；`AutoSZUWeb --auto true` 恢复。手动认证不受该开关影响。

**怎么知道程序正在运行？**
Windows 在任务管理器进程列表查找 `AutoSZUWeb.exe`；macOS 用活动监视器。也可以查看当日日志文件是否有新写入。

**Windows 提示“已保护你的电脑”（SmartScreen）？**
程序未做商业代码签名。点击“更多信息 → 仍要运行”即可。个别杀毒软件可能误报，可添加信任。

**macOS 提示“无法打开，因为无法验证开发者”？**
右键程序 → 打开 → 再点“打开”；或在“系统设置 → 隐私与安全性”中点“仍要打开”。

**命令行认证安全吗？**
密码会以明文出现在命令行、Shell 历史与进程列表中，仅建议手动临时使用。日常认证走后台加密凭据，不受此影响；凭据也不会上传到任何第三方（认证请求按校园网协议发给校内认证服务器）。

**断网判定的依据是什么？**
每 10 秒对三个公共 DNS（`119.29.29.29`、`223.5.5.5`、`114.114.114.114`）的 53 端口做 TCP 连接探测，任一可达即认为在线；全部不可达才触发认证。

**双击运行后好像没反应？**
程序是无窗口后台应用。首次配置请查看桌面是否生成了 `userdata.txt`；日常运行可按上文确认进程或查看日志。

**重复启动会怎样？**
单实例保护生效，第二个实例立即静默退出，不会弹窗或产生副作用。
