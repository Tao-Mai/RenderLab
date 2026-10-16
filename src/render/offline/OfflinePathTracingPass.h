#pragma once

#include <optional>
#include "render/RenderConfig.h"
#include "render/RenderView.h"
#include "render/feature/RenderGraph.h"
#include "render/raytracing/AccelerationStructure.h"
#include "scene/system/SceneRenderData.h"

#include <array>
#include <glm/vec4.hpp>
#include <map>
#include <tuple>
#include <vector>

class RenderGraph;
class GpuMesh;

class OfflinePathTracingPass
{
public:
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
    static constexpr uint32_t kMaxTextures = 128;
    struct Frame
    {
        AccelerationStructure   tlas;
        Buffer                  instances;
        Buffer                  hitData;
        Buffer                  lightData;
        vk::raii::DescriptorSet set           = nullptr;
        uint32_t                instanceCount = ~0u;
    };
    struct HitData
    {
        uint64_t  vertexAddress;
        uint64_t  indexAddress;
        glm::vec4 baseColorFactor;
        uint32_t  firstIndex;
        uint32_t  textureIndex;
        uint32_t  padding[2]{};
    };
    static_assert(sizeof(HitData) == 48);

    struct OfflineParameters
    {
        glm::mat4 inverseViewProjection;
        glm::vec4 cameraPosition;
        glm::vec4 viewport;
        LightRanges lightRanges;
        uint32_t  maxRecursionDepth;
        uint32_t  sampleIndex;
        uint32_t  padding[2]{};
    };
    static_assert(sizeof(OfflineParameters) == 128);
    static_assert(offsetof(OfflineParameters, lightRanges) == 96);
    static_assert(offsetof(OfflineParameters, maxRecursionDepth) == 112);
    static_assert(offsetof(OfflineParameters, sampleIndex) == 116);

    RenderGraph::ImageHandle                                                        rayImage;
    vk::raii::DescriptorSetLayout                                                   descriptorLayout       = nullptr;
    vk::raii::DescriptorPool                                                        descriptorPool         = nullptr;
    vk::raii::PipelineLayout                                                        pipelineLayout         = nullptr;
    vk::raii::Pipeline                                                              pipeline               = nullptr;
    uint32_t                                                                        pipelineRecursionDepth = 0;
    Buffer                                                                          bindingTable;
    vk::StridedDeviceAddressRegionKHR                                               raygenRegion, missRegion, hitRegion;
    Buffer                                                                          rectLightVertices;
    Buffer                                                                          rectLightIndices;
    AccelerationStructure                                                           rectLightBlas;
    std::map<std::tuple<const GpuMesh*, uint32_t, uint32_t>, AccelerationStructure> blas;
    Frame                                                                           frame;
    Buffer                                                                          accumulation;
    vk::Extent2D                                                                    accumulationExtent{};
    RenderGraph::BufferHandle                                                       accumulationHandle;
    uint32_t                                                                        accumulatedSamples = 0;
    std::optional<SceneRenderData>                                                  previousScene;
    RenderView                                                                      previousView{};
    uint64_t                                                                        previousAssetRevision = 0;

    void createPipeline(const PassSetup& setup, uint32_t maxRecursionDepth);
    void createRectLightGeometry(CommandRecorder& commandBuffer, const PassFrame& execution);
    void prepareLights(const PassFrame& execution, Frame& frame) const;
    void prepareScene(RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution, Frame& frame);
};
