#pragma once

#include "render/graph/RenderPass.h"

#include "passes/raster/ShadowPass/ShadowSettings.h"

#include <slang-rhi.h>

#include "render/resources/ShaderData.h"

#include <span>
#include <vector>

class ShadowPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(ShadowPass, "ShadowPass", "Renders shadow maps for scene lights.");

    ShadowPass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void                             setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<ShadowPass> create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<ShadowPass>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    ShadowSettings parameters;

    void updateShadowData(const Falcor::RenderContext& frame, const Falcor::RenderData& renderData);
    void renderShadowMaps(const Falcor::RenderContext& frame, const Falcor::RenderData& renderData);

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
        const Falcor::RenderContext& execution,
        const ShadowTarget&          target,
        rhi::ITexture&               image,
        rhi::IBuffer*               transforms,
        uint32_t                     lightIndex,
        uint32_t                     baseArrayLayer,
        uint32_t                     cascadeIndex = 0
    ) const;
};
