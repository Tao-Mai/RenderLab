#pragma once

#include "core/profiling/Profiler.h"
#include "render/RenderContext.h"
#include "render/PreparedScene.h"
#include "render/device/Device.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"

#include <cstdint>
#include <span>

struct MeshCullPipelines
{
    vk::Pipeline singleSided;
    vk::Pipeline doubleSided;

    [[nodiscard]] vk::Pipeline get(const MaterialTable::MaterialInfo& material) const
    {
        return material.doubleSided ? doubleSided : singleSided;
    }
};

[[nodiscard]] MeshCullPipelines createMeshCullPipelines(Falcor::Device& device, GraphicsPipelineInfo info);
void                            bindMeshGeometry(vk::raii::CommandBuffer& commands, const Falcor::RenderContext& frame);

class MeshDrawRecorder
{
public:
    MeshDrawRecorder(vk::raii::CommandBuffer& commands, const Falcor::RenderContext& frame) :
        commands(commands),
        data(*frame.scene),
        resources(*frame.resources),
        materials(*frame.materialTable),
        layout(frame.getDevice()->meshPipelineLayout),
        profiler(frame.profiler)
    {
    }

    template <class SelectPipeline>
    void drawOpaque(SelectPipeline&& selectPipeline)
    {
        for (uint32_t drawIndex = 0; drawIndex < data.drawItems.size(); ++drawIndex)
        {
            if (!data.isVisible(drawIndex))
                continue;

            const auto  materialIndex = data.materialIndices[drawIndex];
            const auto& material      = materials.info(materialIndex);
            if (material.alphaMode != kAlphaModeBlend)
                draw(data.drawItems[drawIndex], materialIndex, selectPipeline(material));
        }
    }

    template <class SelectPipeline>
    void drawOrdered(std::span<const uint32_t> drawOrder, SelectPipeline&& selectPipeline)
    {
        for (const auto drawIndex : drawOrder)
        {
            const auto materialIndex = data.materialIndices[drawIndex];
            draw(data.drawItems[drawIndex], materialIndex, selectPipeline(materials.info(materialIndex)));
        }
    }

private:
    void draw(const DrawItem& item, uint32_t materialIndex, vk::Pipeline pipeline);

    vk::raii::CommandBuffer& commands;
    const PreparedScene&        data;
    RenderResourceManager&   resources;
    const MaterialTable&     materials;
    vk::PipelineLayout       layout;
    Profiler*                profiler;
    vk::Pipeline             boundPipeline;
    const GpuMesh*           boundMesh = nullptr;
};
