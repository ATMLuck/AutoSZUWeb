# AutoSZUWeb macOS 移植方案

> 状态：已实施（2026-09-09）；完整验证步骤见 `docs/testing.md`
> 适用范围:将当前 Windows 平台实现(C++17 / MinGW / Win32 API)移植为 macOS 实现

---

## 1. 可行性结论回顾

**整体可行,工作量中等偏小。** 网络与协议层(约一半代码量)几乎 100% 可复用,需要重写的仅为一层薄薄的"系统胶水":弹窗、凭据存储、自启注册、路径解析、socket 启动。单人熟手预估约 1 周。

| 文件 | 性质 | 改动量 |
|---|---|---|
| srun.cpp / srun.h / web.h | 不动 | 0 |
| web.cpp | socket 分叉 + UI 调用 | ~60 行 |
| AutoSZUWeb.cpp | UI 调用 + sleep | ~20 行 |
| file_path.cpp | 换路径来源 | ~60 行 |
| sys.cpp | 分叉重写 | ~150 行 |
| mac_ui.mm(新) | NSAlert | ~35 行 |
| CMakeLists / Info.plist | 构建分叉 | 各 ~30 行 |

---

## 2. 移植总策略

### 2.1 全项目字符串统一为 UTF-8 窄串

现状存在两种写法:

- `srun.cpp` / `web.cpp` 已是 UTF-8 窄字面量(`"教学区..."`);
- `AutoSZUWeb.cpp` / `sys.cpp` 使用 `L"..."` 宽字面量直接进 `MessageBoxW`。

**做法**:把 `MessageBoxW` 替换为跨平台 UI 函数,所有宽字面量改回 UTF-8 窄字面量,Windows 端在新函数内部自行转宽。一套字符串两端通用。

### 2.2 平台差异收敛到 4 处

UI 弹窗、凭据加密、自启注册、时间/socket 启动。除 UI 需 1 个新的 `.mm` 文件外,其余在现有文件内用 `#ifdef _WIN32` / `__APPLE__` 分叉。

### 2.3 关键技术选型

| 能力 | Windows 现状 | macOS 对等 |
|---|---|---|
| 凭据加密 | DPAPI (`CryptProtectData`) | Keychain(Security.framework,纯 C API)+ OpenSSL AES-256-GCM |
| 自启 | 自复制到 AppData + 注册表 Run | LaunchAgent plist(`~/Library/LaunchAgents/`),不自复制 |
| 弹窗 | `MessageBoxW` | NSAlert(唯一需要 Obj-C++ 的部分) |
| 路径 | `SHGetFolderPathW` | `$HOME` 相对布局,纯 C++ `getenv("HOME")` |
| socket 启动 | `WSAStartup` + `ioctlsocket` | 无需初始化 + `fcntl(O_NONBLOCK)` |

Keychain(`SecItem*`)与 CommonCrypto 均为纯 C API,`.cpp` 可直接调用;**只有 NSAlert 需要 Obj-C 运行时**,故仅新增一个很小的 `mac_ui.mm`。

---

## 3. 目标文件结构变化

```
src/
  AutoSZUWeb.cpp      # 改 UI 调用 + Sleep
  srun.cpp            # 零改动
  web.cpp             # socket 层分叉 + UI 调用替换
  sys.cpp             # SetAutoStart/Encrypt/Decrypt 分叉 + 时间分叉
  file_path.cpp       # 路径来源换 getenv("HOME")
  mac_ui.mm           # ★新增:仅 NSAlert 弹窗(唯一 Obj-C++ 文件)
CMakeLists.txt        # 按平台分叉源文件/框架/链接库
Info.plist            # ★新增:macOS .app 配置(LSUIElement 后台态)
```

---

## 4. 逐文件改动明细

### 4.1 include/sys.h

新增跨平台 UI 函数声明(替代 Windows `MessageBoxW`):

```cpp
namespace AppUI
{
    enum class Button { Ok, Cancel };
    // 返回 Cancel 仅当用户点取消(供 newuser 的 MB_OKCANCEL 分支使用)
    Button ShowMessage(const std::string& text,
                      const std::string& title = "提示",
                      bool allowCancel = false);
}
```

