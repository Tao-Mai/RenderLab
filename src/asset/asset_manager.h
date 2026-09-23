#pragma once

#include "asset/asset_desc.h"
#include "logger.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>

class AssetManager
{
public:
    AssetManager() = default;

    void init();
    void shutdown() noexcept;
    void reload();

    template <class T>
    [[nodiscard]] const T& desc(const AssetId& id) const
    {
        const auto& table = descMap<T>();
        const auto found = table.find(id);
        CHECK(found != table.end(), "descriptor not loaded: {}", id);
        return found->second;
    }

    template <class T>
    AssetId save(T desc);

    [[nodiscard]] std::filesystem::path path(const std::filesystem::path& relative) const;

private:
    using DescMaps = std::tuple<
        std::unordered_map<AssetId, MeshDesc>,
        std::unordered_map<AssetId, MaterialDesc>,
        std::unordered_map<AssetId, TextureDesc>,
        std::unordered_map<AssetId, ShaderDesc>,
        std::unordered_map<AssetId, SceneDesc>>;

    bool inited = false;
    std::filesystem::path root;
    DescMaps descs;

    void loadAll();
    void clear();

    template <class T>
    [[nodiscard]] std::unordered_map<AssetId, T>& descMap()
    {
        return std::get<std::unordered_map<AssetId, T>>(descs);
    }

    template <class T>
    [[nodiscard]] const std::unordered_map<AssetId, T>& descMap() const
    {
        return std::get<std::unordered_map<AssetId, T>>(descs);
    }
};
