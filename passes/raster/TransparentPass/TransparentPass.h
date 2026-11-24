#pragma once
#include <slang-rhi.h>

#include "render/graph/RenderPass.h"

#include <slang-rhi/shader-cursor.h>
#include "passes/raster/ForwardPbr.h"

#include <vector>

class TransparentPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(TransparentPass, "TransparentPass", "Renders transparent geometry.");

    TransparentPass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties) :
        Falcor::RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static Falcor::ref<TransparentPass> create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<TransparentPass>(std::move(device), properties);
    }

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    void sortDrawItems(const Falcor::RenderContext& frame);

    std::vector<uint32_t> sortedDrawItems;

    ForwardPbrPipelines pipelines;
};
