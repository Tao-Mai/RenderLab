#pragma once

#include "ExposurePassSettings.h"
#include "render/graph/RenderPass.h"

#include <slang-rhi.h>
#include "asset/Asset.h"
#include "core/Logger.h"
#include "render/device/GpuResources.h"
#include "render/postprocess/ExposureState.h"

class ExposurePass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(ExposurePass, kExposurePassType, "Computes exposure from a luminance histogram.");

    ExposurePass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void                             setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<ExposurePass> create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<ExposurePass>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(Falcor::RenderContext* context, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    ExposurePassSettings parameters;

    GpuResources::BufferHandle         histogram;
    rhi::ComPtr<rhi::IComputePipeline> histogramPipeline;
    rhi::ComPtr<rhi::IComputePipeline> resolvePipeline;
    rhi::Extent3D                      extent;
    AssetID<struct SceneAsset>         previousScene;
    bool                               hasHistory = false;
};
