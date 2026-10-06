module;
#include <format>
#include <string>

export module format_validation;

export template<class T>
std::string formatValue(const T& value)
{
    return std::format("{}", value);
}
