#include "asset/AssetDataManager.h"
#include "asset/AssetImporter.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "scene/SceneManager.h"
#include "scene/component/RenderComponent.h"

#include <cmath>
#include <fstream>
#include <glm/vector_relational.hpp>

namespace
{
void writeImage(const std::filesystem::path& file, bool scalar, std::initializer_list<uint8_t> pixels)
{
    std::ofstream output(file, std::ios::binary);
    CHECK(output);
    output << (scalar ? "P5\n" : "P6\n") << "2 2\n255\n";
    output.write(reinterpret_cast<const char*>(pixels.begin()), pixels.size());
    CHECK(output);
}
}  // namespace

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    CHECK(argc == 2);
    const auto root     = std::filesystem::absolute(argv[1]);
    const auto fixtures = root / "test/model_import";
    const auto output   = fixtures / "output";
    std::filesystem::create_directories(output);
    writeImage(fixtures / "color.ppm", false, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    writeImage(fixtures / "scalar.pgm", true, {32, 64, 128, 255});
    writeImage(fixtures / "normal.ppm", false, {128, 128, 255, 128, 128, 255, 128, 128, 255, 128, 128, 255});

    ConfigManager    config;
    AssetManager     assets;
    AssetDataManager data;
    SceneManager     scenes;
    context().config           = &config;
    context().assetManager     = &assets;
    context().assetDataManager = &data;
    config.init();
    data.init(output);
    scenes.init();
    assets.init(output);

    CHECK(AssetImporter::supportsModel("model.OBJ"));
    CHECK(AssetImporter::supportsModel("model.fbx"));
    CHECK(AssetImporter::supportsModel("model.gltf"));
    CHECK(!AssetImporter::supportsModel("model.unsupported"));
    auto prepared = AssetImporter::prepareMesh(fixtures / "quad.obj");
    CHECK(prepared.geometry.indices.size() == 6);
    CHECK(prepared.boundsMin == glm::vec3(0));
    CHECK(prepared.boundsMax == glm::vec3(2, 2, 0));
    CHECK(prepared.geometry.vertices.front().texcoord == glm::vec2(0, 1));
    for (const auto& vertex : prepared.geometry.vertices)
    {
        CHECK(std::abs(glm::length(vertex.normal) - 1) < 1e-5f);
        CHECK(std::abs(glm::dot(vertex.normal, glm::vec3(vertex.tangent))) < 1e-5f);
    }
    const auto meshId = AssetImporter::saveMesh(
        std::move(prepared),
        {
            .source      = fixtures / "quad.obj",
            .id          = "obj_check",
            .importScale = 3,
        }
    );
    const auto& mesh = assets.get<MeshAsset>(meshId);
    CHECK(mesh.bounds.max == glm::vec3(6, 6, 0));
    CHECK(data.readGeometry(mesh).indices.size() == 6);
    const auto* material =
        dynamic_cast<const PbrMaterialAsset*>(&assets.get<MaterialAsset>(mesh.submeshes.front().materialId));
    CHECK(material && material->normalTexture && material->metallicTexture && material->roughnessTexture);
    CHECK(std::abs(*material->metallic - 0.6f) < 1e-5f);
    CHECK(std::abs(*material->roughness - 0.4f) < 1e-5f);
    const auto& color  = assets.get<TextureAsset>(material->baseColorTexture->textureID);
    const auto& scalar = assets.get<TextureAsset>(material->roughnessTexture->textureID);
    CHECK(color.colorSpace == ColorSpace::Srgb && scalar.colorSpace == ColorSpace::Linear);
    CHECK(color.mipLevels == 2 && scalar.mipLevels == 2);
    CHECK(material->metallicTexture == material->roughnessTexture);
    const auto pixels = data.readTexture(scalar);
    CHECK(pixels[0] == 32 && pixels[1] == 32 && pixels[2] == 32);

    const auto  sceneId = AssetImporter::importScene({
         .source      = fixtures / "instances.dae",
         .id          = "instances_check",
         .importScale = 2,
    });
    const auto& scene   = assets.get<SceneAsset>(sceneId);
    CHECK(scene.actors.size() == 2);
    const auto& first  = *scene.actors[0];
    const auto& second = *scene.actors[1];
    CHECK(first.transform().position == glm::vec3(4, 2, 0));
    CHECK(second.transform().position == glm::vec3(10, 0, 0));
    CHECK(first.getComponent<RenderComponent>()->meshId == second.getComponent<RenderComponent>()->meshId);
    CHECK(scene.editorCamera.nearPlane > 0 && scene.editorCamera.farPlane > scene.editorCamera.nearPlane);
    const auto gltf = AssetImporter::importMesh({.source = root / "resource/khronos_box/Box.gltf", .id = "gltf_check"});
    CHECK(data.readGeometry(assets.get<MeshAsset>(gltf)).indices.size() == 36);
    const auto gltfScene = AssetImporter::importScene({
        .source = root / "resource/khronos_box/Box.gltf",
        .id     = "gltf_scene_check",
    });
    CHECK(assets.get<SceneAsset>(gltfScene).actors.size() == 1);
    LOG_INFO(
        "Model import passed: OBJ/PBR textures, mipmaps, scale, Collada instancing, camera fit and glTF regression"
    );

    assets.shutdown();
    data.shutdown();
    scenes.shutdown();
    config.shutdown();
    context() = {};
}
