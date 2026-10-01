# macOS 构建、测试与验收方案

本文档是 `docs/macos_port.md` 的实施结果与可执行验收清单。当前架构已将 UI、路径、凭据保护、自启动、Socket 和常驻状态机从认证协议中分离。

## 1. 自动化测试范围

`AutoSZUWebTests` 不连接深圳大学认证服务器，可在 Windows、macOS 和 Linux 测试桩环境运行，覆盖：

- 凭据加密/解密往返，以及密文篡改拒绝；
- 数据目录、桌面目录和配置目录创建；
- 认证日志文件创建及 UTF-8 内容；
- LaunchAgent plist 的 XML 转义和 Aqua 会话限制；
- 在线/离线状态机切换和轮询间隔；
- POSIX/WinSock 回环 TCP 非阻塞探测；
- macOS 构建时同时编译 AppKit、Security.framework、Keychain 与 AES-GCM 路径。

Windows 的沙盒或非交互 CI 会话可能没有 DPAPI 用户主密钥。测试只在系统明确返回“用户配置文件/登录会话不可用”时跳过 DPAPI 往返；普通桌面 Windows 应完整执行。

## 2. Windows 回归

在仓库根目录执行：

```powershell
cmake -S . -B build/windows-test -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DAUTOSZUWEB_BUILD_TESTS=ON
cmake --build build/windows-test --parallel
ctest --test-dir build/windows-test --output-on-failure
```

预期：生成 `AutoSZUWeb.exe`，所有测试通过。另在真实桌面会话执行一次测试程序，确认 DPAPI 项不是跳过状态。

## 3. macOS 编译与自动测试

要求 macOS 11 或更新版本，并安装 Homebrew。执行：

```bash
brew install cmake ninja curl openssl@3 nlohmann-json
./build_macos.sh
```

预期产物：

```text
build/macos/AutoSZUWeb.app
```

进一步检查：

```bash
plutil -lint build/macos/AutoSZUWeb.app/Contents/Info.plist
/usr/libexec/PlistBuddy -c 'Print :LSUIElement' build/macos/AutoSZUWeb.app/Contents/Info.plist
otool -L build/macos/AutoSZUWeb.app/Contents/MacOS/AutoSZUWeb
```

`LSUIElement` 应为 `true`。`otool` 输出中的 Homebrew curl/OpenSSL 动态库意味着分发前还需做依赖打包或改为静态构建；本步骤验证开发构建，不宣称产物可直接跨机器分发。

## 4. GitHub Actions 编译门禁

`.github/workflows/build.yml` 在每次 push 和 pull request 上执行：

- Windows MinGW 构建与单元测试；
- GitHub 托管的 macOS 14 构建与单元测试；
- `.app` 结构和 `Info.plist` 校验；
- 上传未签名的 macOS `.app` 测试产物。

CI 能证明源码在真实 macOS SDK 上编译并且非交互测试通过，但不能替代 GUI、Keychain 授权、TCC、登录自启和校园网协议的人工验收。

## 5. macOS 真机系统验收

建议创建临时 macOS 用户，避免污染日常 Keychain 和 LaunchAgents。开始前确保 `.app` 位于最终目录，例如 `/Applications/AutoSZUWeb.app`；程序每次启动会刷新 LaunchAgent 中的可执行路径。

### 5.1 首次配置与桌面 TCC

1. 删除旧测试数据：
   ```bash
   rm -f "$HOME/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist"
   rm -rf "$HOME/Library/Application Support/AutoSZUWeb"
   rm -f "$HOME/Desktop/userdata.txt"
   ```
2. 从 Finder 双击 `.app`。
3. 确认出现中文提示框，并在 macOS 请求桌面访问权限时选择允许。
4. 确认桌面生成 `userdata.txt`，且程序退出。
5. 保留模板文本再次启动，确认程序拒绝将提示文本当作凭据。
6. 填入测试账号和密码后再次启动。
7. 确认桌面临时文件被删除，配置生成在：
   `~/Library/Application Support/AutoSZUWeb/setting.json`。
