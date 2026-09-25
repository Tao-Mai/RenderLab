#pragma once

#include "asset/AssetDescManager.h"
#include "core/Context.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

namespace asset_import
{
[[nodiscard]] inline std::filesystem::path sourceRelativeToAssets(
    const std::filesystem::path& source)
{
    const auto assetsRoot = context().config->paths().assets;
    const auto projectRoot = assetsRoot.parent_path();
    const auto absoluteSource = std::filesystem::absolute(source).lexically_normal();
    const auto fromProject = std::filesystem::relative(absoluteSource, projectRoot);
    CHECK(!fromProject.empty() && *fromProject.begin() != "..",
          "asset source is outside project root: {}", source.string());
    return std::filesystem::relative(absoluteSource, assetsRoot);
}

template <class T>
[[nodiscard]] AssetId nextId(const std::filesystem::path& source)
{
    std::string stem = source.stem().string();
    CHECK(!stem.empty(), "asset source has no filename stem: {}", source.string());
    CHECK(context().assetManager != nullptr, "AssetDescManager is not initialized");

    if (context().assetManager->findDesc<T>(stem) == nullptr)
    {
        return stem;
    }

    for (uint32_t number = 1; number < std::numeric_limits<uint32_t>::max(); ++number)
    {
        AssetId id = stem + std::to_string(number);
        if (context().assetManager->findDesc<T>(id) == nullptr)
        {
            return id;
        }
    }
    LOG_FATAL("no available asset ID for {}", source.string());
}
}
