#include "render/feature/DepthPrepass.h"
#include "core/Context.h"

#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/present/Swapchain.h"
#include "render/resource/GpuMaterial.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/ShaderData.h"

#include <array>

DepthPrepass::Output DepthPrepass::setup(RenderGraph& graph, const PassSetup& setup)
{
    CHECK(
        setup.vulkan.physicalDeviceHandle().getFormatProperties(vk::Format::eD32Sfloat).optimalTilingFeatures &
        vk::FormatFeatureFlagBits::eDepthStencilAttachment
    );

    const auto extent = setup.swapchain.extent();
    depth = graph.createImage({.format = vk::Format::eD32Sfloat, .extent = {extent.width, extent.height, 1}});
    depth = graph.write(depth);
    graph.use(
        depth,
        {
            .stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            .access =
                vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            .layout = vk::ImageLayout::eDepthAttachmentOptimal,
        }
    );

    shader         = renderContext().shaderManager->getOrLoad("depth");
    pipelineLayout = &renderContext().device->meshPipelineLayout;
    return {depth};
}

void DepthPrepass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const
{
    auto&      device = *renderContext().device;
    const auto extent = execution.setup.swapchain.extent();

    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView   = graph.getImage(depth).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = vk::Format::eD32Sfloat,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1},
        }),
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eClear,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = vk::ClearDepthStencilValue(1.0f, 0),
    };
    commandBuffer.beginRendering({
        .renderArea       = {.offset = {0, 0}, .extent = extent},
        .layerCount       = 1,
        .pDepthAttachment = &depthAttachment,
    });

    const auto viewport = execution.view.viewport.clamped(extent);
    commandBuffer.setViewport(0, viewport.toViewport());
    commandBuffer.setScissor(0, viewport.toScissor());

    const std::array sceneSets = {execution.data.sceneSetHandle(), execution.renderData.materialSet};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        **pipelineLayout,
        RenderInterface::kSceneSet,
        sceneSets,
        {}
    );

    vk::Pipeline boundPipeline;
    for (const DrawItem& item : execution.renderData.opaqueDrawItems)
    {
        GraphicsPipelineInfo key = item.material->pipelineInfo({}, vk::Format::eD32Sfloat, *pipelineLayout);
        key.shader               = shader;
        key.state.vertexLayout   = VertexLayoutPreset::PositionUv;
        key.state.depthCompare   = vk::CompareOp::eLess;
        key.state.depthWrite     = true;
        key.state.blend          = false;
        const auto pipeline      = device.getOrCreatePipeline(key);
        if (pipeline != boundPipeline)
        {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            boundPipeline = pipeline;
        }

        item.mesh->bind(commandBuffer);
        const MeshPushConstants pushConstants{.model = item.model, .materialIndex = item.materialIndex};
        commandBuffer.pushConstants<MeshPushConstants>(
            **pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            pushConstants
        );
        commandBuffer.drawIndexed(item.indexCount, 1, item.firstIndex, 0, 0);
        recordDraw(context().profiler, item.indexCount);
    }

    commandBuffer.endRendering();
}
