#pragma once

#include "network.h"
#include <string>

bool Login(const std::string& account, const std::string& password);

// message 返回认证结果描述：成功时为成功信息，失败时为教学区/宿舍区合并原因。
bool Login(const std::string& account, const std::string& password, std::string& message);
