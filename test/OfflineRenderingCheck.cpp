#include <GLFW/glfw3.h>
#include <cmath>
#include <glm/gtc/packing.hpp>
#include "LightingSetup.h"
#include "LocalLightSamplingCheck.h"
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "editor/EditorCamera.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/graph/EndPass.h"
#include "render/raytracing/offline/OfflinePathTracingPass.h"
#include "render/raytracing/BlasBuildPass.h"
#include "render/resources/MaterialTable.h"
#include "scene/SceneManager.h"
#include "scene/SceneTypes.h"
#include "scene/system/RenderSystem.h"

namespace
{
struct PixelReadbackPass
{
    RenderGraph::ImageHandle  image;
    RenderGraph::BufferHandle buffer;

    RenderGraph::BufferHandle setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle source)
    {
        image  = source;
        buffer = graph.write(graph.createBuffer({
            .size             = sizeof(uint16_t) * 4,
            .memoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        }));
        graph.use(
            image,
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

    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const
    {
        const auto                extent = graph.imageInfo(image).extent;
        const vk::BufferImageCopy region{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageOffset      = {int32_t(extent.width / 2), int32_t(extent.height / 2), 0},
            .imageExtent      = {1, 1, 1},
        };
        commandBuffer.copyImageToBuffer(
            graph.getImage(image).handle(),
            vk::ImageLayout::eTransferSrcOptimal,
            graph.getBuffer(buffer).handle(),
            region
        );
    }
};

void checkAccumulatedPixels(Renderer& renderer, SceneManager& scenes, EditorCamera& camera)
{
    auto&        vulkan = renderer.vulkanContext();
    FrameContext frame;
    FrameData    data;
    frame.init(vulkan);
    data.init(vulkan, *renderContext().device);
    RenderGraph            graph;
    OfflinePathTracingPass pass;
    BlasBuildPass          blasBuild;
    TlasBuildPass          tlasBuild;
    PixelReadbackPass      readback;
    EndPass                end;
    RenderSystem           system;
    graph.init(vulkan);
    renderContext().materialTable->setup(graph);
    const PassSetup setup{vulkan, renderer.swapchainHandle().extent(), {&data, 1}, context().config->rendererConfig()};
    writeTestEnvironment(setup, scenes.scene().environmentMap);
    const auto blas        = graph.addPass(blasBuild, setup);
    const auto structures  = graph.addPass(tlasBuild, setup, blas);
    const auto lightBuffer = graph.createBuffer({
        .size  = kMaxLights * sizeof(LightData),
        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    });
    const auto image       = graph.addPass(pass, setup, structures).rayImage;
    const auto buffer      = graph.addPass(readback, setup, image);
    graph.addPass(end, setup, RenderGraph::ImageHandle{}, buffer);
    graph.compile(setup);
    bindTestLightBuffer(graph, setup, lightBuffer);
    const auto extent = renderer.swapchainHandle().extent();
    const auto sample = [&]
    {
        const auto  view     = camera.renderView({0, 0, float(extent.width), float(extent.height)});
        const auto& snapshot = system.prepare(scenes.scene());
        graph.beginFrame(0);
        const auto& draws = renderer.prepare(view, snapshot, 0);
        uploadTestLights(graph, lightBuffer, draws.lights);
        data.uploadSceneUniforms(view.uniforms);
        const PassFrame execution{setup, data, view, draws, {}, 0};
        graph.prepare(execution);
        auto& recording = frame.commandBufferHandle();
        vkCheck(recording.reset());
        vkCheck(recording.begin({}));
        graph.execute(recording, execution, frame.gpuProfiler(), context().profiler);
        vkCheck(recording.end());
        const auto           commandBuffer = *frame.commandBufferHandle();
        const vk::SubmitInfo submitInfo{.commandBufferCount = 1, .pCommandBuffers = &commandBuffer};
        vkCheck(vulkan.queueHandle().submit(submitInfo));
        vkCheck(vulkan.queueHandle().waitIdle());
        std::array<uint16_t, 4> pixel;
        graph.getBuffer(buffer).download(pixel.data(), sizeof(pixel));
        return glm::unpackHalf1x16(pixel[0]);
    };
    const auto first = sample();
    CHECK(std::isfinite(first));
    const auto averaged = sample();
    CHECK(std::isfinite(averaged));
    CHECK(pass.sampleCount() == 2);
    LOG_INFO("Offline RGB pixel readback: first {}, averaged {}", first, averaged);
    graph.reset();
    data.reset();
    frame.reset();
}
}  // namespace
// Hidden-window check; no scene is saved and rendering settings are restored.
int main(int argc, char** argv)
{
    logger::init(argv[0]);
    CHECK(argc == 2, "usage: OfflineRenderingCheck <offline-scene-id>");
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
    const auto originalPath           = config.rendererConfig().renderPath;
    const auto originalFeature        = config.rendererConfig().feature;
    auto       feature                = originalFeature;
    feature.offline.maxBounces = 3;
    feature.ibl                       = false;
    feature.shadow.enable             = false;
    config.setRenderFeature(feature);
    config.setSceneRenderMode(SceneRenderMode::Offline);
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();

    scenes.load(SceneAsset::ID{argv[1]});
    CHECK(scenes.scene().renderMode == SceneRenderMode::Offline);
    EditorCamera camera{scenes.scene().editorCamera};
    RenderSystem system;
    renderer.init();
    CHECK(renderer.vulkanContext().supportsRayTracing());
    const auto extent = renderer.swapchainHandle().extent();
    const auto draw   = [&]
    {
        const auto  view     = camera.renderView({0, 0, float(extent.width), float(extent.height)});
        const auto& snapshot = system.prepare(scenes.scene());
        const auto  result   = renderer.render(view, snapshot, {});
        CHECK(!result.swapchainNeedsRebuild);
        renderer.waitIdle();
    };
    draw();
    CHECK(renderer.framesInFlight() == 1);
    CHECK(renderer.offlineSampleCount() == 1);
    draw();
    CHECK(renderer.offlineSampleCount() == 2);
    feature.toneMapping.exposureCompensation += 1;
    config.setRenderFeature(feature);
    draw();
    CHECK(renderer.offlineSampleCount() == 3);
    feature.offline.pixelJitter = !feature.offline.pixelJitter;
    config.setRenderFeature(feature);
    draw();
    CHECK(renderer.offlineSampleCount() == 1);
    draw();
    CHECK(renderer.offlineSampleCount() == 2);
    scenes.scene().editorCamera.position.x += 0.25f;
    draw();
    CHECK(renderer.offlineSampleCount() == 1);
    feature.offline.maxBounces = 2;
    config.setRenderFeature(feature);
    draw();
    CHECK(renderer.offlineSampleCount() == 2);
    LOG_INFO("Offline rendering passed: accumulation, camera/config reset and display changes");
    checkAccumulatedPixels(renderer, scenes, camera);

    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.setSceneRenderMode(originalPath == RenderPath::OfflinePathTracing ? SceneRenderMode::Offline : SceneRenderMode::Realtime);
    if (originalPath != RenderPath::OfflinePathTracing)
        config.setRenderPath(originalPath);
    config.setRenderFeature(originalFeature);
    config.shutdown();
    context() = {};
}
