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
#include "render/RenderPreparation.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/feature/EndPass.h"
#include "render/offline/OfflinePathTracingPass.h"
#include "render/raytracing/BlasBuildPass.h"
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
            .scope            = RenderGraph::ResourceScope::PerFrame,
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
            graph.getBuffer(buffer, execution.frameIndex).handle(),
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
    graph.init(vulkan, renderer.swapchainHandle());
    const PassSetup setup{vulkan, renderer.swapchainHandle(), {&data, 1}};
    const auto      blas       = graph.addPass(blasBuild, setup);
    const auto      structures = graph.addPass(tlasBuild, setup, blas);
    const auto      image      = graph.addPass(pass, setup, structures).rayImage;
    const auto      buffer     = graph.addPass(readback, setup, image);
    graph.addPass(end, setup, RenderGraph::ImageHandle{}, buffer);
    graph.compile(setup);
    const auto extent = renderer.swapchainHandle().extent();
    const auto sample = [&]
    {
        const auto  view     = camera.renderView({0, 0, float(extent.width), float(extent.height)});
        const auto& snapshot = system.prepare(scenes.scene());
        const auto  prepared =
            prepareRenderScene(snapshot, view.uniforms, {.enable = false}, *context().assetManager, context().profiler);
        const auto& draws = renderContext().resources->prepare(snapshot, prepared, 0);
        pass.updateScene(view, snapshot, context().assetManager->contentRevision());
        data.updateSceneUniforms(view.uniforms);
        auto& recording = frame.commandBufferHandle();
        vkCheck(recording.reset());
        vkCheck(recording.begin({}));
        graph.execute(recording, {setup, data, view, draws, {}, 0}, 0, frame.gpuProfiler(), context().profiler);
        vkCheck(recording.end());
        const auto           commandBuffer = *frame.commandBufferHandle();
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
    auto&       light       = *scenes.scene().actors[1]->getComponent<RectAreaLightComponent>();
    const float intensity   = light.luminousIntensity;
    light.luminousIntensity = 0;
    CHECK(sample() == 0);
    CHECK(pass.sampleCount() == 1);
    light.luminousIntensity = intensity;
    CHECK(sample() == first);
    CHECK(pass.sampleCount() == 1);
    LOG_INFO(
        "Offline pixel accumulation passed: first {}, averaged {}, cleared 0, deterministic restart",
        first,
        averaged
    );

    auto& receiver           = *scenes.scene().actors.front()->getComponent<RenderComponent>();
    auto  originalOverrides  = receiver.materialOverrides;
    auto& material           = receiver.materialOverrides[0];
    material.baseColorFactor = glm::vec4{0.8f, 0.2f, 0.1f, 1.0f};
    material.metallic        = 0.0f;
    material.roughness       = 0.12f;
    const auto dielectric    = sample();
    CHECK(pass.sampleCount() == 1);
    material.metallic      = 1.0f;
    const auto smoothMetal = sample();
    material.roughness     = 0.8f;
    const auto roughMetal  = sample();
    CHECK(std::isfinite(dielectric) && dielectric > 0);
    CHECK(std::isfinite(smoothMetal) && smoothMetal > 0);
    CHECK(std::isfinite(roughMetal) && roughMetal > 0);
    CHECK(dielectric != smoothMetal && smoothMetal != roughMetal);

    material.metallic      = 0.0f;
    material.normalTexture = TextureBinding{BuiltinAssets::Texture::kWhiteLinear, BuiltinAssets::Sampler::linearRepeat};
    material.normalScale   = 0.0f;
    const auto unperturbed = sample();
    material.normalScale   = 1.0f;
    const auto perturbed   = sample();
    CHECK(std::isfinite(perturbed) && perturbed >= 0 && perturbed != unperturbed);

    light.luminousIntensity   = 0;
    material.emissive         = glm::vec3{0.5f, 0.125f, 0.025f};
    material.emissiveStrength = 4.0f;
    CHECK(sample() == 2.0f && pass.sampleCount() == 1);
    CHECK(sample() == 2.0f && pass.sampleCount() == 2);
    material.emissiveStrength = 0.0f;
    CHECK(sample() == 0.0f && pass.sampleCount() == 1);
    material.emissiveStrength.reset();
    CHECK(sample() == 0.5f && pass.sampleCount() == 1);
    receiver.materialOverrides = std::move(originalOverrides);
    light.luminousIntensity    = intensity;
    CHECK(sample() == first && pass.sampleCount() == 1);
    LOG_INFO(
        "Offline PBR passed: dielectric {}, smooth metal {}, rough metal {}, normal {} -> {}, accumulated emission 2",
        dielectric,
        smoothMetal,
        roughMetal,
        unperturbed,
        perturbed
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
    auto lamp                  = std::make_unique<ALight>(std::make_unique<RectAreaLightComponent>());
    lamp->transform().position = {2, 2, 3};
    lamp->getComponent<LocalLightComponent>()->luminousIntensity = 159.154943f;
    scene.actors.push_back(std::move(lamp));
    auto disabledSun = std::make_unique<ALight>(std::make_unique<DirectionalLightComponent>());
    disabledSun->getComponent<DirectionalLightComponent>()->enabled = false;
    scene.actors.push_back(std::move(disabledSun));
    scenes.load(scene);
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
    scenes.scene().actors[0]->name = "Renamed";
    feature.toneMapping.exposureCompensation += 1;
    config.setRenderFeature(feature);
    draw();
    CHECK(renderer.offlineSampleCount() == 3);
    feature.hdr = !feature.hdr;
    config.setRenderFeature(feature);
    draw();
    CHECK(renderer.offlineSampleCount() == 1);

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
    scenes.scene().actors[1]->getComponent<LocalLightComponent>()->luminousIntensity *= 2;
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
