#include "app_ui.h"

#include <windows.h>

#include <string>

namespace
{
    std::wstring Utf8ToWide(const std::string& text)
    {
        if (text.empty())
            return {};
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (length <= 0)
            return {};
        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            text.data(), static_cast<int>(text.size()), wide.data(), length);
        return wide;
    }
}

AppUI::Button AppUI::ShowMessage(const std::string& text, const std::string& title,
                          bool allowCancel)
{
    const int result = MessageBoxW(nullptr, Utf8ToWide(text).c_str(),
        Utf8ToWide(title).c_str(),
        (allowCancel ? MB_OKCANCEL : MB_OK) | MB_ICONINFORMATION);
    return allowCancel && result == IDCANCEL ? Button::Cancel : Button::Ok;
}

bool AppUI::ShouldShowAuthenticationResult()
{
    return true;
}
