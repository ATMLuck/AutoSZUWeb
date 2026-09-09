#pragma once

#include <string>

namespace AppUI
{
    enum class Button { Ok, Cancel };

    // 跨平台消息框。文本和标题均使用 UTF-8。
    Button ShowMessage(const std::string& text,
                       const std::string& title = "提示",
                       bool allowCancel = false);
}
