#pragma once

#include "render/editor/EditorFrameInput.h"
#include "render/graph/RenderPass.h"

class EditorPickingPass : public RenderPass
{
    static const RenderPassReflection::Field kPorts[2];

    RL_PLUGIN_CLASS(EditorPickingPass, kEditorPickingPassType, .desc = "Renders object IDs for picking.", .ports = kPorts);

    EditorPickingPass(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties) :
        RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static std::shared_ptr<EditorPickingPass> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<EditorPickingPass>(std::move(device), properties);
    }

    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    rhi::ComPtr<rhi::IRenderPipeline> pipeline;
    rhi::ComPtr<rhi::IBuffer>         readback;
};
