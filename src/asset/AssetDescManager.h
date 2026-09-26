#pragma once

#include "asset/AssetDesc.h"
#include "asset/BuiltinAssets.h"
#include "asset/JsonIo.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <filesystem>
#include <tuple>
#include <unordered_map>
#include <utility>

class AssetDescManager
{
public:
    AssetDescManager() = default;

    void init();
    void shutdown() noexcept;

    template <class T>
    [[nodiscard]] const T& desc(const AssetId& id) const
    {
        const T* found = findDesc<T>(id);
        CHECK(found != nullptr, "descriptor not loaded: {}", id);
        return *found;
    }

    template <class T>
    [[nodiscard]] const T* findDesc(const AssetId& id) const
    {
        auto& table = descMap<T>();
        if (const auto found = table.find(id); found != table.end())
        {
            return &found->second;
        }

        if (const T* builtin = BuiltinAssets::makeDesc<T>(id))
        {
            return builtin;
        }

        return nullptr;
    }

    template <class T>
    AssetId save(T desc)
    {
        CHECK(!desc.id.empty(), "descriptor has empty id");
        const auto file =
            descriptorRoot() / AssetTypes::dir<T>() / (desc.id + ".json");
        asset_json::save(file, desc);
        const AssetId id = desc.id;
        descMap<T>().insert_or_assign(id, std::move(desc));
        return id;
    }

private:
    template <class T>
    using DescMap = std::unordered_map<AssetId, T>; // rehash 不会改变元素地址

    using DescMaps = AssetTypes::wrapTypes<DescMap>;

    bool             inited = false;
    mutable DescMaps assetDescs;

    void                             loadAll();
    void                             clear();
    static std::filesystem::path     descriptorRoot();

    template <class T>
    [[nodiscard]] DescMap<T>& descMap() const
    {
        return std::get<DescMap<T>>(assetDescs);
    }
};
