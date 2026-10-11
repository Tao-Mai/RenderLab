#include "render/PipelineManager.h"

#include "core/Logger.h"
#include "render/DescriptorManager.h"
#include "render/device/VkCheck.h"
#include "render/resource/VertexLayout.h"

#include <array>
#include <functional>
#include <span>
#include <utility>
#include <vulkan/vulkan_hash.hpp>

std::size_t PipelineKeyHash::operator()(const PipelineKey& key) const noexcept
{
    std::size_t seed    = 0;
    const auto  combine = [&seed](uint64_t value)
    { seed ^= std::hash<uint64_t>{}(value) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2); };
    combine(key.shader.index);
    combine(std::hash<vk::PipelineLayout>{}(key.layout));
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
    combine(key.colorFormats.size());
    for (const auto format : key.colorFormats)
        combine(static_cast<uint32_t>(format));
    combine(static_cast<uint32_t>(key.depthFormat));
    combine(key.viewMask);
    return seed;
}

void PipelineManager::init(
    const vk::raii::Device& targetDevice, DescriptorManager& descriptors, ShaderManager& targetShaders
)
{
    device            = &targetDevice;
    this->descriptors = &descriptors;
    shaders           = &targetShaders;
    pipelineCache     = vkCheck(device->createPipelineCache(vk::PipelineCacheCreateInfo{}));
}

void PipelineManager::shutdown() noexcept
{
    pipelines.clear();
    layouts.clear();
    pipelineCache = nullptr;
    descriptors   = nullptr;
    shaders       = nullptr;
    device        = nullptr;
}

vk::PipelineLayout PipelineManager::getOrCreateLayout(
    std::span<const vk::DescriptorSetLayout> sets, std::span<const vk::PushConstantRange> ranges
)
{
    for (const auto& entry : layouts)
        if (std::ranges::equal(entry.sets, sets) && std::ranges::equal(entry.ranges, ranges))
            return *entry.layout;
    auto       layout = vkCheck(device->createPipelineLayout({
              .setLayoutCount         = static_cast<uint32_t>(sets.size()),
              .pSetLayouts            = sets.data(),
              .pushConstantRangeCount = static_cast<uint32_t>(ranges.size()),
              .pPushConstantRanges    = ranges.data(),
    }));
    const auto handle = *layout;
    layouts.push_back({{sets.begin(), sets.end()}, {ranges.begin(), ranges.end()}, std::move(layout)});
    return handle;
}

vk::Pipeline PipelineManager::getOrCreate(const PipelineKey& key)
{
    DCHECK(device && descriptors && shaders, "PipelineManager is not initialized");
    if (const auto found = pipelines.find(key); found != pipelines.end())
    {
        return *found->second;
    }
    const ShaderMetadata& metadata = shaders->metadata(key.shader);
    const auto layout = std::ranges::find_if(layouts, [&](const auto& entry) { return *entry.layout == key.layout; });
    DCHECK(layout != layouts.end());
    for (const auto& binding : metadata.bindings)
        CHECK(
            binding.set < layout->sets.size() && descriptors->containsBinding(layout->sets[binding.set], binding),
            "shader '{}' set {} binding {} does not match pipeline layout",
            metadata.id,
            binding.set,
            binding.binding
        );
    if (metadata.pushConstantSize)
        CHECK(
            std::ranges::any_of(
                layout->ranges,
                [&](const auto& range)
                {
                    return range.offset == 0 && range.size >= metadata.pushConstantSize &&
                        (range.stageFlags & (vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)) ==
                        (vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
                }
            ),
            "shader '{}' push constants do not fit pipeline layout",
            metadata.id
        );
    CHECK(metadata.entries.size() == 2);
    const std::array shaderStages = {
        vk::PipelineShaderStageCreateInfo{
            .stage  = vk::ShaderStageFlagBits::eVertex,
            .module = shaders->module(key.shader),
            .pName  = metadata.entries[0].c_str()
        },
        vk::PipelineShaderStageCreateInfo{
            .stage  = vk::ShaderStageFlagBits::eFragment,
            .module = shaders->module(key.shader),
            .pName  = metadata.entries[1].c_str()
        },
    };
    const vk::VertexInputBindingDescription binding              = VertexLayout::bindingDescription();
    const auto                              meshAttributes       = VertexLayout::attributeDescriptions();
    const auto                              positionAttributes   = VertexLayout::positionAttributeDescription();
    const std::array                        positionUvAttributes = {meshAttributes[0], meshAttributes[2]};
    std::span<const vk::VertexInputAttributeDescription> attributes;
    switch (key.state.vertexLayout)
    {
        case VertexLayoutPreset::Mesh:
            attributes = meshAttributes;
            break;
        case VertexLayoutPreset::PositionOnly:
            attributes = positionAttributes;
            break;
        case VertexLayoutPreset::PositionUv:
            attributes = positionUvAttributes;
            break;
        case VertexLayoutPreset::None:
            break;
    }
    const bool                                   hasVertexInput = !attributes.empty();
    const vk::PipelineVertexInputStateCreateInfo vertexInput{
        .vertexBindingDescriptionCount   = hasVertexInput ? 1u : 0u,
        .pVertexBindingDescriptions      = hasVertexInput ? &binding : nullptr,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions    = attributes.data(),
    };
    const vk::PipelineInputAssemblyStateCreateInfo inputAssembly{.topology = key.state.topology};
    const vk::PipelineViewportStateCreateInfo      viewport{.viewportCount = 1, .scissorCount = 1};
    const vk::PipelineRasterizationStateCreateInfo raster{
        .polygonMode = key.state.polygonMode,
        .cullMode    = key.state.cullMode,
        .frontFace   = key.state.frontFace,
        .lineWidth   = 1.0f,
    };
    const vk::PipelineMultisampleStateCreateInfo multisample{
        .rasterizationSamples = key.state.samples,
    };
    const vk::PipelineDepthStencilStateCreateInfo depth{
        .depthTestEnable  = key.state.depthTest,
        .depthWriteEnable = key.state.depthWrite,
        .depthCompareOp   = key.state.depthCompare,
    };
    const vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable         = key.state.blend,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp        = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .alphaBlendOp        = vk::BlendOp::eAdd,
        .colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    const std::vector                           colorBlendAttachments(key.colorFormats.size(), colorBlendAttachment);
    const vk::PipelineColorBlendStateCreateInfo colorBlend{
        .attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size()),
        .pAttachments    = colorBlendAttachments.data(),
    };
    constexpr std::array                     dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates    = dynamicStates.data(),
    };
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
        {.stageCount          = static_cast<uint32_t>(shaderStages.size()),
         .pStages             = shaderStages.data(),
         .pVertexInputState   = &vertexInput,
         .pInputAssemblyState = &inputAssembly,
         .pViewportState      = &viewport,
         .pRasterizationState = &raster,
         .pMultisampleState   = &multisample,
         .pDepthStencilState  = key.depthFormat == vk::Format::eUndefined ? nullptr : &depth,
         .pColorBlendState    = &colorBlend,
         .pDynamicState       = &dynamic,
         .layout              = key.layout},
        {.viewMask                = key.viewMask,
         .colorAttachmentCount    = static_cast<uint32_t>(key.colorFormats.size()),
         .pColorAttachmentFormats = key.colorFormats.data(),
         .depthAttachmentFormat   = key.depthFormat},
    };
    auto pipeline = vkCheck(device->createGraphicsPipeline(pipelineCache, chain.get<vk::GraphicsPipelineCreateInfo>()));
    const vk::Pipeline handle = *pipeline;
    pipelines.emplace(key, std::move(pipeline));
    return handle;
}
