#pragma once

#include <string>

// 探测指定 IPv4 TCP 端点；主要用于 NetworkCheck 和回环测试。
bool ProbeTcpEndpoint(const std::string& ip, int port, int timeoutMs = 2000);

// 多目标外网连通性检测，任一内置公共端点可达即返回 true。
bool NetworkCheck(int timeoutMs = 2000);

// 通过路由选择取得当前 IPv4 出口地址；失败返回空字符串。
std::string GetLocalIp();
