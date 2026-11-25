#pragma once

#include "ExposurePassSettings.h"
#include "render/graph/RenderPassSettings.h"

#include <slang-rhi.h>
#include <cstdint>

class ExposurePass : public RenderPass
{
public:
    ExposurePass(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties);

    static std::shared_ptr<ExposurePass> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<ExposurePass>(std::move(device), properties);
    }

    void compile(RenderContext* context, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    ExposurePassSettings parameters;

    rhi::ComPtr<rhi::IComputePipeline> histogramPipeline;
    rhi::ComPtr<rhi::IComputePipeline> resolvePipeline;
    uint64_t                           previousSceneGeneration = 0;
    bool                               hasHistory = false;

    static bool drawSettings(
        Gui::Widgets& widgets, ExposurePassSettings& settings, const ExposurePass& pass, const PassEditorContext& context
    );

    static const RenderPassReflection::Field kPorts[3];

    RL_PLUGIN_CLASS(
        ExposurePass,
        "ExposurePass",
        .desc = "Computes exposure from a luminance histogram.",
        .ports = kPorts,
        .settings = pass_settings::bind<&ExposurePass::parameters, &ExposurePass::drawSettings>()
    );
};
