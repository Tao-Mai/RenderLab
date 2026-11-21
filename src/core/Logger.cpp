#include "core/Logger.h"

namespace
{
thread_local std::string* errorLog = nullptr;
}

void logger::detail::logMessage(google::LogSeverity severity, const char* file, int line, std::string_view message)
{
    if (severity == google::GLOG_ERROR && errorLog)
    {
        errorLog->append(message);
        if (errorLog->empty() || errorLog->back() != '\n')
            errorLog->push_back('\n');
        return;
    }
    google::LogMessage(file, line, severity).stream() << message;
}

logger::ScopedErrorLog::ScopedErrorLog(std::string& log) : previous(std::exchange(errorLog, &log)) {}
logger::ScopedErrorLog::~ScopedErrorLog() { errorLog = previous; }

bool logger::ScopedErrorLog::isActive() { return errorLog != nullptr; }
bool logger::ScopedErrorLog::hasErrors() { return isActive() && !errorLog->empty(); }
