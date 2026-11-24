#pragma once

#include <slang-rhi.h>
#include <span>
#include "render/GpuScene.h"
#include "render/resources/MaterialTable.h"

struct MeshCullPipelines
{
    rhi::ComPtr<rhi::IRenderPipeline> singleSided, doubleSided;
    [[nodiscard]] rhi::IShaderObject&
    bind(rhi::IRenderPassEncoder& pass, const MaterialTable::MaterialInfo& material) const;
};
[[nodiscard]] rhi::ComPtr<rhi::IInputLayout> createMeshInputLayout(rhi::IDevice& device);
[[nodiscard]] MeshCullPipelines createMeshCullPipelines(rhi::IDevice& device, rhi::RenderPipelineDesc desc);

class MeshDrawRecorder
{
public:
    MeshDrawRecorder(rhi::IRenderPassEncoder& pass, rhi::RenderState state, GpuScene& scene) :
        pass(pass), state(state), scene(scene)
    {
    }

    template <class BindPipeline>
    void drawOpaque(BindPipeline&& bindPipeline)
    {
        for (const uint32_t index : scene.visibleDraws())
        {
            const auto& item     = scene.drawCalls()[index];
            const auto& material = scene.materials().info(item.materialIndex);
            if (material.alphaMode != kAlphaModeBlend)
                draw(item, bindPipeline(material));
        }
    }

    template <class BindPipeline>
    void drawOrdered(std::span<const uint32_t> drawOrder, BindPipeline&& bindPipeline)
    {
        for (const auto index : drawOrder)
        {
            const auto& item = scene.drawCalls()[index];
            draw(item, bindPipeline(scene.materials().info(item.materialIndex)));
        }
    }

private:
    void draw(const GpuDraw& item, rhi::IShaderObject& object);

    rhi::IRenderPassEncoder& pass;
    rhi::RenderState        state;
    GpuScene&               scene;
};
