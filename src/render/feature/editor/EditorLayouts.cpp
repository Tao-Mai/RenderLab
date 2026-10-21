#include "render/feature/editor/EditorLayouts.h"

#include "render/RenderContext.h"
#include "render/feature/editor/EditorShaderData.h"

#include <array>

void EditorLayouts::init()
{
    auto&            device = *renderContext().device;
    const std::array bindings{vk::DescriptorSetLayoutBinding{
        .binding         = EditorInterface::kViewUniformBinding,
        .descriptorType  = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
    }};
    viewLayout = device.createDescriptorSetLayout(bindings);
    const std::array sets{&viewLayout};
    const std::array ranges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(EditorLightMarkerPushConstants),
    }};
    pipelineLayout = device.createPipelineLayout(sets, ranges);
}
