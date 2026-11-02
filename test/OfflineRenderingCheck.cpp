#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <chrono>
#include <thread>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "LightingSetup.h"
#include "LocalLightSamplingCheck.h"
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/pbrt/PbrtMaterial.h"
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
double conductorFresnel(double cosine, double eta, double k)
{
    const std::complex<double> index{eta, k};
    const auto                 transmittedCosine = std::sqrt(1.0 - (1.0 - cosine * cosine) / (index * index));
    const auto                 parallel = (index * cosine - transmittedCosine) / (index * cosine + transmittedCosine);
    const auto perpendicular            = (cosine - index * transmittedCosine) / (cosine + index * transmittedCosine);
    return 0.5 * (std::norm(parallel) + std::norm(perpendicular));
}

double conductorBrdf(glm::dvec3 wo, glm::dvec3 wi, glm::dvec2 alpha, double eta, double k)
{
    constexpr double kPi           = 3.14159265358979323846;
    const auto       halfDirection = glm::normalize(wo + wi);
    const double     cos2          = halfDirection.z * halfDirection.z;
    const double     phi           = std::atan2(halfDirection.y, halfDirection.x);
    const double     e =
        (1.0 - cos2) / cos2 * (std::pow(std::cos(phi) / alpha.x, 2) + std::pow(std::sin(phi) / alpha.y, 2));
    const double distribution = 1.0 / (kPi * alpha.x * alpha.y * cos2 * cos2 * (1.0 + e) * (1.0 + e));
    const auto   lambda       = [&](glm::dvec3 w)
    { return 0.5 * (std::sqrt(1.0 + (std::pow(alpha.x * w.x, 2) + std::pow(alpha.y * w.y, 2)) / (w.z * w.z)) - 1.0); };
    return conductorFresnel(glm::dot(wo, halfDirection), eta, k) * distribution /
        ((1.0 + lambda(wo) + lambda(wi)) * 4.0 * wo.z * wi.z);
}

struct PixelReadbackPass
{
    RenderGraph::ImageHandle  image;
    RenderGraph::BufferHandle buffer;
    uint32_t                  regionSize = 1;

