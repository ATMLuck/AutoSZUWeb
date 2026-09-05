#pragma once
#include "curl/curl.h"
#include <string>

bool Login(std::string Account, std::string Password);

/*
    TCP 连通性检测 —— 多目标冗余: 依次探测内置公共端点列表,
    任一可达即认为真实外网在线, 避免单一探测点失效造成误判。
    timeoutMs 为单个端点的超时(毫秒)
*/
bool NetworkCheck(int timeoutMs = 2000);