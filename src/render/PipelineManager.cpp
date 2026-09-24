#include "render/PipelineManager.h"

#include "core/Logger.h"
#include "render/DescriptorManager.h"
#include "render/device/VkCheck.h"
#include "render/resource/ShaderData.h"
#include "render/resource/VertexLayout.h"

#include <array>
#include <functional>
#include <utility>

PipelineState PipelineState::preset(RenderMode mode)
{
    PipelineState state;
    switch (mode)
    {
    case RenderMode::Opaque:
    case RenderMode::AlphaTest: break;
    case RenderMode::Transparent:
        state.depthWrite = false;
        state.blend = true;
        break;
    case RenderMode::Shadow:
    case RenderMode::DepthOnly:
        state.vertexLayout = VertexLayoutPreset::PositionOnly;
        break;
    case RenderMode::Skybox:
        state.depthWrite = false;
        state.depthCompare = vk::CompareOp::eLessOrEqual;
        break;
    case RenderMode::PostProcess:
        state.vertexLayout = VertexLayoutPreset::None;
        state.depthTest = false;
        state.depthWrite = false;
        break;
    case RenderMode::LightMarker:
        state.vertexLayout = VertexLayoutPreset::PositionOnly;
        break;
    case RenderMode::Picking:
        state.vertexLayout = VertexLayoutPreset::PositionOnly;
        state.depthWrite = false;
        state.depthCompare = vk::CompareOp::eLessOrEqual;
        break;
    }
    return state;
}

std::size_t PipelineKeyHash::operator()(const PipelineKey& key) const noexcept
{
    std::size_t seed = 0;
    const auto combine = [&seed](uint64_t value)
    {
        seed ^= std::hash<uint64_t>{}(value) + 0x9e3779b97f4a7c15ULL +
            (seed << 6) + (seed >> 2);
    };
    combine(key.shader.index);
    combine(static_cast<uint32_t>(key.layout));
    combine(static_cast<uint32_t>(key.state.vertexLayout));
    combine(static_cast<uint32_t>(key.state.topology));
    combine(static_cast<uint32_t>(key.state.polygonMode));
    combine(static_cast<uint32_t>(key.state.cullMode));
    combine(static_cast<uint32_t>(key.state.frontFace));
    combine(static_cast<uint32_t>(key.state.samples));
    combine(static_cast<uint32_t>(key.state.depthCompare));
    combine(key.state.depthTest);
    combine(key.state.depthWrite);
    combine(key.state.blend);
    combine(static_cast<uint32_t>(key.colorFormat));
    combine(static_cast<uint32_t>(key.depthFormat));
    return seed;
}

void PipelineManager::init(const vk::raii::Device& targetDevice,
    DescriptorManager& descriptors, ShaderManager& targetShaders)
{
    device = &targetDevice;
    this->descriptors = &descriptors;
    shaders = &targetShaders;
    pipelineCache = vkCheck(device->createPipelineCache(vk::PipelineCacheCreateInfo{}));
    const vk::PushConstantRange pushRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(LightPushConstants),
    };
    const std::array sceneMaterialSets = {
        descriptors.layout(DescriptorLayoutPreset::Scene),
        descriptors.layout(DescriptorLayoutPreset::Material),
    };
    const vk::PipelineLayoutCreateInfo sceneMaterialInfo{
        .setLayoutCount = static_cast<uint32_t>(sceneMaterialSets.size()),
        .pSetLayouts = sceneMaterialSets.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };
    sceneMaterialLayout = vkCheck(device->createPipelineLayout(sceneMaterialInfo));

    const vk::DescriptorSetLayout sceneSet = descriptors.layout(DescriptorLayoutPreset::Scene);
    const vk::PipelineLayoutCreateInfo sceneOnlyInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &sceneSet,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };
    sceneOnlyLayout = vkCheck(device->createPipelineLayout(sceneOnlyInfo));
}

void PipelineManager::reset() noexcept
{
    pipelines.clear();
    pipelineCache = nullptr;
    sceneOnlyLayout = nullptr;
    sceneMaterialLayout = nullptr;
    descriptors = nullptr;
    shaders = nullptr;
    device = nullptr;
}

vk::PipelineLayout PipelineManager::layout(PipelineLayoutPreset preset) const
{
    return preset == PipelineLayoutPreset::SceneMaterial
        ? *sceneMaterialLayout : *sceneOnlyLayout;
}

