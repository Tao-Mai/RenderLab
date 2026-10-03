#pragma once

#include "asset/AssetManager.h"
#include "core/Context.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
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

template <AssetType T>
[[nodiscard]] typename T::ID nextId(const std::filesystem::path& source)
{
    std::string stem = source.stem().string();
    CHECK(!stem.empty(), "asset source has no filename stem: {}", source.string());
    DCHECK(context().assetManager);

    const typename T::ID baseId{stem};
    if (context().assetManager->find<T>(baseId) == nullptr)
    {
        return baseId;
    }

    for (uint32_t number = 1; number < std::numeric_limits<uint32_t>::max(); ++number)
    {
        typename T::ID id{stem + std::to_string(number)};
        if (context().assetManager->find<T>(id) == nullptr)
        {
            return id;
        }
    }
    LOG_FATAL("no available asset ID for {}", source.string());
}
}
