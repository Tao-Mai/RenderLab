#pragma once

#include "render/device/Device.h"

struct EditorLayouts
{
    void init();

    DescriptorSetLayout viewLayout;
    PipelineLayout      pipelineLayout;
};
