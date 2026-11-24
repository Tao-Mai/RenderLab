#pragma once

#include "render/graph/RenderPass.h"

#include <slang-rhi.h>

#include "render/device/GpuResources.h"
#include "render/resources/ShaderManager.h"

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>

class GpuMesh;

struct DrawItem;

class EditorPickingPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(EditorPickingPass, "EditorPickingPass", "Renders object IDs for picking.");

    EditorPickingPass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties) :
        Falcor::RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static Falcor::ref<EditorPickingPass> create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<EditorPickingPass>(std::move(device), properties);
    }

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    GpuResources::ImageHandle         depth;
    GpuResources::ImageHandle         pickingImage;
    ShaderHandle                      shader;
    rhi::ComPtr<rhi::IRenderPipeline> pipeline;
};
