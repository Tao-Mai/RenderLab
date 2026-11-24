#pragma once

#include "render/graph/RenderPass.h"

#include "passes/raster/ShadowPass/ShadowSettings.h"

#include <slang-rhi.h>

#include "render/resources/ShaderData.h"

#include <span>
#include <vector>

class ShadowPass : public RenderPass
{
    RL_PLUGIN_CLASS(ShadowPass, "ShadowPass", "Renders shadow maps for scene lights.");

    ShadowPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties);

    [[nodiscard]] Properties getProperties() const override;
    void                             setProperties(const Properties& properties) override;

    static ref<ShadowPass> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<ShadowPass>(std::move(device), properties);
    }

    void renderUI(RenderContext* context, Gui::Widgets& widget) override;

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    ShadowSettings parameters;

    void updateShadowData(const RenderContext& frame, const RenderData& renderData);
    void renderShadowMaps(const RenderContext& frame, const RenderData& renderData);

    static constexpr uint32_t kPointFaceCount = 6;

    struct ShadowTarget
    {
        rhi::ComPtr<rhi::IRenderPipeline> pipeline       = nullptr;
        uint32_t                          viewLayerCount = 1;
    };

    struct ShadowData
    {
        std::vector<PointShadowData>       pointShadows;
        std::vector<SpotShadowData>        spotShadows;
        std::vector<RectAreaShadowData>    rectAreaShadows;
        std::vector<DirectionalShadowData> directionalShadows;
    };

    ShadowTarget          point;
    ShadowTarget          spot;
    ShadowTarget          rectArea;
    ShadowTarget          directional;
    ShadowData            shadowData;
    uint32_t              cascadeCount = 0;
    std::vector<uint32_t> visibilityMasks;

    void
    prepareShadowData(const SceneUniforms& view, std::span<const LightData> frameLights, const ShadowSettings& shadows);

    void recordShadow(
        const RenderContext& execution,
        const ShadowTarget&          target,
        rhi::ITexture&               image,
        rhi::IBuffer*               transforms,
        uint32_t                     lightIndex,
        uint32_t                     baseArrayLayer,
        uint32_t                     cascadeIndex = 0
    ) const;
};
