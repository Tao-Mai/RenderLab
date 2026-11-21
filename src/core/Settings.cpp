#include "core/Settings.h"

#include <utility>

namespace settings
{
namespace detail
{
namespace
{
void flattenDictionary(const nlohmann::json& dict, const std::string& prefix, nlohmann::json& result)
{
    if (!dict.is_object())
    {
        result[prefix] = dict;
        return;
    }

    for (const auto& [name, value] : dict.items())
        flattenDictionary(value, prefix.empty() ? name : prefix + ':' + name, result);
}
}

nlohmann::json flattenDictionary(const nlohmann::json& dict)
{
    CHECK(dict.is_object(), "settings options must be an object");
    auto result = nlohmann::json::object();
    flattenDictionary(dict, {}, result);
    return result;
}
}  // namespace detail

void Attributes::addDict(nlohmann::json jsonDict)
{
    DCHECK(jsonDict.is_object());
    for (auto& [name, value] : jsonDict.items())
        mJsonDict[name] = std::move(value);
}

void Attributes::removePrefix(std::string_view prefix)
{
    auto& values = mJsonDict.get_ref<nlohmann::json::object_t&>();
    auto it = values.lower_bound(prefix);
    while (it != values.end() && it->first.starts_with(prefix))
        it = values.erase(it);
}

std::string encodeName(std::string_view name)
{
    std::string encoded;
    encoded.reserve(name.size());
    for (const auto ch : name)
    {
        if (ch == '%')
            encoded += "%25";
        else if (ch == ':')
            encoded += "%3A";
        else
            encoded += ch;
    }
    return encoded;
}

std::string decodeName(std::string_view name)
{
    std::string decoded;
    decoded.reserve(name.size());
    while (!name.empty())
    {
        if (name.starts_with("%25"))
        {
            decoded += '%';
            name.remove_prefix(3);
        }
        else if (name.starts_with("%3A"))
        {
            decoded += ':';
            name.remove_prefix(3);
        }
        else
        {
            decoded += name.front();
            name.remove_prefix(1);
        }
    }
    return decoded;
}
}  // namespace settings

void Settings::addOption(std::string_view name, const nlohmann::json& value)
{
    addOptions(nlohmann::json{{name, value}});
}

void Settings::addOptions(const nlohmann::json& options)
{
    auto flattened = settings::detail::flattenDictionary(options);
    for (const auto& [name, value] : flattened.items())
        mOptions.removePrefix(name + ':');
    mOptions.addDict(std::move(flattened));
}
