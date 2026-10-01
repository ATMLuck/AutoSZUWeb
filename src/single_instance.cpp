#include "single_instance.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{
#ifdef _WIN32
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
#endif
}

struct Runtime::SingleInstanceGuard::Impl
{
    bool primary = false;
#ifdef _WIN32
    HANDLE mutex = nullptr;
#else
    int descriptor = -1;
#endif
};

Runtime::SingleInstanceGuard::SingleInstanceGuard(const fs::path& lockPath)
    : impl_(std::make_unique<Impl>())
{
#ifdef _WIN32
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
#else
    std::error_code error;
    if (!lockPath.parent_path().empty())
        fs::create_directories(lockPath.parent_path(), error);
    if (error)
        return;

    impl_->descriptor = open(lockPath.c_str(), O_RDWR | O_CREAT, 0600);
    if (impl_->descriptor < 0)
        return;

    if (flock(impl_->descriptor, LOCK_EX | LOCK_NB) != 0)
    {
        close(impl_->descriptor);
        impl_->descriptor = -1;
        return;
    }

    // The file is diagnostic only; flock, not file existence, owns the lock.
    if (ftruncate(impl_->descriptor, 0) == 0)
    {
        const std::string pid = std::to_string(static_cast<long long>(getpid())) + "\n";
        (void)write(impl_->descriptor, pid.data(), pid.size());
    }
    impl_->primary = true;
#endif
}

Runtime::SingleInstanceGuard::~SingleInstanceGuard()
{
    if (!impl_)
        return;
#ifdef _WIN32
    if (impl_->mutex)
    {
        if (impl_->primary)
            ReleaseMutex(impl_->mutex);
        CloseHandle(impl_->mutex);
    }
#else
    if (impl_->descriptor >= 0)
    {
        if (impl_->primary)
            flock(impl_->descriptor, LOCK_UN);
        close(impl_->descriptor);
    }
#endif
}

bool Runtime::SingleInstanceGuard::IsPrimary() const
{
    return impl_ && impl_->primary;
}
