#pragma once

#include <slang-rhi.h>
#include "core/math/Vector.h"
#include "render/DrawItem.h"
#include "render/RenderView.h"

#include "render/device/GpuResources.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "render/raytracing/offline/OfflineTracePipelineCache.h"
namespace Falcor
{
struct RenderContext;
}  // namespace Falcor

struct alignas(16) OfflinePathParameters
{
    glm::vec4  viewport;
    glm::uvec2 samples;
    glm::uvec2 imageSize;
    glm::uvec4 lightCounts;
    glm::uvec4 mappedLightCounts;
};
static_assert(sizeof(OfflinePathParameters) == 64);

class OfflinePathTracing
{
public:
    void compile(const Falcor::RenderContext& context, Falcor::uint2 frameDims, std::string_view resolveProgram);
    void setLightTracingBuffers(GpuResources::BufferHandle splats, GpuResources::BufferHandle distribution)
    {
        lightSplats       = splats;
        lightDistribution = distribution;
    }
    [[nodiscard]] bool
    prepareScene(GpuResources& cache, const Falcor::RenderContext& execution, const rhi::ComPtr<rhi::ITexture>& color);

    [[nodiscard]] PixelBounds viewportBounds(const RenderView& view) const;
    [[nodiscard]] OfflinePathParameters
         pathParameters(const Falcor::RenderContext& execution, uint32_t maxBounces) const;
    void render(
        GpuResources&                               cache,
        rhi::ICommandEncoder&                       commands,
        const Falcor::RenderContext&                execution,
        std::span<const std::byte>                  constants,
        const OfflineTracePipelineCache::Result*    pipeline,
        Extent2D                                    rayExtent,
        std::span<const GpuResources::BufferHandle> extraAccumulations = {}
    );

    void                   resetAccumulation();
    void                   finishSample() { ++completedSamples; }
    [[nodiscard]] uint32_t sampleCount() const { return completedSamples; }

private:
    static constexpr size_t kSceneBufferCount = 11;

    rhi::ComPtr<rhi::IBuffer>                                 accumulationBuffer;
    rhi::ComPtr<rhi::IBuffer>                                 hitBuffer;
    std::array<rhi::ComPtr<rhi::IBuffer>, kSceneBufferCount>  sceneBufferResources;
    GpuResources::BufferHandle                                accumulation;
    GpuResources::BufferHandle                                hitData;
    std::array<GpuResources::BufferHandle, kSceneBufferCount> sceneBuffers;
    uint64_t                                                  uploadedSceneRevision = 0;

    rhi::ComPtr<rhi::ITexture>         output;
    GpuResources::BufferHandle         lightSplats, lightDistribution;
    rhi::ComPtr<rhi::IComputePipeline> resolvePipeline;
    Extent2D                           extent;

    uint32_t              completedSamples = 0;
    bool                  clearPending     = true;
    std::vector<DrawItem> previousDrawItems;
    RenderView            previousView{};
    uint64_t              previousAssetRevision = 0;

    void prepareHits(GpuResources& cache, const Falcor::RenderContext& execution);
};
