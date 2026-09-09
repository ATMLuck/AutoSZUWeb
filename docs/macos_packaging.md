# 构建可分发的 macOS 安装包

项目提供两个 macOS 脚本：

- `build_macos.sh`：编译并测试，生成开发用 `.app`；
- `package_macos.sh`：把第三方动态库嵌入 `.app`、签名并生成 DMG。

## 1. 无 Apple 开发者证书：自包含测试 DMG

在 Apple Silicon Mac 上执行：

```bash
brew install cmake ninja curl openssl@3 nlohmann-json dylibbundler
chmod +x build_macos.sh package_macos.sh
./build_macos.sh
./package_macos.sh
```

产物示例：

```text
dist/macos/AutoSZUWeb-1.3.0-macOS-arm64.dmg
```

该 DMG 已包含 Homebrew 动态库，目标 Mac 不需要安装 Homebrew。但它只使用 ad-hoc 签名，首次使用需要：

1. 双击 DMG；
2. 将 `AutoSZUWeb.app` 拖入 Applications；
3. 在 Finder 中右键应用并选择“打开”；
4. 再次确认打开。

这属于测试包，不能作为“用户可无警告直接安装”的正式发行版。

## 2. 有 Apple Developer Program：正式签名 DMG

要求钥匙串中已安装有效的 `Developer ID Application` 证书。查看身份：

```bash
security find-identity -v -p codesigning
```

设置签名身份：

```bash
export MACOS_SIGN_IDENTITY='Developer ID Application: Your Name (TEAMID)'
```

只生成签名 DMG：

```bash
./build_macos.sh
./package_macos.sh
```

验证：

```bash
codesign --verify --deep --strict --verbose=2 \
  dist/macos/work/AutoSZUWeb.app
```

脚本正常结束后工作目录会删除，因此通常直接验证 DMG：

```bash
codesign --verify --verbose=2 \
  dist/macos/AutoSZUWeb-1.3.0-macOS-arm64.dmg
```

## 3. 签名并提交 Apple 公证

准备：

- Apple Developer Program 账号；
- Developer ID Application 证书；
- Apple ID；
- Apple ID 的 app-specific password；
- 10 位 Team ID。

执行：

```bash
export MACOS_SIGN_IDENTITY='Developer ID Application: Your Name (TEAMID)'
export APPLE_ID='your-apple-id@example.com'
export APPLE_APP_SPECIFIC_PASSWORD='xxxx-xxxx-xxxx-xxxx'
export APPLE_TEAM_ID='TEAMID1234'

./build_macos.sh
./package_macos.sh
```

脚本会自动执行：

1. 递归嵌入非系统动态库；
2. 把加载路径改为 `@executable_path/../Frameworks`；
3. 检查不能残留 `/opt/homebrew`、`/usr/local` 等外部依赖；
4. 从内到外签名动态库和 `.app`；
5. 生成压缩 DMG；
6. 签名 DMG；
7. 使用 `notarytool` 上传并等待公证；
8. 使用 `stapler` 装订票据；
9. 使用 Gatekeeper 验证最终 DMG。

最终产物：

```text
dist/macos/AutoSZUWeb-1.3.0-macOS-arm64.dmg
```

## 4. 在另一台干净 Mac 验证

不要只在构建机验证。将 DMG 下载到未安装 Homebrew、从未运行 AutoSZUWeb 的 Mac：

```bash
xcrun stapler validate AutoSZUWeb-1.3.0-macOS-arm64.dmg
spctl --assess --type open \
  --context context:primary-signature --verbose=4 \
  AutoSZUWeb-1.3.0-macOS-arm64.dmg
```

然后：

1. 双击 DMG；
2. 拖入 Applications；
3. 双击应用；
4. 确认没有“无法验证开发者”警告；
5. 完成桌面权限、Keychain、校园认证、断网重连和注销重登测试。

## 5. 架构范围

当前 GitHub `macos-14` 构建通常生成 `arm64` 包，只能原生支持 Apple Silicon。检查：

```bash
file dist/macos/work/AutoSZUWeb.app/Contents/MacOS/AutoSZUWeb
```

要支持 Intel Mac，需要额外生成 `x86_64` 版本，并确保所有 Homebrew 依赖也具有同一架构；不能仅给主程序添加 `-arch x86_64`。如需同时支持两类 Mac，建议分别发布：

```text
AutoSZUWeb-1.3.0-macOS-arm64.dmg
AutoSZUWeb-1.3.0-macOS-x86_64.dmg
```

或在同时具备双架构依赖后再合成 universal2。