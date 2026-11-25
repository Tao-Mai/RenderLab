#pragma once

#include "render/graph/RenderPass.h"
#include "render/impl/passes/MinimalPathTracerSettings.h"
#include "render/resources/RayTracingProgram.h"

#include <optional>

class MinimalPathTracer final : public RenderPass
{
public:
    static constexpr std::string_view kType = "MinimalPathTracer";

    explicit MinimalPathTracer(rhi::ComPtr<rhi::IDevice> device);
    void compile(RenderContext* context, const CompileData& compileData) override;
    void setScene(GpuScene* scene) override;
    void execute(RenderContext* context, const RenderData& renderData) override;

private:
    static bool drawSettings(
        Gui::Widgets& widgets,
        MinimalPathTracerSettings& editedSettings,
        const MinimalPathTracer& pass,
        const PassEditorContext& context
    );

    MinimalPathTracerSettings settings;
    std::optional<RayTracingProgram> tracer;
    uint32_t sampleIndex = 0;
    bool optionsChanged = false;
};