8. 检查 JSON 中不存在明文账号或密码。

### 5.2 Keychain 与篡改保护

1. 打开“钥匙串访问”，搜索 `com.autoszuweb.AutoSZUWeb`。
2. 确认存在 generic password 项，账户名为 `credential-encryption-key`。
3. 再次启动应用，确认配置可以解密，无需重复配置。
4. 备份后修改 `setting.json` 中密文的一个字符。
5. 启动应用，预期提示无法解密，而不是带错误凭据继续认证。
6. 恢复备份。
7. 删除 Keychain 项后启动，预期旧配置无法解密；删除配置并重新配置后应恢复。

### 5.3 LaunchAgent 自启动

1. 检查：
   ```bash
   plutil -lint "$HOME/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist"
   plutil -p "$HOME/Library/LaunchAgents/com.autoszuweb.AutoSZUWeb.plist"
   ```
2. 确认 `ProgramArguments[0]` 指向当前 `.app/Contents/MacOS/AutoSZUWeb`。
3. 注销并重新登录当前用户。
4. 使用“活动监视器”确认进程自动启动且没有 Dock 图标。
5. 检查 `~/Library/Application Support/AutoSZUWeb/logs/launchd.err.log` 无持续错误。
6. 将 `.app` 移动到新目录并手动启动一次；确认 plist 路径被刷新。

### 5.4 状态机和断网重连

此项应在深圳大学校园网内完成：

1. 启动程序并确认首次认证日志成功。
2. 保持在线至少 30 秒，确认没有每 10 秒重复认证。
3. 关闭 Wi-Fi 或断开网线，等待 10–20 秒。
4. 恢复校园网连接但不要手工打开认证网页。
5. 预期程序在后续轮询中自动认证并恢复外网。
6. 检查 `auth_YYYY-MM-DD.log`：断网期间记录失败，恢复后记录正确方式和设备 IP。
7. 分别在教学/办公区和宿舍区执行，验证 SRun 优先和 ePortal 回退。
8. 使用包含 `&`、`+`、`#` 等字符的测试密码，确认 ePortal 参数经过 URL 编码后仍可登录。

### 5.5 异常场景

逐项验证：

- 配置 JSON 截断或字段缺失：应弹出明确错误，不崩溃；
- Keychain 被锁定：应提示凭据保护/解密失败；
- 桌面访问被拒绝：应提示无法创建或读取 `userdata.txt`；
- LaunchAgents 目录不可写：应提示自启动注册失败，但不得损坏凭据；
- 无网络启动：首次登录失败后仍保持后台轮询，网络恢复后自动登录；
- 重复启动两个实例：第二个实例应在显示 UI 前退出，进程列表中始终只保留一个实例。

## 6. 发布前签名、公证和依赖检查

开发构建默认关闭代码签名。公开分发前必须：

1. 确认所有非系统动态库已嵌入 `.app/Contents/Frameworks`，并用 `install_name_tool` 修正为 `@rpath`；或采用许可兼容的静态依赖。
2. 使用 Developer ID Application 证书对内嵌库及 `.app` 由内向外签名。
3. 用 `codesign --verify --deep --strict --verbose=2` 验证。
4. 用 `spctl --assess --type execute --verbose=4` 验证 Gatekeeper。
5. 上传 Apple 公证服务并 stapler 装订。
6. 在一台从未运行过该应用的 macOS 机器上下载最终压缩包，完成首次启动、TCC、Keychain 和重登自启的全新安装测试。

没有 Developer ID 证书时，只能提供未签名测试版，并明确要求用户通过 Finder“右键 → 打开”；不能将 CI 上传的 unsigned artifact 当作正式发布包。

## 7. 验收通过标准

发布候选版本需同时满足：

