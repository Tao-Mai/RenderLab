#include "asset/AssetManager.h"
#include "core/Serializer.h"

template <AssetType T>
typename T::ID AssetManager::saveAsset(std::unique_ptr<T> asset)
{
    CHECK(!asset->id.empty(), "asset has empty id");

    const typename T::ID id   = asset->id;
    const auto           file = root / T::dir / (id.value + ".json");
    if constexpr (std::is_polymorphic_v<T>)
    {
        asset_json::save(file, asset);
    }
    else
    {
        asset_json::save(file, *asset);
    }

    assetMap<T>().insert_or_assign(id, std::move(asset));
    std::get<RevisionMap<T>>(revisions)[id] = ++nextRevision;
    return id;
}

template TextureAsset::ID        AssetManager::saveAsset(std::unique_ptr<TextureAsset>);
template MeshAsset::ID           AssetManager::saveAsset(std::unique_ptr<MeshAsset>);
template MaterialAsset::ID       AssetManager::saveAsset(std::unique_ptr<MaterialAsset>);
template ShaderAsset::ID         AssetManager::saveAsset(std::unique_ptr<ShaderAsset>);
template SceneAsset::ID          AssetManager::saveAsset(std::unique_ptr<SceneAsset>);
template EnvironmentMapAsset::ID AssetManager::saveAsset(std::unique_ptr<EnvironmentMapAsset>);
template SamplerAsset::ID        AssetManager::saveAsset(std::unique_ptr<SamplerAsset>);
template PbrtSceneAsset::ID      AssetManager::saveAsset(std::unique_ptr<PbrtSceneAsset>);
template PbrtTextureAsset::ID    AssetManager::saveAsset(std::unique_ptr<PbrtTextureAsset>);
template PbrtMediumAsset::ID     AssetManager::saveAsset(std::unique_ptr<PbrtMediumAsset>);
