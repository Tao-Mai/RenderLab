#include "asset/AssetManager.h"
#include "asset/MaterialTypes.h"
#include <cmath>


namespace
{
template <AssetType T>
void loadAssetDirectory(const std::filesystem::path& directory, std::unordered_map<typename T::ID, std::unique_ptr<T>>& out)
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
        std::unique_ptr<T> asset;
        if constexpr (std::is_polymorphic_v<T>)
            asset = asset_json::load<std::unique_ptr<T>>(entry.path());
        else
            asset = std::make_unique<T>(asset_json::load<T>(entry.path()));
        CHECK(asset, "null asset: {}", entry.path().string());
        CHECK(!asset->id.empty(), "asset has empty id: {}", entry.path().string());
        if constexpr (std::same_as<T, MeshAsset>)
            for (glm::length_t axis = 0; axis < 3; ++axis)
                CHECK(std::isfinite(asset->bounds.min[axis]) && std::isfinite(asset->bounds.max[axis]) &&
                    asset->bounds.min[axis] <= asset->bounds.max[axis], "invalid mesh bounds: {}", asset->id);

        const typename T::ID id = asset->id;
        CHECK(out.emplace(id, std::move(asset)).second, "duplicate asset ID: {}", id);
    }
}
}  // namespace

AssetManager::~AssetManager() { DCHECK(!inited); }

void AssetManager::init(std::filesystem::path assetRoot)
{
    DCHECK(!inited);
    DCHECK(!assetRoot.empty());
    static const bool kRegistered = []
    {
        forEachMaterialType([]<class T>(std::string_view name) { RegisterBase<T, MaterialAsset>(name); });
        return true;
    }();

    root = std::move(assetRoot);
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
    AssetTypes::forEach([&]<AssetType T> { loadAssetDirectory<T>(root / T::dir, assetMap<T>()); });
}

void AssetManager::clear()
{
    std::apply([](auto&... maps) { (maps.clear(), ...); }, revisions);
    std::apply([](auto&... maps) { (maps.clear(), ...); }, assets);
}
