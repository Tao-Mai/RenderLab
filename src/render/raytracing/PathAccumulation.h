#pragma once

#include <glm/vec2.hpp>
#include <slang-rhi.h>
#include "render/RenderView.h"
#include "render/device/GpuResources.h"

#include <cstddef>
#include <span>
#include <string_view>

struct GpuEnvironmentMap;
struct PathTracingProgram;

struct RenderContext;

struct alignas(16) AccumulationParameters
{
    glm::vec4  viewport;
    glm::uvec2 samples;
    glm::uvec2 imageSize;
};
static_assert(sizeof(AccumulationParameters) == 32);

class PathAccumulation
{
public:
    void compile(const RenderContext& context, glm::uvec2 frameDims, std::string_view resolveProgram);
    void setLightTracingBuffers(GpuResources::BufferHandle splats, GpuResources::BufferHandle distribution)
    {
        lightSplats       = splats;
        lightDistribution = distribution;
    }
    [[nodiscard]] bool updateFrame(const RenderContext& execution, const rhi::ComPtr<rhi::ITexture>& color);

    [[nodiscard]] PixelBounds            viewportBounds(const RenderView& view) const;
    [[nodiscard]] AccumulationParameters pathParameters(const RenderContext& execution, uint32_t maxBounces) const;
    void                                 render(
                                        GpuResources&                               cache,
                                        rhi::ICommandEncoder&                       commands,
                                        const RenderContext&                        execution,
                                        std::span<const std::byte>                  constants,
                                        const PathTracingProgram*                   pipeline,
                                        Extent2D                                    rayExtent,
                                        std::span<const GpuResources::BufferHandle> extraAccumulations = {}
                                    );

    void                   resetAccumulation();
    void                   finishSample() { ++completedSamples; }
    [[nodiscard]] uint32_t sampleCount() const { return completedSamples; }

private:
    GpuResources::BufferHandle accumulation;

    rhi::ComPtr<rhi::ITexture>         output;
    GpuResources::BufferHandle         lightSplats, lightDistribution;
    rhi::ComPtr<rhi::IComputePipeline> resolvePipeline;
    Extent2D                           extent;

    uint32_t                 completedSamples = 0;
    bool                     clearPending     = true;
    RenderView               previousView{};
    uint64_t                 previousShadingRevision = 0;
    uint64_t                 previousSceneGeneration = 0;
    const GpuEnvironmentMap* previousEnvironment     = nullptr;
};
