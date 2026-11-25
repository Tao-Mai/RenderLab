#pragma once

class NonCopyable
{
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;

    NonCopyable(NonCopyable&&) noexcept            = default;
    NonCopyable& operator=(NonCopyable&&) noexcept = default;

private:
    NonCopyable(const NonCopyable&)            = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};
