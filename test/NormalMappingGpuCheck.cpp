// Small hidden-window integration check; does not save the temporary scene.
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "render/Renderer.h"
#include "scene/SceneManager.h"
#include "scene/SceneTypes.h"

#include <GLFW/glfw3.h>

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    ConfigManager config;
    AssetManager assets;
    AssetDataManager data;
    SceneManager scenes;
    Window window;
    Renderer renderer;
    context().config = &config;
    context().assetManager = &assets;
    context().assetDataManager = &data;
    context().sceneManager = &scenes;
    context().window = &window;
    context().renderer = &renderer;

    config.init();
    const auto originalPath = config.rendererConfig().renderPath;
    const auto originalFeature = config.rendererConfig().feature;
    config.setRenderPath(RenderPath::Rasterization);
    scenes.init();
    assets.init();
    data.init();
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();

    SceneAsset scene;
    scene.id = "normal_mapping_check";
    scene.editorCamera.position = {0, 0, 5};
    auto object = std::make_unique<AStaticMesh>();
    const MeshAsset::ID meshId = argc > 1 ? argv[1] : "NormalTangentTest";
    object->getComponent<RenderComponent>()->meshId = meshId;
    scene.actors.push_back(std::move(object));
    auto light = std::make_unique<ALight>();
    light->transform().position = {1, 2, 3};
    light->getComponent<LightComponent>()->intensity = 100.0f;
    scene.actors.push_back(std::move(light));
    scenes.load(scene);

    renderer.init();
    renderer.loadScene();
    const auto extent = renderer.swapchainHandle().extent();
    const EditorFrameInput input{
        .viewport = {0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height)},
        .aspectRatio = static_cast<float>(extent.width) / extent.height,
    };
    for (int mode = 0; mode < 4; ++mode)
    {
        auto feature = originalFeature;
        feature.ibl = (mode & 1) != 0;
        feature.hdr = mode < 2;
        feature.shadow.enable = mode < 2;
        config.setRenderFeature(feature);
        renderer.loadScene();

        auto& overrides = scenes.scene().actors[0]->getComponent<RenderComponent>()->materialOverrides;
        for (int frame = 0; frame < 4; ++frame)
        {
            window.pollEvents();
            for (size_t index = 0; index < assets.get<MeshAsset>(meshId).submeshes.size(); ++index)
            {
                auto& material = overrides[index];
                material.normalScale = frame % 2 == 0 ? 1.0f : 0.0f;
                material.metallic = frame % 2 == 0 ? 1.0f : 0.0f;
                material.roughness = frame % 2 == 0 ? 0.2f : 1.0f;
                const auto& submesh = assets.get<MeshAsset>(meshId).submeshes[index];
                const auto& sourceMaterial = assets.get<MaterialAsset>(submesh.materialId);
                if (sourceMaterial.clearcoat)
                {
                    auto clearcoat = *sourceMaterial.clearcoat;
                    clearcoat.factor = frame % 2 == 0 ? 0.0f : 1.0f;
                    clearcoat.roughnessFactor = frame < 2 ? 0.0f : 0.5f;
                    if (frame >= 2)
                        clearcoat.normalTexture = sourceMaterial.normalTexture;
                    material.clearcoat = std::move(clearcoat);
                }
            }
            renderer.render(scenes.editorCamera(), input);
        }
    }
    renderer.waitIdle();
    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.setRenderPath(originalPath);
    config.setRenderFeature(originalFeature);
    config.shutdown();
    context() = {};
    LOG_INFO("Mesh '{}' rendered with base/clearcoat overrides and IBL/HDR/shadow toggles", meshId);
}
