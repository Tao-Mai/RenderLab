#include "render/device/VulkanResources.h"

#include "core/Logger.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"

#include <algorithm>
#include <utility>

namespace
{
constexpr vk::DeviceSize kStagingBlockSize = 1024 * 1024;
constexpr vk::DeviceSize kUploadAlignment  = 16;
constexpr auto           kWriteAccess = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderStorageWrite |
    vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
    vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eHostWrite | vk::AccessFlagBits2::eMemoryWrite;

vk::ImageAspectFlags imageAspect(vk::Format format)
{
    return format == vk::Format::eD32Sfloat ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
}
vk::ImageUsageFlags imageUsageFor(vk::AccessFlags2 access)
{
    vk::ImageUsageFlags usage;
    if (access & (vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite))
        usage |= vk::ImageUsageFlagBits::eColorAttachment;
    if (access & (vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite))
        usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    if (access & vk::AccessFlagBits2::eShaderSampledRead)
        usage |= vk::ImageUsageFlagBits::eSampled;
    if (access & (vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite))
        usage |= vk::ImageUsageFlagBits::eStorage;
    if (access & vk::AccessFlagBits2::eTransferRead)
        usage |= vk::ImageUsageFlagBits::eTransferSrc;
    if (access & vk::AccessFlagBits2::eTransferWrite)
        usage |= vk::ImageUsageFlagBits::eTransferDst;
    return usage;
}

vk::BufferUsageFlags bufferUsageFor(vk::AccessFlags2 access)
{
    vk::BufferUsageFlags usage;
    if (access & vk::AccessFlagBits2::eTransferRead)
        usage |= vk::BufferUsageFlagBits::eTransferSrc;
    if (access & vk::AccessFlagBits2::eTransferWrite)
        usage |= vk::BufferUsageFlagBits::eTransferDst;
    if (access & (vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite))
        usage |= vk::BufferUsageFlagBits::eStorageBuffer;
    if (access & vk::AccessFlagBits2::eUniformRead)
        usage |= vk::BufferUsageFlagBits::eUniformBuffer;
    return usage;
}
}  // namespace

struct VulkanResources::ResourceData
{
    bool                     isImage  = false;
    bool                     acquired = false;
    vk::PipelineStageFlags2  firstUseStages;
    Falcor::ref<Falcor::Resource>            pResource;
    ImageAccess              state;
    MemoryAccess             lastWrite;
    bool                     readScopeChanged = false;
    std::vector<ImageAccess> readScopes;
    uint64_t                 revision        = 1;
    uint64_t                 historyRevision = 0;
};

VulkanResources::VulkanResources(VulkanContext& vulkan) : deviceContext(&vulkan) {}
VulkanResources::~VulkanResources() = default;

uint32_t VulkanResources::registerResource(Falcor::ref<Falcor::Resource> resource, bool isImage)
{
    uint32_t slot;
    ResourceData data{.isImage = isImage, .pResource = std::move(resource)};
    if (freeSlots.empty())
    {
        slot = static_cast<uint32_t>(resources.size());
        resources.push_back(std::move(data));
    }
    else
    {
        slot = freeSlots.back();
        freeSlots.pop_back();
        resources[slot] = std::move(data);
    }

    resourceIndices.emplace(resources[slot].pResource.get(), slot);
    return slot;
}

VulkanResources::ImageHandle VulkanResources::registerImage(const Falcor::ref<Falcor::Texture>& texture)
{
    if (const auto found = resourceIndices.find(texture.get()); found != resourceIndices.end())
        return {found->second};
    return {registerResource(texture, true)};
}

VulkanResources::BufferHandle VulkanResources::registerBuffer(const Falcor::ref<Falcor::Buffer>& buffer)
{
    if (const auto found = resourceIndices.find(buffer.get()); found != resourceIndices.end())
        return {found->second};
    return {registerResource(buffer, false)};
}

