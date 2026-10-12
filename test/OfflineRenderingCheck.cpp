#include <GLFW/glfw3.h>
#include <cmath>
#include <glm/gtc/packing.hpp>
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
#include "render/feature/EndPass.h"
#include "render/offline/OfflinePathTracingPass.h"
#include "scene/SceneManager.h"
#include "scene/SceneTypes.h"
#include "scene/system/RenderSystem.h"

namespace
{
struct PixelReadbackPass
{
    RenderGraph::ImageHandle  image;
    RenderGraph::BufferHandle buffer;

    RenderGraph::BufferHandle setup(RenderGraph& graph, RenderGraph::ImageHandle source)
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

    void execute(RenderGraph& graph) const
    {
        const auto                extent = graph.imageInfo(image).extent;
        const vk::BufferImageCopy region{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageOffset      = {int32_t(extent.width / 2), int32_t(extent.height / 2), 0},
            .imageExtent      = {1, 1, 1},
        };
        graph.commands().copyImageToBuffer(
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
    data.init(vulkan, *renderContext().descriptors);
    RenderGraph            graph;
    OfflinePathTracingPass pass;
    PixelReadbackPass      readback;
    EndPass                end;
    RenderSystem           system;
    graph.init(vulkan, renderer.swapchainHandle(), {&frame, 1}, {&data, 1});
    const auto image  = graph.addPass(pass).rayImage;
    const auto buffer = graph.addPass(readback, image);
    graph.addPass(end, RenderGraph::ImageHandle{}, buffer);
    graph.compile();
    const auto extent = renderer.swapchainHandle().extent();
    const auto sample = [&]
    {
        const auto  view     = camera.renderView({0, 0, float(extent.width), float(extent.height)});
        const auto& snapshot = system.prepare(scenes.scene(), camera.worldPosition());
        const auto& draws    = renderContext().resources->prepare(snapshot, 0);
        pass.updateScene(view, snapshot, context().assetManager->contentRevision());
        data.updateView(view.uniforms);
        graph.execute(view, draws, {}, 0, 0);
        const auto commandBuffer = *frame.commandBufferHandle();
        const vk::SubmitInfo submitInfo{.commandBufferCount = 1, .pCommandBuffers = &commandBuffer};
        vkCheck(vulkan.queueHandle().submit(submitInfo));
        vkCheck(vulkan.queueHandle().waitIdle());
        std::array<uint16_t, 4> pixel;
        graph.getBuffer(buffer, 0).download(pixel.data(), sizeof(pixel));
        return glm::unpackHalf1x16(pixel[0]);
    };
    const auto first = sample();
    CHECK(std::isfinite(first) && first > 0);
    const auto averaged = sample();
    CHECK(std::isfinite(averaged) && averaged > 0);
    CHECK(pass.sampleCount() == 2);
    auto&       light     = *scenes.scene().actors[1]->getComponent<LightComponent>();
    const float intensity = light.intensity;
    light.intensity       = 0;
    CHECK(sample() == 0);
    CHECK(pass.sampleCount() == 1);
    light.intensity = intensity;
    CHECK(sample() == first);
    CHECK(pass.sampleCount() == 1);
    LOG_INFO(
        "Offline pixel accumulation passed: first {}, averaged {}, cleared 0, deterministic restart",
        first,
        averaged
    );
    graph.reset();
    data.reset();
    frame.reset();
}
}  // namespace
// Hidden-window check; no scene is saved and rendering settings are restored.
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
    const auto originalPath           = config.rendererConfig().renderPath;
    const auto originalFeature        = config.rendererConfig().feature;
    auto       feature                = originalFeature;
    feature.offline.maxRecursionDepth = 4;
    feature.ibl                       = false;
    feature.shadow.enable             = false;
    config.setRenderFeature(feature);
    config.setRenderPath(RenderPath::OfflinePathTracing);
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();

    SceneAsset scene;
    scene.editorCamera.position                     = {0, 0, 5};
    auto sphere                                     = std::make_unique<AStaticMesh>();
    sphere->getComponent<RenderComponent>()->meshId = BuiltinAssets::Mesh::sphere;
    scene.actors.push_back(std::move(sphere));
    auto lamp                                       = std::make_unique<ALight>();
    lamp->transform().position                      = {2, 2, 3};
    lamp->getComponent<LightComponent>()->type      = LightComponent::Type::RectArea;
    lamp->getComponent<LightComponent>()->intensity = 500;
    scene.actors.push_back(std::move(lamp));
    scenes.load(scene);
    EditorCamera camera{scenes.scene().editorCamera};
    RenderSystem system;
    renderer.init();
    CHECK(renderer.vulkanContext().supportsRayTracing());
    const auto extent = renderer.swapchainHandle().extent();
    const auto draw   = [&]
    {
        const auto  view     = camera.renderView({0, 0, float(extent.width), float(extent.height)});
        const auto& snapshot = system.prepare(scenes.scene(), camera.worldPosition());
        const auto  result   = renderer.render(view, snapshot, {});
        CHECK(!result.swapchainNeedsRebuild);
        renderer.waitIdle();
    };
    draw();
    CHECK(renderer.framesInFlight() == 1);
    CHECK(renderer.offlineSampleCount() == 1);
    draw();
    CHECK(renderer.offlineSampleCount() == 2);
    scenes.scene().actors[0]->name = "Renamed";
    feature.toneMapping.exposureCompensation += 1;
    config.setRenderFeature(feature);
    draw();
    CHECK(renderer.offlineSampleCount() == 3);
    feature.hdr = !feature.hdr;
    config.setRenderFeature(feature);
    draw();
    CHECK(renderer.offlineSampleCount() == 4);

    scenes.scene().editorCamera.position.x += 0.25f;
    draw();
    CHECK(renderer.offlineSampleCount() == 1);
    draw();
    CHECK(renderer.offlineSampleCount() == 2);
    scenes.scene().actors[0]->transform().position.x += 0.2f;
    draw();
    CHECK(renderer.offlineSampleCount() == 1);
    draw();
    scenes.scene().actors[0]->getComponent<RenderComponent>()->materialOverrides[0].roughness = 0.2f;
    draw();
    CHECK(renderer.offlineSampleCount() == 1);
    draw();
    scenes.scene().actors[1]->getComponent<LightComponent>()->intensity *= 2;
    draw();
    CHECK(renderer.offlineSampleCount() == 1);
    feature.offline.maxRecursionDepth = 3;
    config.setRenderFeature(feature);
    draw();
    CHECK(renderer.offlineSampleCount() == 1);
    config.setRenderPath(RenderPath::RayTracing);
    draw();
    CHECK(renderer.framesInFlight() == maxFramesInFlight);
    config.setRenderPath(RenderPath::OfflinePathTracing);
    draw();
    CHECK(renderer.framesInFlight() == 1);
    CHECK(renderer.offlineSampleCount() == 1);
    LOG_INFO("Offline rendering passed: accumulation, camera/scene reset, display changes and route isolation");
    checkAccumulatedPixels(renderer, scenes, camera);

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
