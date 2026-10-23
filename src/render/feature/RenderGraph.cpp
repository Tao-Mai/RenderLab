#include "render/feature/RenderGraph.h"

#include "core/Logger.h"
#include "render/RenderConfig.h"
#include "render/device/GpuProfiler.h"
#include "render/device/Memory.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/resource/Buffer.h"

#include <algorithm>

namespace
{
constexpr vk::DeviceSize kStagingBlockSize = 1024 * 1024;
constexpr vk::DeviceSize kUploadAlignment  = 16;
vk::ImageAspectFlags     imageAspect(vk::Format format)
{
    return format == vk::Format::eD32Sfloat ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
}
}  // namespace

struct RenderGraph::Allocation
{
    vk::raii::DeviceMemory  memory     = nullptr;
    vk::raii::Image         ownedImage = nullptr;
    std::unique_ptr<Buffer> buffer;
    std::unique_ptr<Image>  views;
    Image*                  image = nullptr;
    ImageAccess             state;
    uint64_t                revision = 1;
};

void RenderGraph::init(VulkanContext& vulkan)
{
    DCHECK(!deviceContext);
    deviceContext = &vulkan;
}

void RenderGraph::releaseResources() noexcept
{
    for (auto& pool : stagingPools)
        pool.copies.clear();
    resources.clear();
    nodes.clear();
    sortedNodes.clear();
    nodeStorage.clear();

    setupNode = nullptr;
}

void RenderGraph::reset() noexcept
{
    releaseResources();
    stagingPools  = {};
    deviceContext = nullptr;
}

void RenderGraph::compile(const PassSetup& setup)
{
    compileNodes();
    allocateResources();
    for (auto* node : sortedNodes)
        if (node->writeDescriptorSet)
            node->writeDescriptorSet(*this, setup);
}

void RenderGraph::beginFrame(uint32_t frameIndex)
{
    currentFrameIndex = frameIndex;
    auto& pool        = stagingPools[frameIndex];
    pool.blockIndex   = 0;
    pool.copies.clear();
    for (auto& block : pool.blocks)
        block.used = 0;
}

