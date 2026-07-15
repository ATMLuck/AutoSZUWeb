#pragma once
#include "curl/curl.h"
#include <string>

bool Login(std::string Account, std::string Password);

// TCP connect 测试目标可达性, timeoutMs 毫秒超时
bool NetworkCheck(const char* ip = "119.29.29.29", int port = 443, int timeoutMs = 2000);