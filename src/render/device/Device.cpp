#include "render/device/Device.h"

#include "core/Logger.h"
#include "render/device/VkCheck.h"
#include "render/resource/VertexLayout.h"

#include <algorithm>
#include <array>
#include <functional>
#include <utility>
#include <vulkan/vulkan_hash.hpp>

Device::~Device() { DCHECK(!initialized); }

void Device::init(const vk::raii::Device& targetDevice, ShaderManager& targetShaders)
{
    DCHECK(!initialized);
    device                                   = &targetDevice;
    shaders                                  = &targetShaders;
    constexpr uint32_t   kDescriptorCapacity = 4096;
    constexpr std::array kPoolSizes{
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eSampledImage, .descriptorCount = kDescriptorCapacity},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eSampler, .descriptorCount = kDescriptorCapacity},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = kDescriptorCapacity},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = kDescriptorCapacity},
    };
    descriptorPool = vkCheck(device->createDescriptorPool({
        .flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets       = kDescriptorCapacity,
        .poolSizeCount = static_cast<uint32_t>(kPoolSizes.size()),
        .pPoolSizes    = kPoolSizes.data(),
    }));
    pipelineCache  = vkCheck(device->createPipelineCache(vk::PipelineCacheCreateInfo{}));
    createSharedLayouts();
    DEBUG_EXEC(initialized = true);
}

void Device::shutdown() noexcept
{
    DCHECK(initialized);
    pipelines.clear();
    editorPipelineLayout          = {};
    meshPipelineLayout            = {};
    editorViewDescriptorSetLayout = {};
    materialDescriptorSetLayout   = {};
    sceneDescriptorSetLayout      = {};
    pipelineCache                 = nullptr;
    descriptorPool                = nullptr;
    shaders                       = nullptr;
    device                        = nullptr;
    DEBUG_EXEC(initialized = false);
}

DescriptorSetLayout Device::createDescriptorSetLayout(std::span<const vk::DescriptorSetLayoutBinding> bindings) const
{
    DCHECK(device);
    DescriptorSetLayout result;
    result.bindings.assign(bindings.begin(), bindings.end());
    result.handle = vkCheck(device->createDescriptorSetLayout({
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings    = bindings.data(),
    }));
    return result;
}

PipelineLayout Device::createPipelineLayout(
    std::span<const DescriptorSetLayout* const> sets, std::span<const vk::PushConstantRange> ranges
)
{
    DCHECK(device);
    PipelineLayout result;
    result.id = nextLayoutId++;
    result.ranges.assign(ranges.begin(), ranges.end());
    std::vector<vk::DescriptorSetLayout> handles;
    for (uint32_t index = 0; index < sets.size(); ++index)
    {
        DCHECK(sets[index]);
        handles.push_back(**sets[index]);
        for (const auto& binding : sets[index]->bindings)
            result.bindings.push_back({
                .set     = index,
                .binding = binding.binding,
                .type    = binding.descriptorType,
                .stages  = binding.stageFlags,
                .count   = binding.descriptorCount,
            });
    }
    result.handle = vkCheck(device->createPipelineLayout({
        .setLayoutCount         = static_cast<uint32_t>(handles.size()),
        .pSetLayouts            = handles.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(ranges.size()),
        .pPushConstantRanges    = ranges.data(),
    }));
    return result;
}

vk::raii::DescriptorSet Device::allocateDescriptorSet(const DescriptorSetLayout& layout) const
{
    DCHECK(device && *descriptorPool);
    const auto handle = *layout;
    return std::move(vkCheck(device->allocateDescriptorSets({
                                 .descriptorPool     = *descriptorPool,
                                 .descriptorSetCount = 1,
                                 .pSetLayouts        = &handle,
                             }))
                         .front());
}

std::size_t Device::PipelineKeyHash::operator()(const PipelineKey& key) const noexcept
{
    std::size_t seed    = 0;
    const auto  combine = [&seed](uint64_t value)
    { seed ^= std::hash<uint64_t>{}(value) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2); };
    combine(key.shader.index);
    combine(key.layoutId);
    combine(static_cast<uint32_t>(key.state.vertexLayout));
    combine(static_cast<uint32_t>(key.state.topology));
    combine(static_cast<uint32_t>(key.state.polygonMode));
    combine(static_cast<uint32_t>(key.state.cullMode));
    combine(static_cast<uint32_t>(key.state.frontFace));
    combine(static_cast<uint32_t>(key.state.samples));
    combine(static_cast<uint32_t>(key.state.depthCompare));
    combine(key.state.depthClamp);
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

vk::Pipeline Device::getOrCreatePipeline(const GraphicsPipelineInfo& info)
{
    DCHECK(device && shaders && info.layout);
    const auto& layout = *info.layout;
    DCHECK(layout.id && *layout);
    const PipelineKey key{
        .shader       = info.shader,
        .layoutId     = layout.id,
        .state        = info.state,
        .colorFormats = info.colorFormats,
        .depthFormat  = info.depthFormat,
        .viewMask     = info.viewMask,
    };
    if (const auto found = pipelines.find(key); found != pipelines.end())
        return *found->second;

    const ShaderMetadata& metadata = shaders->metadata(info.shader);
    for (const auto& binding : metadata.bindings)
        CHECK(
            std::ranges::any_of(
                layout.bindings,
                [&](const auto& declared)
                {
                    return declared.set == binding.set && declared.binding == binding.binding &&
                        declared.type == binding.type && declared.count >= binding.count &&
                        (declared.stages & binding.stages) == binding.stages;
                }
            ),
            "shader '{}' set {} binding {} does not match pipeline layout",
            metadata.id,
            binding.set,
            binding.binding
        );
    if (metadata.pushConstantSize)
        CHECK(
            std::ranges::any_of(
                layout.ranges,
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
        .depthClampEnable = key.state.depthClamp,
        .polygonMode      = key.state.polygonMode,
        .cullMode         = key.state.cullMode,
        .frontFace        = key.state.frontFace,
        .lineWidth        = 1.0f,
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
         .layout              = *layout},
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
