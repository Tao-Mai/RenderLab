#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "render/DrawItem.h"
#include "render/RenderConfig.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/graph/RenderGraph.h"
#include "render/raytracing/offline/OfflineScene.h"
#include "render/raytracing/TlasBuildPass.h"
#include "render/resources/ShaderData.h"

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

    Output
    setup(RenderGraph& graph, const PassSetup& setup, TlasBuildPass::Output scene);
    void writeDescriptorSet(RenderGraph& graph, const PassSetup& setup);
    void prepare(RenderGraph& graph, const PassFrame& execution);
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution);
    [[nodiscard]] uint32_t sampleCount() const { return accumulatedSamples; }

private:
    struct Frame
    {
        uint64_t                hitBufferRevision = 0;
        vk::raii::DescriptorSet set               = nullptr;
        uint64_t                tlasRevision      = 0;
    };
    RenderGraph::ImageHandle                 rayImage;
    DescriptorSetLayout                      descriptorSetLayout;
    vk::PipelineLayout                       pipelineLayout;
    vk::raii::Pipeline                       pipeline = nullptr;
    Buffer                                   bindingTable;
    vk::StridedDeviceAddressRegionKHR        raygenRegion, missRegion, hitRegion;
    RenderGraph::AccelerationStructureHandle sceneTlas;
    Frame                                    frame;
    RenderGraph::BufferHandle                hitData;
    std::array<RenderGraph::BufferHandle, 2> sceneBuffers;
    std::array<uint64_t, 2>                 sceneBufferRevisions{};
    uint64_t                               uploadedSceneRevision = 0;
    RenderGraph::BufferHandle                accumulationHandle;
    uint32_t                                 accumulatedSamples  = 0;
    bool                                     previousPixelJitter = true;
    std::vector<DrawItem>                    previousDrawItems;
    RenderView                               previousView{};
    uint64_t                                 previousAssetRevision = 0;

    void createPipeline(const PassSetup& setup);
    void prepareHits(RenderGraph& graph, const PassFrame& execution, Frame& frame);
    void updateTlasDescriptor(RenderGraph& graph, const PassFrame& execution, Frame& frame);
};
