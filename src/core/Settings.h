#pragma once

#include "core/Serializer.h"

#include <optional>
#include <string>
#include <string_view>

namespace settings
{
class Attributes
{
public:
    template <typename T>
    [[nodiscard]] std::optional<T> get(std::string_view name) const
    {
        const auto found = mJsonDict.find(name);
        if (found == mJsonDict.end() || found->is_null())
            return {};

        if constexpr (std::is_same_v<T, bool>)
        {
            if (found->is_number_integer())
                return found->get<int64_t>() != 0;
        }
        else if constexpr (std::is_arithmetic_v<T>)
        {
            if (found->is_boolean())
                return static_cast<T>(found->get<bool>());
        }

        T value{};
        Deserialize(*found, value);
        return value;
    }

    template <typename T>
    [[nodiscard]] T get(std::string_view name, const T& fallback) const
    {
        return get<T>(name).value_or(fallback);
    }

    [[nodiscard]] bool has(std::string_view name) const { return mJsonDict.contains(name); }
    void addDict(nlohmann::json jsonDict);
    void clear() { mJsonDict = nlohmann::json::object(); }
    void removePrefix(std::string_view prefix);

private:
    nlohmann::json mJsonDict = nlohmann::json::object();

    friend void to_json(nlohmann::json& value, const Attributes& attributes) { value = attributes.mJsonDict; }
};

namespace detail
{
[[nodiscard]] nlohmann::json flattenDictionary(const nlohmann::json& dict);
}

// Option names use ':' to separate scopes.
[[nodiscard]] std::string encodeName(std::string_view name);
[[nodiscard]] std::string decodeName(std::string_view name);
}  // namespace settings

class Settings
{
public:
    using Options = settings::Attributes;

    [[nodiscard]] const Options& getOptions() const { return mOptions; }

    template <typename T>
    [[nodiscard]] std::optional<T> getOption(std::string_view name) const
    {
        return mOptions.get<T>(name);
    }

    template <typename T>
    [[nodiscard]] T getOption(std::string_view name, const T& fallback) const
    {
        return mOptions.get(name, fallback);
    }

    void addOption(std::string_view name, const nlohmann::json& value);
    void addOptions(const nlohmann::json& options);
    void clearOptions() { mOptions.clear(); }

private:
    Options mOptions;
};
