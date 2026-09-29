#include "asset/AssetDescManager.h"

namespace
{
template <AssetType T>
void loadDescDirectory(
    const std::filesystem::path&                          directory,
    std::unordered_map<typename T::ID, typename T::Desc>& out)
{
    if (!std::filesystem::exists(directory))
    {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }
        auto desc = asset_json::load<typename T::Desc>(entry.path());
        CHECK(!desc.id.empty(), "descriptor has empty id: {}", entry.path().string());
        const typename T::ID id = desc.id;
        CHECK(out.emplace(id, std::move(desc)).second,
              "duplicate asset ID: {}",
              id);
    }
}
}

void AssetDescManager::init()
{
    if (inited)
    {
        return;
    }
    CHECK(context().config != nullptr, "ConfigManager must exist before AssetDescManager");
    loadAll();
    inited = true;
}

void AssetDescManager::shutdown() noexcept
{
    clear();
    inited = false;
}

void AssetDescManager::loadAll()
{
    clear();
    AssetTypes::forEach(
        [&]<AssetType T>
        {
            loadDescDirectory<T>(
                descriptorRoot() / T::dir,
                descMap<T>());
        });
}

std::filesystem::path AssetDescManager::descriptorRoot()
{
    return context().config->paths().assets;
}

void AssetDescManager::clear()
{
    std::apply([](auto&... maps) { (maps.clear(), ...); }, assetDescs);
}
