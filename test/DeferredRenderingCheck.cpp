#include "editor/EditorCamera.h"
// Small pixel comparison: shared forward/deferred lighting and base-material clearcoat behavior.
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "render/RenderContext.h"
#include "render/RenderPreparation.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/feature/DepthPrepass.h"
#include "render/feature/EndPass.h"
#include "render/feature/ScenePass.h"
#include "render/feature/deferred/DeferredLightingPass.h"
#include "scene/SceneManager.h"
#include "scene/SceneTypes.h"
#include "scene/system/RenderSystem.h"

#include <cmath>

#include <GLFW/glfw3.h>
#include <glm/gtc/packing.hpp>

namespace
{
struct PixelReadbackPass
{
    RenderGraph::ImageHandle  image;
    RenderGraph::BufferHandle buffer;

    RenderGraph::BufferHandle setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle source)
    {
        image  = source;
        buffer = graph.createBuffer({
            .size             = sizeof(uint16_t) * 4,
            .memoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        });
        buffer = graph.write(buffer);
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
            {.stages = vk::PipelineStageFlagBits2::eTransfer, .access = vk::AccessFlagBits2::eTransferWrite}
        );
        return buffer;
    }

    void execute(RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution) const
    {
        const auto                extent = graph.imageInfo(image).extent;
        const vk::BufferImageCopy region{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageOffset      = {static_cast<int32_t>(extent.width / 2), static_cast<int32_t>(extent.height / 2), 0},
            .imageExtent      = {1, 1, 1},
        };
        commandBuffer.copyImageToBuffer(
            graph.getImage(image, execution.frameIndex).handle(),
            vk::ImageLayout::eTransferSrcOptimal,
            graph.getBuffer(buffer, execution.frameIndex).handle(),
            region
        );
    }
};

