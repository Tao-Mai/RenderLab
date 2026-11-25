#pragma once

#include <chrono>

class Timer
{
public:
    Timer() noexcept;
    void                 reset() noexcept;
    [[nodiscard]] double elapsedMilliseconds() const noexcept;
    [[nodiscard]] double restartMilliseconds() noexcept;

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point started;
};
