#pragma once

#include "asset/AssetDescManager.h"
#include "core/Context.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

namespace asset_import
{
template <class T>
[[nodiscard]] AssetId nextId(const std::filesystem::path& source)
{
    std::string stem = source.stem().string();
    CHECK(!stem.empty(), "asset source has no filename stem: {}", source.string());
    CHECK(context().assetManager != nullptr, "AssetDescManager is not initialized");

    if (!isBuiltin<T>(stem) && context().assetManager->findDesc<T>(stem) == nullptr)
    {
        return stem;
    }

    for (uint32_t number = 1; number < std::numeric_limits<uint32_t>::max(); ++number)
    {
        AssetId id = stem + std::to_string(number);
        if (!isBuiltin<T>(id) && context().assetManager->findDesc<T>(id) == nullptr)
        {
            return id;
        }
    }
    LOG_FATAL("no available asset ID for {}", source.string());
}
}
