#include "app_ui.h"

#include <cstdlib>
#include <iostream>
#include <string>

// Linux 仅为开发期逻辑测试桩（build.sh），无图形界面。
AppUI::Button AppUI::ShowMessage(const std::string& text, const std::string& title,
                          bool allowCancel)
{
    std::cout << "[" << title << "] " << text << std::endl;
    const char* response = std::getenv("AUTOSZUWEB_UI_RESPONSE");
    return allowCancel && response && std::string(response) == "cancel"
        ? Button::Cancel : Button::Ok;
}

bool AppUI::ShouldShowAuthenticationResult()
{
    return false;
}