void checkDeferred(Renderer& renderer, SceneManager& scenes)
{
    EditorCamera                                scenesCamera{scenes.scene().editorCamera};
    auto&                                       vulkan = renderer.vulkanContext();
    std::array<FrameContext, maxFramesInFlight> frames;
    for (auto& frame : frames)
        frame.init(vulkan);
    std::array<FrameData, maxFramesInFlight> frameData;
    for (auto& data : frameData)
        data.init(vulkan, *renderContext().descriptors);

    RenderGraph          graph;
    ShadowPass           shadow;
    DepthPrepass         depth;
    ScenePass            forward;
    GBufferPass          gBuffer;
    DeferredLightingPass lighting;
    PixelReadbackPass    readback;
    EndPass              end;
    RenderSystem         renderSystem;
    graph.init(vulkan, renderer.swapchainHandle());
    const PassSetup setup{vulkan, renderer.swapchainHandle(), frameData};
    const auto      originalFeature = context().config->rendererConfig().feature;
    const auto      originalPath    = context().config->rendererConfig().renderPath;
    const auto      extent          = renderer.swapchainHandle().extent();
    const auto      view            = scenesCamera.renderView(
        {64, 32, static_cast<float>(extent.width) - 192, static_cast<float>(extent.height) - 96}
    );
    const auto sample = [&](bool deferred, bool ibl, bool shadows)
    {
        graph.releaseResources();
        auto feature               = originalFeature;
        feature.hdr                = true;
        feature.ibl                = ibl;
        feature.ambientCoefficient = 0.1f;
        feature.shadow.enable      = shadows;
        context().config->setRenderFeature(feature);
        context().config->setRenderPath(deferred ? RenderPath::Deferred : RenderPath::Rasterization);
        ShadowPass::Output shadowOutput;
        if (shadows)
            shadowOutput = graph.addPass(shadow, setup);
        const auto               depthOutput = graph.addPass(depth, setup);
        RenderGraph::ImageHandle color;
        if (deferred)
        {
            const auto geometry = graph.addPass(gBuffer, setup, depthOutput.depth);
            color               = graph.addPass(lighting, setup, geometry, shadowOutput).color;
        }
        else
            color = graph.addPass(forward, setup, depthOutput.depth, shadowOutput).color;
        const auto buffer = graph.addPass(readback, setup, color);
        graph.addPass(end, setup, RenderGraph::ImageHandle{}, buffer);
        graph.compile(setup);
        const auto& sceneData = renderSystem.prepare(scenes.scene());
        const auto  prepared  = prepareRenderScene(
            sceneData,
            view.uniforms,
            context().config->rendererConfig().feature.shadow,
            *context().assetManager,
            context().profiler
        );
        const auto& data = renderContext().resources->prepare(sceneData, prepared, 0);
        if (deferred)
            lighting.sceneBindings().updateFrame(frameData[0], view.uniforms, prepared.lighting);
        else
            forward.sceneBindings().updateFrame(frameData[0], view.uniforms, prepared.lighting);
        auto& recording = frames[0].commandBufferHandle();
        vkCheck(recording.reset());
        vkCheck(recording.begin({}));
        graph.execute(
            recording,
            {setup, frameData[0], view, data, {}, 0},
            0,
            frames[0].gpuProfiler(),
            context().profiler
        );
        vkCheck(recording.end());
        const auto           commandBuffer = *frames[0].commandBufferHandle();
        const vk::SubmitInfo submitInfo{.commandBufferCount = 1, .pCommandBuffers = &commandBuffer};
        vkCheck(vulkan.queueHandle().submit(submitInfo));
        vkCheck(vulkan.queueHandle().waitIdle());
        std::array<uint16_t, 4> pixel;
        graph.getBuffer(buffer, 0).download(pixel.data(), sizeof(pixel));
        return glm::vec3{glm::unpackHalf1x16(pixel[0]), glm::unpackHalf1x16(pixel[1]), glm::unpackHalf1x16(pixel[2])};
    };
    const auto compare = [](glm::vec3 a, glm::vec3 b)
    {
        constexpr float kTolerance = 0.005f;
        for (int channel = 0; channel < 3; ++channel)
            CHECK(
                std::abs(a[channel] - b[channel]) < kTolerance,
                "forward/deferred channel {}: {} vs {}",
                channel,
                a[channel],
                b[channel]
            );
    };
    glm::vec3 lastDeferred;
    for (const bool ibl : {false, true})
        for (const bool shadows : {false, true})
        {
            const auto forwardPixel = sample(false, ibl, shadows);
            lastDeferred            = sample(true, ibl, shadows);
            CHECK(glm::length(lastDeferred) > 0.05f);
            compare(forwardPixel, lastDeferred);
            LOG_INFO(
                "Deferred lighting matches forward: IBL {}, shadows {}, RGB {}, {}, {}",
                ibl,
                shadows,
                lastDeferred.r,
                lastDeferred.g,
                lastDeferred.b
            );
        }
    auto&      lamp             = *scenes.scene().actors[1];
    const auto originalRotation = lamp.transform().rotation;
    replaceLightComponent(*lamp.getComponent<LightComponent>(), std::make_unique<SpotLightComponent>());
    auto& spot    = *lamp.getComponent<SpotLightComponent>();
    spot.cosInner = std::cos(glm::radians(15.0f));
    spot.cosOuter = std::cos(glm::radians(30.0f));
    lamp.transform().setRotationEulerDegrees({0, 45, 0});
    spot.enabled         = false;
    const auto unlit     = sample(false, false, false);
    spot.enabled         = true;
    const auto spotClear = sample(false, false, false);
    compare(spotClear, sample(true, false, false));
    CHECK(glm::length(spotClear - unlit) > 0.05f);
    lamp.transform().setRotationEulerDegrees({0, 65, 0});
    const auto penumbra = sample(false, false, false);
    CHECK(glm::length(penumbra - unlit) > 0.0f && glm::length(penumbra - unlit) < glm::length(spotClear - unlit));
    lamp.transform().setRotationEulerDegrees({0, 180, 0});
    compare(unlit, sample(false, false, false));
    lamp.transform().setRotationEulerDegrees({0, 45, 0});
    compare(spotClear, sample(false, false, true));

    auto& blocker                = scenes.createActor("AStaticMesh");
    blocker.transform().position = {1, 0, 2};
    blocker.transform().scale    = glm::vec3{0.6f};
    compare(spotClear, sample(false, false, false));
    const auto shadowed = sample(false, false, true);
    compare(shadowed, sample(true, false, true));
    compare(shadowed, unlit);
    LOG_INFO(
        "Spot lighting/shadows match forward and deferred: clear {}, shadowed {}",
        glm::length(spotClear),
        glm::length(shadowed)
    );
    scenes.deleteActor(blocker.selectionId());

    const float originalIntensity = spot.intensity;
    replaceLightComponent(spot, std::make_unique<DirectionalLightComponent>());
    lamp.getComponent<LightComponent>()->intensity = 3.0f;
    lamp.transform().setRotationEulerDegrees({0, 45, 0});
    const auto directionalClear = sample(false, false, true);
    compare(directionalClear, sample(true, false, true));
    auto& upstreamBlocker                = scenes.createActor("AStaticMesh");
    upstreamBlocker.transform().position = {100, 0, 101};
    upstreamBlocker.transform().scale    = glm::vec3{2};
    compare(directionalClear, sample(false, false, false));
    const auto directionalShadowed = sample(false, false, true);
    compare(directionalShadowed, sample(true, false, true));
    CHECK(glm::length(directionalClear - directionalShadowed) > 0.05f);
    compare(directionalShadowed, unlit);
    LOG_INFO(
        "Cascade depth clamp retains an upstream caster: clear {}, shadowed {}",
        glm::length(directionalClear),
        glm::length(directionalShadowed)
    );
    scenes.deleteActor(upstreamBlocker.selectionId());
    replaceLightComponent(*lamp.getComponent<LightComponent>(), std::make_unique<PointLightComponent>());
    lamp.getComponent<LightComponent>()->intensity = originalIntensity;
    lamp.transform().rotation                      = originalRotation;

    auto& material     = scenes.scene().actors[0]->getComponent<RenderComponent>()->materialOverrides[0];
    material.clearcoat = PbrMaterialAsset::Clearcoat{.factor = 1.0f, .roughnessFactor = 0.2f};
    compare(lastDeferred, sample(true, true, true));
    LOG_INFO("Deferred uses the base PBR material when clearcoat is present.");
    graph.reset();
    for (auto& data : frameData)
        data.reset();
    for (auto& frame : frames)
        frame.reset();
    context().config->setRenderFeature(originalFeature);
    context().config->setRenderPath(originalPath);
}
}  // namespace

