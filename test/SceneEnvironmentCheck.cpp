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
    Profiler         profiling;
    auto&            ctx = context();
    ctx.config           = &config;
    ctx.assetManager     = &assets;
    ctx.assetDataManager = &data;
    ctx.sceneManager     = &scenes;
    ctx.window           = &window;
    ctx.renderer         = &renderer;
    ctx.profiler         = &profiling;
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
        profiling.reset();
        profiling.beginFrame();
        const auto& snapshot = system.prepare(scenes.scene());
        CHECK(snapshot.environmentMap == scenes.scene().environmentMap);
        renderer.render(view, snapshot, {});
        profiling.endFrame(1.0);

        const auto& stats  = profiling.display();
        const auto skybox = std::ranges::find_if(stats.passes, [](const auto& pass) { return pass.name == "SkyboxPass"; });
        CHECK((skybox != stats.passes.end()) == config.rendererConfig().feature.ibl);
        if (skybox != stats.passes.end())
            CHECK(skybox->draws.drawCalls == (snapshot.environmentMap ? 1u : 0u));

        return std::ranges::any_of(stats.cpuTimings, [](const auto& timing) { return timing.name == "Graph rebuild"; });
    };
    for (const auto path : {RenderPath::Rasterization, RenderPath::Deferred})
    {
        config.setRenderPath(path);
        scenes.scene().environmentMap = environments.front();
        CHECK(render());
        if (environments.size() > 1)
        {
            scenes.scene().environmentMap = environments.back();
            CHECK(!render());
        }
        scene.environmentMap.reset();
        scenes.load(scene);
        CHECK(!render());
        CHECK(config.rendererConfig().feature.ibl);
        scenes.scene().environmentMap = environments.front();
        CHECK(!render());
        renderer.rebuildGraph();
        CHECK(!render());
        renderer.recreateSwapchain();
        CHECK(!render());
        LOG_INFO("Scene environment changes passed for render path {}", static_cast<int>(path));
    }
    feature.ibl = false;
    config.setRenderFeature(feature);
    CHECK(render());
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
