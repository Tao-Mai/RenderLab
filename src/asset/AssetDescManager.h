#pragma once

#include "asset/AssetDesc.h"
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
        const auto& table = descMap<T>();
        const auto  found = table.find(id);
        CHECK(found != table.end(), "descriptor not loaded: {}", id);
        return found->second;
    }

    template <class T>
    [[nodiscard]] const T* findDesc(const AssetId& id) const
    {
        const auto& table = descMap<T>();
        const auto  found = table.find(id);
        return found != table.end() ? &found->second : nullptr;
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

    // 仅注册到内存，不落盘（用于 builtin 等运行时描述符）。
    template <class T>
    void add(T desc)
    {
        CHECK(!desc.id.empty(), "descriptor has empty id");
        descMap<T>().insert_or_assign(desc.id, std::move(desc));
    }

private:
    template <class T>
    using DescMap = std::unordered_map<AssetId, T>; // rehash 不会改变元素地址

    using DescMaps = AssetTypes::wrapTypes<DescMap>;

    bool     inited = false;
    DescMaps assetDescs;

    void                                       loadAll();
    void                                       clear();
    static std::filesystem::path descriptorRoot();

    template <class T>
    [[nodiscard]] DescMap<T>& descMap()
    {
        return std::get<DescMap<T>>(assetDescs);
    }

    template <class T>
    [[nodiscard]] const DescMap<T>& descMap() const
    {
        return std::get<DescMap<T>>(assetDescs);
    }
};
