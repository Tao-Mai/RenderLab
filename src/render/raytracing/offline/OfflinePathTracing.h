#pragma once

#include "core/math/Vector.h"
#include "render/DrawItem.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/device/ResourceBarriers.h"
#include "render/raytracing/offline/OfflineRayTracingBindings.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

class RayTracingPipeline;
namespace Falcor
{
class RenderContext;
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
    void compile(
        const Falcor::RenderContext&                    context,
        Falcor::uint2                                           frameDims,
        std::string_view                                resolveProgram,
        uint32_t                                        parameterSize,
        std::span<const vk::DescriptorSetLayoutBinding> extraBindings = {}
    );
    void writeStorageBuffer(VulkanResources& cache, VulkanResources::BufferHandle buffer, uint32_t binding) const
    {
        cache.writeDescriptor(*descriptorSet, binding, buffer);
    }
    void bindStorageBuffer(
        VulkanResources& cache, const VulkanContext& vulkan, VulkanResources::BufferHandle buffer, uint32_t binding
    ) const;
    [[nodiscard]] bool
    prepareScene(VulkanResources& cache, const Falcor::RenderContext& execution, const Falcor::ref<Falcor::Texture>& color);

    [[nodiscard]] vk::Rect2D            viewportBounds(const RenderView& view) const;
    [[nodiscard]] OfflinePathParameters pathParameters(const Falcor::RenderContext& execution, uint32_t maxBounces) const;
    void                                render(
                                       VulkanResources&                               cache,
                                       vk::raii::CommandBuffer&                       commands,
                                       const Falcor::RenderContext&                           execution,
                                       std::span<const std::byte>                     constants,
                                       const RayTracingPipeline*                      pipeline,
                                       vk::Extent2D                                   rayExtent,
                                       std::span<const VulkanResources::BufferHandle> extraAccumulations = {}
                                   );

    void                             resetAccumulation();
    void                             finishSample() { ++completedSamples; }
    [[nodiscard]] uint32_t           sampleCount() const { return completedSamples; }
    [[nodiscard]] vk::PipelineLayout layout() const { return pipelineLayout; }

private:
    ResourceBarriers        barriers;
    ResourceBarriers        accesses;
    static constexpr size_t kSceneBufferCount =
        OfflineRayTracingBinding::kPortals - OfflineRayTracingBinding::kMaterials + 1;

    Falcor::ref<Falcor::Buffer>                                                 accumulationBuffer;
    Falcor::ref<Falcor::Buffer>                                                 hitBuffer;
    std::array<Falcor::ref<Falcor::Buffer>, kSceneBufferCount>                   sceneBufferResources;
    VulkanResources::BufferHandle                                accumulation;
    VulkanResources::BufferHandle                                hitData;
    std::array<VulkanResources::BufferHandle, kSceneBufferCount> sceneBuffers;
    std::array<uint64_t, kSceneBufferCount>                      sceneBufferRevisions{};
    uint64_t                                                     hitBufferRevision     = 0;
    uint64_t                                                     tlasRevision          = 0;
    uint64_t                                                     uploadedSceneRevision = 0;

    DescriptorSetLayout     descriptorSetLayout;
    DescriptorSetLayout     emptySetLayout;
    vk::PipelineLayout      pipelineLayout;
    vk::raii::DescriptorSet descriptorSet = nullptr;
    ComputePipeline         resolvePipeline;
    vk::Extent2D            extent;

    uint32_t              completedSamples = 0;
    bool                  clearPending     = true;
    std::vector<DrawItem> previousDrawItems;
    RenderView            previousView{};
    uint64_t              previousAssetRevision = 0;

    void prepareHits(VulkanResources& cache, const Falcor::RenderContext& execution);
    void updateTlasDescriptor(const Falcor::RenderContext& execution);
};
