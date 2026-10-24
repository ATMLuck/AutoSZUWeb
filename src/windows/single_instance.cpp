#include "single_instance.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

#include <windows.h>

namespace fs = std::filesystem;

namespace
{
    std::wstring MutexNameForPath(const fs::path& lockPath)
    {

        const std::string value = fs::absolute(lockPath).u8string();
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned char byte : value)
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        return L"Local\\AutoSZUWeb.SingleInstance."
            + std::to_wstring(hash);
    }
}

struct Runtime::SingleInstanceGuard::Impl
{
    bool primary = false;
    HANDLE mutex = nullptr;
};

Runtime::SingleInstanceGuard::SingleInstanceGuard(const fs::path& lockPath)
    : impl_(std::make_unique<Impl>())
{
    const std::wstring mutexName = MutexNameForPath(lockPath);
    impl_->mutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (!impl_->mutex)
        return;

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(impl_->mutex);
        impl_->mutex = nullptr;
        return;
    }
    impl_->primary = true;
}

Runtime::SingleInstanceGuard::~SingleInstanceGuard()
{
    if (!impl_)
        return;
    if (impl_->mutex)
    {
        if (impl_->primary)
            ReleaseMutex(impl_->mutex);
        CloseHandle(impl_->mutex);
    }
}

bool Runtime::SingleInstanceGuard::IsPrimary() const
{
    return impl_ && impl_->primary;
}
