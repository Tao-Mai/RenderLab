// Short manual check; uses temporary actors and never saves a scene.
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "render/Renderer.h"
#include "render/RenderContext.h"
#include "scene/system/RenderSystem.h"
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
    RenderSystem renderSystem;
    LightMarkers     lightMarkers;
    context().config = &config;
    context().assetManager = &assets;
    context().assetDataManager = &data;
    context().sceneManager = &scenes;
    context().window = &window;
    context().renderer = &renderer;
    config.init();
    const auto originalPath = config.rendererConfig().renderPath;
    const auto originalFeature = config.rendererConfig().feature;
    scenes.init();
    assets.init();
    data.init();
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();

    SceneAsset scene;
    scene.id = "editor_overlay_check";
    scene.editorCamera.position = {0, 0, 5};
    scene.editorCamera.yaw = -90;
    scene.editorCamera.pitch = 0;
    scene.actors.push_back(std::make_unique<AStaticMesh>());
    scene.actors.push_back(std::make_unique<ALight>());
    scenes.load(scene);
    renderer.init();
    const auto extent = renderer.swapchainHandle().extent();
    const auto view =
        scenes.editorCamera().renderView({0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height)});
    EditorFrameInput input{
        .requestPick = true,
        .pickX       = extent.width / 2,
        .pickY       = extent.height / 2,
    };

    for (const auto path : {RenderPath::Rasterization, RenderPath::Deferred, RenderPath::RayTracing})
    {
        if (path == RenderPath::RayTracing && !renderer.vulkanContext().supportsRayTracing())
            continue;

        config.setRenderPath(path);
        for (const bool hdr : {false, true})
        {
            auto feature = originalFeature;
            feature.hdr = hdr;
            feature.ibl = false;
            feature.shadow.enable = false;
            config.setRenderFeature(feature);
            renderer.rebuildGraph();
            for (const float z : {1.5f, -1.5f})
            {
                auto& light = *scenes.scene().actors[1];
                light.transform().position = {0, 0, z};
                window.pollEvents();
                const auto& renderData = renderSystem.prepare(
                    scenes.scene(), scenes.editorCamera().worldPosition(), *renderContext().resources);
                lightMarkers.prepare(scenes.scene(), *renderContext().resources);
                input.lightDrawItems = lightMarkers.drawItems();
                const auto result    = renderer.render(view, renderData, input);
                const auto expected = z > 0 ? light.selectionId() : scenes.scene().actors[0]->selectionId();
                CHECK(result.hasPickResult && result.pickedSelectionId == expected,
                      "path {} HDR {} light Z {}: picked {}, expected {}",
                      static_cast<int>(path), hdr, z, result.pickedSelectionId, expected);
            }
        }
        LOG_INFO("Editor marker visibility and picking passed for render path {}", static_cast<int>(path));
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
}
