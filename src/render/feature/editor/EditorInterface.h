#pragma once
#include <vulkan/vulkan.hpp>
class DescriptorManager;
class PipelineManager;
namespace EditorInterface
{
vk::DescriptorSetLayout viewLayout(DescriptorManager& descriptors);
vk::PipelineLayout      pipelineLayout(DescriptorManager& descriptors, PipelineManager& pipelines);
}  // namespace EditorInterface
