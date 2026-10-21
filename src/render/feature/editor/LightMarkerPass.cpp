#include "render/feature/editor/LightMarkerPass.h"
#include "core/Context.h"
#include "render/feature/editor/EditorLayouts.h"

#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/feature/editor/EditorFrameInput.h"
#include "render/feature/editor/EditorShaderData.h"
#include "render/present/Swapchain.h"
#include "render/resource/GpuMesh.h"

#include <algorithm>
#include <array>

LightMarkerPass::Output LightMarkerPass::setup(
    RenderGraph&             graph,
    const PassSetup&         setup,
    RenderGraph::ImageHandle colorHandle,
    RenderGraph::ImageHandle depthHandle
)
{
    color = graph.write(colorHandle);
    depth = graph.write(depthHandle);

    graph.use(
        color,
        {
            .stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .access = vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
            .layout = vk::ImageLayout::eColorAttachmentOptimal,
        }
    );
    graph.use(
        depth,
        {
            .stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            .access =
                vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            .layout = vk::ImageLayout::eDepthAttachmentOptimal,
        }
    );

    shader         = renderContext().shaderManager->getOrLoad("editor_light_marker");
    auto& device   = *renderContext().device;
    pipelineLayout = &renderContext().editorLayouts->pipelineLayout;
    pipeline       = device.getOrCreatePipeline({
              .shader       = shader,
              .layout       = pipelineLayout,
              .state        = PipelineState{.vertexLayout = VertexLayoutPreset::PositionOnly},
              .colorFormats = {graph.imageInfo(color).format},
              .depthFormat  = vk::Format::eD32Sfloat,
    });

    return {color, depth};
}

void LightMarkerPass::execute(
    RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution
) const
{
    const auto lightDrawItems = execution.editor.lightDrawItems;
    if (lightDrawItems.empty())
        return;

    const auto                        extent = execution.setup.swapchain.extent();
    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = graph.getImage(color, execution.frameIndex)
                         .getOrCreateImageView({
                             .viewType = vk::ImageViewType::e2D,
                             .format   = graph.imageInfo(color).format,
                             .subresourceRange =
                                 {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
                         }),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eLoad,
        .storeOp     = vk::AttachmentStoreOp::eStore,
    };
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView = graph.getImage(depth, execution.frameIndex)
                         .getOrCreateImageView({
                             .viewType = vk::ImageViewType::e2D,
                             .format   = vk::Format::eD32Sfloat,
                             .subresourceRange =
                                 {.aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1},
                         }),
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eLoad,
        .storeOp     = vk::AttachmentStoreOp::eStore,
    };
    commandBuffer.beginRendering({
        .renderArea           = {.offset = {0, 0}, .extent = extent},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &colorAttachment,
        .pDepthAttachment     = &depthAttachment,
    });

    const auto&    editorViewport = execution.view.viewport;
    const uint32_t viewportX      = std::min(static_cast<uint32_t>(std::max(editorViewport.x, 0.0f)), extent.width - 1);
    const uint32_t viewportY = std::min(static_cast<uint32_t>(std::max(editorViewport.y, 0.0f)), extent.height - 1);
    const uint32_t viewportWidth =
        std::max(1u, std::min(static_cast<uint32_t>(editorViewport.width), extent.width - viewportX));
    const uint32_t viewportHeight =
        std::max(1u, std::min(static_cast<uint32_t>(editorViewport.height), extent.height - viewportY));
    commandBuffer.setViewport(
        0,
        vk::Viewport(
            static_cast<float>(viewportX),
            static_cast<float>(viewportY),
            static_cast<float>(viewportWidth),
            static_cast<float>(viewportHeight),
            0.0f,
            1.0f
        )
    );
    commandBuffer.setScissor(
        0,
        vk::Rect2D(
            vk::Offset2D(static_cast<int32_t>(viewportX), static_cast<int32_t>(viewportY)),
            vk::Extent2D(viewportWidth, viewportHeight)
        )
    );

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array viewSets = {execution.data.viewSetHandle()};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        **pipelineLayout,
        EditorInterface::kViewSet,
        viewSets,
        {}
    );
    for (const auto& item : lightDrawItems)
    {
        const EditorLightMarkerPushConstants push{
            .model = item.model,
            .color = glm::vec4{item.color, 1.0f},
        };
        commandBuffer.pushConstants<EditorLightMarkerPushConstants>(
            **pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            push
        );
        item.mesh->bind(commandBuffer);
        commandBuffer.drawIndexed(item.indexCount, 1, item.firstIndex, 0, 0);
        recordDraw(context().profiler, item.indexCount);
    }

    commandBuffer.endRendering();
}
