#include "render/raster/RasterAttachments.h"

namespace
{
vk::ImageView attachmentView(VulkanResources& cache, VulkanResources::ImageHandle image, vk::ImageAspectFlagBits aspect)
{
    return cache.getImage(image).getOrCreateImageView({
        .viewType         = vk::ImageViewType::e2D,
        .format           = cache.imageInfo(image).format,
        .subresourceRange = {.aspectMask = aspect, .levelCount = 1, .layerCount = 1},
    });
}
}  // namespace

vk::RenderingAttachmentInfo RasterColorAttachment::renderingInfo(VulkanResources& cache) const
{
    return {
        .imageView   = attachmentView(cache, image, vk::ImageAspectFlagBits::eColor),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = loadOp,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f),
    };
}

vk::RenderingAttachmentInfo RasterDepthAttachment::renderingInfo(VulkanResources& cache) const
{
    return {
        .imageView   = attachmentView(cache, image, vk::ImageAspectFlagBits::eDepth),
        .imageLayout = layout(),
        .loadOp      = readOnly ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear,
        .storeOp     = readOnly ? vk::AttachmentStoreOp::eNone : vk::AttachmentStoreOp::eStore,
        .clearValue  = vk::ClearDepthStencilValue(1.0f, 0),
    };
}
