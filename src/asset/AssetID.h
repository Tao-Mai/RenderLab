#pragma once

#include <cstddef>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

template <class T>
struct AssetID
{
    std::string value;

    AssetID() = default;
    AssetID(std::string text) : value(std::move(text)) {}
    AssetID(const char* text) : value(text) {}

    [[nodiscard]] bool empty() const noexcept { return value.empty(); }
    [[nodiscard]] const char* c_str() const noexcept { return value.c_str(); }

    [[nodiscard]] bool operator==(const AssetID&) const = default;
};

namespace std
{
template <class T>
struct hash<AssetID<T>>
{
    [[nodiscard]] size_t operator()(const AssetID<T>& id) const noexcept
    {
        return hash<string>{}(id.value);
    }
};

template <class T>
struct formatter<AssetID<T>, char> : formatter<string_view, char>
{
    auto format(const AssetID<T>& id, format_context& context) const
    {
        return formatter<string_view, char>::format(id.value, context);
    }
};
}
