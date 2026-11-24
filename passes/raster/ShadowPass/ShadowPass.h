#pragma once

#include "render/graph/RenderPassSettings.h"

#include "passes/raster/ShadowPass/ShadowSettings.h"

#include <slang-rhi.h>

#include "render/raster/ShadowResources.h"
#include "render/resources/ShaderData.h"

#include <span>
#include <vector>

class ShadowPass : public RenderPass
{
public:
    ShadowPass(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties);

    static std::shared_ptr<ShadowPass> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<ShadowPass>(std::move(device), properties);
    }

    [[nodiscard]] RenderPassReflection reflect(const ResourceCache::DefaultProperties&) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    ShadowSettings parameters;

    void onSettingsUpdated(const ShadowSettings& previous);

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

    static bool drawSettings(
        Gui::Widgets& widgets, ShadowSettings& settings, const ShadowPass& pass, const PassEditorContext& context
    );

    RL_PLUGIN_CLASS(
        ShadowPass,
        "ShadowPass",
        .desc = "Renders shadow maps for scene lights.",
        .settings = pass_settings::bind<
            &ShadowPass::parameters,
            &ShadowPass::drawSettings,
            &ShadowPass::onSettingsUpdated
        >()
    );
};
