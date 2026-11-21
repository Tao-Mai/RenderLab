#pragma once

#include "render/device/ResourceBarriers.h"
#include "render/graph/RenderPass.h"

#include "passes/raster/ShadowPass/ShadowSettings.h"

#include "render/device/Device.h"

#include "render/device/VulkanResources.h"
#include "render/resources/ShaderData.h"

#include <span>
#include <vector>

class ShadowPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(ShadowPass, "ShadowPass", "Renders shadow maps for scene lights.");

    ShadowPass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<ShadowPass> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<ShadowPass>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void                               compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    ShadowSettings parameters;

    void prepareTargets(VulkanResources& cache, const Falcor::RenderContext& frame, const Falcor::RenderData& renderData);
    void updateShadowData(VulkanResources& cache, const Falcor::RenderContext& frame);
    void renderShadowMaps(VulkanResources& cache, vk::raii::CommandBuffer& commandBuffer, const Falcor::RenderContext& frame);

    ResourceBarriers      shadowAccesses;
    ResourceBarriers      barriers;
    ResourceBarriers      lightingAccesses;
    bool                  targetsBound = false;
    static constexpr uint32_t kPointFaceCount = 6;
    static constexpr uint32_t kPointViewMask  = 0b111111;

    struct ShadowTarget
    {
        VulkanResources::ImageHandle  image;
        VulkanResources::BufferHandle buffer;
        vk::Pipeline              pipeline       = nullptr;
        uint32_t                  mapSize        = 0;
        uint32_t                  viewMask       = 0;
        uint32_t                  viewLayerCount = 1;
    };

    struct ShadowData
    {
        std::vector<PointShadowData>       pointShadows;
        std::vector<SpotShadowData>        spotShadows;
        std::vector<RectAreaShadowData>    rectAreaShadows;
        std::vector<DirectionalShadowData> directionalShadows;
    };

    ShadowTarget              point;
    ShadowTarget              spot;
    ShadowTarget              rectArea;
    ShadowTarget              directional;
    ShadowData                shadowData;
    uint32_t                  cascadeCount = 0;
    vk::PipelineLayout        pipelineLayout;
    vk::Sampler               linearComparisonSampler;
    vk::Sampler               nearestComparisonSampler;
    std::vector<uint32_t>     visibilityMasks;

    void prepareShadowData(
        const SceneUniforms& view, std::span<const LightData> frameLights, const ShadowSettings& shadows
    );

    void recordShadow(
        VulkanResources&           cache,
        vk::raii::CommandBuffer& commandBuffer,
        const Falcor::RenderContext&         execution,
        const ShadowTarget&      target,
        uint32_t                 lightIndex,
        uint32_t                 baseArrayLayer,
        uint32_t                 cascadeIndex = 0
    ) const;
};
