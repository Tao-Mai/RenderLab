#pragma once

#include "asset/AssetTypes.h"
#include "asset/BuiltinAssets.h"
#include "asset/MeshGeometry.h"
#include "core/Logger.h"

#include <filesystem>
#include <memory>
#include <span>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

class AssetManager
{
public:
    AssetManager() = default;
    ~AssetManager();

    void init(std::filesystem::path root);
    void shutdown() noexcept;

    [[nodiscard]] const std::filesystem::path& rootPath() const { return root; }

    [[nodiscard]] MeshGeometry readGeometry(const MeshAsset& asset) const;
    [[nodiscard]] std::filesystem::path writeGeometry(const MeshAsset::ID& id, const MeshGeometry& mesh) const;

    [[nodiscard]] std::vector<uint8_t> readTexture(const TextureAsset& asset) const;
    [[nodiscard]] std::filesystem::path writeTexture(const TextureAsset& asset, std::span<const uint8_t> bytes) const;

    template <AssetType T>
    [[nodiscard]] uint64_t contentRevision() const
    {
        return std::get<AssetRevisions<T>>(revisions).latest;
    }

    template <AssetType T>
    [[nodiscard]] const T& get(const AssetID<T>& id) const
    {
        const T* found = find<T>(id);
        CHECK(found != nullptr, "asset not loaded: {}", id);
        return *found;
    }

    template <AssetType T>
    [[nodiscard]] const T* find(const AssetID<T>& id) const
    {
        const auto& table = assetMap<T>();
        if (const auto found = table.find(id); found != table.end())
        {
            return found->second.get();
        }

        if (const T* builtin = BuiltinAssets::findAsset<T>(id))
        {
            return builtin;
        }

        return nullptr;
    }

    template <AssetType T>
    typename T::ID save(T asset)
    {
        return saveAsset<T>(std::make_unique<T>(std::move(asset)));
    }

    template <AssetType T>
    [[nodiscard]] std::vector<typename T::ID> loadedIds() const
    {
        std::vector<typename T::ID> ids;
        ids.reserve(assetMap<T>().size());
        for (const auto& [id, asset] : assetMap<T>())
        {
            ids.push_back(id);
        }
        return ids;
    }

    template <AssetType T>
    [[nodiscard]] uint64_t revision(const AssetID<T>& id) const
    {
        const auto& map   = std::get<AssetRevisions<T>>(revisions).byId;
        const auto  found = map.find(id);
        return found == map.end() ? 0 : found->second;
    }

private:
    template <AssetType T>
    struct AssetRevisions
    {
        std::unordered_map<typename T::ID, uint64_t> byId;
        uint64_t latest = 0;
    };
    AssetTypes::wrapTypes<AssetRevisions> revisions;
    uint64_t                           nextRevision = 0;
    std::filesystem::path              root;

    template <AssetType T>
    using AssetMap = std::unordered_map<typename T::ID, std::unique_ptr<T>>;

    using AssetMaps = AssetTypes::wrapTypes<AssetMap>;


    AssetMaps assets;

    void loadAll();
    void clear();
    [[nodiscard]] std::filesystem::path binaryPath(const std::filesystem::path& relative) const;

    template <AssetType T>
    typename T::ID saveAsset(std::unique_ptr<T> asset);

    template <AssetType T>
    [[nodiscard]] AssetMap<T>& assetMap()
    {
        return std::get<AssetMap<T>>(assets);
    }

    template <AssetType T>
    [[nodiscard]] const AssetMap<T>& assetMap() const
    {
        return std::get<AssetMap<T>>(assets);
    }
};
