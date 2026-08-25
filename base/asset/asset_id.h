#pragma once

#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>

using AssetId = uint64_t;

constexpr AssetId kInvalidAssetId = 0;

[[nodiscard]] inline std::string format_asset_id(AssetId id)
{
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(id));
    return buf;
}

[[nodiscard]] inline std::optional<AssetId> parse_asset_id(std::string_view text)
{
    if (text.empty())
        return std::nullopt;
    if (text.size() > 2 && (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')))
        text.remove_prefix(2);
    if (text.size() > 16)
        return std::nullopt;

    AssetId value = 0;
    for (const char c : text)
    {
        value <<= 4;
        if (c >= '0' && c <= '9')
            value |= static_cast<AssetId>(c - '0');
        else if (c >= 'a' && c <= 'f')
            value |= static_cast<AssetId>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            value |= static_cast<AssetId>(c - 'A' + 10);
        else
            return std::nullopt;
    }
    return value;
}

[[nodiscard]] inline AssetId generate_asset_id()
{
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    AssetId id = 0;
    do
    {
        id = rng();
    } while (id == kInvalidAssetId);
    return id;
}
