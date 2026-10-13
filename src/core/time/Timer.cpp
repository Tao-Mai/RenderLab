#include "core/time/Timer.h"

Timer::Timer() noexcept : started(Clock::now()) {}
void Timer::reset() noexcept { started = Clock::now(); }

double Timer::elapsedMilliseconds() const noexcept
{
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

double Timer::restartMilliseconds() noexcept
{
    const auto   now     = Clock::now();
    const double elapsed = std::chrono::duration<double, std::milli>(now - started).count();
    started              = now;
    return elapsed;
}
