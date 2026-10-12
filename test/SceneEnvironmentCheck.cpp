#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "editor/EditorCamera.h"
#include "render/Renderer.h"
#include "scene/SceneManager.h"
#include "scene/actor/AStaticMesh.h"
#include "scene/system/RenderSystem.h"

#include <algorithm>

// Short GPU check for scene-owned environment changes. No scene is saved.
int main(int, char** argv)
{
    logger::init(argv[0]);
    ConfigManager    config;
    AssetManager     assets;
    AssetDataManager data;
    SceneManager     scenes;
    Window           window;
    Renderer         renderer;
    auto&            ctx = context();
    ctx.config           = &config;
    ctx.assetManager     = &assets;
    ctx.assetDataManager = &data;
    ctx.sceneManager     = &scenes;
    ctx.window           = &window;
    ctx.renderer         = &renderer;
    config.init();
    const auto original   = config.rendererConfig();
    auto       feature    = original.feature;
    feature.ibl           = true;
    feature.shadow.enable = false;
    config.setRenderFeature(feature);
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    const auto environments = assets.loadedIds<EnvironmentMapAsset>();
    CHECK(!environments.empty());
    SceneAsset scene;
    scene.editorCamera.position = {0, 0, 5};
    scene.actors.push_back(std::make_unique<AStaticMesh>());
    scenes.load(scene);
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();
    renderer.init();
    RenderSystem system;
    EditorCamera camera{scenes.scene().editorCamera};
    const auto   extent = renderer.swapchainHandle().extent();
    const float  side   = static_cast<float>(std::min(extent.width, extent.height));
    const auto   view   = camera.renderView({(extent.width - side) * 0.5f, (extent.height - side) * 0.5f, side, side});
    const auto   render = [&]
    {
        const auto& snapshot = system.prepare(scenes.scene(), camera.worldPosition());
        CHECK(snapshot.environmentMap == scenes.scene().environmentMap);
        renderer.render(view, snapshot, {});
    };
    for (const auto path : {RenderPath::Rasterization, RenderPath::Deferred})
    {
        config.setRenderPath(path);
        scenes.scene().environmentMap = environments.front();
        render();
        if (environments.size() > 1)
        {
            scenes.scene().environmentMap = environments.back();
            render();
        }
        scene.environmentMap.reset();
        scenes.load(scene);
        render();
        CHECK(config.rendererConfig().feature.ibl);
        scenes.scene().environmentMap = environments.front();
        render();
        LOG_INFO("Scene environment changes passed for render path {}", static_cast<int>(path));
    }
    feature.ibl = false;
    config.setRenderFeature(feature);
    render();
    CHECK(scenes.scene().environmentMap == environments.front());
    renderer.waitIdle();
    renderer.shutdown();
    window.shutdown();
    assets.shutdown();
    scenes.shutdown();
    data.shutdown();
    config.setRenderPath(original.renderPath);
    config.setRenderFeature(original.feature);
    config.shutdown();
    context() = {};
}
