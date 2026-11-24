#include "render/raytracing/offline/OfflinePathTracing.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "asset/AssetManager.h"
#include "core/Context.h"
#include "core/Vertex.h"
#include "render/FrameData.h"
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/raytracing/offline/OfflineScene.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include "render/DispatchGroups.h"

namespace
{

struct alignas(16) AccumulatedPixel
{
    glm::vec3 color;
    uint32_t  sampleCount;
};
static_assert(sizeof(AccumulatedPixel) == 16);

constexpr std::array kSceneBufferNames{
    "materialInstructions",
    "textureInstructions",
    "areaEmissions",
    "pointLights",
    "spotLights",
    "directionalLights",
    "lightTriangles",
    "environmentLight",
    "projectionLights",
    "goniometricLights",
    "lightPortals"
};
constexpr std::array<uint64_t, kSceneBufferNames.size()> kSceneBufferSizes{
    sizeof(pbrt::MaterialInstruction),
    sizeof(pbrt::TextureInstruction),
    sizeof(OfflineLightEmission),
    sizeof(OfflinePointLight),
    sizeof(OfflineSpotLight),
    sizeof(OfflineDirectionalLight),
    sizeof(OfflineLightTriangle),
    sizeof(OfflineEnvironmentLight),
    sizeof(OfflineProjectionLight),
    sizeof(OfflineGoniometricLight),
    sizeof(glm::vec4),
};

}  // namespace

void OfflinePathTracing::compile(
    const Falcor::RenderContext& context, Falcor::uint2 frameDims, std::string_view resolveProgram
)
{
    auto& cache = *context.gpuResources;
    CHECK(context.device->hasFeature(rhi::Feature::RayTracing));
    extent = {frameDims.x, frameDims.y};
    rhi::BufferDesc desc;
    desc.usage =
        rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopyDestination;
    desc.size = sizeof(OfflineHitData);
    rhiCheck(context.device->createBuffer(desc, nullptr, hitBuffer.writeRef()));
    hitData   = cache.registerBuffer(hitBuffer);
    desc.size = size_t(extent.width) * extent.height * sizeof(AccumulatedPixel);
    rhiCheck(context.device->createBuffer(desc, nullptr, accumulationBuffer.writeRef()));
    accumulation = cache.registerBuffer(accumulationBuffer);
    for (size_t index = 0; index < sceneBuffers.size(); ++index)
    {
        desc.size = kSceneBufferSizes[index];
        rhiCheck(context.device->createBuffer(desc, nullptr, sceneBufferResources[index].writeRef()));
        sceneBuffers[index] = cache.registerBuffer(sceneBufferResources[index]);
    }
    rhiCheck(context.device->createComputePipeline(
        {.program = context.shaderManager->program(context.shaderManager->getOrLoad(resolveProgram))},
        resolvePipeline.writeRef()
    ));
    uploadedSceneRevision = 0;
    resetAccumulation();
}

void OfflinePathTracing::prepareHits(GpuResources& cache, const Falcor::RenderContext& execution)
{
    auto& resources = *execution.resources;
    static_assert(
        sizeof(Vertex) == 48 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, texcoord) == 24 &&
        offsetof(Vertex, tangent) == 32
    );

    const auto& items  = execution.scene->drawItems;
    const auto  bytes  = std::max<size_t>(items.size(), 1) * sizeof(OfflineHitData);
    const auto  upload = cache.allocateUpload(hitData, bytes);
    auto*       hits   = reinterpret_cast<OfflineHitData*>(upload.data.data());
    for (uint32_t index = 0; index < items.size(); ++index)
    {
        const auto& item = items[index];
        const auto& mesh = resources.mesh(item.mesh);
        const auto& draw = execution.scene->offlineScene->draws[index];
        const auto& part = mesh.submeshes()[item.submeshIndex];
        std::construct_at(
            hits + index,
            OfflineHitData{
                .vertexAddress = mesh.vertices()->getDeviceAddress(),
                .indexAddress  = mesh.indices()->getDeviceAddress(),
                .material      = {part.firstIndex, draw.materialIndex, draw.reverseOrientation, draw.firstAreaLight},
            }
        );
    }
    if (items.empty())
        std::construct_at(hits, OfflineHitData{});
}

bool OfflinePathTracing::prepareScene(
    GpuResources& cache, const Falcor::RenderContext& execution, const rhi::ComPtr<rhi::ITexture>& color
)
{
    output = color;
    prepareHits(cache, execution);

    const auto& offline        = *execution.scene->offlineScene;
    const bool  offlineChanged = uploadedSceneRevision != offline.revision;
    if (offlineChanged)
    {
        const auto upload = [&](uint32_t index, const auto& values)
        {
            using Value           = typename std::remove_cvref_t<decltype(values)>::value_type;
            const auto bytes      = std::max<size_t>(values.size(), 1) * sizeof(Value);
            const auto allocation = cache.allocateUpload(sceneBuffers[index], bytes);
            if (values.empty())
                std::construct_at(reinterpret_cast<Value*>(allocation.data.data()));
            else
                std::memcpy(allocation.data.data(), values.data(), values.size() * sizeof(Value));
        };

        upload(0, offline.materials);
        upload(1, offline.textures);
        upload(2, offline.areaEmissions);
        upload(3, offline.pointLights);
        upload(4, offline.spotLights);
        upload(5, offline.directionalLights);
        upload(6, offline.lightTriangles);
        upload(7, std::array{offline.environmentLight});
        upload(8, offline.projectionLights);
        upload(9, offline.goniometricLights);
        upload(10, offline.lightPortals);
        uploadedSceneRevision = offline.revision;
    }

    const auto& view          = *execution.view;
    const auto& scene         = *execution.scene;
    const auto  assetRevision = context().assetManager->contentRevision();
    const bool  cameraChanged = view.uniforms.inverseViewProjection != previousView.uniforms.inverseViewProjection ||
        view.uniforms.cameraPosition != previousView.uniforms.cameraPosition ||
        view.viewport.x != previousView.viewport.x || view.viewport.y != previousView.viewport.y ||
        view.viewport.width != previousView.viewport.width || view.viewport.height != previousView.viewport.height;
    const bool sceneChanged = !std::ranges::equal(previousDrawItems, scene.drawItems);
    const bool assetChanged = previousAssetRevision != assetRevision;

    if (sceneChanged)
        previousDrawItems.assign(scene.drawItems.begin(), scene.drawItems.end());
    previousView          = view;
    previousAssetRevision = assetRevision;
    return offlineChanged || sceneChanged || cameraChanged || assetChanged;
}

