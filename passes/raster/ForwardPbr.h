#pragma once

#include "render/device/Device.h"
#include "render/resources/MaterialTable.h"

#include <array>

namespace Falcor
{
class RenderContext;
}  // namespace Falcor

class ForwardPbrPipelines
{
public:
    void setup(const Falcor::Device& device, vk::Format colorFormat, PipelineState state, bool ibl, bool clearcoat);
    [[nodiscard]] vk::Pipeline get(const Falcor::RenderContext& context, const MaterialTable::MaterialInfo& material);

private:
    GraphicsPipelineInfo                       pipelineInfo;
    std::array<ShaderHandle, 4>                shaders;
    std::array<std::array<vk::Pipeline, 2>, 4> pipelines;
    bool                                       iblEnabled       = false;
    bool                                       clearcoatEnabled = false;
};

void bindForwardPbrResources(vk::raii::CommandBuffer& commands, const Falcor::RenderContext& frame);
