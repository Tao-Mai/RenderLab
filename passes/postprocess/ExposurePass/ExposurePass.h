#pragma once

#include "ExposurePassSettings.h"
#include "render/graph/RenderPass.h"

#include <slang-rhi.h>
#include <cstdint>

class ExposurePass : public RenderPass
{
    RL_PLUGIN_CLASS(ExposurePass, "ExposurePass", "Computes exposure from a luminance histogram.");

    ExposurePass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties);

    [[nodiscard]] Properties getProperties() const override;
    void                             setProperties(const Properties& properties) override;

    static ref<ExposurePass> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<ExposurePass>(std::move(device), properties);
    }

    void renderUI(RenderContext* context, Gui::Widgets& widget) override;

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* context, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    ExposurePassSettings parameters;

    rhi::ComPtr<rhi::IComputePipeline> histogramPipeline;
    rhi::ComPtr<rhi::IComputePipeline> resolvePipeline;
    uint64_t                           previousSceneGeneration = 0;
    bool                               hasHistory = false;
};