PixelBounds OfflinePathTracing::viewportBounds(const RenderView& view) const
{
    const auto& viewport = view.viewport;
    const auto  firstX   = std::clamp(int32_t(std::ceil(viewport.x - 0.5f)), 0, int32_t(extent.width));
    const auto  firstY   = std::clamp(int32_t(std::ceil(viewport.y - 0.5f)), 0, int32_t(extent.height));
    const auto endX = std::clamp(int32_t(std::ceil(viewport.x + viewport.width - 0.5f)), firstX, int32_t(extent.width));
    const auto endY =
        std::clamp(int32_t(std::ceil(viewport.y + viewport.height - 0.5f)), firstY, int32_t(extent.height));
    return {{uint32_t(firstX), uint32_t(firstY)}, {uint32_t(endX - firstX), uint32_t(endY - firstY)}};
}

OfflinePathParameters
OfflinePathTracing::pathParameters(const Falcor::RenderContext& execution, uint32_t maxBounces) const
{
    const auto& viewport = execution.view->viewport;
    const auto& scene    = *execution.scene->offlineScene;
    return {
        .viewport  = {viewport.x, viewport.y, viewport.width, viewport.height},
        .samples   = {completedSamples, maxBounces},
        .imageSize = {extent.width, extent.height},
        .lightCounts =
            {scene.pointLights.size(),
             scene.spotLights.size(),
             scene.directionalLights.size(),
             scene.lightTriangles.size()},
        .mappedLightCounts = {
            scene.hasEnvironmentLight ? 1u : 0u,
            scene.projectionLights.size(),
            scene.goniometricLights.size(),
            scene.lightPortals.size()
        },
    };
}

void OfflinePathTracing::resetAccumulation()
{
    completedSamples = 0;
    clearPending     = true;
}

void OfflinePathTracing::render(
    GpuResources&                               cache,
    rhi::ICommandEncoder&                       commands,
    const Falcor::RenderContext&                execution,
    std::span<const std::byte>                  constants,
    const OfflineTracePipelineCache::Result*    pipeline,
    Extent2D                                    rayExtent,
    std::span<const GpuResources::BufferHandle> extraAccumulations
)
{
    CHECK(completedSamples < std::numeric_limits<uint32_t>::max());
    if (clearPending)
    {
        commands.clearBuffer(cache.getBuffer(accumulation));
        for (const auto buffer : extraAccumulations)
            commands.clearBuffer(cache.getBuffer(buffer));
        clearPending = false;
    }
    const auto bindOutput = [&](rhi::IShaderObject* object)
    {
        const rhi::ShaderCursor root(object);
        rhiCheck(root["accumulation"].setBinding(cache.getBuffer(accumulation)));
        const auto parameters = root["parameters"].getDereferenced();
        CHECK(parameters.getTypeLayout()->getSize() == constants.size());
        rhiCheck(parameters.setData(constants.data(), constants.size()));
        if (const auto field = root["splats"]; field.isValid())
            rhiCheck(field.setBinding(cache.getBuffer(lightSplats)));
    };
    if (pipeline)
    {
        auto* pass = commands.beginRayTracingPass();
        CHECK(pass);
        auto* object = pass->bindPipeline(pipeline->pipeline, pipeline->shaderTable);
        CHECK(object);
        bindFrameData(object, *execution.frameData);
        execution.materialTable->bind(object);
        bindOutput(object);
        const rhi::ShaderCursor root(object);
        rhiCheck(root["scene"].setBinding(execution.sceneTlas));
        rhiCheck(root["objects"].setBinding(cache.getBuffer(hitData)));
        for (size_t index = 0; index < sceneBuffers.size(); ++index)
            rhiCheck(root[kSceneBufferNames[index]].setBinding(cache.getBuffer(sceneBuffers[index])));
        if (const auto field = root["lightDistribution"]; field.isValid())
            rhiCheck(field.setBinding(cache.getBuffer(lightDistribution)));
        pass->dispatchRays(0, rayExtent.width, rayExtent.height, 1);
        pass->end();
    }
    auto* pass = commands.beginComputePass();
    CHECK(pass);
    auto* object = pass->bindPipeline(resolvePipeline);
    CHECK(object);
    bindOutput(object);
    rhiCheck(rhi::ShaderCursor(object)["outputImage"].setBinding(output->getDefaultView()));
    const auto groups = dispatchGroups(resolvePipeline, {extent.width, extent.height, 1});
    pass->dispatchCompute(groups.width, groups.height, groups.depth);
    pass->end();
}
