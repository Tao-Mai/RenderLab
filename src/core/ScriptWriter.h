#pragma once

#include "core/Scripting.h"

#include <algorithm>
#include <filesystem>
#include <string>

namespace Falcor
{
class ScriptWriter
{
public:
    struct VariableName
    {
        std::string name;
        explicit VariableName(const std::string& name) : name(name) {}
    };

    template <typename T>
    static std::string getArgString(const T& value)
    {
        Scripting::start();
        return ScriptBindings::repr(value);
    }

    static std::string makeFunc(const std::string& function) { return function + "()\n"; }

    template <class Arg, class... Args>
    static std::string makeFunc(const std::string& function, const Arg& first, const Args&... args)
    {
        auto result = function + '(' + getArgString(first);
        (result.append(", ").append(getArgString(args)), ...);
        return result + ")\n";
    }

    static std::string makeMemberFunc(const std::string& variable, const std::string& function)
    {
        return variable + '.' + makeFunc(function);
    }

    template <typename Arg, typename... Args>
    static std::string
    makeMemberFunc(const std::string& variable, const std::string& function, const Arg& first, const Args&... args)
    {
        return variable + '.' + makeFunc(function, first, args...);
    }

    static std::string makeGetProperty(const std::string& variable, const std::string& property)
    {
        return variable + '.' + property + '\n';
    }

    template <typename Arg>
    static std::string makeSetProperty(const std::string& variable, const std::string& property, const Arg& value)
    {
        return variable + '.' + property + " = " + getArgString(value) + '\n';
    }

    static std::string getPathString(const std::filesystem::path& path)
    {
        auto text = path.string();
        std::ranges::replace(text, '\\', '/');
        return text;
    }
};

template <>
inline std::string ScriptWriter::getArgString(const VariableName& value)
{
    return value.name;
}
}  // namespace Falcor
