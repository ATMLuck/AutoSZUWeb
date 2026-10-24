// macOS 与 Linux 共用的 POSIX flock 实现。
#include "single_instance.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace fs = std::filesystem;

struct Runtime::SingleInstanceGuard::Impl
{
    bool primary = false;
    int descriptor = -1;
};

Runtime::SingleInstanceGuard::SingleInstanceGuard(const fs::path& lockPath)
    : impl_(std::make_unique<Impl>())
{
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
}

Runtime::SingleInstanceGuard::~SingleInstanceGuard()
{
    if (!impl_)
        return;
    if (impl_->descriptor >= 0)
    {
        if (impl_->primary)
            flock(impl_->descriptor, LOCK_UN);
        close(impl_->descriptor);
    }
}

bool Runtime::SingleInstanceGuard::IsPrimary() const
{
    return impl_ && impl_->primary;
}
