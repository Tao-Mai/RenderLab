#include "render/feature/editor/LightMarkerPass.h"

#include "render/FrameData.h"
#include "render/PipelineManager.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/feature/LightMarkers.h"
#include "render/present/Swapchain.h"
#include "render/resource/ShaderData.h"
#include "scene/system/RenderSystem.h"

#include <algorithm>
#include <array>

LightMarkerPass::Output
LightMarkerPass::setup(RenderGraph& graph, RenderGraph::ImageHandle colorHandle, RenderGraph::ImageHandle depthHandle)
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

    shader          = renderContext().shaderManager->getOrLoad("light");
    auto& pipelines = *renderContext().pipelineManager;
    pipeline        = pipelines.getOrCreate({
               .shader      = shader,
               .layout      = PipelineLayoutPreset::SceneMaterial,
               .state       = PipelineState::preset(RenderMode::LightMarker),
               .colorFormat = graph.swapchain().surfaceFormat().format,
               .depthFormat = vk::Format::eD32Sfloat,
    });
    pipelineLayout  = pipelines.layout(PipelineLayoutPreset::SceneMaterial);

    return {color, depth};
}

void LightMarkerPass::execute(RenderGraph& graph) const
{
    const auto& lightDrawItems = graph.renderData().lightDrawItems;
    if (lightDrawItems.empty())
        return;

    auto&                             commandBuffer = graph.commands();
    const auto                        extent        = graph.swapchain().extent();
    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView   = graph.getImage(color).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = graph.swapchain().surfaceFormat().format,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
        }),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eLoad,
        .storeOp     = vk::AttachmentStoreOp::eStore,
    };
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView   = graph.getImage(depth).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = vk::Format::eD32Sfloat,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1},
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

    const auto&    editorViewport = graph.editorInput().viewport;
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
    const std::array sceneSets = {graph.frameData(graph.frameIndex()).sceneSetHandle()};
    commandBuffer
        .bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, RenderInterface::sceneSet, sceneSets, {});
    graph.lightMarkers().record(commandBuffer, pipelineLayout, lightDrawItems);

    commandBuffer.endRendering();
}
