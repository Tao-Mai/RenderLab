#include "render/feature/DepthPrepass.h"
#include "core/Context.h"

#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/present/Swapchain.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/MaterialTable.h"
#include "render/resource/RenderResourceManager.h"
#include "render/resource/ShaderData.h"

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

    // 由于双面材质需要修改pipeline的cullMode，所以即使用了全局材质数组也需要两份pipeline
    // 这里直接缓存两个pipeline，执行时调用
    auto& device   = *renderContext().device;
    pipelineLayout = device.meshPipelineLayout;
    GraphicsPipelineInfo pipelineInfo{
        .shader = renderContext().shaderManager->getOrLoad("depth"),
        .layout = pipelineLayout,
        .state =
            {
                .vertexLayout = VertexLayoutPreset::PositionUv,
                .cullMode     = vk::CullModeFlagBits::eBack,
                .depthCompare = vk::CompareOp::eLess,
                .depthWrite   = true,
                .blend        = false,
            },
        .depthFormat = vk::Format::eD32Sfloat,
    };
    singleSidedPipeline         = device.getOrCreatePipeline(pipelineInfo);
    pipelineInfo.state.cullMode = vk::CullModeFlagBits::eNone;
    doubleSidedPipeline         = device.getOrCreatePipeline(pipelineInfo);
    return {depth};
}

void DepthPrepass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const
{
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

    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        RenderInterface::kSceneSet,
        execution.data.sceneSetHandle(),
        {}
    );
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        RenderInterface::kMaterialSet,
        execution.renderData.materialSetHandle(),
        {}
    );

    vk::Pipeline boundPipeline;
    auto&        resources = *renderContext().resources;
    const auto&  materials = *renderContext().materialTable;
    for (uint32_t itemIndex = 0; itemIndex < execution.renderData.drawItems.size(); ++itemIndex)
    {
        if (!execution.renderData.isVisible(itemIndex))
            continue;
        const auto& item          = execution.renderData.drawItems[itemIndex];
        const auto  materialIndex = execution.renderData.materialIndices[itemIndex];
        const auto& material      = materials.info(materialIndex);
        if (material.alphaMode == kAlphaModeBlend)
            continue;

        const auto pipeline = material.doubleSided ? doubleSidedPipeline : singleSidedPipeline;
        if (pipeline != boundPipeline)
        {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            boundPipeline = pipeline;
        }

        auto&       mesh    = resources.mesh(item.mesh);
        const auto& submesh = mesh.submeshes()[item.submeshIndex];
        mesh.bind(commandBuffer);
        const MeshPushConstants pushConstants{.model = item.model, .materialIndex = materialIndex};
        commandBuffer.pushConstants<MeshPushConstants>(
            pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            pushConstants
        );
        commandBuffer.drawIndexed(submesh.indexCount, 1, submesh.firstIndex, 0, 0);
        recordDraw(context().profiler, submesh.indexCount);
    }

    commandBuffer.endRendering();
}