RenderGraph::BufferUpload
RenderGraph::allocateUpload(BufferHandle buffer, vk::DeviceSize size, vk::DeviceSize destinationOffset)
{
    auto& resource = resolveResource(buffer.id);
    DCHECK(resource.type == ResourceType::Buffer);
    DCHECK(resource.bufferUsage & vk::BufferUsageFlagBits::eTransferDst);
    DCHECK(size && size % 4 == 0 && destinationOffset % 4 == 0);

    const auto requiredSize = destinationOffset + size;
    const auto limit        = deviceContext->properties().limits.maxStorageBufferRange;
    CHECK(requiredSize <= limit, "buffer upload requires {} bytes; maxStorageBufferRange is {}", requiredSize, limit);
    if (requiredSize > resource.bufferInfo.size)
    {
        // A shared buffer can still be referenced by any frame in flight.
        vkCheck(deviceContext->deviceHandle().waitIdle());
        resource.bufferInfo.size =
            std::min<vk::DeviceSize>(std::max(requiredSize, resource.bufferInfo.size * 2), limit);
        auto& allocation  = *resource.allocation;
        allocation.buffer = std::make_unique<Buffer>(
            deviceContext->physicalDeviceHandle(),
            deviceContext->deviceHandle(),
            resource.bufferInfo.size,
            resource.bufferUsage,
            resource.bufferInfo.memoryProperties
        );
        allocation.state = {};
        ++allocation.revision;
    }

    auto& pool = stagingPools[currentFrameIndex];
    while (pool.blockIndex < pool.blocks.size())
    {
        const auto& block  = pool.blocks[pool.blockIndex];
        const auto  offset = (block.used + kUploadAlignment - 1) & ~(kUploadAlignment - 1);
        if (offset + size <= block.buffer.size())
            break;
        ++pool.blockIndex;
    }
    if (pool.blockIndex == pool.blocks.size())
    {
        auto& block  = pool.blocks.emplace_back();
        block.buffer = Buffer(
            deviceContext->physicalDeviceHandle(),
            deviceContext->deviceHandle(),
            std::max(kStagingBlockSize, size),
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
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

void RenderGraph::recordUploads(vk::raii::CommandBuffer& commandBuffer)
{
    const auto& pool = stagingPools[currentFrameIndex];
    for (const auto& copy : pool.copies)
    {
        useResource(
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
}

void RenderGraph::allocateResources()
{
    const auto& device         = deviceContext->deviceHandle();
    const auto& physicalDevice = deviceContext->physicalDeviceHandle();
    for (Resource& entry : resources)
    {
        if (!entry.data)
            continue;

        auto& resource      = *entry.data;
        resource.allocation = std::make_shared<Allocation>();
        auto& allocation    = *resource.allocation;
        allocation.state    = resource.initialState;
        if (resource.externalImage || resource.type == ResourceType::AccelerationStructure)
            continue;

        if (resource.type == ResourceType::Buffer)
        {
            DCHECK(resource.bufferUsage);
            allocation.buffer = std::make_unique<Buffer>(
                physicalDevice,
                device,
                resource.bufferInfo.size,
                resource.bufferUsage,
                resource.bufferInfo.memoryProperties
            );
            continue;
        }

        const auto& info = resource.imageInfo;
        DCHECK(resource.imageUsage);
        allocation.ownedImage   = vkCheck(device.createImage({
              .flags         = info.flags,
              .imageType     = info.imageType,
              .format        = info.format,
              .extent        = info.extent,
              .mipLevels     = info.mipLevels,
              .arrayLayers   = info.arrayLayers,
              .samples       = vk::SampleCountFlagBits::e1,
              .tiling        = vk::ImageTiling::eOptimal,
              .usage         = resource.imageUsage,
              .sharingMode   = vk::SharingMode::eExclusive,
              .initialLayout = vk::ImageLayout::eUndefined,
        }));
        const auto requirements = allocation.ownedImage.getMemoryRequirements();
        allocation.memory       = vkCheck(device.allocateMemory({
                  .allocationSize  = requirements.size,
                  .memoryTypeIndex = vulkan_memory::findType(
                physicalDevice,
                requirements.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            ),
        }));
        vkCheck(allocation.ownedImage.bindMemory(*allocation.memory, 0));
        allocation.views = std::make_unique<Image>(device, *allocation.ownedImage);
        allocation.image = allocation.views.get();
    }
}

void RenderGraph::bindImage(ImageHandle handle, Image& image)
{
    auto& resource = resolveResource(handle.id);
    DCHECK(resource.type == ResourceType::Image && resource.externalImage && resource.allocation);
    resource.allocation->image = &image;
}

const RenderGraph::ImageCreateInfo& RenderGraph::imageInfo(ImageHandle image)
{
    const auto& resource = resolveResource(image.id);
    DCHECK(resource.type == ResourceType::Image);
    return resource.imageInfo;
}

vk::Extent2D RenderGraph::imageExtent(ImageHandle image)
{
    const auto& extent = imageInfo(image).extent;
    return {extent.width, extent.height};
}

vk::PipelineStageFlags2 RenderGraph::firstUseStages(ImageHandle image) const
{
    const auto& resource = resolveResource(image.id);
    DCHECK(resource.externalImage);
    return resource.firstUseStages;
}

Image& RenderGraph::getImage(ImageHandle image)
{
    const auto& resource = resolveResource(image.id);
    DCHECK(resource.type == ResourceType::Image && resource.allocation && resource.allocation->image);
    return *resource.allocation->image;
}

Buffer& RenderGraph::getBuffer(BufferHandle buffer)
{
    const auto& resource = resolveResource(buffer.id);
    DCHECK(resource.type == ResourceType::Buffer && resource.allocation);
    return *resource.allocation->buffer;
}

void RenderGraph::bindAccelerationStructures(
    AccelerationStructureHandle handle, std::span<AccelerationStructure* const> structures
)
{
    auto& resource = resolveResource(handle.id);
    DCHECK(resource.type == ResourceType::AccelerationStructure);
    resource.accelerationStructures.assign(structures.begin(), structures.end());
}

AccelerationStructure& RenderGraph::getAccelerationStructure(AccelerationStructureHandle handle, size_t index)
{
    auto& resource = resolveResource(handle.id);
    DCHECK(resource.type == ResourceType::AccelerationStructure);
    DCHECK(index < resource.accelerationStructures.size());
    DCHECK(resource.accelerationStructures[index]);
    return *resource.accelerationStructures[index];
}

void RenderGraph::useResource(vk::raii::CommandBuffer& commandBuffer, const ResourceUse& use)
{
    auto& resource   = resolveResource(use.resource);
    auto& allocation = *resource.allocation;
    // Shared resources retain the previous submission's access, including all trailing readers.
    // Their first use in this frame therefore also synchronizes across frame boundaries.
    const auto previous = allocation.state;
    const auto next     = use.access;
    if (!needsBarrier(previous, next))
    {
        allocation.state.stages |= next.stages;
        allocation.state.access |= next.access;
        return;
    }

    if (resource.type == ResourceType::AccelerationStructure)
    {
        const vk::MemoryBarrier2 barrier{
            .srcStageMask  = previous.stages,
            .srcAccessMask = previous.access,
            .dstStageMask  = next.stages,
            .dstAccessMask = next.access,
        };
        commandBuffer.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});
    }
    else if (resource.type == ResourceType::Buffer)
    {
        const vk::BufferMemoryBarrier2 barrier{
            .srcStageMask        = previous.stages,
            .srcAccessMask       = previous.access,
            .dstStageMask        = next.stages,
            .dstAccessMask       = next.access,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = allocation.buffer->handle(),
            .size                = resource.bufferInfo.size,
        };
        commandBuffer.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier});
    }
    else
    {
        const vk::ImageMemoryBarrier2 barrier{
            .srcStageMask        = previous.stages,
            .srcAccessMask       = previous.access,
            .dstStageMask        = next.stages,
            .dstAccessMask       = next.access,
            .oldLayout           = previous.layout,
            .newLayout           = next.layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = allocation.image->handle(),
            .subresourceRange    = {
                   .aspectMask = imageAspect(resource.imageInfo.format),
                   .levelCount = resource.imageInfo.mipLevels,
                   .layerCount = resource.imageInfo.arrayLayers
            },
        };
        commandBuffer.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
    }
    allocation.state = next;
}

void RenderGraph::transitionResources(vk::raii::CommandBuffer& commandBuffer, const std::vector<ResourceUse>& uses)
{
    for (const ResourceUse& use : uses)
        useResource(commandBuffer, use);
}

void RenderGraph::prepare(const PassFrame& frame)
{
    for (auto* node : sortedNodes)
        if (node->prepare)
            node->prepare(*this, frame);
}

void RenderGraph::execute(
    vk::raii::CommandBuffer& commandBuffer, const PassFrame& frame, GpuProfiler& gpuProfiler, Profiler* profiler
)
{
    DCHECK(!sortedNodes.empty());

    // External targets discard old contents; their first barrier joins the acquire wait stage.
    for (auto& entry : resources)
        if (entry.data && entry.data->externalImage)
            entry.data->allocation->state = {.stages = entry.data->firstUseStages};

    std::vector<std::string_view> names;
    if (profiler)
    {
        for (const auto* node : sortedNodes)
            names.push_back(node->name);
    }
    gpuProfiler.beginFrame(commandBuffer, names, profiler);
    recordUploads(commandBuffer);
    auto execution        = frame;
    execution.gpuProfiler = profiler ? &gpuProfiler : nullptr;
    uint32_t passIndex    = 0;
    for (auto* node : sortedNodes)
    {
        CpuProfileScope timing{profiler, node->name};
        if (profiler)
            profiler->beginPass(node->name);
        gpuProfiler.beginPass(commandBuffer, passIndex);
        transitionResources(commandBuffer, node->uses);
        node->execute(*this, commandBuffer, execution);
        gpuProfiler.endPass(commandBuffer, passIndex);
        ++passIndex;
    }
    gpuProfiler.endFrame(commandBuffer);
}

uint64_t RenderGraph::bufferRevision(BufferHandle buffer) const
{
    const auto& resource = resolveResource(buffer.id);
    return resource.allocation->revision;
}
