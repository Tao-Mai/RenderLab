#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "core/profiling/Profiler.h"
#include "editor/EditorCamera.h"
#include "render/Renderer.h"
#include "scene/SceneManager.h"
#include "scene/system/RenderSystem.h"

#include <charconv>
#include <chrono>
#include <string_view>

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    CHECK(argc == 2 || argc == 3, "usage: SvgfToggleCheck <scene-id> [size=192]");
    int side = 192;
    if (argc == 3)
    {
        const std::string_view text{argv[2]};
        const auto             parsed = std::from_chars(text.data(), text.data() + text.size(), side);
        CHECK(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && side > 0);
    }

    ConfigManager    config;
    AssetManager     assets;
    AssetDataManager data;
    SceneManager     scenes;
    Window           window;
    Renderer         renderer;
    Profiler         profiler;
    context().profiler         = &profiler;
    context().config           = &config;
    context().assetManager     = &assets;
    context().assetDataManager = &data;
    context().sceneManager     = &scenes;
    context().window           = &window;
    context().renderer         = &renderer;
    config.init();
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    scenes.load(SceneAsset::ID{argv[1]});
    config.setSceneRenderMode(scenes.scene().renderMode);
    config.setRenderPath(RenderPath::RayTracing);
    config.setEditorSize(side, side);

    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();
    renderer.init();
    const auto   extent = renderer.swapchainHandle().extent();
    EditorCamera camera{scenes.scene().editorCamera};
    const auto   view = camera.renderView({0, 0, float(extent.width), float(extent.height)});
    RenderSystem system;
    const auto&  scene = system.prepare(scenes.scene());

    constexpr uint32_t kPhaseCount     = 8;
    constexpr uint32_t kFramesPerPhase = 7;
    for (uint32_t phase = 0; phase < kPhaseCount; ++phase)
    {
        auto feature            = config.rendererConfig().feature;
        feature.rayTracing.svgf = phase % 2 == 0;
        config.setRenderFeature(feature);
        LOG_INFO("SVGF phase {}: {}", phase, feature.rayTracing.svgf);
        const auto begin = std::chrono::steady_clock::now();
        for (uint32_t frame = 0; frame < kFramesPerPhase; ++frame)
        {
            profiler.beginFrame();
            const auto result = renderer.render(view, scene, {}, 1.0f / 60.0f);
            profiler.endFrame(1000.0 / 60.0);
            CHECK(!result.swapchainNeedsRebuild);
            LOG_INFO("Phase {} submitted frame {}", phase, frame);
        }
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        LOG_INFO("SVGF phase {} finished in {} s", phase, elapsed);
    }

    renderer.waitIdle();
    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.shutdown();
    profiler.reset();
    context() = {};
}
