// One-time import through the production importer; no scene or renderer settings are saved.
#include "asset/AssetDataManager.h"
#include "asset/AssetImporter.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "scene/SceneManager.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string_view>

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    CHECK(argc == 3);
    const std::string_view sizeArgument = argv[2];
    float                  targetSize   = 0.0f;
    const auto parsed = std::from_chars(sizeArgument.data(), sizeArgument.data() + sizeArgument.size(), targetSize);
    CHECK(parsed.ec == std::errc{} && parsed.ptr == sizeArgument.data() + sizeArgument.size());
    CHECK(std::isfinite(targetSize) && targetSize > 0.0f);

    ConfigManager    config;
    AssetManager     assets;
    AssetDataManager data;
    SceneManager     scenes;
    context().config           = &config;
    context().assetManager     = &assets;
    context().assetDataManager = &data;

    config.init();
    scenes.init();
    assets.init(config.paths().assets);
    data.init(config.paths().assets);

    auto        prepared    = AssetImporter::prepareMesh(argv[1]);
    const auto  extent      = prepared.boundsMax - prepared.boundsMin;
    const float importScale = targetSize / std::max({extent.x, extent.y, extent.z});
    const float floorOffset = -prepared.boundsMin.y * importScale;
    const auto  id = AssetImporter::saveMesh(std::move(prepared), {.source = argv[1], .importScale = importScale});

    // Reload persisted descriptions, including the new extension and texture bindings.
    assets.shutdown();
    assets.init(config.paths().assets);
    bool hasClearcoat = false;
    for (const auto& submesh : assets.get<MeshAsset>(id).submeshes)
    {
        const auto& material = assets.get<MaterialAsset>(submesh.materialId).as<PbrMaterialAsset>();
        if (!material.clearcoat)
            continue;
        const auto& clearcoat = *material.clearcoat;
        CHECK(clearcoat.factor > 0.0f && clearcoat.texture);
        for (const auto& binding : {clearcoat.texture, clearcoat.roughnessTexture, clearcoat.normalTexture})
        {
            if (!binding)
                continue;
            const auto& texture = assets.get<TextureAsset>(binding->textureID);
            CHECK(texture.colorSpace == ColorSpace::Linear);
            CHECK(!data.readTexture(texture).empty());
        }
        hasClearcoat = true;
    }
    CHECK(hasClearcoat);
    LOG_INFO("Imported '{}', import scale {}, floor offset {}", id.value, importScale, floorOffset);

    data.shutdown();
    assets.shutdown();
    scenes.shutdown();
    config.shutdown();
    context() = {};
}
