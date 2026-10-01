#include "runtime.h"

#include <stdexcept>

namespace Runtime
{
    ConnectionState RunIteration(ConnectionState state,
                                 const MonitorCallbacks& callbacks)
    {
        if (state == ConnectionState::Online)
        {
            if (!callbacks.networkCheck)
                throw std::invalid_argument("networkCheck callback is required");
            return callbacks.networkCheck()
                ? ConnectionState::Online : ConnectionState::Offline;
        }

        if (!callbacks.login)
            throw std::invalid_argument("login callback is required");
        return callbacks.login()
            ? ConnectionState::Online : ConnectionState::Offline;
    }

    void RunMonitorLoop(ConnectionState initialState,
                        const MonitorCallbacks& callbacks,
                        std::chrono::milliseconds interval)
    {
        if (!callbacks.sleep || !callbacks.shouldStop)
            throw std::invalid_argument("sleep and shouldStop callbacks are required");

        ConnectionState state = initialState;
        while (!callbacks.shouldStop())
        {
            state = RunIteration(state, callbacks);
            if (!callbacks.shouldStop())
                callbacks.sleep(interval);
        }
    }
}
