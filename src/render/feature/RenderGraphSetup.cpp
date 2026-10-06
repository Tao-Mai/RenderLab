#include "render/feature/RenderGraph.h"

#include "core/Logger.h"

#include <algorithm>

namespace
{
constexpr auto kWriteAccess = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderStorageWrite |
    vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
    vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eHostWrite | vk::AccessFlagBits2::eMemoryWrite;
}

RenderGraph::ImageHandle RenderGraph::createImage(ImageCreateInfo info)
{
    DCHECK(info.format != vk::Format::eUndefined && info.extent.width && info.extent.height);
    const auto id = static_cast<ResourceHandle>(resources.size());
    resources.push_back({.imageInfo = info});
    return {id};
}

RenderGraph::BufferHandle RenderGraph::createBuffer(BufferCreateInfo info)
{
    DCHECK(info.size);
    const auto id = static_cast<ResourceHandle>(resources.size());
    resources.push_back({.isBuffer = true, .bufferInfo = info});
    return {id};
}

void RenderGraph::use(ImageHandle image, ImageAccess access)
{
    DCHECK(setupUses && image.id >= 0 && image.id < resources.size());
    Resource& resource = resources.at(image.id);
    DCHECK(!resource.isBuffer && access.layout != vk::ImageLayout::eUndefined);

    // Creation usage is the union of the actual uses declared by passes.
    if (access.access & (vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite))
        resource.imageUsage |= vk::ImageUsageFlagBits::eColorAttachment;
    if (access.access &
        (vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite))
        resource.imageUsage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    if (access.access & vk::AccessFlagBits2::eShaderSampledRead)
        resource.imageUsage |= vk::ImageUsageFlagBits::eSampled;
    if (access.access & (vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite))
        resource.imageUsage |= vk::ImageUsageFlagBits::eStorage;
    if (access.access & vk::AccessFlagBits2::eTransferRead)
        resource.imageUsage |= vk::ImageUsageFlagBits::eTransferSrc;
    if (access.access & vk::AccessFlagBits2::eTransferWrite)
        resource.imageUsage |= vk::ImageUsageFlagBits::eTransferDst;

    auto&      uses  = *setupUses;
    const auto found = std::ranges::find(uses, image.id, &ResourceUse::resource);
    if (found != uses.end())
    {
        DCHECK(found->access.layout == access.layout);
        found->access.stages |= access.stages;
        found->access.access |= access.access;
    }
    else
        uses.push_back({image.id, access});
}

void RenderGraph::use(BufferHandle buffer, BufferAccess access)
{
    DCHECK(setupUses && buffer.id >= 0 && buffer.id < resources.size());
    Resource& resource = resources.at(buffer.id);
    DCHECK(resource.isBuffer);

    if (access.access & vk::AccessFlagBits2::eTransferRead)
        resource.bufferUsage |= vk::BufferUsageFlagBits::eTransferSrc;
    if (access.access & vk::AccessFlagBits2::eTransferWrite)
        resource.bufferUsage |= vk::BufferUsageFlagBits::eTransferDst;
    if (access.access & (vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite))
        resource.bufferUsage |= vk::BufferUsageFlagBits::eStorageBuffer;
    if (access.access & vk::AccessFlagBits2::eUniformRead)
        resource.bufferUsage |= vk::BufferUsageFlagBits::eUniformBuffer;

    auto&      uses  = *setupUses;
    const auto found = std::ranges::find(uses, buffer.id, &ResourceUse::resource);
    if (found != uses.end())
    {
        found->access.stages |= access.stages;
        found->access.access |= access.access;
    }
    else
        uses.push_back({buffer.id, {.stages = access.stages, .access = access.access}});
}

bool RenderGraph::needsBarrier(const ImageAccess& previous, const ImageAccess& next)
{
    return previous.layout != next.layout || static_cast<bool>((previous.access | next.access) & kWriteAccess);
}
