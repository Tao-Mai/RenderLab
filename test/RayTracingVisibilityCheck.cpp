#include "editor/EditorCamera.h"
// Manual GPU check: PBR materials, visibility, BRDF bounces, and triangle light emission.
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
#include "render/feature/EndPass.h"
#include "render/raytracing/BlasBuildPass.h"
#include "render/raytracing/RayTracingPass.h"
#include "scene/SceneManager.h"
#include "scene/SceneTypes.h"
#include "scene/system/RenderSystem.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
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
            .scope            = RenderGraph::ResourceScope::PerFrame,
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

    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const
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

void checkVisibility(Renderer& renderer, SceneManager& scenes)
{
    EditorCamera scenesCamera{scenes.scene().editorCamera};
    auto&        vulkan = renderer.vulkanContext();
    CHECK(vulkan.supportsRayTracing());
    std::array<FrameContext, maxFramesInFlight> frames;
    for (auto& frame : frames)
        frame.init(vulkan);
    std::array<FrameData, maxFramesInFlight> frameData;
    for (auto& data : frameData)
        data.init(vulkan, *renderContext().device);

    RenderGraph       graph;
    BlasBuildPass     blasBuild;
    TlasBuildPass     tlasBuild;
    RayTracingPass    trace;
    PixelReadbackPass readback;
    EndPass           end;
    RenderSystem      renderSystem;
    graph.init(vulkan, renderer.swapchainHandle());
    const PassSetup           setup{vulkan, renderer.swapchainHandle(), frameData};
    RenderGraph::BufferHandle buffer;
    const auto                rebuildGraph = [&]
    {
        graph.releaseResources();
        const auto blas       = graph.addPass(blasBuild, setup);
        const auto structures = graph.addPass(tlasBuild, setup, blas);
        const auto output     = graph.addPass(trace, setup, structures);
        CHECK(graph.imageInfo(output.rayImage).format == kHdrColorFormat);
        buffer = graph.addPass(readback, setup, output.rayImage);
        graph.addPass(end, setup, RenderGraph::ImageHandle{}, buffer);
        graph.compile(setup);
    };
    rebuildGraph();

    const auto extent = renderer.swapchainHandle().extent();
    const auto view =
        scenesCamera.renderView({0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height)});
    const auto sample = [&]
    {
        // Independent lighting checks use the same initial random sample.
        rebuildGraph();
        const auto& sceneData = renderSystem.prepare(scenes.scene());
        const auto  prepared  = prepareRenderScene(
            sceneData,
            view.uniforms,
            {.enable = false},
            *context().assetManager,
            context().profiler
        );
        const auto& data = renderContext().resources->prepare(sceneData, prepared, 0);
        frameData[0].updateView(view.uniforms);
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
        return glm::unpackHalf1x16(pixel[0]);
    };

    const auto clear = sample();
    CHECK(clear > 1.0f, "HDR surface luminance is unexpectedly clipped or dark: {}", clear);

    {
        auto& receiver           = *scenes.scene().actors.front()->getComponent<RenderComponent>();
        auto  originalOverrides  = receiver.materialOverrides;
        auto& material           = receiver.materialOverrides[0];
        material.baseColorFactor = glm::vec4{0.8f, 0.2f, 0.1f, 1.0f};
        material.metallic        = 0.0f;
        material.roughness       = 0.12f;
        const auto dielectric    = sample();
        material.metallic        = 1.0f;
        const auto smoothMetal   = sample();
        material.roughness       = 0.8f;
        const auto roughMetal    = sample();
        CHECK(std::isfinite(dielectric) && dielectric > 0);
        CHECK(std::isfinite(smoothMetal) && smoothMetal > 0);
        CHECK(std::isfinite(roughMetal) && roughMetal > 0);
        CHECK(dielectric != smoothMetal && smoothMetal != roughMetal);

        material.metallic = 0.0f;
        material.normalTexture =
            TextureBinding{BuiltinAssets::Texture::kWhiteLinear, BuiltinAssets::Sampler::linearRepeat};
        material.normalScale   = 0.0f;
        const auto unperturbed = sample();
        material.normalScale   = 1.0f;
        const auto perturbed   = sample();
        CHECK(std::isfinite(perturbed) && perturbed >= 0 && perturbed != unperturbed);

        auto& point               = *scenes.scene().actors[1]->getComponent<PointLightComponent>();
        point.enabled             = false;
        material.emissive         = glm::vec3{0.5f, 0.125f, 0.025f};
        material.emissiveStrength = 4.0f;
        CHECK(sample() == 2.0f);
        material.emissiveStrength = 2.0f;
        CHECK(sample() == 1.0f);
        material.emissiveStrength = 0.0f;
        CHECK(sample() == 0.0f);
        material.emissiveStrength.reset();
        CHECK(sample() == 0.5f);
        point.enabled              = true;
        receiver.materialOverrides = std::move(originalOverrides);
        CHECK(sample() == clear);
        LOG_INFO(
            "PBR passed: dielectric {}, smooth metal {}, rough metal {}, normal {} -> {}, emission 2",
            dielectric,
            smoothMetal,
            roughMetal,
            unperturbed,
            perturbed
        );
    }

    // Proportional, colocated point lights yield the same compensated estimate whichever is selected.
    const auto& pointActor            = *scenes.scene().actors[1];
    const auto& point                 = *pointActor.getComponent<PointLightComponent>();
    auto&       secondPoint           = scenes.createActor("ALight");
    secondPoint.transform().position  = pointActor.transform().position;
    auto& second                      = *secondPoint.getComponent<PointLightComponent>();
    second.color                      = point.color;
    second.luminousIntensity          = point.luminousIntensity * 3.0f;
    auto& ignoredPoint                = scenes.createActor("ALight");
    ignoredPoint.transform().position = pointActor.transform().position;
    auto& ignored                     = *ignoredPoint.getComponent<PointLightComponent>();
    ignored.luminousIntensity         = point.luminousIntensity * 100.0f;
    ignored.color                     = glm::vec3{0.0f};

    constexpr float kSamplingRelativeTolerance = 0.005f;
    for (int index = 0; index < 8; ++index)
    {
        CHECK(std::abs(sample() - clear * 4.0f) <= clear * 4.0f * kSamplingRelativeTolerance);
        ignored.color   = point.color;
        ignored.enabled = false;
    }
    scenes.deleteActor(secondPoint.selectionId());
    scenes.deleteActor(ignoredPoint.selectionId());
    CHECK(sample() == clear);
    LOG_INFO("Streaming point-light selection passed: 1:3 weights, inverse-PMF compensation, black/disabled lights");

    auto& blocker                = scenes.createActor("AStaticMesh");
    blocker.transform().position = {1, 0, 2};
    blocker.transform().scale    = glm::vec3{0.6f};
    const auto blocked           = sample();
    CHECK(blocked == 0, "blocker did not shadow the surface: {}", blocked);

    blocker.transform().position = {4, 0, 5};
    const auto beyondLight       = sample();
    CHECK(beyondLight == clear, "geometry behind the light changed visibility: {} vs {}", beyondLight, clear);
    LOG_INFO("Ray tracing visibility passed: clear {}, blocked {}, beyond light {}", clear, blocked, beyondLight);

    // Same image, different sampler: the receiver's texture index stays 0 while
    // the preceding off-camera material occupies sampler slot 0.
    auto& receiver          = *scenes.scene().actors.front()->getComponent<RenderComponent>();
    auto  originalOverrides = receiver.materialOverrides;
    receiver.materialOverrides[0].baseColorTexture =
        TextureBinding{BuiltinAssets::Texture::white, BuiltinAssets::Sampler::linearClamp};
    auto&      samplerCarrier           = scenes.createActor("AStaticMesh");
    const auto carrierId                = samplerCarrier.selectionId();
    samplerCarrier.transform().position = {1000, 1000, 1000};
    auto& actors                        = scenes.scene().actors;
    std::rotate(actors.begin(), actors.end() - 1, actors.end());
    CHECK(sample() == beyondLight, "independent texture/sampler indices changed base color");
    scenes.deleteActor(carrierId);
    receiver.materialOverrides = std::move(originalOverrides);

    auto&       sun            = *scenes.scene().actors[1];
    const float pointIntensity = sun.getComponent<LocalLightComponent>()->luminousIntensity;
    replaceLightComponent(*sun.getComponent<LightComponent>(), std::make_unique<DirectionalLightComponent>());
    auto& sunData       = *sun.getComponent<DirectionalLightComponent>();
    sunData.illuminance = 100;
    sun.transform().setRotationEulerDegrees({0, 45, 0});
    blocker.transform().position = {4, 3, 5};
    const auto directionalClear  = sample();
    CHECK(std::isfinite(directionalClear) && directionalClear > 0);
    blocker.transform().position = {1, 0, 2};
    CHECK(sample() == 0);
    blocker.transform().position = {20, 0, 21};
    CHECK(sample() == 0);
    blocker.transform().position = {4, 3, 5};
    sun.transform().position     = {200, 0, 300};
    CHECK(sample() == directionalClear);
    sunData.enabled = false;
    CHECK(sample() == 0);
    sunData.enabled = true;
    sun.transform().setRotationEulerDegrees({0, 225, 0});
    CHECK(sample() == 0);
    LOG_INFO(
        "Directional light passed: luminance {}, near/far blockers, position independence and direction",
        directionalClear
    );

    replaceLightComponent(sunData, std::make_unique<PointLightComponent>());
    sun.getComponent<PointLightComponent>()->luminousIntensity = pointIntensity;
    sun.transform().setRotationEulerDegrees({0, 0, 0});
    sun.transform().position     = {2, 0, 3};
    blocker.transform().position = {4, 0, 5};

    // Two facing diffuse planes catch the secondary ray without blocking the camera or point light.
    for (float height : {-2.0f, 2.0f})
    {
        auto& plane                                   = scenes.createActor("AStaticMesh");
        plane.getComponent<RenderComponent>()->meshId = BuiltinAssets::Mesh::plane;
        plane.transform().position.y                  = height;
        plane.transform().scale                       = {100.0f, 1.0f, 100.0f};
        if (height > 0)
            plane.transform().setRotationEulerDegrees({180.0f, 0.0f, 0.0f});
    }

    auto feature = context().config->rendererConfig().feature;
    for (uint32_t depth : {3u, 4u, 2u})
    {
        feature.rayTracing.maxRecursionDepth = depth;
        context().config->setRenderFeature(feature);
        rebuildGraph();
        const auto luminance = sample();
        CHECK(
            std::isfinite(luminance) && luminance > 0,
            "invalid luminance {} at recursion depth {}",
            luminance,
            depth
        );
        if (depth > kMinRayRecursionDepth)
            CHECK(luminance > clear, "secondary ray contributed no indirect light at depth {}: {}", depth, luminance);
        else
            CHECK(luminance == clear);
        LOG_INFO("Ray tracing recursion depth {} passed: luminance {}", depth, luminance);
    }

    SceneAsset areaScene;
    areaScene.editorCamera                          = scenes.scene().editorCamera;
    auto sphere                                     = std::make_unique<AStaticMesh>();
    sphere->getComponent<RenderComponent>()->meshId = BuiltinAssets::Mesh::sphere;
    areaScene.actors.push_back(std::move(sphere));
    auto areaLight                  = std::make_unique<ALight>(std::make_unique<RectAreaLightComponent>());
    areaLight->transform().position = {0, 0, 7};
    auto& lightData                 = *areaLight->getComponent<RectAreaLightComponent>();
    lightData.areaSize              = {20, 20};
    lightData.luminousIntensity     = 8.0f * 20.0f * 20.0f;
    areaScene.actors.push_back(std::move(areaLight));
    auto disabledSun = std::make_unique<ALight>(std::make_unique<DirectionalLightComponent>());
    disabledSun->getComponent<DirectionalLightComponent>()->enabled = false;
    areaScene.actors.push_back(std::move(disabledSun));  // Rect TLAS IDs must include preceding light groups.
    scenes.load(areaScene);
    rebuildGraph();

    const auto areaClear = sample();
    CHECK(std::isfinite(areaClear) && areaClear > 0);
    auto& light   = *scenes.scene().actors[1]->getComponent<RectAreaLightComponent>();
    light.enabled = false;
    CHECK(sample() == 0);
    light.enabled = true;
    scenes.scene().actors[1]->transform().setRotationEulerDegrees({0, 180, 0});
    CHECK(sample() == 0);
    scenes.scene().actors[1]->transform().setRotationEulerDegrees({0, 0, 0});

    // No other surface can receive this outgoing ray: terminal emission and full tracing must agree.
    feature.rayTracing.maxRecursionDepth = 3;
    context().config->setRenderFeature(feature);
    rebuildGraph();
    CHECK(sample() == areaClear);
    feature.rayTracing.maxRecursionDepth = kMinRayRecursionDepth;
    context().config->setRenderFeature(feature);
    rebuildGraph();

    auto& areaBlocker                = scenes.createActor("AStaticMesh");
    areaBlocker.transform().position = {0, 0, 6};
    areaBlocker.transform().scale    = {100, 100, 0.2f};
    CHECK(sample() == 0);
    areaBlocker.transform().position.z = 8;
    CHECK(sample() == areaClear);
    LOG_INFO(
        "Triangle area light passed: luminance {}, disabled/back face/blocked 0, beyond light unchanged",
        areaClear
    );

    scenes.deleteActor(areaBlocker.selectionId());
    auto& farLight                = scenes.createActor("ALight");
    farLight.transform().position = {0, 0, 9};
    replaceLightComponent(*farLight.getComponent<LightComponent>(), std::make_unique<RectAreaLightComponent>());
    auto& farData             = *farLight.getComponent<RectAreaLightComponent>();
    farData.areaSize          = {20, 20};
    farData.luminousIntensity = 100.0f * 20.0f * 20.0f;
    // The near emitter still occludes the farther lamp when its emitted luminance is zero.
    light.luminousIntensity = 0;
    CHECK(sample() == 0);
    light.enabled = false;
    CHECK(sample() > 0);

    SceneAsset onlyLights;
    onlyLights.editorCamera    = areaScene.editorCamera;
    auto lamp                  = std::make_unique<ALight>(std::make_unique<RectAreaLightComponent>());
    lamp->transform().position = {0, 0, 3};
    onlyLights.actors.push_back(std::move(lamp));
    scenes.load(onlyLights);
    // Light geometry is excluded from primary rays, including scenes with no mesh HitData.
    CHECK(sample() == 0);
    LOG_INFO("Triangle lights passed: nearest emitter, disabled index mapping, and camera mask without scene meshes");

    graph.reset();
    for (auto& data : frameData)
        data.reset();
    for (auto& frame : frames)
        frame.reset();
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
    const auto originalFeature           = config.rendererConfig().feature;
    auto       feature                   = originalFeature;
    feature.rayTracing.maxRecursionDepth = kMinRayRecursionDepth;
    config.setRenderFeature(feature);
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();

    SceneAsset scene;
    scene.editorCamera.position                     = {0, 0, 5};
    scene.editorCamera.yaw                          = -90;
    scene.editorCamera.pitch                        = 0;
    auto sphere                                     = std::make_unique<AStaticMesh>();
    sphere->getComponent<RenderComponent>()->meshId = BuiltinAssets::Mesh::sphere;
    scene.actors.push_back(std::move(sphere));
    auto light                                                    = std::make_unique<ALight>();
    light->transform().position                                   = {2, 0, 3};
    light->getComponent<LocalLightComponent>()->luminousIntensity = 100.0f;
    scene.actors.push_back(std::move(light));
    scenes.load(scene);
    renderer.init();
    checkVisibility(renderer, scenes);
    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.setRenderFeature(originalFeature);
    config.shutdown();
    context() = {};
}