其余声明(`SetAutoStart` / `EncryptStr` / `DecryptStr` / `WriteAuthLog`)签名不变,两端共用,仅实现分叉。需补一个 Windows 端实现(UTF-8→宽后调 `MessageBoxW`)。

### 4.2 include/file_path.h

**声明不变**,需重定义语义并在注释写明:

- `GetUsersFolderPath()`:Windows 返回 `%APPDATA%`(Roaming);mac 上**改为返回 `~/Library/Application Support`**(AutoSZUWeb 数据根目录)。名字略有误导但能保住所有调用点(后续都拼 `AutoSZUWeb/...`)。
- `GetDesktopPath()`:Windows `%USERPROFILE%\Desktop`;mac `~/Desktop`。
- `GetConfigPath()`:不变,目录统一 `Application Support/AutoSZUWeb/setting.json`;旧配置 `autoWEB.json` 迁移逻辑仅对 Windows 有意义(mac 无历史文件,自然跳过)。

### 4.3 include/web.h / include/srun.h

**零改动。** `Login` / `NetworkCheck` / `SrunLogin` 均为跨平台声明。

### 4.4 src/srun.cpp

**零改动。** 仅依赖 `curl.h`、nlohmann/json、OpenSSL `evp/hmac/sha`,无 Windows 头。XXTEA、自定义 Base64、HTTP 流程原样编译。约 1/4 代码量直接保留。

工程注意:链接的 curl / OpenSSL 需替换为 mac 版本(见 CMake 一节),且 curl 须以 OpenSSL 为 TLS 后端编译(Homebrew 默认满足)。

### 4.5 src/web.cpp

改动集中在 socket 层与 UI 调用,认证与探测逻辑不动。

**(a) 头文件分叉 + 轻量 socket 抽象(文件顶部)**

```cpp
#ifdef _WIN32
    #include <winsock2.h>
    #include <windows.h>
    #include <ws2tcpip.h>
    using SockT = SOCKET;
    inline void CloseSock(SockT s)
    {
        closesocket(s);
    }
    inline void SetNonBlock(SockT s)
    {
        u_long m = 1;
        ioctlsocket(s, FIONBIO, &m);
    }
    inline bool SockValid(SockT s)
    {
        return s != INVALID_SOCKET;
    }
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <cerrno>
    using SockT = int;
    inline void CloseSock(SockT s)
    {
        ::close(s);
    }
    inline void SetNonBlock(SockT s)
    {
        int fl = fcntl(s, F_GETFL, 0);
        fcntl(s, F_SETFL, fl | O_NONBLOCK);
    }
    inline bool SockValid(SockT s)
    {
        return s >= 0;
    }
#endif
```

**(b) `GetLocalIp()`(现 22–47 行)**

`SOCKET` / `INVALID_SOCKET` / `closesocket` 换成上方抽象。UDP `connect` + `getsockname` + `inet_ntop` 逻辑 BSD/mac 原生支持,代码体不变。

**(c) `Login()` 内两个 MessageBoxW(现 150–160 行)**

替换为:

```cpp
if (FirstBoot)
{
    std::string title = "提示";
    if (ok)
    {
        AppUI::ShowMessage(okMsg, title);
    }
    else
    {
        AppUI::ShowMessage("教学区认证失败: " + srunMsg + "\n宿舍区认证失败: " + dormMsg, title);
    }
}
```

文本本来就是 UTF-8 窄串,直接透传,不再需要 `Utf8ToWide`。

**(d) `ProbeTcp()`(现 178–211 行)**

`socket` → 抽象、`ioctlsocket(FIONBIO)` → `SetNonBlock`、`closesocket` → `CloseSock`。`select`、`getsockopt(SO_ERROR)`、`timeval` mac 完全相同。

**(e) `NetworkCheck()`(现 215–232 行)**

`WSAStartup` / `WSACleanup` 包进 `#ifdef _WIN32`,mac 分支不调用(BSD socket 无需初始化)。

**(f) 附注(非必须)**

`Login()` 每次调用 `curl_global_init/cleanup` 属低效但两端均正确,移植时不必顺手改。

### 4.6 src/sys.cpp

改动最重的一处。

**(a) `SetAutoStart()`(现 13–53 行)— 整体分叉**

