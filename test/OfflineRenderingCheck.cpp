#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/matrix_transform.hpp>
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
    std::vector<OfflineLightData> extraLights;
    const OfflineSceneData* preparedScene = nullptr;
    std::optional<SceneRenderData> fixtureScene;
    std::optional<RenderView> fixtureView;
    OfflineSceneData fixtureData;
    const auto sample = [&](bool backgroundOnly = false)
    {
        const auto view = fixtureView ? *fixtureView
            : camera.renderView({0, 0, float(extent.width), float(extent.height)});
        auto snapshot = fixtureScene ? *fixtureScene : system.prepare(scenes.scene());
        if (backgroundOnly) { snapshot.drawItems.clear(); snapshot.offlineLights.clear(); }
        snapshot.offlineLights.insert(snapshot.offlineLights.end(), extraLights.begin(), extraLights.end());
        graph.beginFrame(0);
        auto draws = renderer.prepare(view, snapshot, 0);
        if (fixtureScene)
        {
            // Fix the fixture's Lambert albedo without creating persistent test material assets.
            fixtureData = *draws.offlineScene;
            constexpr uint64_t kFixtureRevisionBit = uint64_t{1} << 63;
            fixtureData.revision |= kFixtureRevisionBit;
            fixtureData.materials = {{.instructionCount = 1},
                {.model = OfflineMaterialPreviewModel::Color, .firstTexture = 0}};
            fixtureData.textures = {
                {.metadata = {std::to_underlying(OfflineTextureOp::Program), 1, 0, 0}},
                {.metadata = {std::to_underlying(OfflineTextureOp::Constant), 0, 0, 0},
                 .value = {0.5f, 0.5f, 0.5f, 0}},
            };
            for (auto& draw : fixtureData.draws) draw.materialIndex = 0;
            draws.offlineScene = &fixtureData;
        }
        preparedScene = draws.offlineScene;
        size_t expectedLights = 0, expectedTriangles = 0, expectedAreaEmissions = 0;
        for (const auto& light : snapshot.offlineLights)
        {
            if (const auto* area = std::get_if<OfflineLightData::MeshArea>(&light.model))
            {
                const auto& item = snapshot.drawItems[area->drawItemIndex];
                const auto count = context().assetManager->get(item.mesh).submeshes[item.submeshIndex].indexCount / 3;
                const auto first = preparedScene->draws[area->drawItemIndex].firstAreaLight;
                CHECK(first + count <= preparedScene->lightTriangles.size());
                for (uint32_t index = 0; index < count; ++index)
                {
                    const auto& triangle = preparedScene->lightTriangles[first + index];
                    CHECK(triangle.emissionIndex == preparedScene->lightTriangles[first].emissionIndex);
                    CHECK(triangle.twoSided == (area->twoSided ? 1u : 0u));
                    const auto area = glm::length(glm::cross(glm::vec3{triangle.p1 - triangle.p0Area},
                        glm::vec3{triangle.p2 - triangle.p0Area})) * 0.5f;
                    CHECK(std::abs(area - triangle.p0Area.w) < 1e-5f);
                }
                expectedLights += count;
                expectedTriangles += count;
                ++expectedAreaEmissions;
            }
            else ++expectedLights;
        }
        CHECK(preparedScene->pointLights.size() + preparedScene->spotLights.size() +
            preparedScene->directionalLights.size() + preparedScene->lightTriangles.size() +
            preparedScene->environmentLights.size() + preparedScene->projectionLights.size() +
            preparedScene->goniometricLights.size() == expectedLights);
        CHECK(preparedScene->areaEmissions.size() == expectedAreaEmissions);
        CHECK(preparedScene->lightTriangles.size() == expectedTriangles);
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

    extraLights.resize(6);
    extraLights[0].model = OfflineLightData::Point{{4, 5, 6}};
    extraLights[1].model = OfflineLightData::Spot{.position = {1, 2, 3}, .cosInner = 0.9f, .cosOuter = 0.8f};
    extraLights[2].model = OfflineLightData::Directional{};
    extraLights[3].model = OfflineLightData::Projection{.fieldOfView = 47.0f};
    extraLights[4].model = OfflineLightData::Goniometric{};
    extraLights[5].model = OfflineLightData::Environment{.portal = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}};
    extraLights[0].emission.power = 12.0;
    extraLights[5].emission.illuminance = 8.0;
    extraLights[5].emission.spectrum = PbrtConstantSpectrum{0.25};
    extraLights[5].emission.scale = 2.0;
    sample();
    CHECK(pass.sampleCount() == 1);
    CHECK(preparedScene->pointLights.back().position == glm::vec3(4, 5, 6));
    CHECK(preparedScene->pointLights.back().emission.normalization.x == 12.0f);
    CHECK(preparedScene->spotLights.back().position == glm::vec3(1, 2, 3));
    CHECK(preparedScene->spotLights.back().direction == glm::vec3(0, 0, -1));
    CHECK(preparedScene->spotLights.back().cosInner == 0.9f);
    CHECK(preparedScene->spotLights.back().cosOuter == 0.8f);
    CHECK(preparedScene->projectionLights.back().fieldOfView == 47.0f);
    CHECK(!preparedScene->directionalLights.empty());
    CHECK(!preparedScene->goniometricLights.empty());
    CHECK(preparedScene->environmentLights.back().emission.normalization.y == 8.0f);
    CHECK(preparedScene->environmentLights.back().portalCount == 4);
    CHECK(preparedScene->lightPortals.size() >= 4);
    CHECK(std::abs(sample(true) - 0.5f) < 0.001f);
    CHECK(pass.sampleCount() == 1);
    sample(true);
    CHECK(pass.sampleCount() == 2);
    extraLights[5].emission.image = PbrtImageReference{.image = {
        BuiltinAssets::Texture::kWhiteLinear, BuiltinAssets::Sampler::linearClamp}};
    CHECK(std::abs(sample(true) - 2.0f) < 0.001f);
    CHECK(pass.sampleCount() == 1);
    extraLights.clear();
    CHECK(sample(true) == 0.0f);
    LOG_INFO("Offline lights passed: triangle expansion, all light types, environment/image readback and light reset");

    const auto& source = system.prepare(scenes.scene());
    CHECK(!source.drawItems.empty());
    fixtureScene = SceneRenderData{};
    fixtureScene->drawItems.push_back({.mesh = BuiltinAssets::Mesh::plane,
        .materialId = source.drawItems.front().materialId, .offlineSurface = PbrtSurface{},
        .model = glm::scale(glm::mat4{1}, glm::vec3{4})});
    const auto viewMatrix = glm::lookAt(glm::vec3{0, 1, 3}, glm::vec3{0}, glm::vec3{0, 1, 0});
    auto projection = glm::perspective(glm::radians(45.0f), float(extent.width) / extent.height, 0.1f, 100.0f);
    projection[1][1] *= -1;
    const auto viewProjection = projection * viewMatrix;
    fixtureView = RenderView{.uniforms = {.viewProjection = viewProjection,
        .inverseViewProjection = glm::inverse(viewProjection), .cameraPosition = {0, 1, 3, 1}, .view = viewMatrix},
        .viewport = {0, 0, float(extent.width), float(extent.height)}};
    auto feature = context().config->rendererConfig().feature;
    feature.offline.maxBounces = 1;
    context().config->setRenderFeature(feature);
    constexpr float kPi = 3.14159265358979323846f;
    OfflineLightData point{.emission = {.spectrum = PbrtConstantSpectrum{4.0 * kPi}},
        .model = OfflineLightData::Point{{0, 2, 0}}};
    fixtureScene->offlineLights = {point};
    const auto pointValue = sample();
    CHECK(std::abs(pointValue - 0.5f) < 0.01f);
    auto spot = point;
    spot.model = OfflineLightData::Spot{.position = {0, 2, 0}, .direction = {0, -1, 0},
        .cosInner = 0.9f, .cosOuter = 0.8f};
    fixtureScene->offlineLights.push_back(spot);
    CHECK(std::abs(sample() - 2.0f * pointValue) < 0.01f);
    std::get<OfflineLightData::Spot>(fixtureScene->offlineLights.back().model).direction = {0, 1, 0};
    CHECK(std::abs(sample() - pointValue) < 0.01f);
    fixtureScene->drawItems.push_back({.mesh = BuiltinAssets::Mesh::cube,
        .materialId = source.drawItems.front().materialId, .offlineSurface = PbrtSurface{},
        .model = glm::scale(glm::translate(glm::mat4{1}, glm::vec3{0, 1, 0}), glm::vec3{0.4f, 0.2f, 0.4f})});
    CHECK(sample() == 0.0f);
    fixtureScene->drawItems.pop_back();

    OfflineLightData distant{.emission = {.spectrum = PbrtConstantSpectrum{kPi}},
        .model = OfflineLightData::Directional{{0, -1, 0}}};
    fixtureScene->offlineLights = {distant};
    CHECK(std::abs(sample() - 0.5f) < 0.005f);
    distant.emission.scale = 2.0;
    fixtureScene->offlineLights.push_back(distant);
    CHECK(std::abs(sample() - 1.5f) < 0.005f);

    auto ceiling = fixtureScene->drawItems.front();
    ceiling.model = glm::scale(glm::rotate(glm::translate(glm::mat4{1}, glm::vec3{0, 2, 0}),
        kPi, glm::vec3{1, 0, 0}), glm::vec3{4});
    fixtureScene->drawItems.push_back(ceiling);
    fixtureScene->offlineLights = {{.emission = {.spectrum = PbrtConstantSpectrum{1.0}},
        .model = OfflineLightData::MeshArea{1, false, false}}};
    float referenceArea = 0;
    constexpr uint32_t kQuadratureSteps = 64;
    constexpr float kStep = 4.0f / kQuadratureSteps;
    for (uint32_t y = 0; y < kQuadratureSteps; ++y)
        for (uint32_t x = 0; x < kQuadratureSteps; ++x)
        {
            const float u = -2.0f + (x + 0.5f) * kStep;
            const float v = -2.0f + (y + 0.5f) * kStep;
            const float r2 = u * u + v * v + 4.0f;
            referenceArea += 0.5f / kPi * 4.0f / (r2 * r2) * kStep * kStep;
        }
    float areaValue = 0;
    for (uint32_t sampleIndex = 0; sampleIndex < 64; ++sampleIndex) areaValue = sample();
    CHECK(std::abs(areaValue - referenceArea) < 0.08f,
        "area MIS energy mismatch: {} vs {}", areaValue, referenceArea);

    fixtureScene->drawItems.back().model = glm::scale(glm::rotate(
        glm::translate(glm::mat4{1}, glm::vec3{0, 2, 0}), kPi, glm::vec3{1, 0, 0}), glm::vec3{1e6f});
    fixtureScene->offlineLights = {{.emission = {.spectrum = PbrtConstantSpectrum{1.0}},
        .model = OfflineLightData::Environment{}}};
    CHECK(sample() == 0.0f);
    feature.offline.maxBounces = 2;
    context().config->setRenderFeature(feature);
    float indirectValue = 0;
    for (uint32_t sampleIndex = 0; sampleIndex < 32; ++sampleIndex) indirectValue = sample();
    CHECK(indirectValue > 0.01f && indirectValue <= 0.25f,
        "two diffuse bounces did not reach the environment: {}", indirectValue);
    LOG_INFO("Offline diffuse checks passed: point {}, area {} (reference {}), two-bounce {}",
        pointValue, areaValue, referenceArea, indirectValue);
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
    draw();
    CHECK(renderer.offlineSampleCount() == 4);
    scenes.scene().editorCamera.position.x += 0.25f;
    draw();
    CHECK(renderer.offlineSampleCount() == 1);
    feature.offline.maxBounces = 2;
    config.setRenderFeature(feature);
    draw();
    CHECK(renderer.offlineSampleCount() == 1);
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
