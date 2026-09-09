#pragma once

#include <chrono>
#include <functional>

namespace Runtime
{
    enum class ConnectionState { Online, Offline };

    struct MonitorCallbacks
    {
        std::function<bool()> login;
        std::function<bool()> networkCheck;
        std::function<void(std::chrono::milliseconds)> sleep;
        std::function<bool()> shouldStop;
    };

    ConnectionState RunIteration(ConnectionState state,
                                 const MonitorCallbacks& callbacks);

    void RunMonitorLoop(ConnectionState initialState,
                        const MonitorCallbacks& callbacks,
                        std::chrono::milliseconds interval =
                            std::chrono::seconds(10));
}
