// macOS 与 Linux 共用的 POSIX socket 实现。
#include "network.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

using SockT = int;
static constexpr SockT kInvalidSocket = -1;

namespace
{
    struct Probe { const char* ip; int port; };

    constexpr std::array<Probe, 4> kDefaultProbes = {{
        {"119.29.29.29", 53},
        {"223.5.5.5", 53},
        {"114.114.114.114", 53},
        {"8.8.8.8", 53},
    }};

    bool SocketStartup()
    {
        return true;
    }

    void SocketCleanup()
    {
    }

    void CloseSock(SockT socketHandle)
    {
        ::close(socketHandle);
    }

    bool SetNonBlocking(SockT socketHandle)
    {
        const int flags = fcntl(socketHandle, F_GETFL, 0);
        return flags >= 0 && fcntl(socketHandle, F_SETFL, flags | O_NONBLOCK) == 0;
    }
}

std::string GetLocalIp()
{
    if (!SocketStartup())
        return {};

    SockT socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == kInvalidSocket)
    {
        SocketCleanup();
        return {};
    }

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);

    std::string ip;
    if (connect(socketHandle, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) == 0)
    {
        sockaddr_in local{};
        socklen_t length = sizeof(local);
        if (getsockname(socketHandle, reinterpret_cast<sockaddr*>(&local), &length) == 0)
        {
            char buffer[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &local.sin_addr, buffer, sizeof(buffer)) &&
                std::strcmp(buffer, "0.0.0.0") != 0)
                ip = buffer;
        }
    }

    CloseSock(socketHandle);
    SocketCleanup();
    return ip;
}

bool ProbeTcpEndpoint(const std::string& ip, int port, int timeoutMs)
{
    if (ip.empty() || port <= 0 || port > 65535 || timeoutMs < 0)
        return false;
    if (!SocketStartup())
        return false;

    SockT socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == kInvalidSocket)
    {
        SocketCleanup();
        return false;
    }

    bool ok = false;
    if (SetNonBlocking(socketHandle))
    {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<unsigned short>(port));
        if (inet_pton(AF_INET, ip.c_str(), &address.sin_addr) == 1)
        {
            int connectResult = connect(socketHandle,
                reinterpret_cast<sockaddr*>(&address), sizeof(address));
            if (connectResult == 0)
            {
                ok = true;
            }
            else
            {
                fd_set writable;
                FD_ZERO(&writable);
                FD_SET(socketHandle, &writable);
                timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
                const int selectResult = select(socketHandle + 1, nullptr, &writable, nullptr, &timeout);
                if (selectResult > 0)
                {
                    int error = 0;
                    socklen_t length = sizeof(error);
                    if (getsockopt(socketHandle, SOL_SOCKET, SO_ERROR,
                            reinterpret_cast<char*>(&error), &length) == 0)
                        ok = (error == 0);
                }
            }
        }
    }

    CloseSock(socketHandle);
    SocketCleanup();
    return ok;
}

bool NetworkCheck(int timeoutMs)
{
    for (const Probe& probe : kDefaultProbes)
    {
        if (ProbeTcpEndpoint(probe.ip, probe.port, timeoutMs))
            return true;
    }
    return false;
}
