#pragma once

#include "asset/AssetManager.h"
#include "core/Context.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

namespace asset_import
{
[[nodiscard]] inline std::filesystem::path sourcePathForAsset(
    const std::filesystem::path& source)
{
    const auto assetsRoot = context().config->paths().assets;
    const auto projectRoot = assetsRoot.parent_path();
    const auto absoluteSource = std::filesystem::absolute(source).lexically_normal();
    std::error_code error;
    const auto fromProject = std::filesystem::relative(absoluteSource, projectRoot, error);
    if (!error && !fromProject.empty() && *fromProject.begin() != "..")
    {
        return std::filesystem::relative(absoluteSource, assetsRoot);
    }
    return absoluteSource;
}

[[nodiscard]] inline bool validId(std::string_view id)
{
    if (id.empty() || id.back() == '.' || id.back() == ' ' ||
        id.find_first_of("<>:\"/\\|?*") != std::string_view::npos)
        return false;

    for (unsigned char character : id)
        if (character < 32)
            return false;

    return true;
}

template <AssetType T>
[[nodiscard]] typename T::ID nextId(const std::string& baseName)
{
    CHECK(validId(baseName), "invalid asset ID: {}", baseName);
    DCHECK(context().assetManager);
    DCHECK(context().config);

    const auto available = [](const typename T::ID& id)
    {
        if (context().assetManager->find<T>(id))
            return false;

        const auto      file = context().config->paths().assets / T::dir / (id.value + ".json");
        std::error_code error;
        const bool      exists = std::filesystem::exists(file, error);
        CHECK(!error, "cannot check asset ID '{}': {}", id.value, error.message());
        return !exists;
    };

    const typename T::ID baseId{baseName};
    if (available(baseId))
    {
        return baseId;
    }

    for (uint32_t number = 1; number < std::numeric_limits<uint32_t>::max(); ++number)
    {
        typename T::ID id{baseName + std::to_string(number)};
        if (available(id))
        {
            return id;
        }
    }
    LOG_FATAL("no available asset ID for {}", baseName);
}
}  // namespace asset_import
