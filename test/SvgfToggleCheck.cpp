#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "RendererOptions.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "core/profiling/Profiler.h"
#include "editor/EditorCamera.h"
#include "render/Renderer.h"
#include "scene/SceneManager.h"
#include "scene/system/RenderSystem.h"

#include <array>
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
    const auto originalGraph = config.renderGraphName();
    const auto originalSettings = config.getSettings();
    const auto originalEditor = config.editorConfig();
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    scenes.load(SceneAsset::ID{argv[1]});
    config.setSceneRenderMode(scenes.scene().renderMode);
    struct Route
    {
        std::string_view graph;
        std::string_view type;
        std::string_view instance;
        bool             supportsSvgf;
        nlohmann::json parameters;
    };
    std::array routes{
        Route{"ReSTIR", "RayTracingPass", "rayTracing", true},
        Route{"MinimalPathTracer", "MinimalPathTracer", "pathTracer", false},
    };
    for (auto& route : routes)
    {
        config.setRenderGraph(route.graph);
        route.parameters = config.getOptions().at("parameters");
    }
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
        const auto& route = routes[(phase / 2) % routes.size()];
        config.setRenderGraph(route.graph);
        auto parameters = route.parameters;
        const bool enableSvgf = route.supportsSvgf && phase % 2 == 0;
        if (route.supportsSvgf)
            parameters["svgf"] = enableSvgf;
        parameters["hdr"] = (phase & 2u) == 0;
        const auto maxBounces = phase % 3 + 1;
        renderer_test::setParameters(renderer, config, parameters);
        renderer_test::setPassProperties(renderer, config, route.type, route.instance, {{"maxBounces", maxBounces}});
        LOG_INFO(
            "RT phase {}: graph={}, SVGF={}, maxBounces={}", phase, route.graph, enableSvgf, maxBounces
        );
        const auto begin = std::chrono::steady_clock::now();
        for (uint32_t frame = 0; frame < kFramesPerPhase; ++frame)
        {
            profiler.beginFrame();
            const auto result = renderer.render(view, scene, {}, 1.0f / 60.0f);
            profiler.endFrame(1000.0 / 60.0);
            CHECK(!result.swapchainNeedsRebuild);
            CHECK(renderer.getActiveGraph()->getName() == route.graph);
            const auto& trace = renderer.getActiveGraph()->getPass(std::string{route.instance});
            CHECK(trace && trace->getType() == route.type);
            CHECK(trace->getProperties().get<uint32_t>("maxBounces") == maxBounces);
            CHECK(renderer.getActiveGraph()->doesPassExist("svgf") == enableSvgf);
            if (frame == 0)
            {
                Falcor::RenderPass::CompileData compileData;
                compileData.defaultTexDims = {extent.width, extent.height};
                using Field = Falcor::RenderPassReflection::Field;
                const auto reflection = trace->reflect(compileData);
                const auto* color = reflection.getField("color");
                CHECK(
                    color && color->getVisibility() == Field::Visibility::Output &&
                    color->getType() == Field::Type::Texture2D && color->getArraySize() == (enableSvgf ? 2 : 1) &&
                    !is_set(color->getFlags(), Field::Flags::Optional)
                );
                if (!route.supportsSvgf)
                    CHECK(reflection.getFieldCount() == 1);
                if (enableSvgf)
                {
                    const auto svgf = renderer.getActiveGraph()->getPass("svgf")->reflect(compileData);
                    constexpr std::array kHistoryFields{
                        std::pair{"Previous Linear Z and Packed Normal", Falcor::ResourceFormat::RGBA32Float},
                        std::pair{"Previous Lighting", Falcor::ResourceFormat::RGBA32Float},
                        std::pair{"Previous Moments", Falcor::ResourceFormat::RG32Float},
                    };
                    size_t internalCount = 0;
                    for (size_t index = 0; index < svgf.getFieldCount(); ++index)
                        internalCount += svgf.getField(index)->getVisibility() == Field::Visibility::Internal;
                    CHECK(internalCount == kHistoryFields.size());
                    for (const auto& [name, format] : kHistoryFields)
                    {
                        const auto* field = svgf.getField(name);
                        CHECK(field && field->getVisibility() == Field::Visibility::Internal && field->getFormat() == format);
                    }
                }
            }
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
    config.getSettings() = originalSettings;
    config.setRenderGraph(originalGraph);
    config.setEditorSize(originalEditor.width, originalEditor.height);
    config.shutdown();
    profiler.reset();
    context() = {};
}
