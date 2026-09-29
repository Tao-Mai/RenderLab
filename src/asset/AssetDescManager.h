#pragma once

#include "asset/Asset.h"
#include "asset/BuiltinAssets.h"
#include "asset/JsonIo.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <filesystem>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

class AssetDescManager
{
public:
    AssetDescManager() = default;

    void init();
    void shutdown() noexcept;

    template <AssetType T>
    [[nodiscard]] const typename T::Desc& desc(const AssetID<T>& id) const
    {
        const typename T::Desc* found = findDesc<T>(id);
        CHECK(found != nullptr, "descriptor not loaded: {}", id);
        return *found;
    }

    template <AssetType T>
    [[nodiscard]] const typename T::Desc* findDesc(const AssetID<T>& id) const
    {
        auto& table = descMap<T>();
        if (const auto found = table.find(id); found != table.end())
        {
            return &found->second;
        }

        if (const typename T::Desc* builtin = BuiltinAssets::makeDesc<T>(id))
        {
            return builtin;
        }

        return nullptr;
    }

    template <AssetType T>
    typename T::ID save(typename T::Desc desc)
    {
        CHECK(!desc.id.empty(), "descriptor has empty id");
        const auto file =
            descriptorRoot() / T::dir / (desc.id.value + ".json");
        asset_json::save(file, desc);
        const typename T::ID id = desc.id;
        descMap<T>().insert_or_assign(id, std::move(desc));
        return id;
    }

    template <AssetType T>
    [[nodiscard]] std::vector<typename T::ID> loadedIds() const
    {
        std::vector<typename T::ID> ids;
        ids.reserve(descMap<T>().size());
        for (const auto& [id, desc] : descMap<T>())
        {
            ids.push_back(id);
        }
        return ids;
    }

private:
    template <AssetType T>
    using DescMap = std::unordered_map<typename T::ID, typename T::Desc>;

    using DescMaps = AssetTypes::wrapTypes<DescMap>;

    bool             inited = false;
    mutable DescMaps assetDescs;

    void                         loadAll();
    void                         clear();
    static std::filesystem::path descriptorRoot();

    template <AssetType T>
    [[nodiscard]] DescMap<T>& descMap() const
    {
        return std::get<DescMap<T>>(assetDescs);
    }
};