    RenderGraph::BufferHandle setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle source)
    {
        image  = source;
        buffer = graph.write(graph.createBuffer({
            .size             = sizeof(uint16_t) * 4 * regionSize * regionSize,
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
            .imageOffset = {int32_t(extent.width / 2 - regionSize / 2), int32_t(extent.height / 2 - regionSize / 2), 0},
            .imageExtent = {regionSize, regionSize, 1},
        };
        commandBuffer.copyImageToBuffer(
            graph.getImage(image).handle(),
            vk::ImageLayout::eTransferSrcOptimal,
            graph.getBuffer(buffer).handle(),
            region
        );
    }
};

void checkAccumulatedPixels(
    Renderer& renderer, SceneManager& scenes, EditorCamera& camera, OfflineAlgorithm algorithm = OfflineAlgorithm::PT
)
{
    auto settings                      = context().config->rendererConfig().feature;
    settings.offline.algorithm         = algorithm;
    settings.offline.bdpt.maxBounces   = 1;
    settings.offline.bdpt.lightTracing = false;
    context().config->setRenderFeature(settings);
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
    const auto extent =
        algorithm == OfflineAlgorithm::BDPT ? vk::Extent2D{64, 64} : renderer.swapchainHandle().extent();
    readback.regionSize = algorithm == OfflineAlgorithm::BDPT ? 16 : 1;
    const PassSetup setup{vulkan, extent, {&data, 1}, context().config->rendererConfig()};
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
    std::vector<OfflineLightData>          extraLights;
    const OfflineSceneData*                preparedScene = nullptr;
    std::optional<SceneRenderData>         fixtureScene;
    std::optional<RenderView>              fixtureView;
    OfflineSceneData                       fixtureData;
    std::optional<OfflineMaterialPreview>  fixtureMaterial;
    std::vector<OfflineTextureInstruction> fixtureTextures;
    uint64_t                               fixtureRevision = 0, fixtureSourceRevision = ~uint64_t{0};
    glm::vec3                              lastPixel{};
    float                                  regionMean = 0;
    std::optional<uint32_t>                glassDraw;
    const auto                             sample = [&](bool backgroundOnly = false)
    {
        uint32_t   targetSamples = 0;
        const auto started       = std::chrono::steady_clock::now();
        for (;;)
        {
            const auto view =
                fixtureView ? *fixtureView : camera.renderView({0, 0, float(extent.width), float(extent.height)});
            auto snapshot = fixtureScene ? *fixtureScene : system.prepare(scenes.scene());
            if (backgroundOnly)
            {
                snapshot.drawItems.clear();
                snapshot.offlineLights.clear();
            }
            snapshot.offlineLights.insert(snapshot.offlineLights.end(), extraLights.begin(), extraLights.end());
            graph.beginFrame(0);
            auto draws = renderer.prepare(view, snapshot, 0);
            if (fixtureScene)
            {
                // Fix the fixture's Lambert albedo without creating persistent test material assets.
                fixtureData                            = *draws.offlineScene;
                constexpr uint64_t kFixtureRevisionBit = uint64_t{1} << 63;
                if (fixtureSourceRevision != draws.offlineScene->revision)
                {
                    fixtureSourceRevision = draws.offlineScene->revision;
                    ++fixtureRevision;
                }
                fixtureData.revision  = kFixtureRevisionBit | fixtureRevision;
                fixtureData.materials = {
                    {.instructionCount = 1},
                    {.model = OfflineMaterialPreviewModel::Color, .firstTexture = 0}
                };
                fixtureData.textures = {
                    {.metadata = {std::to_underlying(OfflineTextureOp::Program), 1, 0, 0}},
                    {.metadata = {std::to_underlying(OfflineTextureOp::Constant), 0, 0, 0},
                     .value    = {0.5f, 0.5f, 0.5f, 0}},
                };
                if (fixtureMaterial)
                {
                    fixtureData.materials = {*fixtureMaterial};
                    fixtureData.textures  = fixtureTextures;
                }
                for (auto& draw : fixtureData.draws)
                    draw.materialIndex = 0;
                if (glassDraw)
                {
                    const auto etaTexture = uint32_t(fixtureData.textures.size());
                    fixtureData.textures.push_back(
                        {.metadata = {std::to_underlying(OfflineTextureOp::Program), 1, 0, 0}}
                    );
                    fixtureData.textures.push_back(
                        {.metadata = {std::to_underlying(OfflineTextureOp::Constant), 0, 0, 0},
                         .value    = {1.5f, 1.5f, 1.5f, 0}}
                    );
                    fixtureData.draws[*glassDraw].materialIndex = uint32_t(fixtureData.materials.size());
                    fixtureData.materials.push_back(
                        {.model = OfflineMaterialPreviewModel::SmoothDielectric, .firstTexture = etaTexture}
                    );
                }
                draws.offlineScene = &fixtureData;
            }

            preparedScene = draws.offlineScene;
            if (!fixtureScene)
                for (size_t index = 0; index < snapshot.drawItems.size(); ++index)
                {
                    const auto& asset =
                        context().assetManager->get(snapshot.drawItems[index].materialId).as<PbrtMaterial>();
                    if (const auto* conductor = std::get_if<PbrtMaterial::Conductor>(&asset.model))
                    {
                        const auto& material = preparedScene->materials[preparedScene->draws[index].materialIndex];
                        CHECK(material.model == OfflineMaterialPreviewModel::Conductor);
                        CHECK(material.inputs.z == (conductor->remapRoughness ? 1u : 0u));
                        CHECK(
                            material.inputs.w ==
                            (std::holds_alternative<PbrtMaterial::Reflectance>(conductor->optics) ? 1u : 0u)
                        );
                    }
                }

            size_t expectedLights = 0, expectedTriangles = 0, expectedAreaEmissions = 0;
            for (const auto& light : snapshot.offlineLights)
            {
                if (const auto* area = std::get_if<OfflineLightData::MeshArea>(&light.model))
                {
                    const auto& item = snapshot.drawItems[area->drawItemIndex];
                    const auto  count =
                        context().assetManager->get(item.mesh).submeshes[item.submeshIndex].indexCount / 3;
                    const auto first = preparedScene->draws[area->drawItemIndex].firstAreaLight;
                    CHECK(first + count <= preparedScene->lightTriangles.size());
                    for (uint32_t index = 0; index < count; ++index)
                    {
                        const auto& triangle = preparedScene->lightTriangles[first + index];
                        CHECK(triangle.emissionIndex == preparedScene->lightTriangles[first].emissionIndex);
                        CHECK(triangle.twoSided == (area->twoSided ? 1u : 0u));
                        const auto area = glm::length(
                                              glm::cross(
                                                  glm::vec3{triangle.p1 - triangle.p0Area},
                                                  glm::vec3{triangle.p2 - triangle.p0Area}
                                              )
                                          ) *
                            0.5f;
                        CHECK(std::abs(area - triangle.p0Area.w) < 1e-5f);
                    }
                    expectedLights += count;
                    expectedTriangles += count;
                    ++expectedAreaEmissions;
                }
                else
                    ++expectedLights;
            }

            CHECK(
                preparedScene->pointLights.size() + preparedScene->spotLights.size() +
                    preparedScene->directionalLights.size() + preparedScene->lightTriangles.size() +
                    (preparedScene->hasEnvironmentLight ? 1u : 0u) + preparedScene->projectionLights.size() +
                    preparedScene->goniometricLights.size() ==
                expectedLights
            );
            CHECK(preparedScene->areaEmissions.size() == expectedAreaEmissions);
            CHECK(preparedScene->lightTriangles.size() == expectedTriangles);

            uploadTestLights(graph, lightBuffer, draws.lights);
            data.uploadSceneUniforms(view.uniforms);
            const PassFrame execution{setup, data, view, draws, {}, 0, 1.0f / 60.0f};
            graph.prepare(execution);
            if (targetSamples == 0)
                targetSamples = pass.sampleCount() + 1;

            auto& recording = frame.commandBufferHandle();
            vkCheck(recording.reset());
            vkCheck(recording.begin({}));
            graph.execute(recording, execution, frame.gpuProfiler(), context().profiler);
            vkCheck(recording.end());

            const auto           commandBuffer = *frame.commandBufferHandle();
            const vk::SubmitInfo submitInfo{.commandBufferCount = 1, .pCommandBuffers = &commandBuffer};
            vkCheck(vulkan.queueHandle().submit(submitInfo));
            vkCheck(vulkan.queueHandle().waitIdle());

            std::vector<uint16_t> pixels(readback.regionSize * readback.regionSize * 4);
            graph.getBuffer(buffer).download(pixels.data(), pixels.size() * sizeof(uint16_t));
            const auto center = (readback.regionSize / 2 * readback.regionSize + readback.regionSize / 2) * 4;
            lastPixel         = {
                glm::unpackHalf1x16(pixels[center]),
                glm::unpackHalf1x16(pixels[center + 1]),
                glm::unpackHalf1x16(pixels[center + 2])
            };

            regionMean = 0;
            for (size_t index = 0; index < pixels.size(); index += 4)
                regionMean += glm::unpackHalf1x16(pixels[index]);
            regionMean /= readback.regionSize * readback.regionSize;

            if (pass.sampleCount() >= targetSamples)
                return lastPixel.x;

            CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{90});
            glfwPollEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
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
    extraLights[0].emission.power       = 12.0;
    extraLights[5].emission.illuminance = 8.0;
    extraLights[5].emission.spectrum    = PbrtConstantSpectrum{0.25};
    extraLights[5].emission.scale       = 2.0;
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
    CHECK(preparedScene->hasEnvironmentLight);
    CHECK(preparedScene->environmentLight.emission.normalization.y == 8.0f);
    CHECK(preparedScene->environmentLight.portalCount == 4);
    CHECK(preparedScene->lightPortals.size() >= 4);
    CHECK(std::abs(sample(true) - 0.5f) < 0.001f);
    CHECK(pass.sampleCount() == 1);
    sample(true);
    CHECK(pass.sampleCount() == 2);
    extraLights[5].emission.image =
        PbrtImageReference{.image = {BuiltinAssets::Texture::kWhiteLinear, BuiltinAssets::Sampler::linearClamp}};
    CHECK(std::abs(sample(true) - 2.0f) < 0.001f);
    CHECK(pass.sampleCount() == 1);
    extraLights.clear();
    CHECK(sample(true) == 0.0f);
    LOG_INFO("Offline lights passed: triangle expansion, all light types, environment/image readback and light reset");

    const auto& source = system.prepare(scenes.scene());
    CHECK(!source.drawItems.empty());
    fixtureScene = SceneRenderData{};
    fixtureScene->drawItems.push_back(
        {.mesh           = BuiltinAssets::Mesh::plane,
         .materialId     = source.drawItems.front().materialId,
         .offlineSurface = PbrtSurface{},
         .model          = glm::scale(glm::mat4{1}, glm::vec3{4})}
    );
    const auto viewMatrix = glm::lookAt(glm::vec3{0, 1, 3}, glm::vec3{0}, glm::vec3{0, 1, 0});
    auto       projection = glm::perspective(glm::radians(45.0f), float(extent.width) / extent.height, 0.1f, 100.0f);
    projection[1][1] *= -1;
    const auto viewProjection = projection * viewMatrix;
    fixtureView               = RenderView{
                      .uniforms =
                          {.viewProjection        = viewProjection,
                           .inverseViewProjection = glm::inverse(viewProjection),
                           .cameraPosition        = {0, 1, 3, 1},
                           .view                  = viewMatrix},
                      .viewport = {0, 0, float(extent.width), float(extent.height)}
    };
    auto feature                  = context().config->rendererConfig().feature;
    feature.offline.pt.maxBounces = 1;
    context().config->setRenderFeature(feature);
    constexpr float  kPi = 3.14159265358979323846f;
    OfflineLightData point{
        .emission = {.spectrum = PbrtConstantSpectrum{4.0 * kPi}},
        .model    = OfflineLightData::Point{{0, 2, 0}}
    };
    if (algorithm == OfflineAlgorithm::BDPT)
    {
        const auto average = [&](uint32_t count)
        {
            for (uint32_t index = 0; index < count; ++index)
                sample();
            CHECK(std::isfinite(regionMean));
            return regionMean;
        };
        const auto floorPosition = [&](uint32_t x, uint32_t y)
        {
            const glm::vec2 ndc   = (glm::vec2{x, y} + 0.5f) / glm::vec2{extent.width, extent.height} * 2.0f - 1.0f;
            const auto      point = fixtureView->uniforms.inverseViewProjection * glm::vec4{ndc, 1, 1};
            const auto      cameraPosition = glm::vec3{fixtureView->uniforms.cameraPosition};
            const auto      direction      = glm::normalize(glm::vec3{point} / point.w - cameraPosition);
            return cameraPosition - direction * (cameraPosition.y / direction.y);
        };
        const auto integrateRegion = [&](const auto& evaluate)
        {
            double sum = 0;
            for (uint32_t y = extent.height / 2 - 8; y < extent.height / 2 + 8; ++y)
                for (uint32_t x = extent.width / 2 - 8; x < extent.width / 2 + 8; ++x)
                    sum += evaluate(floorPosition(x, y));
            return float(sum / 256.0);
        };
        fixtureScene->offlineLights = {point};
        const float expectedPoint   = integrateRegion(
            [](glm::vec3 position)
            {
                const auto offset = glm::vec3{0, 2, 0} - position;
                return 2.0f * offset.y / std::pow(glm::length(offset), 3);
            }
        );
        const float pointNee = average(16);
        CHECK(std::abs(pointNee - expectedPoint) < 0.01f);
        feature.offline.bdpt.lightTracing = true;
        context().config->setRenderFeature(feature);
        const float pointSplats = average(256);
        CHECK(
            std::abs(pointSplats - expectedPoint) < 0.04f,
            "BDPT point/splat energy mismatch: {} vs {}",
            pointSplats,
            expectedPoint
        );
        feature.offline.bdpt.lightSelection = BdptLightSelection::Uniform;
        feature.offline.bdpt.misHeuristic   = BdptMisHeuristic::Power;
        context().config->setRenderFeature(feature);
        const float pointPower = average(128);
        CHECK(std::abs(pointPower - expectedPoint) < 0.05f);

        feature.offline.bdpt.lightTracing = false;
        context().config->setRenderFeature(feature);
        auto spot = point;
        spot.model =
            OfflineLightData::Spot{.position = {0, 2, 0}, .direction = {0, -1, 0}, .cosInner = 0.98f, .cosOuter = 0.8f};
        fixtureScene->offlineLights       = {spot};
        const float spotNee               = average(64);
        feature.offline.bdpt.lightTracing = true;
        context().config->setRenderFeature(feature);
        const float spotSplats = average(256);
        CHECK(std::abs(spotNee - spotSplats) < 0.04f);
        OfflineLightData distant{
            .emission = {.spectrum = PbrtConstantSpectrum{kPi}},
            .model    = OfflineLightData::Directional{{0, -1, 0}}
        };
        auto strongerPoint                  = point;
        strongerPoint.emission.scale        = 2.0;
        fixtureScene->offlineLights         = {point, spot, distant, strongerPoint};
        feature.offline.bdpt.lightTracing   = false;
        feature.offline.bdpt.lightSelection = BdptLightSelection::Power;
        context().config->setRenderFeature(feature);
        const float mixedPower              = average(256);
        feature.offline.bdpt.lightSelection = BdptLightSelection::Uniform;
        context().config->setRenderFeature(feature);
        const float mixedUniform  = average(256);
        const float expectedMixed = 3.0f * expectedPoint + spotNee + 0.5f;
        CHECK(
            std::abs(mixedPower - expectedMixed) < 0.06f && std::abs(mixedUniform - expectedMixed) < 0.06f,
            "BDPT mixed light distribution mismatch: power {}, uniform {}, expected {}",
            mixedPower,
            mixedUniform,
            expectedMixed
        );
        LOG_INFO("BDPT mixed lights: power {}, uniform {}, reference {}", mixedPower, mixedUniform, expectedMixed);
        feature.offline.bdpt.lightTracing = true;
        context().config->setRenderFeature(feature);
        fixtureScene->offlineLights = {distant};
        const float distantValue    = average(256);
        CHECK(std::abs(distantValue - 0.5f) < 0.05f, "BDPT distant energy: {}", distantValue);
        fixtureScene->offlineLights = {
            {.emission = {.spectrum = PbrtConstantSpectrum{0.5}}, .model = OfflineLightData::Environment{}}
        };
        const float environmentValue = average(256);
        CHECK(std::abs(environmentValue - 0.25f) < 0.04f, "BDPT environment energy: {}", environmentValue);

        auto ceiling  = fixtureScene->drawItems.front();
        ceiling.model = glm::scale(
            glm::rotate(glm::translate(glm::mat4{1}, glm::vec3{0, 2, 0}), kPi, glm::vec3{1, 0, 0}),
            glm::vec3{4}
        );
        fixtureScene->drawItems.push_back(ceiling);
        fixtureScene->offlineLights = {
            {.emission = {.spectrum = PbrtConstantSpectrum{1.0}}, .model = OfflineLightData::MeshArea{1, false, false}}
        };
        feature.offline.bdpt.misHeuristic   = BdptMisHeuristic::Balance;
        feature.offline.bdpt.lightSelection = BdptLightSelection::Power;
        feature.offline.bdpt.lightTracing   = false;
        context().config->setRenderFeature(feature);
        const float areaNee               = average(256);
        feature.offline.bdpt.lightTracing = true;
        context().config->setRenderFeature(feature);
        const float areaSplats = average(256);
        CHECK(
            areaNee > 0.1f && std::abs(areaNee - areaSplats) < 0.03f,
            "BDPT area MIS energy mismatch: {} vs {}",
            areaNee,
            areaSplats
        );

        auto wall  = fixtureScene->drawItems.front();
        wall.model = glm::scale(
            glm::rotate(glm::translate(glm::mat4{1}, glm::vec3{0, 1, -1}), kPi * 0.5f, glm::vec3{1, 0, 0}),
            glm::vec3{4}
        );
        fixtureScene->drawItems.push_back(wall);
        feature.offline.algorithm     = OfflineAlgorithm::PT;
        feature.offline.pt.maxBounces = 2;
        context().config->setRenderFeature(feature);
        const float indirectPt          = average(256);
        feature.offline.algorithm       = OfflineAlgorithm::BDPT;
        feature.offline.bdpt.maxBounces = 2;
        context().config->setRenderFeature(feature);
        const float indirectBdpt = average(256);
        CHECK(
            std::abs(indirectBdpt - indirectPt) < 0.04f,
            "BDPT surface connection energy mismatch: {} vs PT {}",
            indirectBdpt,
            indirectPt
        );

        // A delta-position light behind glass cannot be found by BSDF-only PT.
        // The light -> glass -> floor -> camera strategy must produce its caustic.
        fixtureScene->drawItems.resize(2);
        fixtureScene->drawItems.back().model =
            glm::translate(glm::mat4{1}, glm::vec3{0, 1, 0}) * glm::scale(glm::mat4{1}, glm::vec3{4});
        const auto lowView                   = glm::lookAt(glm::vec3{0, 0.5f, 3}, glm::vec3{0}, glm::vec3{0, 1, 0});
        fixtureView->uniforms.view           = lowView;
        fixtureView->uniforms.viewProjection = projection * lowView;
        fixtureView->uniforms.inverseViewProjection = glm::inverse(projection * lowView);
        fixtureView->uniforms.cameraPosition        = {0, 0.5f, 3, 1};
        fixtureScene->offlineLights                 = {point};
        glassDraw                                   = 1;
        ++fixtureRevision;
        feature.offline.bdpt.lightTracing = false;
        context().config->setRenderFeature(feature);
        CHECK(average(16) == 0.0f);
        feature.offline.bdpt.lightTracing = true;
        context().config->setRenderFeature(feature);
        const float caustic     = average(512);
        const auto  floorBounds = context()
                                     .assetManager->get(fixtureScene->drawItems.front().mesh)
                                     .bounds.transformed(fixtureScene->drawItems.front().model);
        const float expectedCaustic = integrateRegion(
            [&](glm::vec3 position)
            {
                if (position.x < floorBounds.min.x || position.x > floorBounds.max.x ||
                    position.z < floorBounds.min.z || position.z > floorBounds.max.z)
                    return 0.0;
                constexpr double kEta    = 1.5;
                const double     radius  = std::hypot(position.x, position.z);
                double           minimum = 0, maximum = 1.5707963267948966;
                for (int iteration = 0; iteration < 40; ++iteration)
                {
                    const double theta       = (minimum + maximum) * 0.5;
                    const double transmitted = std::asin(std::sin(theta) / kEta);
                    if (std::tan(theta) + std::tan(transmitted) < radius)
                        minimum = theta;
                    else
                        maximum = theta;
                }
                const double theta       = (minimum + maximum) * 0.5;
                const double transmitted = std::asin(std::sin(theta) / kEta);
                const double cosine = std::cos(theta), transmittedCosine = std::cos(transmitted);
                const double parallel      = (kEta * cosine - transmittedCosine) / (kEta * cosine + transmittedCosine);
                const double perpendicular = (cosine - kEta * transmittedCosine) / (cosine + kEta * transmittedCosine);
                const double transmission  = 1.0 - 0.5 * (parallel * parallel + perpendicular * perpendicular);
                const double derivative    = 1.0 / (cosine * cosine) +
                    cosine / (kEta * transmittedCosine * transmittedCosine * transmittedCosine);
                const double jacobian =
                    radius > 1e-8 ? std::sin(theta) / (radius * derivative) : 1.0 / std::pow(1.0 + 1.0 / kEta, 2);
                return 2.0 * transmission * jacobian;
            }
        );
        CHECK(
            std::abs(caustic - expectedCaustic) < 0.04f,
            "BDPT glass importance transport mismatch: {} vs {}",
            caustic,
            expectedCaustic
        );
        LOG_INFO(
            "BDPT checks passed: point {} / {}, spot {} / {}, distant {}, environment {}, area {} / {}, "
            "indirect {} / PT {}, caustic {} / {}",
            pointSplats,
            expectedPoint,
            spotNee,
            spotSplats,
            distantValue,
            environmentValue,
            areaNee,
            areaSplats,
            indirectBdpt,
            indirectPt,
            caustic,
            expectedCaustic
        );
        graph.reset();
        data.reset();
        frame.reset();
        return;
    }
    fixtureScene->offlineLights = {point};
    const auto pointValue       = sample();
    CHECK(std::abs(pointValue - 0.5f) < 0.01f);
    auto spot = point;
    spot.model =
        OfflineLightData::Spot{.position = {0, 2, 0}, .direction = {0, -1, 0}, .cosInner = 0.9f, .cosOuter = 0.8f};
    fixtureScene->offlineLights.push_back(spot);
    CHECK(std::abs(sample() - 2.0f * pointValue) < 0.01f);
    std::get<OfflineLightData::Spot>(fixtureScene->offlineLights.back().model).direction = {0, 1, 0};
    CHECK(std::abs(sample() - pointValue) < 0.01f);
    fixtureScene->drawItems.push_back(
        {.mesh           = BuiltinAssets::Mesh::cube,
         .materialId     = source.drawItems.front().materialId,
         .offlineSurface = PbrtSurface{},
         .model          = glm::scale(glm::translate(glm::mat4{1}, glm::vec3{0, 1, 0}), glm::vec3{0.4f, 0.2f, 0.4f})}
    );
    CHECK(sample() == 0.0f);
    fixtureScene->drawItems.pop_back();

    OfflineLightData distant{
        .emission = {.spectrum = PbrtConstantSpectrum{kPi}},
        .model    = OfflineLightData::Directional{{0, -1, 0}}
    };
    fixtureScene->offlineLights = {distant};
    CHECK(std::abs(sample() - 0.5f) < 0.005f);
    distant.emission.scale = 2.0;
    fixtureScene->offlineLights.push_back(distant);
    CHECK(std::abs(sample() - 1.5f) < 0.005f);

    auto ceiling  = fixtureScene->drawItems.front();
    ceiling.model = glm::scale(
        glm::rotate(glm::translate(glm::mat4{1}, glm::vec3{0, 2, 0}), kPi, glm::vec3{1, 0, 0}),
        glm::vec3{4}
    );
    fixtureScene->drawItems.push_back(ceiling);
    fixtureScene->offlineLights = {
        {.emission = {.spectrum = PbrtConstantSpectrum{1.0}}, .model = OfflineLightData::MeshArea{1, false, false}}
    };
    float              referenceArea    = 0;
    constexpr uint32_t kQuadratureSteps = 64;
    constexpr float    kStep            = 4.0f / kQuadratureSteps;
    for (uint32_t y = 0; y < kQuadratureSteps; ++y)
        for (uint32_t x = 0; x < kQuadratureSteps; ++x)
        {
            const float u  = -2.0f + (x + 0.5f) * kStep;
            const float v  = -2.0f + (y + 0.5f) * kStep;
            const float r2 = u * u + v * v + 4.0f;
            referenceArea += 0.5f / kPi * 4.0f / (r2 * r2) * kStep * kStep;
        }
    float areaValue = 0;
    for (uint32_t sampleIndex = 0; sampleIndex < 64; ++sampleIndex)
        areaValue = sample();
    CHECK(std::abs(areaValue - referenceArea) < 0.08f, "area MIS energy mismatch: {} vs {}", areaValue, referenceArea);

    fixtureScene->drawItems.back().model = glm::scale(
        glm::rotate(glm::translate(glm::mat4{1}, glm::vec3{0, 2, 0}), kPi, glm::vec3{1, 0, 0}),
        glm::vec3{1e6f}
    );
    fixtureScene->offlineLights = {
        {.emission = {.spectrum = PbrtConstantSpectrum{1.0}}, .model = OfflineLightData::Environment{}}
    };
    CHECK(sample() == 0.0f);

    feature.offline.pt.maxBounces = 2;
    context().config->setRenderFeature(feature);
    float indirectValue = 0;
    for (uint32_t sampleIndex = 0; sampleIndex < 32; ++sampleIndex)
        indirectValue = sample();
    CHECK(
        indirectValue > 0.01f && indirectValue <= 0.25f,
        "two diffuse bounces did not reach the environment: {}",
        indirectValue
    );
    LOG_INFO(
        "Offline diffuse checks passed: point {}, area {} (reference {}), two-bounce {}",
        pointValue,
        areaValue,
        referenceArea,
        indirectValue
    );

    // Check metal reflection against independent complex Fresnel and numerical area integration.
    const glm::vec3 eta{0.2f, 0.6f, 1.1f}, k{3.9f, 2.4f, 2.1f};
    const auto      setConductor =
        [&](glm::vec3 opticalInput, glm::vec2 roughness, bool remap = false, bool reflectanceInput = false)
    {
        fixtureTextures.clear();
        const auto constant = [&](glm::vec3 value)
        {
            const auto index = static_cast<uint32_t>(fixtureTextures.size());
            fixtureTextures.push_back({.metadata = {std::to_underlying(OfflineTextureOp::Program), 1, 0, 0}});
            fixtureTextures.push_back(
                {.metadata = {std::to_underlying(OfflineTextureOp::Constant), 0, 0, 0}, .value = {value, 0}}
            );
            return index;
        };
        fixtureMaterial = OfflineMaterialPreview{
            .model         = OfflineMaterialPreviewModel::Conductor,
            .firstTexture  = constant(opticalInput),
            .secondTexture = constant(k),
            .inputs        = {
                constant(glm::vec3{roughness.x}),
                constant(glm::vec3{roughness.y}),
                remap ? 1u : 0u,
                reflectanceInput ? 1u : 0u
            },
        };
        ++fixtureRevision;
    };
    const auto plane =
        context().assetDataManager->readGeometry(context().assetManager->get(BuiltinAssets::Mesh::plane));
    const auto tangent   = glm::normalize(glm::dvec3{plane.vertices.front().tangent});
    const auto bitangent = glm::cross(glm::dvec3{0, 1, 0}, tangent);
    const auto toLocal   = [&](glm::dvec3 w) { return glm::dvec3{glm::dot(tangent, w), glm::dot(bitangent, w), w.y}; };
    const auto wo        = toLocal(glm::normalize(glm::dvec3{0, 1, 3}));
    fixtureScene->drawItems.resize(1);
    feature.offline.pt.maxBounces = 1;
    context().config->setRenderFeature(feature);
    fixtureScene->offlineLights = {
        {.emission = {.spectrum = PbrtConstantSpectrum{1.0}}, .model = OfflineLightData::Environment{}}
    };
    setConductor(eta, {0, 0});
    sample();
    for (int channel = 0; channel < 3; ++channel)
        CHECK(
            std::abs(lastPixel[channel] - conductorFresnel(wo.z, eta[channel], k[channel])) < 0.003,
            "smooth conductor Fresnel mismatch: {}",
            lastPixel[channel]
        );
    setConductor(glm::vec3{0.6f}, {0, 0}, false, true);
    CHECK(std::abs(sample() - conductorFresnel(wo.z, 1.0, 2.0 * std::sqrt(0.6 / 0.4))) < 0.003);

    fixtureScene->offlineLights = {OfflineLightData{
        .emission = {.spectrum = PbrtConstantSpectrum{kPi}},
        .model    = OfflineLightData::Directional{{0, -1, 0}}
    }};
    const glm::dvec2 alpha{0.2, 0.5};
    setConductor(eta, glm::vec2{alpha});
    const auto roughValue    = sample();
    const auto expectedRough = conductorBrdf(wo, {0, 0, 1}, alpha, eta.x, k.x) * kPi;
    CHECK(
        std::abs(roughValue - expectedRough) < 0.002 + expectedRough * 0.03,
        "rough conductor direct lighting mismatch: {} vs {}",
        roughValue,
        expectedRough
    );
    setConductor(eta, {0.04f, 0.25f}, true);
    CHECK(std::abs(sample() - roughValue) < 0.001f);
    setConductor(eta, {0.5f, 0.2f});
    const auto swappedValue    = sample();
    const auto expectedSwapped = conductorBrdf(wo, {0, 0, 1}, {0.5, 0.2}, eta.x, k.x) * kPi;
    CHECK(std::abs(swappedValue - expectedSwapped) < 0.002 + expectedSwapped * 0.03);
    CHECK(std::abs(swappedValue - roughValue) > 0.01f);

    fixtureScene->drawItems.push_back(ceiling);
    fixtureScene->offlineLights = {
        {.emission = {.spectrum = PbrtConstantSpectrum{1.0}}, .model = OfflineLightData::MeshArea{1, false, false}}
    };
    setConductor(eta, glm::vec2{alpha});
    double referenceMetalArea = 0;
    for (uint32_t y = 0; y < kQuadratureSteps; ++y)
        for (uint32_t x = 0; x < kQuadratureSteps; ++x)
        {
            const glm::dvec3 offset{-2.0 + (x + 0.5) * kStep, 2.0, -2.0 + (y + 0.5) * kStep};
            const double     r2        = glm::dot(offset, offset);
            const auto       direction = offset / std::sqrt(r2);
            referenceMetalArea += conductorBrdf(wo, toLocal(direction), alpha, eta.x, k.x) * direction.y * direction.y /
                r2 * kStep * kStep;
        }
    float metalAreaValue = 0;
    for (uint32_t sampleIndex = 0; sampleIndex < 128; ++sampleIndex)
        metalAreaValue = sample();
    CHECK(
        std::abs(metalAreaValue - referenceMetalArea) < 0.01 + referenceMetalArea * 0.15,
        "conductor area MIS energy mismatch: {} vs {}",
        metalAreaValue,
        referenceMetalArea
    );
    fixtureScene->drawItems.back().model = glm::scale(
        glm::rotate(glm::translate(glm::mat4{1}, glm::vec3{0, 2, 0}), kPi, glm::vec3{1, 0, 0}),
        glm::vec3{32}
    );
    setConductor(eta, {0, 0});
    CHECK(std::abs(sample() - conductorFresnel(wo.z, eta.x, k.x)) < 0.003);
    LOG_INFO(
        "Offline conductor checks passed: rough {}, anisotropic {}, area {} (reference {})",
        roughValue,
        swappedValue,
        metalAreaValue,
        referenceMetalArea
    );
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

    const auto originalPath       = config.rendererConfig().renderPath;
    const auto originalFeature    = config.rendererConfig().feature;
    auto       feature            = originalFeature;
    feature.offline.algorithm     = OfflineAlgorithm::PT;
    feature.offline.pt.maxBounces = 3;
    feature.ibl                   = false;
    feature.shadow.enable         = false;

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
    const auto extent    = renderer.swapchainHandle().extent();
    const auto drawFrame = [&]
    {
        const auto  view     = camera.renderView({0, 0, float(extent.width), float(extent.height)});
        const auto& snapshot = system.prepare(scenes.scene());
        const auto  result   = renderer.render(view, snapshot, {}, 1.0f / 60.0f);
        CHECK(!result.swapchainNeedsRebuild);
        renderer.waitIdle();
    };

    const auto draw = [&]
    {
        const auto started = std::chrono::steady_clock::now();

        // The first frame may apply settings and reset accumulation.
        drawFrame();

        const auto target = renderer.offlineSampleCount() +
            (renderer.offlineSampleProgress() > renderer.offlineSampleCount() || renderer.offlinePreparingPipeline()
                 ? 1u
                 : 0u);
        while (renderer.offlinePreparingPipeline() || renderer.offlineSampleCount() < target)
        {

            CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{90});
            glfwPollEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            drawFrame();
        }
    };

    draw();
    CHECK(renderer.framesInFlight() == 1);
    CHECK(renderer.offlineSampleCount() == 1);
    draw();
    CHECK(renderer.offlineSampleCount() == 2);

    feature.toneMapping.exposure.exposureCompensation += 1;

    config.setRenderFeature(feature);
    draw();
    CHECK(renderer.offlineSampleCount() == 3);
    draw();
    CHECK(renderer.offlineSampleCount() == 4);
    scenes.scene().editorCamera.position.x += 0.25f;
    draw();
    CHECK(renderer.offlineSampleCount() == 1);

    feature.offline.pt.maxBounces = 2;

    config.setRenderFeature(feature);
    draw();
    CHECK(renderer.offlineSampleCount() == 1);
    draw();
    CHECK(renderer.offlineSampleCount() == 2);
    LOG_INFO("Offline rendering passed: accumulation, camera/config reset and display changes");
    checkAccumulatedPixels(renderer, scenes, camera);
    checkAccumulatedPixels(renderer, scenes, camera, OfflineAlgorithm::BDPT);

    feature.offline.algorithm       = OfflineAlgorithm::BDPT;
    feature.offline.bdpt.maxBounces = 2;

    config.setRenderFeature(feature);
    draw();
    CHECK(renderer.offlineSampleCount() == 1);
    draw();
    CHECK(renderer.offlineSampleCount() == 2);

    feature.offline.bdpt.misHeuristic = BdptMisHeuristic::Power;

    config.setRenderFeature(feature);
    draw();
    CHECK(renderer.offlineSampleCount() == 1);

    feature.offline.algorithm = OfflineAlgorithm::PT;

    config.setRenderFeature(feature);
    draw();
    CHECK(renderer.offlineSampleCount() == 1);

    LOG_INFO("PT / BDPT pipeline switching and BDPT parameter resets passed");

    // The editor must keep producing frames during a cold build and a partial BDPT sample.
    feature.offline.algorithm       = OfflineAlgorithm::BDPT;
    feature.offline.bdpt.maxBounces = 6;

    config.setRenderFeature(feature);

    auto began = std::chrono::steady_clock::now();
    drawFrame();
    const auto switchMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    CHECK(switchMilliseconds < 1000.0, "BDPT switch blocked for {} ms", switchMilliseconds);
    CHECK(renderer.offlineSampleCount() == 0);
    CHECK(renderer.offlineSampleProgress() < 1.0f);

    feature.offline.algorithm = OfflineAlgorithm::PT;

    config.setRenderFeature(feature);
    began = std::chrono::steady_clock::now();
    drawFrame();
    const auto cancelMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    CHECK(cancelMilliseconds < 1000.0, "canceling BDPT blocked for {} ms", cancelMilliseconds);
    CHECK(renderer.offlineSampleCount() == 1);

    feature.offline.algorithm = OfflineAlgorithm::BDPT;

    config.setRenderFeature(feature);
    began = std::chrono::steady_clock::now();
    do
    {
        drawFrame();
        CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds{90});
    }
    while (renderer.offlinePreparingPipeline());

    const auto partial = renderer.offlineSampleProgress();
    CHECK(partial > 0 && partial < 1);
    drawFrame();
    CHECK(renderer.offlineSampleProgress() > partial);
    CHECK(renderer.offlineSampleCount() == 0);

    LOG_INFO(
        "BDPT responsiveness: switch {} ms, cancel {} ms, partial sample {} -> {}",
        switchMilliseconds,
        cancelMilliseconds,
        partial,
        renderer.offlineSampleProgress()
    );

    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.setSceneRenderMode(
        originalPath == RenderPath::OfflinePathTracing ? SceneRenderMode::Offline : SceneRenderMode::Realtime
    );
    if (originalPath != RenderPath::OfflinePathTracing)
        config.setRenderPath(originalPath);
    config.setRenderFeature(originalFeature);
    config.shutdown();
    context() = {};
}
