#pragma once

#include "render/graph/RenderPass.h"
#include "render/impl/passes/restir/RestirPathTracerSettings.h"
#include "render/resources/RayTracingProgram.h"

#include <optional>

class RestirPathTracer final : public RenderPass
{
public:
    static constexpr std::string_view kType = "RestirPathTracer";

    explicit RestirPathTracer(rhi::ComPtr<rhi::IDevice> device);
    void compile(RenderContext* context, const CompileData& compileData) override;
    void setScene(GpuScene* scene) override;
    void execute(RenderContext* context, const RenderData& renderData) override;

private:
    static bool drawSettings(
        Gui::Widgets& widgets,
        RestirPathTracerSettings& editedSettings,
        const RestirPathTracer& pass,
        const PassEditorContext& context
    );

    RestirPathTracerSettings settings;
    std::optional<RayTracingProgram> tracer;
    uint32_t sampleIndex = 0;
    bool optionsChanged = false;
};
