#pragma once

#include <optional>
#include "render/RenderConfig.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/feature/RenderGraph.h"
#include "render/raytracing/RayTracingData.h"
#include "render/raytracing/TlasBuildPass.h"
#include "scene/system/SceneRenderData.h"

#include <array>
#include <glm/vec4.hpp>

class OfflinePathTracingPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle rayImage;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup, TlasBuildPass::Output scene);
    void   writeDescriptorSet(RenderGraph& graph, const PassSetup& setup);
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution);
    void   updateScene(const RenderView& view, const SceneRenderData& scene, uint64_t assetRevision);
    [[nodiscard]] uint32_t sampleCount() const { return accumulatedSamples; }

private:
    struct Frame
    {
        Buffer                  hitData;
        Buffer                  lightData;
        vk::raii::DescriptorSet set          = nullptr;
        uint64_t                tlasRevision = 0;
    };
    struct OfflineParameters
    {
        glm::vec4   viewport;
        LightRanges lightRanges;
        uint32_t    maxRecursionDepth;
        uint32_t    sampleIndex;
        uint32_t    padding[2]{};
    };
    static_assert(sizeof(OfflineParameters) == 48);
    static_assert(offsetof(OfflineParameters, lightRanges) == 16);
    static_assert(offsetof(OfflineParameters, maxRecursionDepth) == 32);
    static_assert(offsetof(OfflineParameters, sampleIndex) == 36);

    RenderGraph::ImageHandle                 rayImage;
    DescriptorSetLayout                      descriptorSetLayout;
    vk::PipelineLayout                       pipelineLayout;
    vk::raii::Pipeline                       pipeline               = nullptr;
    uint32_t                                 pipelineRecursionDepth = 0;
    Buffer                                   bindingTable;
    vk::StridedDeviceAddressRegionKHR        raygenRegion, missRegion, hitRegion;
    RenderGraph::AccelerationStructureHandle sceneTlas;
    Frame                                    frame;
    Buffer                                   accumulation;
    RenderGraph::BufferHandle                accumulationHandle;
    uint32_t                                 accumulatedSamples = 0;
    std::optional<SceneRenderData>           previousScene;
    RenderView                               previousView{};
    uint64_t                                 previousAssetRevision = 0;

    void createPipeline(const PassSetup& setup, uint32_t maxRecursionDepth);
    void prepareLights(const PassFrame& execution, Frame& frame) const;
    void prepareBindings(RenderGraph& graph, const PassFrame& execution, Frame& frame);
};
