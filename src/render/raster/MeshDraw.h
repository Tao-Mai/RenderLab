#pragma once
#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include <span>
#include "core/profiling/Profiler.h"
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"

class ShadowBindings;

struct MeshCullPipelines
{
    rhi::ComPtr<rhi::IRenderPipeline>   singleSided, doubleSided;
    [[nodiscard]] rhi::IRenderPipeline* get(const MaterialTable::MaterialInfo& material) const
    {
        return material.doubleSided ? doubleSided.get() : singleSided.get();
    }
};
[[nodiscard]] rhi::ComPtr<rhi::IInputLayout> createMeshInputLayout(rhi::IDevice& device);
[[nodiscard]] MeshCullPipelines createMeshCullPipelines(rhi::IDevice& device, rhi::RenderPipelineDesc desc);

class MeshDrawRecorder
{
public:
    MeshDrawRecorder(
        rhi::IRenderPassEncoder& pass,
        rhi::RenderState state,
        const Falcor::RenderContext& frame,
        const ShadowBindings* shadows = nullptr
    ) :
        pass(pass), state(state), frame(frame), shadows(shadows)
    {
    }
    template <class SelectPipeline>
    void drawOpaque(SelectPipeline&& selectPipeline)
    {
        for (uint32_t index = 0; index < frame.scene->drawItems.size(); ++index)
        {
            if (!frame.scene->isVisible(index))
                continue;
            const auto  materialIndex = frame.scene->materialIndices[index];
            const auto& material      = frame.materialTable->info(materialIndex);
            if (material.alphaMode != kAlphaModeBlend)
                draw(frame.scene->drawItems[index], materialIndex, selectPipeline(material));
        }
    }
    template <class SelectPipeline>
    void drawOrdered(std::span<const uint32_t> drawOrder, SelectPipeline&& selectPipeline)
    {
        for (const auto index : drawOrder)
        {
            const auto materialIndex = frame.scene->materialIndices[index];
            draw(
                frame.scene->drawItems[index],
                materialIndex,
                selectPipeline(frame.materialTable->info(materialIndex))
            );
        }
    }

private:
    void                         draw(const DrawItem& item, uint32_t materialIndex, rhi::IRenderPipeline* pipeline);
    rhi::IRenderPassEncoder&     pass;
    rhi::RenderState             state;
    const Falcor::RenderContext& frame;
    const ShadowBindings*        shadows;
};
