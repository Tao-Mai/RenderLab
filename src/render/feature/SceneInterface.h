#pragma once

#include "render/device/Device.h"

// Shared layouts are created once and owned here.
struct SceneInterface
{
    void init();

    DescriptorSetLayout sceneLayout;
    DescriptorSetLayout materialLayout;
    PipelineLayout      meshLayout;
    PipelineLayout      shadowLayout;
    PipelineLayout      skyboxLayout;
};
