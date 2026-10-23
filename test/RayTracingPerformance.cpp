// Fixed-camera, offscreen timing of the configured scene. Does not save the scene.
#include "LightingSetup.h"
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "core/profiling/Profiler.h"
#include "editor/EditorCamera.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/raytracing/BlasBuildPass.h"
#include "render/raytracing/RayTracingPass.h"
#include "render/resource/MaterialTable.h"
#include "scene/SceneManager.h"
#include "scene/component/LightComponent.h"
#include "scene/system/RenderSystem.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numeric>

namespace
{
constexpr uint32_t kWarmupFrames   = 24;
constexpr uint32_t kMeasuredFrames = 64;

struct SinkPass
{
    struct Output
    {
    };
    Output setup(RenderGraph& graph, const PassSetup&, RayTracingPass::Output source)
    {
        for (auto image : {source.rayImage, source.depth, source.worldNormal})
            graph.use(
                image,
                {.stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                 .access = vk::AccessFlagBits2::eShaderStorageRead,
                 .layout = vk::ImageLayout::eGeneral}
            );
        return {};
    }
    void execute(RenderGraph&, vk::raii::CommandBuffer&, const PassFrame&) const {}
};

void measure(Renderer& renderer, SceneManager& scenes, Profiler& profiler, const std::filesystem::path& logPath)
{
    auto& vulkan = renderer.vulkanContext();
    CHECK(vulkan.supportsRayTracing());
    std::array<FrameContext, maxFramesInFlight> frames;
    std::array<FrameData, maxFramesInFlight>    data;
    for (auto& frame : frames)
        frame.init(vulkan);
    for (auto& frame : data)
        frame.init(vulkan, *renderContext().device);
    RenderGraph graph;
    graph.init(vulkan);
    renderContext().materialTable->setup(graph);
    RayTracingPass  trace;
    BlasBuildPass   blasBuild;
    TlasBuildPass   tlasBuild;
    SinkPass        sink;
    const PassSetup setup{vulkan, renderer.swapchainHandle().extent(), data, context().config->rendererConfig()};
    writeTestEnvironment(setup, scenes.scene().environmentMap);
    const auto blas       = graph.addPass(blasBuild, setup);
    const auto structures = graph.addPass(tlasBuild, setup, blas);
    const auto source     = graph.addPass(trace, setup, structures);
    graph.addPass(sink, setup, source);
    graph.compile(setup);

    RenderSystem system;
    EditorCamera camera{scenes.scene().editorCamera};
    const auto   extent = renderer.swapchainHandle().extent();
    const float  side   = static_cast<float>(std::min(extent.width, extent.height));
    const auto   view   = camera.renderView(
        {std::floor((extent.width - side) * 0.5f), std::floor((extent.height - side) * 0.5f), side, side}
    );
    std::map<std::string, std::vector<double>> samples;
    LOG_INFO(
        "RT benchmark: GPU '{}', scene '{}', extent {}x{}, viewport {}x{}, recursion {}, warmup {}, samples {}",
        vulkan.properties().deviceName.data(),
        scenes.scene().id.value,
        extent.width,
        extent.height,
        side,
        side,
        context().config->rendererConfig().feature.rayTracing.maxRecursionDepth,
        kWarmupFrames,
        kMeasuredFrames
    );
    const auto memory = vulkan.physicalDeviceHandle().getMemoryProperties();
    for (uint32_t index = 0; index < memory.memoryTypeCount; ++index)
        LOG_INFO("Memory type {}: {}", index, vk::to_string(memory.memoryTypes[index].propertyFlags));

    for (uint32_t iteration = 0; iteration < kWarmupFrames + kMeasuredFrames; ++iteration)
    {
        if (iteration == kWarmupFrames)
            profiler.startLogging(logPath);
        profiler.beginFrame();
        Timer          timer;
        const uint32_t index = iteration % maxFramesInFlight;
        frames[index].wait();
        const auto& snapshot = system.prepare(scenes.scene());
        graph.beginFrame(index);
        const auto& draws = renderer.prepare(view, snapshot, index);
        if (iteration == 0)
            LOG_INFO(
                "Submitted draw items {}, point lights {}, area lights {}",
                draws.drawItems.size(),
                draws.lightRanges.pointEnd - draws.lightRanges.directionalEnd,
                draws.lightRanges.rectAreaEnd - draws.lightRanges.pointEnd
            );
        data[index].uploadSceneUniforms(view.uniforms);
        const PassFrame execution{setup, data[index], view, draws, {}, index};
        graph.prepare(execution);
        auto& commands = frames[index].commandBufferHandle();
        vkCheck(commands.reset());
        vkCheck(commands.begin({}));
        graph.execute(commands, execution, frames[index].gpuProfiler(), &profiler);
        vkCheck(commands.end());
        const auto command = *commands;
        vkCheck(vulkan.deviceHandle().resetFences(frames[index].drawFenceHandle()));
        vkCheck(vulkan.queueHandle().submit(
            vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &command},
            frames[index].drawFenceHandle()
        ));
        frames[index].wait();
        profiler.endFrame(timer.elapsedMilliseconds());
        if (iteration >= kWarmupFrames)
            for (const auto& timing : profiler.gpuTimings())
                samples[timing.name].push_back(timing.milliseconds);
    }
    profiler.stopLogging();
    for (auto& [name, values] : samples)
    {
        std::ranges::sort(values);
        LOG_INFO(
            "{}: mean {:.3f} ms, median {:.3f} ms, p95 {:.3f} ms",
            name,
            std::accumulate(values.begin(), values.end(), 0.0) / values.size(),
            values[values.size() / 2],
            values[static_cast<size_t>(std::ceil(values.size() * 0.95)) - 1]
        );
    }
    graph.reset();
    for (auto& frame : data)
        frame.reset();
    for (auto& frame : frames)
        frame.reset();
}
}  // namespace

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    CHECK(argc >= 2 && argc <= 4, "usage: RayTracingPerformance <csv> [recursion depth] [all-lights]");
    ConfigManager    config;
    AssetManager     assets;
    AssetDataManager data;
    SceneManager     scenes;
    Window           window;
    Renderer         renderer;
    Profiler         profiler;
    context() = {
        .config           = &config,
        .window           = &window,
        .assetManager     = &assets,
        .assetDataManager = &data,
        .sceneManager     = &scenes,
        .renderer         = &renderer,
        .profiler         = &profiler
    };
    config.init();
    const auto original = config.rendererConfig();
    config.setRenderPath(RenderPath::RayTracing);
    if (argc >= 3)
    {
        auto                   feature = original.feature;
        const std::string_view text    = argv[2];
        const auto             result =
            std::from_chars(text.data(), text.data() + text.size(), feature.rayTracing.maxRecursionDepth);
        CHECK(result.ec == std::errc{} && result.ptr == text.data() + text.size());
        config.setRenderFeature(feature);
    }
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    scenes.loadDefaultScene();
    if (argc == 4)
    {
        CHECK(std::string_view{argv[3]} == "all-lights");
        for (auto& actor : scenes.scene().actors)
            for (auto* light : actor->getComponents<LightComponent>())
                light->enabled = true;
    }
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();
    renderer.init();
    renderer.rebuildGraph();
    measure(renderer, scenes, profiler, argv[1]);
    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.setRenderPath(original.renderPath);
    config.setRenderFeature(original.feature);
    config.shutdown();
    context() = {};
}
