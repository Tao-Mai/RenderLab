#pragma once

#include "render/editor/EditorFrameInput.h"
#include "render/graph/RenderPass.h"

class EditorPickingPass : public RenderPass
{
    RL_PLUGIN_CLASS(EditorPickingPass, kEditorPickingPassType, "Renders object IDs for picking.");

    EditorPickingPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) : RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static ref<EditorPickingPass> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<EditorPickingPass>(std::move(device), properties);
    }

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    rhi::ComPtr<rhi::IRenderPipeline> pipeline;
    rhi::ComPtr<rhi::IBuffer>         readback;
};
