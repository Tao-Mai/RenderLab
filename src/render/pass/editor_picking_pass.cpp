#include "render/pass/editor_picking_pass.h"

#include "render/device/memory.h"
#include "render/resource/mesh.h"
#include "render/resource/shader.h"
#include "render/resource/shader_data.h"
#include "render/resource/vertex_layout.h"

#include <array>
#include <stdexcept>

void EditorPickingPass::initialize(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::Device& device,
    vk::Extent2D extent,
    vk::Format depthFormat,
    vk::DescriptorSetLayout sceneLayout)
{
    reset();

    const vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eColorAttachment |
            vk::ImageUsageFlagBits::eTransferSrc,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    image = vk::raii::Image(device, imageInfo);
    const vk::MemoryRequirements requirements = image.getMemoryRequirements();
    const vk::MemoryAllocateInfo allocationInfo{
        .allocationSize = requirements.size,
        .memoryTypeIndex = vulkan_memory::findType(
            physicalDevice,
            requirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal),
    };
    imageMemory = vk::raii::DeviceMemory(device, allocationInfo);
    image.bindMemory(*imageMemory, 0);

    const vk::ImageViewCreateInfo viewInfo{
        .image = *image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    imageView = vk::raii::ImageView(device, viewInfo);
    readbackBuffer = Buffer(
        physicalDevice,
        device,
        sizeof(uint32_t),
        vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent);

    Shader shader(device, "shaders/editor_picking.spv");
    const std::array shaderStages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = shader.handle(),
            .pName = "vertMain",
        },
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = shader.handle(),
            .pName = "fragMain",
        },
    };
    const vk::VertexInputBindingDescription binding = VertexLayout::bindingDescription();
    const auto attributes = VertexLayout::positionAttributeDescription();
    const vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };
    const vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    const vk::PipelineViewportStateCreateInfo viewportState{
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f,
    };
    const vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
    };
    const vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::False,
        .depthCompareOp = vk::CompareOp::eLessOrEqual,
        .depthBoundsTestEnable = vk::False,
    };
    const vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR,
    };
    const vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };
    constexpr std::array dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };
    const vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    const vk::PushConstantRange pushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex |
            vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(EditorPickingPushConstants),
    };
    const vk::PipelineLayoutCreateInfo layoutInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &sceneLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };
    pipelineLayout = vk::raii::PipelineLayout(device, layoutInfo);

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo>
        pipelineCreateInfoChain = {
            {
                .stageCount = static_cast<uint32_t>(shaderStages.size()),
                .pStages = shaderStages.data(),
                .pVertexInputState = &vertexInputInfo,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depthStencil,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,
                .layout = *pipelineLayout,
                .renderPass = nullptr,
            },
            {
                .colorAttachmentCount = 1,
                .pColorAttachmentFormats = &format,
                .depthAttachmentFormat = depthFormat,
            },
        };
    pipeline = vk::raii::Pipeline(
        device,
        nullptr,
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
}

void EditorPickingPass::reset() noexcept
{
    pipeline = nullptr;
    pipelineLayout = nullptr;
    readbackBuffer.reset();
    imageView = nullptr;
    image = nullptr;
    imageMemory = nullptr;
}

void EditorPickingPass::begin(
    vk::raii::CommandBuffer& commandBuffer,
    vk::Image depthImage,
    vk::ImageView depthImageView,
    vk::DescriptorSet sceneDescriptorSet,
    uint32_t x,
    uint32_t y) const
{
    const std::array barriers = {
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *image,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        },
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                vk::PipelineStageFlagBits2::eLateFragmentTests,
            .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests,
            .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead,
            .oldLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = depthImage,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        },
    };
    const vk::DependencyInfo dependency{
        .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
        .pImageMemoryBarriers = barriers.data(),
    };
    commandBuffer.pipelineBarrier2(dependency);

    const vk::ClearValue clear = vk::ClearColorValue(
        std::array<uint32_t, 4>{0u, 0u, 0u, 0u});
    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = *imageView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clear,
    };
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView = depthImageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
    };
    const vk::RenderingInfo renderingInfo{
        .renderArea = {
            .offset = {static_cast<int32_t>(x), static_cast<int32_t>(y)},
            .extent = {1, 1},
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    };
    commandBuffer.beginRendering(renderingInfo);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    const std::array sets = {sceneDescriptorSet};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, sets, {});
    commandBuffer.setScissor(
        0,
        vk::Rect2D(
            vk::Offset2D(static_cast<int32_t>(x), static_cast<int32_t>(y)),
            vk::Extent2D(1, 1)));
}

void EditorPickingPass::draw(
    vk::raii::CommandBuffer& commandBuffer,
    Mesh& mesh,
    const glm::mat4& model,
    uint32_t selectionId) const
{
    const EditorPickingPushConstants pushConstants{
        .model = model,
        .selectionId = selectionId,
    };
    commandBuffer.pushConstants<EditorPickingPushConstants>(
        *pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        pushConstants);
    mesh.bind(commandBuffer);
    for (const SubmeshData& submesh : mesh.submeshes())
    {
        commandBuffer.drawIndexed(submesh.indexCount, 1, submesh.firstIndex, 0, 0);
    }
}

void EditorPickingPass::end(
    vk::raii::CommandBuffer& commandBuffer,
    uint32_t x,
    uint32_t y) const
{
    commandBuffer.endRendering();
    const vk::ImageMemoryBarrier2 readBarrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *image,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    const vk::DependencyInfo dependency{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &readBarrier,
    };
    commandBuffer.pipelineBarrier2(dependency);

    const vk::BufferImageCopy copyRegion{
        .imageSubresource = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .layerCount = 1,
        },
        .imageOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0},
        .imageExtent = {1, 1, 1},
    };
    commandBuffer.copyImageToBuffer(
        *image,
        vk::ImageLayout::eTransferSrcOptimal,
        readbackBuffer.handle(),
        copyRegion);
}

uint32_t EditorPickingPass::readSelectionId()
{
    uint32_t selectionId = 0;
    readbackBuffer.download(&selectionId, sizeof(selectionId));
    return selectionId;
}

vk::PipelineLayout EditorPickingPass::layout() const
{
    return *pipelineLayout;
}
