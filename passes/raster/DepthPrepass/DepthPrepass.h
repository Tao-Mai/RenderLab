#pragma once
#include <slang-rhi.h>

#include "render/graph/RenderPass.h"

#include <slang-rhi/shader-cursor.h>
#include "render/raster/MeshDraw.h"

class DepthPrepass : public RenderPass
{
    static const RenderPassReflection::Field kPorts[1];

    RL_PLUGIN_CLASS(DepthPrepass, "DepthPrepass", .desc = "Writes the main view depth buffer.", .ports = kPorts);

    DepthPrepass(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties) : RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static std::shared_ptr<DepthPrepass> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<DepthPrepass>(std::move(device), properties);
    }

    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    MeshCullPipelines pipelines;
};
