#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "editor/EditorCamera.h"
#include "render/RenderContext.h"
#include "render/RenderPreparation.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/feature/DepthPrepass.h"
#include "render/feature/EndPass.h"
#include "render/feature/ScenePass.h"
#include "render/feature/SkyboxPass.h"
#include "render/feature/TransparentPass.h"
#include "render/feature/postprocess/BloomPass.h"
#include "render/feature/postprocess/ToneMappingPass.h"
#include "render/offline/OfflinePathTracingPass.h"
#include "render/raytracing/BlasBuildPass.h"
#include "render/raytracing/TlasBuildPass.h"
#include "scene/SceneManager.h"
#include "scene/system/RenderSystem.h"

#include <algorithm>
#include <charconv>
#include <stb_image_write.h>
#include <string_view>

namespace
{
template <class T>
T parseNumber(std::string_view text)
{
    T          value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    CHECK(result.ec == std::errc{} && result.ptr == text.data() + text.size(), "invalid number: {}", text);
    return value;
}

struct ImageReadbackPass
{
    RenderGraph::ImageHandle  image;
    RenderGraph::BufferHandle buffer;

    RenderGraph::BufferHandle setup(RenderGraph& graph, const PassSetup&, RenderGraph::ImageHandle source)
    {
        image             = source;
        const auto extent = graph.imageInfo(image).extent;
        buffer            = graph.write(graph.createBuffer({
                       .size             = vk::DeviceSize(extent.width) * extent.height * 4,
                       .memoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        }));
        graph.use(
            image,
            {
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferRead,
                .layout = vk::ImageLayout::eTransferSrcOptimal,
            }
        );
        graph.use(
            buffer,
            {
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferWrite,
            }
        );
        return buffer;
    }

    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame) const
    {
        const vk::BufferImageCopy copy{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageExtent      = graph.imageInfo(image).extent,
        };
        commands.copyImageToBuffer(
            graph.getImage(image, frame.frameIndex).handle(),
            vk::ImageLayout::eTransferSrcOptimal,
            graph.getBuffer(buffer, frame.frameIndex).handle(),
            copy
        );
    }
};