void VulkanResources::releaseUnusedResources()
{
    for (uint32_t slot = 0; slot < resources.size(); ++slot)
    {
        auto& resource = resources[slot];
        if (resource.pResource && resource.pResource->refCount() == 1)
        {
            resourceIndices.erase(resource.pResource.get());
            resource = {};
            freeSlots.push_back(slot);
        }
    }
}

void VulkanResources::declareAccess(const Access& use)
{
    auto&      resource = resources[use.slot];
    const auto access   = use.access.access;
    if (resource.isImage)
    {
        DCHECK(use.access.layout != vk::ImageLayout::eUndefined);
        CHECK(!(imageUsageFor(access) & ~imageInfo(ImageHandle{use.slot}).usage));
    }
    else
        CHECK(!(bufferUsageFor(access) & ~getBuffer(BufferHandle{use.slot}).usage()));

    if (access && !(access & kWriteAccess))
    {
        const auto scope = std::ranges::find(resource.readScopes, use.access.layout, &ImageAccess::layout);
        if (resource.lastWrite.access && resource.state.layout == use.access.layout && resource.state.access &&
            !(resource.state.access & kWriteAccess) &&
            (scope == resource.readScopes.end() || (use.access.stages & ~scope->stages) || (access & ~scope->access)))
            resource.readScopeChanged = true;

        if (scope == resource.readScopes.end())
            resource.readScopes.push_back(use.access);
        else
        {
            scope->stages |= use.access.stages;
            scope->access |= access;
        }
    }
}

