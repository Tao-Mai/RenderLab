#include "render/feature/editor/EditorInterface.h"
#include <array>
#include "render/DescriptorManager.h"
#include "render/PipelineManager.h"
#include "render/feature/editor/EditorShaderData.h"
vk::DescriptorSetLayout EditorInterface::viewLayout(DescriptorManager& descriptors)
{
    const std::array viewBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding         = EditorInterface::kViewUniformBinding,
            .descriptorType  = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
    };
    return descriptors.getOrCreateLayout(viewBindings);
}
vk::PipelineLayout EditorInterface::pipelineLayout(DescriptorManager& descriptors, PipelineManager& pipelines)
{
    const std::array sets{viewLayout(descriptors)};
    const std::array ranges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(EditorLightMarkerPushConstants)
    }};
    return pipelines.getOrCreateLayout(sets, ranges);
}
