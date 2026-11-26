#pragma once

#include "render/graph/RenderPass.h"

class DebugPass final : public RenderPass
{
public:
    static constexpr std::string_view kType = "DebugPass";

    explicit DebugPass(rhi::ComPtr<rhi::IDevice> device);

    void compile(RenderContext* context, const CompileData& compileData) override;
    void execute(RenderContext* context, const RenderData& renderData) override;

private:
    static bool drawSettings(Gui::Widgets& widgets, bool& enabled, const DebugPass&, const PassEditorContext&);

    rhi::ComPtr<rhi::IRenderPipeline> pipeline;

    bool showAnalyticLights = false;
};
