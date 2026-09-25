#include "asset/AssetDescManager.h"

namespace
{
template <class T>
void loadDescDirectory(
    const std::filesystem::path& directory, std::unordered_map<AssetId, T>& out)
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
        T desc = asset_json::load<T>(entry.path());
        CHECK(!desc.id.empty(), "descriptor has empty id: {}", entry.path().string());
        CHECK(out.emplace(desc.id, desc).second,
              "duplicate asset ID: {}",
              desc.id);
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
        [&]<class Entry>
        {
            using T = typename Entry::type;
            loadDescDirectory(
                descriptorRoot() / Entry::dir,
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
