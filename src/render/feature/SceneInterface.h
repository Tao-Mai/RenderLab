#pragma once
#include <vulkan/vulkan.hpp>
class DescriptorManager;
class PipelineManager;
namespace SceneInterface
{
vk::DescriptorSetLayout sceneLayout(DescriptorManager& descriptors);
vk::DescriptorSetLayout materialLayout(DescriptorManager& descriptors);
vk::PipelineLayout      meshLayout(DescriptorManager& descriptors, PipelineManager& pipelines);
vk::PipelineLayout      shadowLayout(DescriptorManager& descriptors, PipelineManager& pipelines);
vk::PipelineLayout      skyboxLayout(DescriptorManager& descriptors, PipelineManager& pipelines);
}  // namespace SceneInterface
