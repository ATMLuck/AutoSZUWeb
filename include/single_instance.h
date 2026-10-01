#pragma once

#include <filesystem>
#include <memory>

namespace Runtime
{
    class SingleInstanceGuard
    {
    public:
        explicit SingleInstanceGuard(const std::filesystem::path& lockPath);
        ~SingleInstanceGuard();

        SingleInstanceGuard(const SingleInstanceGuard&) = delete;
        SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;
        SingleInstanceGuard(SingleInstanceGuard&&) = delete;
        SingleInstanceGuard& operator=(SingleInstanceGuard&&) = delete;

        bool IsPrimary() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}