- **Windows 分支**:保留现状(自复制 + 注册表 Run)。
- **macOS 分支**:写 LaunchAgent plist,不做自复制:

```cpp
#else // __APPLE__
void SetAutoStart()
{
    // 1. 取自身可执行路径: 打包运行时为 .app 内可执行文件, 直接指向它
    const char* exe = /* NSBundle mainBundle executablePath 或 argv[0] */;
    // 2. 写 ~/Library/LaunchAgents/com.autoszweb.AutoSZUWeb.plist
    std::string agent = std::string(getenv("HOME"))
                      + "/Library/LaunchAgents/com.autoszweb.AutoSZUWeb.plist";
    std::ofstream plist(agent);
    plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
          << "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
          << "<plist version=\"1.0\"><dict>\n"
          << "  <key>Label</key><string>com.autoszweb.AutoSZUWeb</string>\n"
          << "  <key>ProgramArguments</key><array><string>" << exe << "</string></array>\n"
          << "  <key>RunAtLoad</key><true/>\n"
          << "  <key>KeepAlive</key><false/>\n"   // 程序自身常驻循环, 无需 launchd 保活
          << "</dict></plist>\n";
    // 3. 不主动 bootstrap(避免当前进程外再拉一个实例); 下次登录由 launchd 自动拉起
}
#endif
```

模型差异:mac 不依赖"exe 落位 AppData"(`.app` 通常放 `/Applications`,当前正在运行就不需要搬),plist 指向正在运行的二进制路径即可。**"复制自己"这一步在 mac 上整体删除。**

**(b) `EncryptStr` / `DecryptStr`(现 56–100 行)— 语义对等替换**

- **Windows 分支**:保留 DPAPI。
- **macOS 分支**:当前用户登录钥匙串保管密钥 + OpenSSL AES-256-GCM:

```cpp
// 密钥获取：当前用户默认登录钥匙串中的 generic password
// service = "com.autoszuweb.AutoSZUWeb"
// CI 使用独立临时钥匙串和唯一 service 名，避免污染 runner 环境。
static std::string GetKey()
{
    /* SecItemCopyMatching / SecItemAdd 取 32B */
}

std::string EncryptStr(const std::string& plaintext)
{
    // IV 12B 随机 (SecRandomCopyBytes) + EVP_aes_256_gcm 加密 + tag 16B
    // 存储格式: Base64( IV || 密文 || tag ), 用 EVP_EncodeBlock(项目已在用)
    // 失败返回 "" —— 与 Windows 分支失败语义保持一致
}

std::string DecryptStr(const std::string& ciphertext)
{
    // 反向: 拆 IV → EVP_DecryptInit_ex(aes-256-gcm) → 校验 tag → 明文
}
```

注意:存储格式与 Windows 的 DPAPI blob 完全不同,但本就跨不了机(绑定不同密钥)。mac 用户为新配置,无兼容负担;`autoWEB.json` 迁移同样仅 Windows 有意义。

**(c) `WriteAuthLog()`(现 105–116 行)— 时间获取分叉**

```cpp
SYSTEMTIME st; GetLocalTime(&st);           // Windows 保留
// ── mac 分支 ──
time_t t = time(nullptr);
struct tm tmv;
localtime_r(&t, &tmv);                      // 之后按 tmv.tm_year... 填同构的 timeBuf/dateBuf
```

snprintf 拼时间、`ofstream` 追加写、路径 `GetUsersFolderPath()/AutoSZUWeb/logs` 两端共用。mac 日志落在 `~/Library/Application Support/AutoSZUWeb/logs/auth_YYYY-MM-DD.log`,纯 UTF-8 写。

### 4.7 src/file_path.cpp

**三函数整体分叉**,不再用 `SHGetFolderPathW` / `WideCharToMultiByte`。

mac 分支目录为固定 `$HOME` 相对布局,纯 C++ 即可,不需 Foundation:

```cpp
// GetUsersFolderPath(): 返回 std::string(getenv("HOME")) + "/Library/Application Support"
// GetDesktopPath():     返回 std::string(getenv("HOME")) + "/Desktop"
```

- `GetConfigPath()`(现 77–97 行):建目录逻辑共用;`autoWEB.json` 迁移包进 `#ifdef _WIN32`。
- Windows 分支原样保留。

