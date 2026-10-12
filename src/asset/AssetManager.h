#pragma once

#include "asset/AssetTypes.h"
#include "asset/BuiltinAssets.h"
#include "core/Logger.h"
#include "core/Serializer.h"

#include <filesystem>
#include <tuple>
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
    [[nodiscard]] uint64_t contentRevision() const { return nextRevision; }

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
            return &found->second;
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
        CHECK(!asset.id.empty(), "asset has empty id");
        const auto file = root / T::dir / (asset.id.value + ".json");
        asset_json::save(file, asset);

        const typename T::ID id = asset.id;
        assetMap<T>().insert_or_assign(id, std::move(asset));
        std::get<RevisionMap<T>>(revisions)[id] = ++nextRevision;
        return id;
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
        const auto& map   = std::get<RevisionMap<T>>(revisions);
        const auto  found = map.find(id);
        return found == map.end() ? 0 : found->second;
    }

private:
    template <AssetType T>
    using RevisionMap = std::unordered_map<typename T::ID, uint64_t>;
    AssetTypes::wrapTypes<RevisionMap> revisions;
    uint64_t                           nextRevision = 0;
    std::filesystem::path              root;

    template <AssetType T>
    using AssetMap = std::unordered_map<typename T::ID, T>;

    using AssetMaps = AssetTypes::wrapTypes<AssetMap>;

    DEBUG_ONLY(bool inited = false;)
    AssetMaps assets;

    void loadAll();
    void clear();

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
