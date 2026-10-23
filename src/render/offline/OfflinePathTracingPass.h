#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "render/DrawItem.h"
#include "render/RenderConfig.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/feature/RenderGraph.h"
#include "render/raytracing/RayTracingData.h"
#include "render/raytracing/TlasBuildPass.h"
#include "render/resource/ShaderData.h"

#include <array>
#include <glm/vec4.hpp>
#include <optional>
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

private:
    struct Frame
    {
        uint64_t                hitBufferRevision   = 0;
        uint64_t                lightBufferRevision = 0;
        vk::raii::DescriptorSet set                 = nullptr;
        uint64_t                tlasRevision        = 0;
    };
    struct OfflineParameters
    {
        glm::vec4   viewport;
        LightRanges lightRanges;
        uint32_t    maxRecursionDepth;
        uint32_t    sampleIndex;
        uint32_t    iblEnabled;
        uint32_t    padding = 0;
    };
    static_assert(sizeof(OfflineParameters) == 48);
    static_assert(offsetof(OfflineParameters, lightRanges) == 16);
    static_assert(offsetof(OfflineParameters, maxRecursionDepth) == 32);
    static_assert(offsetof(OfflineParameters, sampleIndex) == 36);
    static_assert(offsetof(OfflineParameters, iblEnabled) == 40);

    RenderGraph::ImageHandle                 rayImage;
    DescriptorSetLayout                      descriptorSetLayout;
    vk::PipelineLayout                       pipelineLayout;
    vk::raii::Pipeline                       pipeline               = nullptr;
    uint32_t                                 pipelineRecursionDepth = 0;
    Buffer                                   bindingTable;
    vk::StridedDeviceAddressRegionKHR        raygenRegion, missRegion, hitRegion;
    RenderGraph::AccelerationStructureHandle sceneTlas;
    Frame                                    frame;
    RenderGraph::BufferHandle                hitData;
    RenderGraph::BufferHandle                lightData;
    RenderGraph::BufferHandle                accumulationHandle;
    uint32_t                                 accumulatedSamples = 0;
    std::vector<DrawItem>                    previousDrawItems;
    std::vector<LightData>                   previousLights;
    std::optional<EnvironmentMapAsset::ID>   previousEnvironmentMap;
    RenderView                               previousView{};
    uint64_t                                 previousAssetRevision = 0;

    void createPipeline(const PassSetup& setup, uint32_t maxRecursionDepth);
    void prepareLights(RenderGraph& graph, const PassFrame& execution, Frame& frame) const;
    void prepareHits(RenderGraph& graph, const PassFrame& execution, Frame& frame);
    void updateTlasDescriptor(RenderGraph& graph, const PassFrame& execution, Frame& frame);
};
