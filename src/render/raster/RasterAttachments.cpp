#include "render/raster/RasterAttachments.h"

namespace
{
vk::ImageView attachmentView(RenderGraph& graph, RenderGraph::ImageHandle image, vk::ImageAspectFlagBits aspect)
{
    return graph.getImage(image).getOrCreateImageView({
        .viewType         = vk::ImageViewType::e2D,
        .format           = graph.imageInfo(image).format,
        .subresourceRange = {.aspectMask = aspect, .levelCount = 1, .layerCount = 1},
    });
}
}  // namespace

void RasterColorAttachment::declare(RenderGraph& graph) const
{
    auto access = vk::AccessFlags2{vk::AccessFlagBits2::eColorAttachmentWrite};
    if (loadOp == vk::AttachmentLoadOp::eLoad)
        access |= vk::AccessFlagBits2::eColorAttachmentRead;
    graph.use(
        image,
        {vk::PipelineStageFlagBits2::eColorAttachmentOutput, access, vk::ImageLayout::eColorAttachmentOptimal}
    );
}

vk::RenderingAttachmentInfo RasterColorAttachment::renderingInfo(RenderGraph& graph) const
{
    return {
        .imageView   = attachmentView(graph, image, vk::ImageAspectFlagBits::eColor),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = loadOp,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f),
    };
}

void RasterDepthAttachment::declare(RenderGraph& graph) const
{
    auto access = vk::AccessFlags2{vk::AccessFlagBits2::eDepthStencilAttachmentRead};
    if (!readOnly)
        access |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    graph.use(
        image,
        {vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
         access,
         layout()}
    );
}

vk::RenderingAttachmentInfo RasterDepthAttachment::renderingInfo(RenderGraph& graph) const
{
    return {
        .imageView   = attachmentView(graph, image, vk::ImageAspectFlagBits::eDepth),
        .imageLayout = layout(),
        .loadOp      = readOnly ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear,
        .storeOp     = readOnly ? vk::AttachmentStoreOp::eNone : vk::AttachmentStoreOp::eStore,
        .clearValue  = vk::ClearDepthStencilValue(1.0f, 0),
    };
}
