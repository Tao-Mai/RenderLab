#pragma once

#include <cstdlib>
#include <format>
#include <utility>

#include <glog/logging.h>

#undef CHECK
#undef CHECK_EQ
#undef CHECK_NE
#undef CHECK_LT
#undef CHECK_LE
#undef CHECK_GT
#undef CHECK_GE
#undef CHECK_NOTNULL
#undef LOG
#undef VLOG
#undef DLOG
#undef DCHECK

namespace logger
{
inline void init(const char* argv0)
{
    google::InitGoogleLogging(argv0);
    FLAGS_logtostderr = true;
    FLAGS_colorlogtostderr = true;
}

namespace detail
{
template <class... Args>
void log(
    google::LogSeverity severity,
    const char* file,
    int line,
    std::format_string<Args...> fmt,
    Args&&... args)
{
    google::LogMessage(file, line, severity).stream()
        << std::format(fmt, std::forward<Args>(args)...);
}

template <class... Args>
[[noreturn]] void fatal(
    const char* file,
    int line,
    std::format_string<Args...> fmt,
    Args&&... args)
{
    google::LogMessage(file, line, google::GLOG_FATAL).stream()
        << std::format(fmt, std::forward<Args>(args)...);
    std::abort();
}

template <class... Args>
[[noreturn]] void checkFail(
    const char* file,
    int line,
    const char* condition,
    std::format_string<Args...> fmt,
    Args&&... args)
{
    google::LogMessage(file, line, google::GLOG_FATAL).stream()
        << "Check failed: (" << condition << ") "
        << std::format(fmt, std::forward<Args>(args)...);
    std::abort();
}
}
}

#define LOG_INFO(...) \
    ::logger::detail::log(google::GLOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) \
    ::logger::detail::log(google::GLOG_WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) \
    ::logger::detail::log(google::GLOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) ::logger::detail::fatal(__FILE__, __LINE__, __VA_ARGS__)

#define CHECK(condition, ...)                                            \
    do                                                                   \
    {                                                                    \
        if (!(condition))                                                \
        {                                                                \
            ::logger::detail::checkFail(                                 \
                __FILE__, __LINE__, #condition, __VA_ARGS__);            \
        }                                                                \
    } while (0)
