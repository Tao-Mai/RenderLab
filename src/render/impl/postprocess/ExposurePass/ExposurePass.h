#pragma once

#include "ExposurePassSettings.h"
#include "render/graph/RenderPass.h"

#include <slang-rhi.h>
#include <cstdint>

class ExposurePass final : public RenderPass
{
public:
    static constexpr std::string_view kType = "ExposurePass";

    explicit ExposurePass(rhi::ComPtr<rhi::IDevice> device);

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
};
