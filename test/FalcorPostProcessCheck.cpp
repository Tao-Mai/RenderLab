#include "GraphSelection.h"
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "editor/EditorCamera.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "passes/StandardPasses.h"
#include "render/Renderer.h"
#include "render/device/FrameContext.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/editor/EditorFrameInput.h"
#include "render/graph/EndPass.h"
#include "passes/postprocess/ExposurePass/ExposurePass.h"
#include "passes/postprocess/FalcorPostProcess/FalcorPostProcess.h"
#include "passes/postprocess/ToneMappingPass/ToneMappingPass.h"
#include "render/resources/RenderResourceManager.h"
#include "scene/SceneManager.h"
#include "scene/system/SceneRenderData.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
constexpr vk::Extent3D kExtent{
    35, 19, 1
};  // Non-power-of-two, non-workgroup-aligned, and narrower than the bloom pyramid.
using Pixel = std::array<uint8_t, 4>;

struct SourcePass
{
    RenderGraph::ImageHandle color;
    RenderGraph::ImageHandle setup(RenderGraph& graph, const PassSetup&)
    {
        color = graph.write(graph.createImage({.format = kHdrColorFormat, .extent = kExtent}));
        graph.use(
            color,
            {.stages = vk::PipelineStageFlagBits2::eTransfer,
             .access = vk::AccessFlagBits2::eTransferWrite,
             .layout = vk::ImageLayout::eTransferDstOptimal}
        );
        return color;
    }
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame&) const
    {
        commands.clearColorImage(
            graph.getImage(color).handle(),
            vk::ImageLayout::eTransferDstOptimal,
            vk::ClearColorValue{std::array{0.25f, 1.0f, 4.0f, 0.5f}},
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        );
    }
};
struct ReadbackPass
{
    RenderGraph::ImageHandle  color;
    RenderGraph::BufferHandle buffer;
    RenderGraph::BufferHandle setup(RenderGraph& graph, const PassSetup&, RenderGraph::ImageHandle input)
    {
        color  = input;
        buffer = graph.write(graph.createBuffer({
            .size             = kExtent.width * kExtent.height * sizeof(Pixel),
            .memoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        }));
        graph.use(
            color,
            {.stages = vk::PipelineStageFlagBits2::eTransfer,
             .access = vk::AccessFlagBits2::eTransferRead,
             .layout = vk::ImageLayout::eTransferSrcOptimal}
        );
        graph.use(
            buffer,
            {.stages = vk::PipelineStageFlagBits2::eTransfer, .access = vk::AccessFlagBits2::eTransferWrite}
        );
        return buffer;
    }
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame&) const
    {
        commands.copyImageToBuffer(
            graph.getImage(color).handle(),
            vk::ImageLayout::eTransferSrcOptimal,
            graph.getBuffer(buffer).handle(),
            vk::BufferImageCopy{
                .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
                .imageExtent      = kExtent,
            }
        );
    }
};

std::vector<Pixel> run(
    VulkanContext& vulkan, const FalcorPostProcessConfig& settings, vk::Format format,
    PostProcessMode mode = PostProcessMode::Falcor
)
{
    FrameContext frame;
    frame.init(vulkan);
    FrameData       frameData;
    RendererConfig config;
    config.feature.postProcess = mode;
    const PassSetup setup{vulkan, {kExtent.width, kExtent.height}, {}, config};
    RenderGraph     graph;
    graph.init(vulkan);
    SourcePass        sourcePass;
    ReadbackPass      readback;
    EndPass           end;
    FalcorPostProcess falcor;
    ExposurePass      exposure;
    ToneMappingPass   native;
    auto              color  = graph.addPass(sourcePass, setup);
    const auto        target = graph.createImage({.format = format, .extent = kExtent});
    if (mode == PostProcessMode::Falcor)
    {
        falcor.setSettings(settings);
        color = falcor.setup(graph, setup, color, target).color;
    }
    else
    {
        const auto state = graph.addPass(exposure, setup, color).state;
        color            = graph.addPass(native, setup, color, target, state).color;
    }
    const auto buffer = graph.addPass(readback, setup, color);
    graph.addPass(end, setup, RenderGraph::ImageHandle{}, buffer);
    graph.compile(setup);
    const RenderView       view;
    const RenderData       renderData;
    const EditorFrameInput editor;
    const PassFrame        execution{setup, frameData, view, renderData, editor, 0, 1.0f / 60.0f};
    for (uint32_t index = 0; index < 3; ++index)
    {
        frame.wait();
        graph.beginFrame(0);
        graph.prepare(execution);
        auto& commands = frame.commandBufferHandle();
        vkCheck(commands.reset());
        vkCheck(commands.begin({}));
        graph.beginGpuProfiling(commands, frame.gpuProfiler(), nullptr);
        graph.execute(commands, execution, frame.gpuProfiler(), nullptr);
        frame.gpuProfiler().endFrame(commands);
        vkCheck(commands.end());
        const auto command = *commands;
        vkCheck(vulkan.deviceHandle().resetFences(frame.drawFenceHandle()));
        vkCheck(vulkan.queueHandle().submit(
            vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &command},
            frame.drawFenceHandle()
        ));
    }
    frame.wait();
    std::vector<Pixel> pixels(kExtent.width * kExtent.height);
    graph.getBuffer(buffer).download(pixels.data(), pixels.size() * sizeof(Pixel));
    graph.reset();
    frame.reset();
    return pixels;
}

