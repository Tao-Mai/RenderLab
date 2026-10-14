// One-time sample import through the production importer; no renderer or scene changes.
#include "asset/AssetDataManager.h"
#include "asset/AssetImporter.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "scene/SceneManager.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_set>

namespace
{
void checkTextures(
    const json&                           value,
    const AssetManager&                   assets,
    const AssetDataManager&               data,
    std::unordered_set<TextureAsset::ID>& checked
)
{
    if (value.is_object() && value.contains("textureID") && value.contains("samplerID"))
    {
        TextureBinding binding;
        Deserialize(value, binding);
        CHECK(assets.find<SamplerAsset>(binding.samplerID));
        if (checked.insert(binding.textureID).second)
            CHECK(!data.readTexture(assets.get<TextureAsset>(binding.textureID)).empty());
    }
    if (value.is_object() || value.is_array())
        for (const auto& child : value)
            checkTextures(child, assets, data, checked);
}
}  // namespace

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    CHECK(argc == 2);
    const std::filesystem::path manifest = std::filesystem::absolute(argv[1]);
    std::ifstream               input{manifest};
    CHECK(input);
    const auto samples = json::parse(input, nullptr, false);
    CHECK(samples.is_array());

    ConfigManager    config;
    AssetManager     assets;
    AssetDataManager data;
    SceneManager     scenes;
    auto&            ctx = context();
    ctx.config           = &config;
    ctx.assetManager     = &assets;
    ctx.assetDataManager = &data;
    config.init();
    scenes.init();
    assets.init(config.paths().assets);
    data.init(config.paths().assets);

    std::unordered_set<TextureAsset::ID> checkedTextures;
    for (const auto& sample : samples)
    {
        const auto  source     = (manifest.parent_path() / sample.at("source").get<std::string>()).lexically_normal();
        const auto  field      = sample.at("field").get<std::string>();
        const auto  targetSize = sample.at("targetSize").get<float>();
        auto        prepared   = AssetImporter::prepareMesh(source);
        const auto  extent     = prepared.boundsMax - prepared.boundsMin;
        const float size       = std::max({extent.x, extent.y, extent.z});
        CHECK(std::isfinite(size) && size > 0.0f && targetSize > 0.0f);
        const auto id = AssetImporter::saveMesh(
            std::move(prepared),
            {.source = source, .id = sample.at("model").get<std::string>(), .importScale = targetSize / size}
        );

        const auto mesh = asset_json::load<MeshAsset>(config.paths().assets / MeshAsset::dir / (id.value + ".json"));
        CHECK(!data.readGeometry(mesh).indices.empty());
        size_t                                matchingMaterials = 0;
        std::unordered_set<MaterialAsset::ID> checkedMaterials;
        for (const auto& submesh : mesh.submeshes)
        {
            if (!checkedMaterials.insert(submesh.materialId).second)
                continue;
            const auto persisted = asset_json::load<std::unique_ptr<MaterialAsset>>(
                config.paths().assets / MaterialAsset::dir / (submesh.materialId.value + ".json")
            );
            const auto& material = persisted->as<PbrMaterialAsset>();
            CHECK(material == assets.get<MaterialAsset>(submesh.materialId).as<PbrMaterialAsset>());
            const auto description = Serialize(material);
            if (!description.at(field).is_null())
                ++matchingMaterials;
            checkTextures(description, assets, data, checkedTextures);
        }
        CHECK(matchingMaterials > 0, "sample '{}' has no '{}' material", id, field);
        LOG_INFO("Sample '{}' verified: {} '{}' materials, scale {}", id, matchingMaterials, field, mesh.importScale);
    }

    data.shutdown();
    assets.shutdown();
    scenes.shutdown();
    config.shutdown();
    ctx = {};
}
