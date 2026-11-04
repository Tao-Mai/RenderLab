#pragma once

#include "render/RenderConfig.h"
#include "render/device/Device.h"
#include "render/graph/RenderGraph.h"
#include "render/raytracing/realtime/RayTracingData.h"
#include "render/raytracing/realtime/RayTracingScenePass.h"
#include "render/raytracing/TlasBuildPass.h"
#include "render/resources/ShaderData.h"
#include "render/RendererConfig.h"
#include "asset/EnvironmentMapAsset.h"

#include <array>
#include <glm/vec4.hpp>

class RayTracingPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle rayImage;  // Combined lighting; valid when SVGF is disabled.
        RenderGraph::ImageHandle depth;
        RenderGraph::ImageHandle worldNormal;
        RenderGraph::ImageHandle direct;  // Separated lighting; valid when SVGF is enabled.
        RenderGraph::ImageHandle indirect;
        RenderGraph::ImageHandle emission;
        RenderGraph::ImageHandle albedo;  // Primary reflectance: saturate(diffuseColor + specularF0).
    };

    Output setup(
        RenderGraph&                graph,
        const PassSetup&            setup,
        TlasBuildPass::Output       scene,
        RayTracingScenePass::Output geometry,
        RenderGraph::BufferHandle   lights
    );
    void writeDescriptorSet(RenderGraph& graph, const PassSetup& setup);
    void prepare(RenderGraph& graph, const PassFrame& execution);
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution);
    [[nodiscard]] RenderGraph::BufferHandle reservoirs() const { return reservoirHistory; }

private:
    struct Frame
    {
        std::array<uint64_t, 7> bufferRevisions{};
        uint32_t                surfaceLayer = ~0u;
        vk::raii::DescriptorSet set          = nullptr;
        uint64_t                tlasRevision = 0;
    };
    struct PipelineVariant
    {
        DescriptorSetLayout                              descriptorSetLayout;
        vk::PipelineLayout                               layout;
        vk::raii::Pipeline                               pipeline = nullptr;
        Buffer                                           bindingTable;
        std::array<vk::StridedDeviceAddressRegionKHR, 3> raygenRegions;
        vk::StridedDeviceAddressRegionKHR                missRegion, hitRegion;
    };

    Output                                           output;
    bool                                             svgfEnabled = false;
    uint32_t                                         sampleIndex = 0;
    std::array<PipelineVariant, 2>                   variants;
    RenderGraph::AccelerationStructureHandle         sceneTlas;
    RayTracingScenePass::Output                      geometry;
    std::array<Frame, maxFramesInFlight>             frames;
    std::array<RenderGraph::BufferHandle, 2>         restirSurfaces;
    RenderGraph::BufferHandle                        initialReservoirs;
    RenderGraph::BufferHandle                        reservoirHistory;
    RenderGraph::BufferHandle                        restirParameters;
    vk::Extent2D                                     extent;
    uint32_t                                         writeLayer       = 0;
    bool                                             hasRestirHistory = false;
    glm::mat4                                        previousViewProjection{1.0f};
    glm::vec4                                        previousViewport{};
    glm::vec4                                        previousDepthRow{};
    uint64_t                                         previousSceneSignature   = 0;
    uint64_t                                         previousMaterialRevision = 0;
    RenderFeature::Restir                            previousRestir;
    uint32_t                                         previousMaxBounces = 0;
    bool                                             previousIbl        = false;
    std::vector<LightData>                           previousLights;
    std::optional<EnvironmentMapAsset::ID>           previousEnvironment;

    void createPipeline(const PassSetup& setup, bool enableSvgf);
    void updateBufferDescriptors(RenderGraph& graph, const PassFrame& execution, Frame& frame);
    void updateTlasDescriptor(RenderGraph& graph, const PassFrame& execution, Frame& frame);
    void prepareRestir(RenderGraph& graph, const PassFrame& execution);
};
