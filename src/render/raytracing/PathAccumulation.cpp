#include "render/raytracing/PathAccumulation.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/raytracing/PathTracingShader.h"

#include <algorithm>
#include <cmath>
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

}  // namespace

void PathAccumulation::compile(
    const RenderContext& context, glm::uvec2 frameDims, std::string_view resolveProgram
)
{
    auto& cache = *context.gpuResources;
    extent      = {frameDims.x, frameDims.y};
    rhi::BufferDesc desc;
    desc.usage =
        rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopyDestination;
    desc.size = size_t(extent.width) * extent.height * sizeof(AccumulatedPixel);
    rhi::ComPtr<rhi::IBuffer> buffer;
    rhiCheck(context.device->createBuffer(desc, nullptr, buffer.writeRef()));
    accumulation = cache.registerBuffer(buffer);
    rhiCheck(context.device->createComputePipeline(
        {.program = context.shaderManager->program(context.shaderManager->getOrLoad(resolveProgram))},
        resolvePipeline.writeRef()
    ));
    resetAccumulation();
}

bool PathAccumulation::updateFrame(const RenderContext& execution, const rhi::ComPtr<rhi::ITexture>& color)
{
    output = color;

    const auto& view          = *execution.view;
    const auto& scene         = *execution.scene;
    const bool  cameraChanged = view.uniforms.inverseViewProjection != previousView.uniforms.inverseViewProjection ||
        view.uniforms.cameraPosition != previousView.uniforms.cameraPosition ||
        view.viewport.x != previousView.viewport.x || view.viewport.y != previousView.viewport.y ||
        view.viewport.width != previousView.viewport.width || view.viewport.height != previousView.viewport.height;
    const bool sceneChanged = previousSceneGeneration != scene.sceneGeneration ||
        previousShadingRevision != scene.shading->revision || previousEnvironment != scene.environmentMap;

    previousView            = view;
    previousSceneGeneration = scene.sceneGeneration;
    previousShadingRevision = scene.shading->revision;
    previousEnvironment     = scene.environmentMap;
    return cameraChanged || sceneChanged;
}

PixelBounds PathAccumulation::viewportBounds(const RenderView& view) const
{
    const auto& viewport = view.viewport;
    const auto  firstX   = std::clamp(int32_t(std::ceil(viewport.x - 0.5f)), 0, int32_t(extent.width));
    const auto  firstY   = std::clamp(int32_t(std::ceil(viewport.y - 0.5f)), 0, int32_t(extent.height));
    const auto endX = std::clamp(int32_t(std::ceil(viewport.x + viewport.width - 0.5f)), firstX, int32_t(extent.width));
    const auto endY =
        std::clamp(int32_t(std::ceil(viewport.y + viewport.height - 0.5f)), firstY, int32_t(extent.height));
    return {{uint32_t(firstX), uint32_t(firstY)}, {uint32_t(endX - firstX), uint32_t(endY - firstY)}};
}

AccumulationParameters
PathAccumulation::pathParameters(const RenderContext& execution, uint32_t maxBounces) const
{
    const auto& viewport = execution.view->viewport;
    return {
        .viewport              = {viewport.x, viewport.y, viewport.width, viewport.height},
        .samples               = {completedSamples, maxBounces},
        .imageSize             = {extent.width, extent.height},
    };
}

void PathAccumulation::resetAccumulation()
{
    completedSamples = 0;
    clearPending     = true;
}

void PathAccumulation::render(
    GpuResources&                               cache,
    rhi::ICommandEncoder&                       commands,
    const RenderContext&                execution,
    std::span<const std::byte>                  constants,
    const PathTracingProgram*                   pipeline,
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
        execution.scene->rays.bind(object, execution);
        bindOutput(object);
        const rhi::ShaderCursor root(object);
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