int main(int, char** argv)
{
    logger::init(argv[0]);
    ConfigManager    config;
    AssetManager     assets;
    AssetDataManager data;
    SceneManager     scenes;
    EditorCamera     scenesCamera{scenes.scene().editorCamera};
    Window           window;
    Renderer         renderer;
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
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();

    SceneAsset scene;
    scene.editorCamera.position = {0, 0, 5};
    scene.editorCamera.yaw      = -90;
    scene.editorCamera.pitch    = 0;
    auto cube                   = std::make_unique<AStaticMesh>();
    cube->transform().scale     = glm::vec3{2.0f};
    auto& material              = cube->getComponent<RenderComponent>()->materialOverrides[0];
    material.baseColorFactor    = glm::vec4{0.4f, 0.6f, 0.8f, 1.0f};
    material.metallic           = 0.3f;
    material.roughness          = 0.65f;
    material.ao                 = 0.65f;
    material.emissive           = glm::vec3{0.04f, 0.02f, 0.01f};
    scene.actors.push_back(std::move(cube));
    auto light                                        = std::make_unique<ALight>();
    light->transform().position                       = {2, 0, 3};
    light->getComponent<LightComponent>()->intensity  = 30.0f;
    light->getComponent<LightComponent>()->castShadow = true;
    scene.actors.push_back(std::move(light));
    scenes.load(scene);
    renderer.init();
    checkDeferred(renderer, scenes);
    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.shutdown();
    context() = {};
}
