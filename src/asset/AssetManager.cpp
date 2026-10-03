#include "asset/AssetManager.h"

namespace
{
template <AssetType T>
void loadAssetDirectory(
    const std::filesystem::path&          directory,
    std::unordered_map<typename T::ID, T>& out)
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
        auto asset = asset_json::load<T>(entry.path());
        CHECK(!asset.id.empty(), "asset has empty id: {}", entry.path().string());

        const typename T::ID id = asset.id;
        CHECK(out.emplace(id, std::move(asset)).second,
              "duplicate asset ID: {}",
              id);
    }
}
}

AssetManager::~AssetManager()
{
    DCHECK(!inited);
}

void AssetManager::init()
{
    DCHECK(!inited);
    DCHECK(context().config);
    loadAll();
    DEBUG_EXEC(inited = true);
}

void AssetManager::shutdown() noexcept
{
    clear();
    DEBUG_EXEC(inited = false);
}

void AssetManager::loadAll()
{
    clear();
    AssetTypes::forEach(
        [&]<AssetType T>
        {
            loadAssetDirectory<T>(
                assetRoot() / T::dir,
                assetMap<T>());
        });
}

std::filesystem::path AssetManager::assetRoot()
{
    return context().config->paths().assets;
}

void AssetManager::clear()
{
    std::apply([](auto&... maps) { (maps.clear(), ...); }, assets);
}
