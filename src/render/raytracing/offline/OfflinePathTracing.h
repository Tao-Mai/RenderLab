#pragma once

#include "render/DrawItem.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/graph/RenderGraph.h"
#include "render/raytracing/offline/OfflineRayTracingBindings.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

class RayTracingPipeline;

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
    [[nodiscard]] RenderGraph::ImageHandle compile(
        RenderGraph& graph,
        const PassSetup& setup,
        std::string_view resolveProgram,
        uint32_t parameterSize,
        std::span<const vk::DescriptorSetLayoutBinding> extraBindings = {}
    );
    void bindResources(RenderGraph& graph, const PassSetup& setup);
    void bindStorageBuffer(
        RenderGraph& graph, const VulkanContext& vulkan, RenderGraph::BufferHandle buffer, uint32_t binding
    ) const;
    [[nodiscard]] bool prepareScene(RenderGraph& graph, const PassFrame& execution);

    [[nodiscard]] vk::Rect2D viewportBounds(const RenderView& view) const;
    [[nodiscard]] OfflinePathParameters pathParameters(const PassFrame& execution, uint32_t maxBounces) const;
    void render(
        RenderGraph& graph,
        vk::raii::CommandBuffer& commands,
        const PassFrame& execution,
        std::span<const std::byte> constants,
        const RayTracingPipeline* pipeline,
        vk::Extent2D rayExtent,
        std::span<const RenderGraph::BufferHandle> extraAccumulations = {}
    );

    void resetAccumulation();
    void finishSample() { ++completedSamples; }
    [[nodiscard]] uint32_t sampleCount() const { return completedSamples; }
    [[nodiscard]] vk::PipelineLayout layout() const { return pipelineLayout; }

private:
    static constexpr size_t kSceneBufferCount =
        OfflineRayTracingBinding::kPortals - OfflineRayTracingBinding::kMaterials + 1;

    RenderGraph::ImageHandle rayImage;
    RenderGraph::BufferHandle accumulation;
    RenderGraph::BufferHandle hitData;
    std::array<RenderGraph::BufferHandle, kSceneBufferCount> sceneBuffers;
    std::array<uint64_t, kSceneBufferCount> sceneBufferRevisions{};
    uint64_t hitBufferRevision = 0;
    uint64_t tlasRevision = 0;
    uint64_t uploadedSceneRevision = 0;

    DescriptorSetLayout descriptorSetLayout;
    DescriptorSetLayout emptySetLayout;
    vk::PipelineLayout pipelineLayout;
    vk::raii::DescriptorSet descriptorSet = nullptr;
    ComputePipeline resolvePipeline;
    vk::Extent2D extent;

    uint32_t completedSamples = 0;
    bool clearPending = true;
    std::vector<DrawItem> previousDrawItems;
    RenderView previousView{};
    uint64_t previousAssetRevision = 0;

    void prepareHits(RenderGraph& graph, const PassFrame& execution);
    void updateTlasDescriptor(const PassFrame& execution);
};
