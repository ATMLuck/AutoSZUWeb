#pragma once

#include <string>

namespace AppUI
{
    enum class Button { Ok, Cancel };

    // 跨平台消息框。文本和标题均使用 UTF-8。
    Button ShowMessage(const std::string& text,
                       const std::string& title = "提示",
                       bool allowCancel = false);

    // Windows 保留启动认证结果弹窗；macOS 后台认证仅写日志。
    bool ShouldShowAuthenticationResult();
}