void VulkanResources::writeDescriptor(
    vk::DescriptorSet set, uint32_t binding, ImageHandle image, vk::DescriptorType type, vk::ImageViewType view
)
{
    auto& texture = getImage(image);
    const auto& info = texture.imageInfo();
    const vk::DescriptorImageInfo descriptor{
        .imageView = texture.getOrCreateImageView({
            .viewType = view,
            .format = info.format,
            .subresourceRange = {
                .aspectMask = imageAspect(info.format),
                .levelCount = 1,
                .layerCount = info.arrayLayers,
            },
        }),
        .imageLayout = type == vk::DescriptorType::eStorageImage
            ? vk::ImageLayout::eGeneral
            : vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::WriteDescriptorSet write{
        .dstSet = set,
        .dstBinding = binding,
        .descriptorCount = 1,
        .descriptorType = type,
        .pImageInfo = &descriptor,
    };
    deviceContext->deviceHandle().updateDescriptorSets(write, {});
}

void VulkanResources::writeDescriptor(vk::DescriptorSet set, uint32_t binding, BufferHandle buffer)
{
    const auto& allocation = getBuffer(buffer);
    const vk::DescriptorBufferInfo descriptor{.buffer = allocation.handle(), .range = allocation.getSize()};
    const vk::WriteDescriptorSet write{
        .dstSet = set,
        .dstBinding = binding,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .pBufferInfo = &descriptor,
    };
    deviceContext->deviceHandle().updateDescriptorSets(write, {});
}

void VulkanResources::writeDescriptor(vk::DescriptorSet set, uint32_t binding, vk::Sampler sampler)
{
    const vk::DescriptorImageInfo descriptor{.sampler = sampler};
    const vk::WriteDescriptorSet write{
        .dstSet = set,
        .dstBinding = binding,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eSampler,
        .pImageInfo = &descriptor,
    };
    deviceContext->deviceHandle().updateDescriptorSets(write, {});
}

void VulkanResources::setAcquiredImage(ImageHandle image)
{
    auto& resource = resources[image.id];
    DCHECK(resource.isImage && resource.pResource);
    resource.acquired         = true;
    resource.state            = {};
    resource.firstUseStages   = {};
    resource.lastWrite        = {};
    resource.readScopeChanged = false;
}

const vk::ImageCreateInfo& VulkanResources::imageInfo(ImageHandle image) const
{
    DCHECK(resources[image.id].isImage && resources[image.id].pResource);
    return static_cast<const Falcor::Texture&>(*resources[image.id].pResource).imageInfo();
}

vk::Extent2D VulkanResources::imageExtent(ImageHandle image) const
{
    const auto& extent = imageInfo(image).extent;
    return {extent.width, extent.height};
}

vk::PipelineStageFlags2 VulkanResources::firstUseStages(ImageHandle image) const
{
    DCHECK(resources[image.id].acquired);
    return resources[image.id].firstUseStages;
}

Falcor::Texture& VulkanResources::getImage(ImageHandle image)
{
    DCHECK(resources[image.id].isImage && resources[image.id].pResource);
    return static_cast<Falcor::Texture&>(*resources[image.id].pResource);
}

Falcor::Buffer& VulkanResources::getBuffer(BufferHandle buffer)
{
    DCHECK(!resources[buffer.id].isImage && resources[buffer.id].pResource);
    return static_cast<Falcor::Buffer&>(*resources[buffer.id].pResource);
}

uint64_t VulkanResources::bufferRevision(BufferHandle buffer) const { return resources[buffer.id].revision; }

void VulkanResources::invalidateImageHistory(ImageHandle image) { ++resources[image.id].historyRevision; }

uint64_t VulkanResources::imageHistoryRevision(ImageHandle image) const { return resources[image.id].historyRevision; }

void VulkanResources::beginFrame(uint32_t frameIndex)
{
    currentFrameIndex = frameIndex;
    auto& pool        = stagingPools[frameIndex];
    pool.blockIndex   = 0;
    pool.copies.clear();
    for (auto& block : pool.blocks)
        block.used = 0;
}

VulkanResources::BufferUpload
VulkanResources::allocateUpload(BufferHandle buffer, vk::DeviceSize size, vk::DeviceSize destinationOffset)
{
    DCHECK(getBuffer(buffer).usage() & vk::BufferUsageFlagBits::eTransferDst);
    DCHECK(size && size % 4 == 0 && destinationOffset % 4 == 0);

    ensureBufferCapacity(buffer, destinationOffset + size);

    auto& pool = stagingPools[currentFrameIndex];
    while (pool.blockIndex < pool.blocks.size())
    {
        const auto& block  = pool.blocks[pool.blockIndex];
        const auto  offset = (block.used + kUploadAlignment - 1) & ~(kUploadAlignment - 1);
        if (offset + size <= block.buffer.getSize())
            break;
        ++pool.blockIndex;
    }
    if (pool.blockIndex == pool.blocks.size())
    {
        auto& block  = pool.blocks.emplace_back();
        block.buffer = Falcor::Buffer(
            *deviceContext,
            std::max(kStagingBlockSize, size),
            vk::BufferUsageFlagBits::eTransferSrc,
            Falcor::MemoryType::Upload,
            Falcor::ResourceBindFlags::None
        );
        block.mapped = static_cast<std::byte*>(block.buffer.map());
    }
    auto&      block  = pool.blocks[pool.blockIndex];
    const auto offset = (block.used + kUploadAlignment - 1) & ~(kUploadAlignment - 1);
    block.used        = offset + size;
    pool.copies.push_back(
        {buffer, pool.blockIndex, {.srcOffset = offset, .dstOffset = destinationOffset, .size = size}}
    );
    return {offset, {block.mapped + offset, static_cast<size_t>(size)}};
}

void VulkanResources::ensureBufferCapacity(BufferHandle buffer, vk::DeviceSize requiredSize)
{
    auto& resource = resources[buffer.id];
    DCHECK(!resource.isImage && requiredSize);

    const auto limit = deviceContext->properties().limits.maxStorageBufferRange;
    CHECK(requiredSize <= limit, "buffer requires {} bytes; maxStorageBufferRange is {}", requiredSize, limit);
    auto& allocation = getBuffer(buffer);
    if (requiredSize <= allocation.getSize())
        return;

    // Existing submissions may still reference the old allocation.
    vkCheck(deviceContext->deviceHandle().waitIdle());
    const auto size = std::min<vk::DeviceSize>(std::max<vk::DeviceSize>(requiredSize, allocation.getSize() * 2), limit);
    Falcor::Buffer replacement(
        *deviceContext,
        size,
        allocation.usage(),
        allocation.getMemoryType(),
        allocation.getBindFlags()
    );
    if (!allocation.getName().empty())
        replacement.setName(allocation.getName());
    allocation = std::move(replacement);
    resource.state            = {};
    resource.lastWrite        = {};
    resource.readScopeChanged = false;
    ++resource.revision;
}

void VulkanResources::recordUploads(vk::raii::CommandBuffer& commandBuffer)
{
    auto& pool = stagingPools[currentFrameIndex];
    for (const auto& copy : pool.copies)
    {
        transition(
            commandBuffer,
            {copy.destination.id,
             {
                 .stages = vk::PipelineStageFlagBits2::eCopy,
                 .access = vk::AccessFlagBits2::eTransferWrite,
             }}
        );
        commandBuffer.copyBuffer(
            pool.blocks[copy.blockIndex].buffer.handle(),
            getBuffer(copy.destination).handle(),
            copy.region
        );
    }
    pool.copies.clear();
}

void VulkanResources::transition(vk::raii::CommandBuffer& commandBuffer, const Access& use)
{
    auto& resource = resources[use.slot];
    if (resource.acquired && !resource.firstUseStages)
    {
        resource.firstUseStages = use.access.stages;
        resource.state.stages   = use.access.stages;
    }

    // Shared resources retain the previous submission's access, including all trailing readers.
    // Their first use in this frame therefore also synchronizes across frame boundaries.
    auto       previous = resource.state;
    const auto next     = use.access;
    if (previous.layout == next.layout && !((previous.access | next.access) & kWriteAccess) &&
        !resource.readScopeChanged)
    {
        resource.state.stages |= next.stages;
        resource.state.access |= next.access;
        return;
    }

    if (resource.readScopeChanged)
    {
        previous.stages |= resource.lastWrite.stages;
        previous.access |= resource.lastWrite.access;
        resource.readScopeChanged = false;
    }
    if (next.access & kWriteAccess)
        resource.lastWrite = {next.stages, next.access};

    // Publish the write to every reader of this layout, including later passes in other shader stages.
    auto destination = next;
    if (next.access && !(next.access & kWriteAccess))
    {
        const auto scope = std::ranges::find(resource.readScopes, next.layout, &ImageAccess::layout);
        DCHECK(scope != resource.readScopes.end());
        destination = *scope;
    }

    if (!resource.isImage)
    {
        const auto& buffer = getBuffer(BufferHandle{use.slot});
        const vk::BufferMemoryBarrier2 barrier{
            .srcStageMask        = previous.stages,
            .srcAccessMask       = previous.access,
            .dstStageMask        = destination.stages,
            .dstAccessMask       = destination.access,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = buffer.handle(),
            .size                = buffer.getSize(),
        };
        commandBuffer.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier});
    }
    else
    {
        const auto& texture = getImage(ImageHandle{use.slot});
        const auto& info = texture.imageInfo();
        const vk::ImageMemoryBarrier2 barrier{
            .srcStageMask        = previous.stages,
            .srcAccessMask       = previous.access,
            .dstStageMask        = destination.stages,
            .dstAccessMask       = destination.access,
            .oldLayout           = previous.layout,
            .newLayout           = next.layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = texture.handle(),
            .subresourceRange    = {
                   .aspectMask = imageAspect(info.format),
                   .levelCount = info.mipLevels,
                   .layerCount = info.arrayLayers
            },
        };
        commandBuffer.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
    }
    resource.state = next;
}

void VulkanResources::transition(vk::raii::CommandBuffer& commands, ImageHandle image, ImageAccess access)
{
    const Access use{image.id, access};
    declareAccess(use);
    transition(commands, use);
}

void VulkanResources::transition(vk::raii::CommandBuffer& commands, BufferHandle buffer, MemoryAccess access)
{
    const Access use{buffer.id, {.stages = access.stages, .access = access.access}};
    declareAccess(use);
    transition(commands, use);
}