### 4.8 新增 src/mac_ui.mm(唯一 Obj-C++ 文件)

实现 `AppUI::ShowMessage`,约 30 行,无 ARC(@autoreleasepool 手管):

```objc
#import <AppKit/AppKit.h>

AppUI::Button AppUI::ShowMessage(const std::string& text,
                          const std::string& title, bool allowCancel)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];               // 后台 agent 需自建 app 实例
        [NSApp activateIgnoringOtherApps:YES];           // accessory 态要前台化才可见

        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = [NSString stringWithUTF8String:title.c_str()];
        alert.informativeText = [NSString stringWithUTF8String:text.c_str()];
        [alert addButtonWithTitle:@"确定"];
        if (allowCancel)
        {
            [alert addButtonWithTitle:@"取消"];
        }
        NSModalResponse r = [alert runModal];            // runModal 自带模态事件循环

        return (allowCancel && r == NSAlertSecondButtonReturn)
             ? AppUI::Button::Cancel : AppUI::Button::Ok;
    }
}
```

Windows 端同函数实现放 sys.cpp 或独立 `.cpp`(UTF-8→宽→`MessageBoxW`,IDOK/IDCANCEL 映射)。

### 4.9 src/AutoSZUWeb.cpp

**逻辑零改动,只换 4 处 UI 调用 + 1 处休眠:**

- 引入 `AppUI::ShowMessage`,替换 4 个 `MessageBoxW`(现 58、66、87、100 行),宽字面量 `L"..."` 改为 UTF-8 窄字面量 `"..."`。
- 第 87 行的 `MB_OKCANCEL` 分支(现 90–93 行)映射:

```cpp
if (AppUI::ShowMessage("请打开桌面上的userdata.txt文件,并按照文件内提示写入"
                   "校园卡号和统一身份认证平台密码,并重新启动本程序。",
                   "提示", true) == AppUI::Button::Cancel)
{
    std::exit(0);
}
```

- 第 47 行 `Sleep(1000 * 10)` → `std::this_thread::sleep_for(std::chrono::milliseconds(10000))`(Windows 同样适用)。补 `#include <thread>` / `<chrono>`。
- `main()` / `mainwork()` /
ewuser()` 流程、`fs::path`、`std::remove` 跨平台,不动。

### 4.10 CMakeLists.txt

按平台分叉(`if(WIN32)` / `elseif(APPLE)`):

```cmake
set(SOURCES src/AutoSZUWeb.cpp src/web.cpp src/file_path.cpp src/srun.cpp)

if(WIN32)
    list(APPEND SOURCES src/sys.cpp)          # Windows 系统实现
    # windres 图标、-static -mwindows、ws2_32/crypt32/... 全留在此分支
elseif(APPLE)
    list(APPEND SOURCES src/sys.cpp src/mac_ui.mm)   # .mm 启用 Obj-C++
    enable_language(OBJCXX)
    find_package(CURL REQUIRED)               # Homebrew curl (OpenSSL 后端)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(AutoSZUWeb PRIVATE
        CURL::libcurl OpenSSL::SSL OpenSSL::Crypto
        "-framework Security"                 # Keychain
        "-framework Cocoa")                   # AppKit/Foundation (含 CommonCrypto)
    set_target_properties(AutoSZUWeb PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER com.autoszweb.AutoSZUWeb)
    # 生成 .app bundle: 自动带入 Info.plist