void near(Pixel actual, Pixel expected)
{
    for (size_t channel = 0; channel < actual.size(); ++channel)
        CHECK(
            std::abs(int(actual[channel]) - int(expected[channel])) <= 1,
            "pixel channel {}: expected {}, got {}",
            channel,
            expected[channel],
            actual[channel]
        );
}

void check(VulkanContext& vulkan)
{
    FalcorPostProcessConfig settings;
    // Independently evaluated from the original Falcor equations, including sRGB presentation.
    constexpr std::array<Pixel, 6> kExpected{
        Pixel{137, 255, 255, 128},
        {96, 183, 255, 128},
        {255, 255, 255, 128},
        {150, 216, 244, 128},
        {143, 213, 250, 128},
        {128, 214, 247, 128},
    };
    for (uint32_t mode = 0; mode < kExpected.size(); ++mode)
    {
        settings.toneMapper.mode = static_cast<FalcorToneMappingOperator>(mode);
        for (const auto format : {vk::Format::eR8G8B8A8Srgb, vk::Format::eR8G8B8A8Unorm})
            for (const auto pixel : run(vulkan, settings, format))
                near(pixel, kExpected[mode]);
        LOG_INFO("Falcor tone operator {} passed for sRGB and UNORM", mode);
    }
    settings.toneMapper              = {};
    settings.toneMapper.autoExposure = true;
    for (const auto pixel : run(vulkan, settings, vk::Format::eR8G8B8A8Srgb))
        near(pixel, {6, 30, 93, 128});
    LOG_INFO("Falcor logarithmic luminance / auto exposure passed");

    auto& toneMapper                = settings.toneMapper;
    toneMapper                      = {};
    toneMapper.mode                 = FalcorToneMappingOperator::Linear;
    toneMapper.filmSpeed            = 200;
    toneMapper.fNumber              = 2;
    toneMapper.shutter              = 4;
    toneMapper.exposureCompensation = 1;
    for (const auto pixel : run(vulkan, settings, vk::Format::eR8G8B8A8Unorm))
        near(pixel, {71, 137, 255, 128});
    toneMapper.exposureMode = FalcorExposureMode::AperturePriority;
    toneMapper.setExposureValue(5);
    CHECK(toneMapper.fNumber == 2 && toneMapper.shutter == 8);
    toneMapper.exposureMode = FalcorExposureMode::ShutterPriority;
    toneMapper.setExposureValue(3);
    CHECK(toneMapper.fNumber == 1 && toneMapper.shutter == 8);
    toneMapper              = {};
    toneMapper.whiteBalance = true;
    const auto identity     = toneMapper.whiteBalanceTransform();
    for (glm::length_t row = 0; row < 3; ++row)
        for (glm::length_t column = 0; column < 3; ++column)
            CHECK(std::abs(identity[column][row] - (row == column ? 1.0f : 0.0f)) < 0.00001f);
    toneMapper.whitePoint = 10000.0f;
    for (const auto pixel : run(vulkan, settings, vk::Format::eR8G8B8A8Srgb))
        near(pixel, {161, 214, 244, 128});
    LOG_INFO("Falcor physical exposure and 6500 K white balance passed");

    toneMapper                        = {};
    auto& effects                     = settings.simplePostFx;
    effects.bloomAmount               = 0.2f;
    effects.starAmount                = 0.3f;
    effects.vignetteAmount            = 0.3f;
    effects.chromaticAberrationAmount = 0.5f;
    effects.barrelDistortAmount       = 0.25f;
    effects.saturationCurve           = {0.6f, 0.8f, 1.2f};
    effects.colorOffsetScalar         = 0.1f;
    effects.colorScaleScalar          = -0.1f;
    effects.colorPowerScalar          = 0.2f;
    effects.wipe                      = 0.5f;
    const auto pixels                 = run(vulkan, settings, vk::Format::eR8G8B8A8Srgb);
    CHECK(pixels.front() != pixels.back());
    for (uint32_t y = 0; y < kExtent.height; ++y)
        for (uint32_t x = 0; x < kExtent.width; ++x)
            if (x < kExtent.width * effects.wipe)
                near(pixels[y * kExtent.width + x], kExpected[5]);
            else
                CHECK(pixels[y * kExtent.width + x][3] == 255);
    effects.enabled = false;
    for (const auto pixel : run(vulkan, settings, vk::Format::eR8G8B8A8Srgb))
        near(pixel, kExpected[5]);
    LOG_INFO("Falcor 8-level bloom, stars, lens/color effects, wipe and bypass passed");

    const auto native = run(vulkan, settings, vk::Format::eR8G8B8A8Srgb, PostProcessMode::RenderLab);
    const auto restored          = run(vulkan, settings, vk::Format::eR8G8B8A8Srgb);
    CHECK(native.front() != restored.front());
    for (const auto pixel : restored)
        near(pixel, kExpected[5]);
    LOG_INFO("Native / Falcor graph replacement passed");
}

