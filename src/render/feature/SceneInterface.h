#pragma once

#include <vulkan/vulkan.hpp>

// Fixed scene interfaces; the managers retain ownership of the Vulkan layouts.
struct SceneInterface
{
    void init();

    vk::DescriptorSetLayout sceneLayout    = nullptr;
    vk::DescriptorSetLayout materialLayout = nullptr;
    vk::PipelineLayout      meshLayout     = nullptr;
    vk::PipelineLayout      shadowLayout   = nullptr;
    vk::PipelineLayout      skyboxLayout   = nullptr;
};
