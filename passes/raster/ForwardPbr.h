#pragma once

#include "render/device/Device.h"
#include "render/resources/MaterialTable.h"

#include <array>

struct PassFrame;

class ForwardPbrPipelines
{
public:
    void                       setup(vk::Format colorFormat, PipelineState state, bool ibl, bool clearcoat);
    [[nodiscard]] vk::Pipeline get(const MaterialTable::MaterialInfo& material);

private:
    GraphicsPipelineInfo                       pipelineInfo;
    std::array<ShaderHandle, 4>                shaders;
    std::array<std::array<vk::Pipeline, 2>, 4> pipelines;
    bool                                       iblEnabled       = false;
    bool                                       clearcoatEnabled = false;
};

void bindForwardPbrResources(vk::raii::CommandBuffer& commands, const PassFrame& frame);
