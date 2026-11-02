#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "render/DrawItem.h"
#include "render/RenderConfig.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/device/Device.h"
#include "render/graph/RenderGraph.h"
#include "render/raytracing/offline/OfflineScene.h"
#include "render/raytracing/TlasBuildPass.h"
#include "render/resources/ShaderData.h"

#include <array>
#include <glm/vec4.hpp>
#include <optional>
#include <map>
#include <future>
#include <vector>

class OfflinePathTracingPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle rayImage;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup, TlasBuildPass::Output scene);
    void   writeDescriptorSet(RenderGraph& graph, const PassSetup& setup);
    void   prepare(RenderGraph& graph, const PassFrame& execution);
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution);

    [[nodiscard]] uint32_t sampleCount() const { return accumulatedSamples; }
    [[nodiscard]] float    sampleProgress() const { return accumulatedProgress; }
    [[nodiscard]] bool     preparingPipeline() const { return activePipeline == nullptr; }

private:
    static constexpr size_t kSceneBufferCount =
        OfflineRayTracingBinding::kPortals - OfflineRayTracingBinding::kMaterials + 1;

    struct Frame
    {
        uint64_t                hitBufferRevision = 0;
        vk::raii::DescriptorSet set               = nullptr;
        uint64_t                tlasRevision      = 0;
    };

    struct PipelineVariant
    {
        vk::raii::Pipeline                trace = nullptr;
        Buffer                            bindingTable;
        vk::StridedDeviceAddressRegionKHR raygen, miss, hit;
    };

    struct PipelineBuild
    {
        std::optional<PipelineVariant> result;
        // Join before destroying resources if the pass is closed during compilation.
        std::future<PipelineVariant> future;
    };

    RenderGraph::ImageHandle rayImage;
    DescriptorSetLayout      descriptorSetLayout;
    DescriptorSetLayout      emptySetLayout;
    vk::PipelineLayout       pipelineLayout;

    std::map<std::pair<OfflineAlgorithm, uint32_t>, PipelineBuild> pipelines;

    vk::raii::Pipeline ptResolvePipeline   = nullptr;
    vk::raii::Pipeline bdptResolvePipeline = nullptr;

    PipelineVariant* activePipeline    = nullptr;
    OfflineAlgorithm pipelineAlgorithm = OfflineAlgorithm::PT;
    uint32_t         pipelineDepth     = 0;

    RenderGraph::AccelerationStructureHandle sceneTlas;
    Frame                                    frame;
    RenderGraph::BufferHandle                hitData;

    std::array<RenderGraph::BufferHandle, kSceneBufferCount> sceneBuffers;
    std::array<uint64_t, kSceneBufferCount>                  sceneBufferRevisions{};
    uint64_t                                                 uploadedSceneRevision = 0;

    RenderGraph::BufferHandle accumulationHandle;
    RenderGraph::BufferHandle splatHandle;
    RenderGraph::BufferHandle lightDistributionHandle;
    uint64_t                  lightDistributionRevision = 0;

    glm::vec4 sceneSphere{};
    uint32_t  accumulatedSamples  = 0;
    float     accumulatedProgress = 0;
    uint32_t  tileIndex           = 0;
    uint64_t  processedPixels     = 0;
    bool      resetAccumulation   = true;

    RenderFeature::Offline previousSettings{};
    std::vector<DrawItem>  previousDrawItems;
    RenderView             previousView{};
    uint64_t               previousAssetRevision = 0;

    void preparePipeline(const PassSetup& setup);
    static PipelineVariant
    createPipeline(const VulkanContext& vulkan, vk::PipelineLayout layout, OfflineAlgorithm algorithm, uint32_t depth);

    void prepareHits(RenderGraph& graph, const PassFrame& execution, Frame& frame);
    void updateTlasDescriptor(RenderGraph& graph, const PassFrame& execution, Frame& frame);
    void prepareLightDistribution(RenderGraph& graph, const PassFrame& execution);
};