void checkRendererSwitches(ConfigManager& config)
{
    const auto originalGraph = config.renderGraph().id;
    selectGraphForPath(config, RenderPath::Forward);
    const auto forwardGraph = config.renderGraph().id;
    const auto originalParameters = config.graphParameterOverrides();
    const auto originalOverrides = config.passOverrides();
    auto parameters = originalParameters;
    parameters.update({{"ibl", false}, {"shadows", false}, {"hdr", true}});
    FalcorPostProcessConfig settings;
    config.setGraphParameterOverrides(parameters);
    Renderer renderer;
    context().renderer = &renderer;
    renderer.init(standardRenderPasses());
    const auto            extent = renderer.swapchainHandle().extent();
    EditorCameraConfig    cameraConfig;
    EditorCamera          camera{cameraConfig};
    const auto            view = camera.renderView({0, 0, float(extent.width), float(extent.height)});
    const SceneRenderData scene;
    for (uint32_t phase = 0; phase < 10; ++phase)
    {
        const auto mode = phase % 2 == 0 ? PostProcessMode::RenderLab : PostProcessMode::Falcor;
        parameters["postProcess"] = Serialize(mode);
        settings.toneMapper.autoExposure  = (phase & 2u) != 0;
        settings.simplePostFx.bloomAmount = (phase & 4u) != 0 ? 0.2f : 0.0f;
        settings.simplePostFx.enabled     = phase != 9;
        config.setGraphParameterOverrides(parameters);
        renderer.setPassOverrides("FalcorPostProcess", "falcor", Serialize(settings));
        for (uint32_t frame = 0; frame < 4; ++frame)
            CHECK(!renderer.render(view, scene, {}, 1.0f / 60.0f).swapchainNeedsRebuild);
        CHECK((renderer.renderPasses().activePass<FalcorPostProcess>() != nullptr) == (mode == PostProcessMode::Falcor));
    }
    const auto forwardOverrides = config.passOverrides();
    selectGraphForPath(config, RenderPath::Deferred);
    const auto deferredParameters = config.graphParameterOverrides();
    auto ldrParameters = deferredParameters;
    ldrParameters.update({{"hdr", false}, {"ibl", false}, {"shadows", false}});
    config.setGraphParameterOverrides(std::move(ldrParameters));
    CHECK(!renderer.render(view, scene, {}, 1.0f / 60.0f).swapchainNeedsRebuild);
    CHECK(renderer.activeRenderGraph() == &config.renderGraph() && !config.rendererConfig().feature.hdr);
    config.setGraphParameterOverrides(deferredParameters);

    config.setRenderGraph(forwardGraph);
    CHECK(config.graphParameterOverrides() == parameters && config.passOverrides() == forwardOverrides);
    CHECK(!renderer.render(view, scene, {}, 1.0f / 60.0f).swapchainNeedsRebuild);
    CHECK(renderer.activeRenderGraph() == &config.renderGraph() && config.rendererConfig().feature.hdr);
    LOG_INFO("Graph selection and isolated overrides passed");

    renderer.shutdown();
    context().renderer = nullptr;
    config.setGraphParameterOverrides(originalParameters);
    config.passOverrides() = originalOverrides;
    config.setRenderGraph(originalGraph);
    LOG_INFO("Renderer runtime post process switching passed (10 phases, 40 frames)");
}
}  // namespace

int main(int, char** argv)
{
    logger::init(argv[0]);
    ConfigManager config;
    context().config = &config;
    config.init();
    const auto originalEditor = config.editorConfig();
    config.setEditorSize(64, 64);
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    Window window;
    window.init();
    VulkanContext vulkan;
    vulkan.init(window);
    SceneManager scenes;
    scenes.init();
    AssetManager assets;
    assets.init(config.paths().assets);
    AssetDataManager data;
    data.init(config.paths().assets);
    ShaderManager shaders;
    shaders.init(vulkan.deviceHandle());
    Device device;
    device.init(vulkan, shaders);
    RenderResourceManager resources;
    resources.init(vulkan, assets, data);
    renderContext().device        = &device;
    renderContext().shaderManager = &shaders;
    renderContext().resources     = &resources;
    check(vulkan);
    renderContext() = {};
    resources.shutdown();
    device.shutdown();
    shaders.shutdown();
    vulkan.reset();
    context().window           = &window;
    context().assetManager     = &assets;
    context().assetDataManager = &data;
    checkRendererSwitches(config);
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.setEditorSize(originalEditor.width, originalEditor.height);
    config.shutdown();
    context() = {};
}