void capture(Renderer& renderer, SceneManager& scenes, const std::filesystem::path& output, uint32_t samples)
{
    auto&       vulkan    = renderer.vulkanContext();
    const auto& swapchain = renderer.swapchainHandle();
    const auto  extent    = swapchain.extent();
    const auto  format    = swapchain.surfaceFormat().format;
    const auto& config    = context().config->rendererConfig();
    const bool  offline   = config.renderPath == RenderPath::OfflinePathTracing;

    FrameContext frame;
    frame.init(vulkan);
    std::array<FrameData, maxFramesInFlight> frameData;
    for (auto& data : frameData)
        data.init(vulkan, *renderContext().descriptors);
    const PassSetup setup{vulkan, renderer.swapchainHandle(), frameData};
    RenderGraph     graph;
    graph.init(vulkan, renderer.swapchainHandle());
    ShadowPass             shadow;
    DepthPrepass           depth;
    ScenePass              scene;
    SkyboxPass             skybox;
    TransparentPass        transparent;
    BlasBuildPass          blas;
    TlasBuildPass          tlas;
    OfflinePathTracingPass tracing;
    BloomPass              bloom;
    ToneMappingPass        toneMapping;
    ImageReadbackPass      readback;
    EndPass                end;

    RenderGraph::ImageHandle color;
    if (offline)
    {
        const auto meshes     = graph.addPass(blas, setup);
        const auto structures = graph.addPass(tlas, setup, meshes);
        color                 = graph.addPass(tracing, setup, structures).rayImage;
    }
    else
    {
        ShadowPass::Output shadows;
        if (config.feature.shadow.enable)
            shadows = graph.addPass(shadow, setup);
        const auto depthOutput = graph.addPass(depth, setup);
        const auto sceneOutput = graph.addPass(scene, setup, depthOutput.depth, shadows);
        color                  = sceneOutput.color;
        if (config.feature.ibl)
            color = graph.addPass(skybox, setup, color, sceneOutput.depth).color;
        color = graph.addPass(transparent, setup, color, sceneOutput.depth, sceneOutput.shadow).color;
    }
    if (config.feature.bloom.enable)
        color = graph.addPass(bloom, setup, color).color;
    const auto target = graph.createImage({.format = format, .extent = {extent.width, extent.height, 1}});
    color             = graph.addPass(toneMapping, setup, color, target).color;
    const auto buffer = graph.addPass(readback, setup, color);
    graph.addPass(end, setup, RenderGraph::ImageHandle{}, buffer);
    graph.compile(setup);

    EditorCamera camera{scenes.scene().editorCamera};
    const auto   view = camera.renderView({0, 0, float(extent.width), float(extent.height)});
    RenderSystem system;
    const auto&  snapshot = system.prepare(scenes.scene());
    auto         shadows  = config.feature.shadow;
    if (offline)
        shadows.enable = false;
    const auto  prepared   = prepareRenderScene(snapshot, view.uniforms, shadows, *context().assetManager, nullptr);
    const auto& renderData = renderContext().resources->prepare(snapshot, prepared, 0);
    if (offline)
    {
        frameData[0].updateView(view.uniforms);
        tracing.updateScene(view, snapshot, context().assetManager->contentRevision());
    }
    else
    {
        scene.sceneBindings().updateEnvironmentMap(renderData.environmentMap);
        scene.sceneBindings().updateFrame(frameData[0], view.uniforms, prepared.lighting);
    }

    for (uint32_t sample = 0; sample < samples; ++sample)
    {
        frame.wait();
        auto& commands = frame.commandBufferHandle();
        vkCheck(commands.reset());
        vkCheck(commands.begin({}));
        graph.execute(commands, {setup, frameData[0], view, renderData, {}, 0}, 0, frame.gpuProfiler(), nullptr);
        vkCheck(commands.end());
        const auto command = *commands;
        vkCheck(vulkan.deviceHandle().resetFences(frame.drawFenceHandle()));
        vkCheck(vulkan.queueHandle().submit(
            vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &command},
            frame.drawFenceHandle()
        ));
        if ((sample + 1) % 64 == 0 || sample + 1 == samples)
            LOG_INFO("{}: sample {}/{}", scenes.scene().id, sample + 1, samples);
    }
    frame.wait();

    std::vector<uint8_t> pixels(size_t(extent.width) * extent.height * 4);
    graph.getBuffer(buffer, 0).download(pixels.data(), pixels.size());
    if (format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eB8G8R8A8Unorm)
        for (size_t offset = 0; offset < pixels.size(); offset += 4)
            std::swap(pixels[offset], pixels[offset + 2]);
    std::error_code error;
    if (!output.parent_path().empty())
        std::filesystem::create_directories(output.parent_path(), error);
    CHECK(!error, "cannot create output directory '{}': {}", output.string(), error.message());
    CHECK(
        stbi_write_png(
            output.string().c_str(),
            int(extent.width),
            int(extent.height),
            4,
            pixels.data(),
            int(extent.width * 4)
        ),
        "cannot write capture: {}",
        output.string()
    );
    LOG_INFO("Saved {} ({}x{}, {} samples)", output.string(), extent.width, extent.height, samples);

    graph.reset();
    for (auto& data : frameData)
        data.reset();
    frame.reset();
}
}  // namespace

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    CHECK(
        argc >= 4 && argc <= 7,
        "usage: CaptureScene <scene-id> <forward|offline> <output.png> [samples=1] [size=960] [ev100]"
    );
    const std::string_view route{argv[2]};
    CHECK(route == "forward" || route == "offline", "unsupported capture route: {}", route);
    const auto samples = argc > 4 ? parseNumber<uint32_t>(argv[4]) : 1u;
    const auto side    = argc > 5 ? parseNumber<int>(argv[5]) : 960;
    CHECK(samples > 0 && side > 0);

    ConfigManager    config;
    AssetManager     assets;
    AssetDataManager data;
    SceneManager     scenes;
    Window           window;
    Renderer         renderer;
    context().config           = &config;
    context().assetManager     = &assets;
    context().assetDataManager = &data;
    context().sceneManager     = &scenes;
    context().window           = &window;
    context().renderer         = &renderer;
    config.init();
    const auto originalConfig = config.rendererConfig();
    const auto originalEditor = config.editorConfig();
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    scenes.load(SceneAsset::ID{argv[1]});
    auto feature = originalConfig.feature;
    feature.hdr  = true;
    feature.ibl  = route == "forward" && scenes.scene().environmentMap.has_value();
    if (argc > 6)
        feature.toneMapping.ev100 = parseNumber<float>(argv[6]);
    config.setRenderFeature(feature);
    config.setRenderPath(route == "offline" ? RenderPath::OfflinePathTracing : RenderPath::Rasterization);
    config.setEditorSize(side, side);
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();
    renderer.init();
    capture(renderer, scenes, std::filesystem::path{argv[3]}, samples);
    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.setRenderPath(originalConfig.renderPath);
    config.setRenderFeature(originalConfig.feature);
    config.setEditorSize(originalEditor.width, originalEditor.height);
    config.shutdown();
    context() = {};
}