- Windows 和 macOS CI 全绿；
- 真机 `.app` 无 Dock 图标，提示框可见；
- 凭据不以明文落盘，密文篡改会被拒绝；
- 注销重登后 LaunchAgent 自动拉起；
- 教学区 SRun 和宿舍区 ePortal 各至少完成一次真实认证；
- 断网恢复测试至少连续通过 3 次；
- 日志路径、内容、权限和日期滚动正确；
- 最终分发包完成依赖封装、签名、公证和干净机器验收。
## macOS 弹窗关闭回归测试

新版 UI 实现针对后台 `.app`、Finder 双击和 LaunchAgent 启动场景做了以下处理：

- 显式调用 `finishLaunching`；
- 弹窗交互期间临时切换到 `NSApplicationActivationPolicyRegular`，确保能够获得键盘和鼠标焦点；
- 将窗口置为 key window 并置前；
- “确定”按钮正常结束模态会话；
- “取消”按钮返回 `AppUI::Button::Cancel`；
- 标题栏关闭按钮通过 `windowShouldClose` 显式调用 `abortModal`；
- 非主线程触发 UI 时同步切回主线程；
- 模态结束后恢复原来的后台应用策略。

在真 Mac 上重新构建后，必须测试以下三种关闭方式：

1. 首次运行提示框：点击“确定”，确认程序创建 `userdata.txt` 后退出；
2. 首次运行提示框：点击标题栏关闭按钮，确认程序退出且不挂起；
3. 填写凭据后的认证结果框：点击“确定”，确认程序进入后台常驻；
4. 取消流程：在首次配置提示框点击“取消”，确认程序退出；
5. LaunchAgent 自动启动后出现错误提示时，点击按钮后确认进程仍可继续或正常退出。

进程检查：

```bash
pgrep -fl AutoSZUWeb
```

如果弹窗已经关闭但进程仍然存在，这是正常的：认证成功后的弹窗关闭会回到后台常驻循环；首次配置取消或错误退出则应没有进程。

## macOS 弹窗与单实例回归测试

单实例保护用于阻止 Finder 手动启动与 LaunchAgent 同时运行；但如果进程数已经确认只有一个，而“确定”仍不能关闭窗口，则属于 AppKit 模态事件链问题。新版将按钮绑定到项目自己的 target/action，并在 action 中显式调用 `stopModalWithCode:`，不再依赖 `NSAlert::runModal()` 的内部按钮处理。

测试新版前必须结束全部旧版本进程：

```bash
pkill -x AutoSZUWeb 2>/dev/null || true
sleep 1
pgrep -fl AutoSZUWeb || echo "PASS: no old process"
```

安装新版后执行：

```bash
open -a AutoSZUWeb
open -a AutoSZUWeb
sleep 2
pgrep -x AutoSZUWeb | wc -l
```

预期进程数为 `1`。第二次启动必须在显示任何认证弹窗前退出。

认证成功弹窗出现后：

1. 点击一次“确定”；
2. 确认窗口立即消失且不会出现位置完全相同的第二个窗口；
3. 执行 `pgrep -fl AutoSZUWeb`，确认只有一个后台进程；
4. 正常在线保持 30 秒，确认日志没有重复的同秒认证记录。

应用锁文件位于：

```text
~/Library/Application Support/AutoSZUWeb/AutoSZUWeb.instance.lock
```

该文件可以长期存在；真正的所有权由内核 `flock` 管理，不能以文件存在与否判断程序是否正在运行。进程退出后锁会自动释放。

### macOS UI 自动回归测试

macOS 构建现在包含 `AutoSZUWebMacUITests`。测试会真正创建 NSAlert，并通过 AppKit `performClick:` 分别点击“确定”和“取消”。测试要求：

- “确定”必须让 `ShowMessage()` 返回 `AppUI::Button::Ok`；
- “取消”必须让 `ShowMessage()` 返回 `AppUI::Button::Cancel`；
- 任一按钮未结束模态循环时，CTest 在 15 秒后判定失败。

可单独运行：

```bash
ctest --test-dir build-ci -R AutoSZUWebMacUITests --output-on-failure
```