vk::Pipeline PipelineManager::getOrCreate(const PipelineKey& key)
{
    CHECK(device != nullptr && descriptors != nullptr && shaders != nullptr,
        "PipelineManager is not initialized");
    if (const auto found = pipelines.find(key); found != pipelines.end())
    {
        return *found->second;
    }
    const ShaderMetadata& metadata = shaders->metadata(key.shader);
    for (const ShaderMetadata::Binding& binding : metadata.bindings)
    {
        const bool supported = binding.set == RenderInterface::sceneSet
            ? descriptors->containsBinding(DescriptorLayoutPreset::Scene,
                binding.binding, binding.type)
            : binding.set == RenderInterface::materialSet &&
                key.layout == PipelineLayoutPreset::SceneMaterial &&
                descriptors->containsBinding(DescriptorLayoutPreset::Material,
                    binding.binding, binding.type);
        CHECK(supported,
            "shader '{}' binding set {} binding {} ({}) is not in the selected pipeline layout",
            metadata.id, binding.set, binding.binding, vk::to_string(binding.type));
    }
    const std::array shaderStages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = shaders->module(key.shader), .pName = "vertMain"},
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = shaders->module(key.shader), .pName = "fragMain"},
    };
    const vk::VertexInputBindingDescription binding = VertexLayout::bindingDescription();
    const auto meshAttributes = VertexLayout::attributeDescriptions();
    const auto positionAttributes = VertexLayout::positionAttributeDescription();
    const bool hasVertexInput = key.state.vertexLayout != VertexLayoutPreset::None;
    const bool fullVertexInput = key.state.vertexLayout == VertexLayoutPreset::Mesh;
    const vk::PipelineVertexInputStateCreateInfo vertexInput{
        .vertexBindingDescriptionCount = hasVertexInput ? 1u : 0u,
        .pVertexBindingDescriptions = hasVertexInput ? &binding : nullptr,
        .vertexAttributeDescriptionCount = hasVertexInput
            ? (fullVertexInput ? static_cast<uint32_t>(meshAttributes.size())
                               : static_cast<uint32_t>(positionAttributes.size()))
            : 0u,
        .pVertexAttributeDescriptions = hasVertexInput
            ? (fullVertexInput ? meshAttributes.data() : positionAttributes.data())
            : nullptr,
    };
    const vk::PipelineInputAssemblyStateCreateInfo inputAssembly{.topology = key.state.topology};
    const vk::PipelineViewportStateCreateInfo viewport{.viewportCount = 1, .scissorCount = 1};
    const vk::PipelineRasterizationStateCreateInfo raster{
        .polygonMode = key.state.polygonMode,
        .cullMode = key.state.cullMode,
        .frontFace = key.state.frontFace,
        .lineWidth = 1.0f,
    };
    const vk::PipelineMultisampleStateCreateInfo multisample{
        .rasterizationSamples = key.state.samples,
    };
    const vk::PipelineDepthStencilStateCreateInfo depth{
        .depthTestEnable = key.state.depthTest,
        .depthWriteEnable = key.state.depthWrite,
        .depthCompareOp = key.state.depthCompare,
    };
    const vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = key.state.blend,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR |
            vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
            vk::ColorComponentFlagBits::eA,
    };
    const bool hasColor = key.colorFormat != vk::Format::eUndefined;
    const vk::PipelineColorBlendStateCreateInfo colorBlend{
        .attachmentCount = hasColor ? 1u : 0u,
        .pAttachments = hasColor ? &colorBlendAttachment : nullptr,
    };
    constexpr std::array dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
        {.stageCount = static_cast<uint32_t>(shaderStages.size()),
         .pStages = shaderStages.data(),
         .pVertexInputState = &vertexInput,
         .pInputAssemblyState = &inputAssembly,
         .pViewportState = &viewport,
         .pRasterizationState = &raster,
         .pMultisampleState = &multisample,
         .pDepthStencilState = key.depthFormat == vk::Format::eUndefined ? nullptr : &depth,
         .pColorBlendState = &colorBlend,
         .pDynamicState = &dynamic,
         .layout = layout(key.layout)},
        {.colorAttachmentCount = hasColor ? 1u : 0u,
         .pColorAttachmentFormats = hasColor ? &key.colorFormat : nullptr,
         .depthAttachmentFormat = key.depthFormat},
    };
    auto pipeline = vkCheck(device->createGraphicsPipeline(
        pipelineCache, chain.get<vk::GraphicsPipelineCreateInfo>()));
    const vk::Pipeline handle = *pipeline;
    pipelines.emplace(key, std::move(pipeline));
    return handle;
}