endif()
```

- 去掉 [lib/](lib/) 内全部 MinGW `.a` 的直链——仅在 WIN32 分支生效;mac 用 Homebrew/vcpkg 的静态或系统动态 curl/OpenSSL。
- nlohmann/json 为 header-only,建议 Git 子模块或 FetchContent。
- Windows 侧保留,让两端都能构建。`build.bat` 为纯 Windows 脚本,保留不动;mac 端用 `cmake ..` 即可。

### 4.11 新增 Info.plist + 打包

`.app` 的 `LSUIElement = true` 是对应 `-mwindows` 无窗口后台态的关键(无 Dock 图标、不出主窗口)。最小字段:

```xml
CFBundleExecutable  → AutoSZUWeb
CFBundleIdentifier  → com.autoszweb.AutoSZUWeb
LSUIElement         → true        ← 关键: 隐藏 Dock 图标
NSHighResolutionCapable → true
```

图标:Windows `.ico` 不适用,mac 需 `.icns`(可从原图转换,属可选项)。

---

## 5. mac 特有坑(决定成败的关键点)

### 5.1 TCC 桌面权限

macOS(Catalina 起)对非沙盒应用访问 `~/Desktop` 也会弹"允许访问桌面文件夹"授权。首次配置流程恰恰在桌面写/读 userdata.txt,第一次运行就会触发系统授权框。规避方案二选一:

- 接受一次性授权弹窗(最简单);或
- 将 userdata.txt 模板改写到 `$HOME/AutoSZUWeb_userdata.txt`(home 根目录不受 TCC 保护),改动仅在
ewuser()` 的路径选择上。

### 5.2 AppKit 主线程

