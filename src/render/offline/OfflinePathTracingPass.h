#pragma once

#include <optional>
#include "render/RenderConfig.h"
#include "render/RenderView.h"
#include "render/feature/RenderGraph.h"
#include "render/raytracing/RayTracingData.h"
#include "render/raytracing/TlasBuildPass.h"
#include "scene/system/SceneRenderData.h"

#include <array>
#include <glm/vec4.hpp>

class OfflinePathTracingPass
{
public:
    OfflinePathTracingPass()                                         = default;
    OfflinePathTracingPass(const OfflinePathTracingPass&)            = delete;
    OfflinePathTracingPass& operator=(const OfflinePathTracingPass&) = delete;
    OfflinePathTracingPass(OfflinePathTracingPass&&)                 = delete;
    OfflinePathTracingPass& operator=(OfflinePathTracingPass&&)      = delete;

    struct Output
    {
        RenderGraph::ImageHandle rayImage;
    };

    Output                 setup(RenderGraph& graph, const PassSetup& setup);
    void                   execute(RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution);
    void                   updateScene(const RenderView& view, const SceneRenderData& scene, uint64_t assetRevision);
    void                   resetAccumulation() noexcept;
    [[nodiscard]] uint32_t sampleCount() const { return accumulatedSamples; }

private:
    struct Frame
    {
        Buffer                  hitData;
        Buffer                  lightData;
        Buffer                  materialData;
        vk::raii::DescriptorSet set = nullptr;
    };
    struct OfflineParameters
    {
        glm::mat4   inverseViewProjection;
        glm::vec4   cameraPosition;
        glm::vec4   viewport;
        LightRanges lightRanges;
        uint32_t    maxRecursionDepth;
        uint32_t    sampleIndex;
        uint32_t    padding[2]{};
    };
    static_assert(sizeof(OfflineParameters) == 128);
    static_assert(offsetof(OfflineParameters, lightRanges) == 96);
    static_assert(offsetof(OfflineParameters, maxRecursionDepth) == 112);
    static_assert(offsetof(OfflineParameters, sampleIndex) == 116);

    RenderGraph::ImageHandle          rayImage;
    vk::raii::DescriptorSetLayout     descriptorLayout       = nullptr;
    vk::raii::DescriptorPool          descriptorPool         = nullptr;
    vk::raii::PipelineLayout          pipelineLayout         = nullptr;
    vk::raii::Pipeline                pipeline               = nullptr;
    uint32_t                          pipelineRecursionDepth = 0;
    Buffer                            bindingTable;
    vk::StridedDeviceAddressRegionKHR raygenRegion, missRegion, hitRegion;
    BlasBuildPass                     bottomLevels;
    TlasBuildPass                     topLevel{bottomLevels};
    Frame                             frame;
    Buffer                            accumulation;
    vk::Extent2D                      accumulationExtent{};
    RenderGraph::BufferHandle         accumulationHandle;
    uint32_t                          accumulatedSamples = 0;
    std::optional<SceneRenderData>    previousScene;
    RenderView                        previousView{};
    uint64_t                          previousAssetRevision = 0;

    void createPipeline(const PassSetup& setup, uint32_t maxRecursionDepth);
    void prepareLights(const PassFrame& execution, Frame& frame) const;
    void prepareBindings(RenderGraph& graph, const PassFrame& execution, Frame& frame);
};
