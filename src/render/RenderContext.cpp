#include "render/RenderContext.h"

#include "core/Logger.h"
#include "render/device/ResourceBarriers.h"

#include <array>

namespace Falcor
{
void RenderContext::clearTexture(Texture* texture, const float4& clearColor)
{
    DCHECK(texture);
    const auto format = texture->getFormat();
    if (format == ResourceFormat::Unknown || isIntegerFormat(format))
    {
        LOG_WARNING("clearTexture requires a normalized, floating-point, or depth texture");
        return;
    }

    constexpr auto kClearUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage |
        vk::ImageUsageFlagBits::eDepthStencilAttachment;
    if (!(texture->imageInfo().usage & kClearUsage))
    {
        LOG_WARNING("clearTexture requires a render target, unordered access, or depth-stencil texture");
        return;
    }

    const auto resource = texture->asTexture();
    DCHECK(resource);
    vulkanResources->transition(*commands, vulkanResources->registerImage(resource), vulkan_access::kTransferTarget);

    vk::ImageSubresourceRange range{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = texture->imageInfo().arrayLayers,
    };
    if (isDepthStencilFormat(format))
    {
        range.aspectMask = {};
        if (isDepthFormat(format))
            range.aspectMask |= vk::ImageAspectFlagBits::eDepth;
        if (isStencilFormat(format))
        {
            range.aspectMask |= vk::ImageAspectFlagBits::eStencil;
            if (clearColor.y != 0)
                LOG_WARNING("clearTexture clears stencil to zero");
        }

        commands->clearDepthStencilImage(
            texture->handle(), vk::ImageLayout::eTransferDstOptimal, {clearColor.x, 0}, range
        );
    }
    else
    {
        const vk::ClearColorValue value{std::array{clearColor.x, clearColor.y, clearColor.z, clearColor.w}};
        commands->clearColorImage(texture->handle(), vk::ImageLayout::eTransferDstOptimal, value, range);
    }
}

void RenderContext::resolveResource(const ref<Texture>& source, const ref<Texture>& target)
{
    const auto& sourceInfo = source->imageInfo();
    const auto& targetInfo = target->imageInfo();
    CHECK(
        source->getType() == Resource::Type::Texture2DMultisample && target->getType() == Resource::Type::Texture2D &&
            sourceInfo.format == targetInfo.format && sourceInfo.extent == targetInfo.extent &&
            sourceInfo.arrayLayers == targetInfo.arrayLayers,
        "multisample resolve requires matching 2D textures"
    );
    const auto format = source->getFormat();
    CHECK(
        format != Falcor::ResourceFormat::D16Unorm && format != Falcor::ResourceFormat::D32Float &&
            format != Falcor::ResourceFormat::D32FloatS8Uint,
        "Vulkan transfer resolve requires a color format"
    );

    auto&            resources = *vulkanResources;
    ResourceBarriers barriers;
    barriers.add(resources.registerImage(source), vulkan_access::kTransferSource);
    barriers.add(resources.registerImage(target), vulkan_access::kTransferTarget);
    barriers.declare(resources);
    barriers.transition(resources, *commands);

    const vk::ImageSubresourceLayers layers{vk::ImageAspectFlagBits::eColor, 0, 0, sourceInfo.arrayLayers};
    const vk::ImageResolve region{.srcSubresource = layers, .dstSubresource = layers, .extent = sourceInfo.extent};
    commands->resolveImage(
        source->handle(),
        vk::ImageLayout::eTransferSrcOptimal,
        target->handle(),
        vk::ImageLayout::eTransferDstOptimal,
        region
    );
}
}  // namespace Falcor
