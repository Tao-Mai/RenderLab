#include "render/pass/scene_pipelines.h"

#include "render/device/vk_check.h"
#include "render/resource/shader.h"
#include "render/resource/shader_data.h"
#include "render/resource/vertex_layout.h"

#include <array>
#include <vector>

void ScenePipelines::init(
    const vk::raii::Device& device,
    vk::Format colorFormat,
    vk::Format depthFormat,
    vk::DescriptorSetLayout sceneLayout,
    vk::DescriptorSetLayout materialLayout,
    const Shader& sceneShader,
    const Shader& lightShader)
{
    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eVertex, .module = sceneShader.handle(),
        .pName = "vertMain"};
    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eFragment, .module = sceneShader.handle(),
        .pName = "fragMain"};
    vk::PipelineShaderStageCreateInfo shaderStages[] = {
        vertShaderStageInfo, fragShaderStageInfo};

    const vk::VertexInputBindingDescription binding =
        VertexLayout::bindingDescription();
    const auto                             attributes = VertexLayout::attributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList
    };
    vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1,
                                                      .scissorCount = 1};

    vk::PipelineRasterizationStateCreateInfo rasterizer{.depthClampEnable = vk::False,
                                                        .rasterizerDiscardEnable =
                                                        vk::False,
                                                        .polygonMode =
                                                        vk::PolygonMode::eFill,
                                                        .cullMode =
                                                        vk::CullModeFlagBits::eNone,
                                                        .frontFace =
                                                        vk::FrontFace::eClockwise,
                                                        .depthBiasEnable = vk::False,
                                                        .lineWidth = 1.0f};

    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False};

    vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False,
    };

    const vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR |
        vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB |
        vk::ColorComponentFlagBits::eA,
    };

    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };

    std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport,
                                                   vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()};

    const vk::PushConstantRange pushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex |
        vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(LightPushConstants),
    };
    const std::array descriptorSetLayouts = {
        sceneLayout,
        materialLayout,
    };
    vk::PipelineLayoutCreateInfo layoutInfo{
        .setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size()),
        .pSetLayouts = descriptorSetLayouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };
    layout = vkCheck(device.createPipelineLayout(layoutInfo), "vkCreatePipelineLayout");

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo>
        pipelineCreateInfoChain = {
            {.stageCount = 2,
             .pStages = shaderStages,
             .pVertexInputState = &vertexInputInfo,
             .pInputAssemblyState = &inputAssembly,
             .pViewportState = &viewportState,
             .pRasterizationState = &rasterizer,
             .pMultisampleState = &multisampling,
             .pDepthStencilState = &depthStencil,
             .pColorBlendState = &colorBlending,
             .pDynamicState = &dynamicState,
             .layout = layout,
             .renderPass = nullptr},
            {.colorAttachmentCount = 1,
             .pColorAttachmentFormats = &colorFormat,
             .depthAttachmentFormat = depthFormat}};

    scenePipeline = vkCheck(
        device.createGraphicsPipeline(
            nullptr,
            pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()),
        "vkCreateGraphicsPipelines");

    const std::array lightShaderStages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = lightShader.handle(),
            .pName = "vertMain",
        },
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = lightShader.handle(),
            .pName = "fragMain",
        },
    };
    const auto lightAttributes = VertexLayout::positionAttributeDescription();
    const vk::PipelineVertexInputStateCreateInfo lightVertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(lightAttributes.size()),
        .pVertexAttributeDescriptions = lightAttributes.data(),
    };
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo>
        lightMarkerPipelineCreateInfoChain = {
            {.stageCount = static_cast<uint32_t>(lightShaderStages.size()),
             .pStages = lightShaderStages.data(),
             .pVertexInputState = &lightVertexInputInfo,
             .pInputAssemblyState = &inputAssembly,
             .pViewportState = &viewportState,
             .pRasterizationState = &rasterizer,
             .pMultisampleState = &multisampling,
             .pDepthStencilState = &depthStencil,
             .pColorBlendState = &colorBlending,
             .pDynamicState = &dynamicState,
             .layout = layout,
             .renderPass = nullptr},
            {.colorAttachmentCount = 1,
             .pColorAttachmentFormats = &colorFormat,
             .depthAttachmentFormat = depthFormat}};
    lightMarkerPipeline = vkCheck(
        device.createGraphicsPipeline(
            nullptr,
            lightMarkerPipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()),
        "vkCreateGraphicsPipelines");
}

void ScenePipelines::reset() noexcept
{
    lightMarkerPipeline = nullptr;
    scenePipeline = nullptr;
    layout = nullptr;
}

void ScenePipelines::bindScene(vk::raii::CommandBuffer& commandBuffer) const
{
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *scenePipeline);
}

void ScenePipelines::bindLightMarkers(vk::raii::CommandBuffer& commandBuffer) const
{
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *lightMarkerPipeline);
}

vk::PipelineLayout ScenePipelines::layoutHandle() const
{
    return *layout;
}
