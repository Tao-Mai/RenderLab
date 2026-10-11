// Standalone import using the application's configured asset directory.
#include "asset/AssetDataManager.h"
#include "asset/AssetImporter.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "scene/SceneManager.h"

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    CHECK(argc == 2);

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

    const auto meshId = AssetImporter::importMesh({.source = argv[1]});
    // Reload descriptions from disk to check the persisted material references.
    assets.shutdown();
    assets.init(config.paths().assets);
    const auto& mesh     = assets.get<MeshAsset>(meshId);
    const auto  geometry = data.readGeometry(mesh);
    CHECK(!geometry.vertices.empty() && !geometry.indices.empty());

    bool hasNormalTexture = false;
    for (const auto& submesh : mesh.submeshes)
    {
        const auto& material = assets.get<MaterialAsset>(submesh.materialId);
        if (!material.normalTexture)
            continue;

        const auto& normal = assets.get<TextureAsset>(material.normalTexture->textureID);
        CHECK(normal.colorSpace == ColorSpace::Linear);
        CHECK(!data.readTexture(normal).empty());
        LOG_INFO(
            "Material '{}' uses normal texture '{}' ({}x{})",
            material.id.value,
            normal.id.value,
            normal.width,
            normal.height
        );
        hasNormalTexture = true;
    }
    CHECK(hasNormalTexture);
    LOG_INFO(
        "Imported mesh '{}': {} vertices, {} indices",
        meshId.value,
        geometry.vertices.size(),
        geometry.indices.size()
    );

    data.shutdown();
    assets.shutdown();
    scenes.shutdown();
    config.shutdown();
    context() = {};
}
