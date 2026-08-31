#pragma once
#include <string>

// 教学/办公区 SRun challenge 认证 (net.szu.edu.cn)。
// 成功返回 true; 失败返回 false 并填充 message 描述原因。
bool SrunLogin(const std::string& account, const std::string& password, std::string& message);
