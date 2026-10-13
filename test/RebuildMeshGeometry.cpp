// One-time geometry rebake after adding vertex tangents; material IDs stay intact.
#include "asset/AssetDataManager.h"
#include "asset/AssetImporter.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "scene/SceneManager.h"

#include <cmath>
#include <glm/geometric.hpp>

int main(int argc, char** argv)
{
    logger::init(argv[0]);
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

    for (const auto& id : assets.loadedIds<MeshAsset>())
    {
        const auto& asset = assets.get<MeshAsset>(id);
        if (asset.source != Source::File)
            continue;
        CHECK(asset.path);
        auto prepared = AssetImporter::prepareMesh(config.paths().assets / *asset.path, asset.meshIndex);
        CHECK(prepared.submeshes.size() == asset.submeshes.size());
        for (size_t index = 0; index < asset.submeshes.size(); ++index)
        {
            CHECK(prepared.submeshes[index].firstIndex == asset.submeshes[index].firstIndex);
            CHECK(prepared.submeshes[index].indexCount == asset.submeshes[index].indexCount);
        }
        for (auto& vertex : prepared.geometry.vertices)
            vertex.position *= asset.importScale;
        CHECK(data.writeGeometry(id, prepared.geometry) == asset.geometry);
        const auto saved = data.readGeometry(asset);
        CHECK(saved.indices == prepared.geometry.indices);
        CHECK(saved.vertices.size() == prepared.geometry.vertices.size());
        for (size_t index = 0; index < saved.vertices.size(); ++index)
        {
            const auto& vertex = saved.vertices[index];
            CHECK(vertex.tangent == prepared.geometry.vertices[index].tangent);
            CHECK(std::abs(glm::dot(vertex.normal, glm::vec3(vertex.tangent))) < 1e-4f);
            CHECK(std::abs(glm::length(glm::vec3(vertex.tangent)) - 1.0f) < 1e-4f);
            CHECK(vertex.tangent.w == 1.0f || vertex.tangent.w == -1.0f);
        }
        LOG_INFO(
            "Rebuilt geometry '{}': {} vertices, {} indices",
            id.value,
            saved.vertices.size(),
            saved.indices.size()
        );
        auto rebuilt = asset;
        rebuilt.bounds = calculateBounds(saved);
        (void)assets.save(std::move(rebuilt));
    }

    for (const auto& id :
         {BuiltinAssets::Mesh::plane,
          BuiltinAssets::Mesh::cube,
          BuiltinAssets::Mesh::sphere,
          BuiltinAssets::Mesh::arrow})
    {
        const auto geometry = data.readGeometry(assets.get<MeshAsset>(id));
        for (const auto& vertex : geometry.vertices)
        {
            CHECK(std::abs(glm::dot(vertex.normal, glm::vec3(vertex.tangent))) < 1e-4f);
            CHECK(std::abs(glm::length(glm::vec3(vertex.tangent)) - 1.0f) < 1e-4f);
        }
        LOG_INFO("Builtin '{}' tangent frames checked", id.value);
    }

    data.shutdown();
    assets.shutdown();
    scenes.shutdown();
    config.shutdown();
    context() = {};
}