mac 上所有 UI 调用必须在主线程。当前单线程模型天然满足(`mainwork` /
ewuser` 在主线程),但若将来把登录挪到工作线程,NSAlert 会崩,需 `dispatch_async(dispatch_get_main_queue(), ...)`。移植时保持现状即可。

### 5.3 启动方式决定弹窗能否显示

程序必须作为**用户级 LaunchAgent** 运行(而非 launchd 系统级),才有 Aqua GUI 会话权限弹 NSAlert;`mac_ui.mm` 内要 `[NSApplication sharedApplication]` 并激活。自启注册与弹窗依赖是同一件事,勿拆开配置。

---

## 6. 多平台验证策略与 Linux 测试桩(开发期无 Mac)

> 开发者只有 Windows / Linux 机器,无真 Mac。本方案把"程序在 mac 上的运行效果"拆成三层分别验证,其中仅最后一层的 1~2 小时验收才需临时租用真 Mac,开发期不依赖 Mac。

### 6.1 三层验证总览

| 层 | 验证内容 | 手段 | 成本 |
|---|---|---|---|
| ① 纯逻辑 | 状态机 / 协议 / socket / 路径逻辑 | 本机 Linux/Windows 编译运行 + Linux 测试桩 | $0 |
| ② 编译门禁 | mac 侧能否编译通过、.app 结构正确 | GitHub Actions `macos-latest`(免费 runner) | $0 |
| ③ 系统行为 | NSAlert / Keychain / LaunchAgent / Gatekeeper / TCC | 按小时租真 Mac(MacStadium / MacinCloud / AWS EC2 Mac) | 约 $1~2/时,收尾一次性 |

平台专属 API 已被刻意收敛到 4 处(弹窗 / Keychain / 自启 / 路径),天然支持这种分层验证。

### 6.2 Linux 测试桩设计

**动机**:`sys.cpp` 的 mac 分支依赖 Security.framework、`mac_ui.mm` 依赖 AppKit,Linux 均无对应头文件;而 `srun.cpp`、`web.cpp` 的 `#else` 分支本就是通用 POSIX(`sys/socket.h` 等),Linux 可直接编译。因此只需为"系统胶水"提供 Linux 桩,即可在本机跑通核心链路。

**平台分支三态化**:所有平台分叉点从 `_WIN32 / __APPLE__` 扩展出 `#elif defined(__linux__)` 测试桩分支:

```cpp
#ifdef _WIN32
    /* Windows 实现 */
#elif defined(__APPLE__)
    /* macOS 实现 */
#elif defined(__linux__)
    /* 测试桩: 仅参与本地逻辑回归编译, 不进入发布产物 */
#endif
```

**各函数桩行为**:

| 函数 | 桩实现 |
|---|---|
| `AppUI::ShowMessage` | 打印到 stdout,默认返回 `Ok`;测试可用注入的脚本化应答覆盖返回 `Cancel` |
| `EncryptStr` / `DecryptStr` | 固定测试密钥 + OpenSSL AES,或明文加平台标记——只用于本地回归,验证加解密往返一致性,不代表 Keychain 语义 |
| `SetAutoStart` | 打印将写入的 plist 路径后 no-op |
| `WriteAuthLog` 时间 | 无需桩——`localtime_r` 本就是 POSIX,与 mac 分支共用 |
| file_path.cpp | 无需额外桩——`getenv("HOME")` 已通用,Linux 下配置落到 `$HOME/...`,仅测试用,不追求与 mac 目录一致 |

**桩的隔离方式(二选一)**:
- 用宏开关 `-DAUTOSZUWEB_LINUX_TEST`,仅测试构建定义;
- 或将桩分支独立成 `src/sys_linux_test.cpp`,CMake 只对测试 target 追加。

推荐前者:改动集中在现有函数内,不新增文件。发布构建(Windows/macOS)永不定义该宏,桩代码不生效。

**不桩的内容**:`srun.cpp`、`web.cpp` 的协议与 socket 逻辑提供真实实现、真编译真测,确保 ① 层验证的是与发布版完全一致的代码路径。

### 6.3 行为 ↔ 验证手段映射

| 程序行为 | 验证手段 |
|---|---|
| 编译成功、.app bundle 结构 | ② GitHub Actions `macos-latest` |
| 主状态机、10s 轮询翻转 | ① Linux 桩运行 |
| SRun/ePortal 认证协议 | ① 校园网真机(平台无关;本次协议零改动,无回归风险) |
| NetworkCheck 多目标 TCP 探测 | ① Linux 真 socket 测试 |
| NSAlert 弹窗可见 | ③ 租真 Mac |
| Keychain 授权弹窗 / 加解密语义 | ③ 租真 Mac(桩只验往返,不验语义) |
| LaunchAgent 自启、注销重登拉起 | ③ 租真 Mac |
| .app 双击、Gatekeeper、TCC 桌面授权 | ③ 租真 Mac |

> ⚠️ 协议与校园网耦合:SRun/ePortal 依赖校内 172.30.255.42 / net.szu.edu.cn,**任何平台都只能在校园网内验证**。本次移植协议代码零改动,故不存在"mac 上协议写错"的回归,验收重心放在 mac 系统层即可。

### 6.4 GitHub Actions macOS 编译门禁(②层,免费)

用 `macos-latest` runner 提供持续的真 macOS 编译,防止开发期改坏 mac 构建。概念工作流:

```yaml
name: macos-build
on: [push, pull_request]
jobs:
  build:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v4
      - name: 安装依赖
        run: brew install curl openssl cmake
      - name: 构建
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
      - name: 运行无 GUI 逻辑测试
        run: ./build/<测试目标>   # Linux 桩逻辑同样可在 mac runner 上执行
```

### 6.5 推荐节奏

1. **开发期**:编码时同步补 `__linux__` 桩,本机跑核心链路回归;
2. **每次提交**:GitHub Actions 自动做 mac 编译门禁,把编译错误成本降到零;
3. **收尾**:代码冻结后,租 1~2 小时真 Mac,按 6.3 表完成 ③ 层全部系统行为验收。

---

## 7. 实施顺序建议

1. **CMakeLists / Info.plist / 目录结构**:先让空壳 .app 能在 mac 上构建运行,建立工具链基线。
2. **file_path.cpp + web.cpp socket 层**:纯替换,风险最低,先打通。
3. **AutoSZUWeb.cpp + UI 替换**:接入 `AppUI::ShowMessage`,去掉 `L""`。
4. **mac_ui.mm**:落地 NSAlert。
5. **sys.cpp 时间 + WriteAuthLog**:验证日志落盘。
6. **sys.cpp Keychain 加解密**:替换 DPAPI,验证账号配置读写。
7. **sys.cpp SetAutoStart**:写 LaunchAgent,完整验证常驻/自启/弹窗链路。
8. **端到端验证**:模拟断网重连、首启配置流程、TCC 授权处理。

---

## 8. 遗留风险清单

| 风险 | 影响 | 缓解 |
|---|---|---|
| TCC 桌面授权弹窗 | 首启流程受阻 | 改模板落点 / 接受一次性授权 |
| Homebrew curl/OpenSSL 版本漂移 | 行为不一致 | 固定版本或静态链入 |
| arm64 / x86_64 双架构 | 分发范围 | 出通用(universal)二进制 |
| Gatekeeper 签名 | 未签名后台程序被拦 | 文档引导用户右键打开/放行 |
| 无 Dock 图标退出方式 | 用户体验 | 文档说明用 `launchctl` / 活动监视器退出 |
