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
[[nodiscard]] inline std::filesystem::path sourcePathForAsset(const std::filesystem::path& source)
{
    const auto&     assetsRoot     = context().assetManager->rootPath();
    const auto      projectRoot    = assetsRoot.parent_path();
    const auto      absoluteSource = std::filesystem::absolute(source).lexically_normal();
    std::error_code error;
    const auto      fromProject = std::filesystem::relative(absoluteSource, projectRoot, error);
    if (!error && !fromProject.empty() && *fromProject.begin() != "..")
    {
        return std::filesystem::relative(absoluteSource, assetsRoot);
    }
    return absoluteSource;
}

[[nodiscard]] inline bool validId(std::string_view id)
{
    if (id.empty() || id.back() == '.' || id.back() == ' ' || id.find_first_of("<>:\"/\\|?*") != std::string_view::npos)
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


    const auto available = [](const typename T::ID& id)
    {
        return !context().assetManager->exists<T>(id);
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
