#include "render/feature/RenderGraph.h"

#include "core/Logger.h"
#include "render/RenderConfig.h"
#include "render/device/GpuProfiler.h"
#include "render/device/Memory.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/present/Swapchain.h"
#include "render/resource/Buffer.h"

#include <algorithm>

namespace
{
vk::ImageAspectFlags imageAspect(vk::Format format)
{
    return format == vk::Format::eD32Sfloat ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
}
}  // namespace

struct RenderGraph::Allocation
{
    struct Instance
    {
        vk::raii::DeviceMemory  memory     = nullptr;
        vk::raii::Image         ownedImage = nullptr;
        std::unique_ptr<Buffer> buffer;
        std::unique_ptr<Image>  image;
        ImageAccess             state;
    };
    std::vector<Instance> instances;
};

void RenderGraph::init(VulkanContext& vulkan, Swapchain& swapchain)
{
    DCHECK(!deviceContext);
    deviceContext   = &vulkan;
    targetSwapchain = &swapchain;
}

void RenderGraph::releaseResources() noexcept
{
    resources.clear();
    nodes.clear();
    sortedNodes.clear();
    nodeStorage.clear();

    setupNode      = nullptr;
    swapchainImage = {};
}

void RenderGraph::reset() noexcept
{
    releaseResources();
    deviceContext   = nullptr;
    targetSwapchain = nullptr;
}

void RenderGraph::compile(const PassSetup& setup)
{
    compileNodes();
    allocateResources();
    for (auto* node : sortedNodes)
        if (node->writeDescriptorSet)
            node->writeDescriptorSet(*this, setup);
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
        const bool imported = resource.importedImage || resource.importedBuffer ||
            resource.type == ResourceType::AccelerationStructure;
        const bool perFrame =
            resource.type == ResourceType::Buffer && resource.bufferInfo.scope == ResourceScope::PerFrame;
        const size_t count = resource.swapchainIndexed ? resource.externalImages.size()
            : !imported && perFrame                    ? maxFramesInFlight
                                                       : 1;
        resource.allocation->instances.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            auto& instance = resource.allocation->instances[i];
            if (imported)
            {
                instance.state = resource.initialState;
                continue;
            }
            if (!resource.externalImages.empty())
            {
                instance.image = std::make_unique<Image>(device, resource.externalImages[i]);
                instance.state = resource.initialState;
                continue;
            }
            if (resource.type == ResourceType::Buffer)
            {
                DCHECK(resource.bufferUsage);
                instance.buffer = std::make_unique<Buffer>(
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
            instance.ownedImage     = vkCheck(device.createImage({
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
            const auto requirements = instance.ownedImage.getMemoryRequirements();
            instance.memory         = vkCheck(device.allocateMemory({
                        .allocationSize  = requirements.size,
                        .memoryTypeIndex = vulkan_memory::findType(
                    physicalDevice,
                    requirements.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eDeviceLocal
                ),
            }));
            vkCheck(instance.ownedImage.bindMemory(*instance.memory, 0));
            instance.image = std::make_unique<Image>(device, *instance.ownedImage);
        }
    }
}

RenderGraph::ImageHandle RenderGraph::importSwapchain()
{
    if (swapchainImage.id != kInvalidHandle)
        return swapchainImage;

    const auto extent   = targetSwapchain->extent();
    auto       resource = std::make_unique<ResourceData>(ResourceData{
              .imageInfo = {.format = targetSwapchain->surfaceFormat().format, .extent = {extent.width, extent.height, 1}},
              .swapchainIndexed = true,
    });
    for (uint32_t i = 0; i < targetSwapchain->imageCount(); ++i)
        resource->externalImages.push_back(targetSwapchain->image(i));
    swapchainImage = {static_cast<ResourceHandle>(resources.size())};
    resources.push_back({.data = std::move(resource)});
    return swapchainImage;
}

uint32_t RenderGraph::instanceIndex(const ResourceData& resource, uint32_t frameIndex) const
{
    if (resource.swapchainIndexed)
        return currentImage;
    return resource.allocation->instances.size() == 1 ? 0 : frameIndex;
}

const RenderGraph::ImageCreateInfo& RenderGraph::imageInfo(ImageHandle image)
{
    const auto& resource = resolveResource(image.id);
    DCHECK(resource.type == ResourceType::Image);
    return resource.imageInfo;
}

Image& RenderGraph::getImage(ImageHandle image)
{
    const auto& resource = resolveResource(image.id);
    DCHECK(resource.type == ResourceType::Image && resource.allocation);
    if (resource.importedImage)
        return *resource.importedImage;
    return *resource.allocation->instances.at(resource.swapchainIndexed ? currentImage : 0).image;
}

Buffer& RenderGraph::getBuffer(BufferHandle buffer, uint32_t frameIndex)
{
    const auto& resource = resolveResource(buffer.id);
    DCHECK(resource.type == ResourceType::Buffer && resource.allocation);
    if (resource.importedBuffer)
        return *resource.importedBuffer;
    return *resource.allocation->instances.at(instanceIndex(resource, frameIndex)).buffer;
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

void RenderGraph::useResource(vk::raii::CommandBuffer& commandBuffer, const ResourceUse& use, uint32_t frameIndex)
{
    auto&      resource = resolveResource(use.resource);
    auto&      instance = resource.allocation->instances.at(instanceIndex(resource, frameIndex));
    // Shared instances retain the previous submission's access, including all trailing readers.
    // Their first use in this frame therefore also synchronizes across frame boundaries.
    const auto previous = instance.state;
    const auto next     = use.access;
    if (!needsBarrier(previous, next))
    {
        instance.state.stages |= next.stages;
        instance.state.access |= next.access;
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
            .buffer = resource.importedBuffer ? resource.importedBuffer->handle() : instance.buffer->handle(),
            .size   = resource.bufferInfo.size,
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
            .image               = resource.importedImage ? resource.importedImage->handle() : instance.image->handle(),
            .subresourceRange    = {
                   .aspectMask = imageAspect(resource.imageInfo.format),
                   .levelCount = resource.imageInfo.mipLevels,
                   .layerCount = resource.imageInfo.arrayLayers
            },
        };
        commandBuffer.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
    }
    instance.state = next;
}

void RenderGraph::transitionResources(
    vk::raii::CommandBuffer& commandBuffer, const std::vector<ResourceUse>& uses, uint32_t frameIndex
)
{
    for (const ResourceUse& use : uses)
        useResource(commandBuffer, use, frameIndex);
}

void RenderGraph::prepare(const PassFrame& frame)
{
    for (auto* node : sortedNodes)
        if (node->prepare)
            node->prepare(*this, frame);
}

void RenderGraph::execute(
    vk::raii::CommandBuffer& commandBuffer,
    const PassFrame&         frame,
    uint32_t                 imageIndex,
    GpuProfiler&             gpuProfiler,
    Profiler*                profiler
)
{
    DCHECK(!sortedNodes.empty());

    // TODO
    currentImage = imageIndex;
    for (Resource& entry : resources)
    {
        if (!entry.data)
            continue;

        auto& resource = *entry.data;
        if (resource.swapchainIndexed)
        {
            auto& state  = resource.allocation->instances.at(currentImage).state;
            state.stages = resource.initialState.stages;
            state.access = {};
        }
    }

    std::vector<std::string_view> names;
    if (profiler)
    {
        for (const auto* node : sortedNodes)
            names.push_back(node->name);
    }
    gpuProfiler.beginFrame(commandBuffer, names, profiler);
    auto execution = frame;
    execution.gpuProfiler = profiler ? &gpuProfiler : nullptr;
    uint32_t passIndex = 0;
    for (auto* node : sortedNodes)
    {
        CpuProfileScope timing{profiler, node->name};
        if (profiler)
            profiler->beginPass(node->name);
        gpuProfiler.beginPass(commandBuffer, passIndex);
        transitionResources(commandBuffer, node->uses, frame.frameIndex);
        node->execute(*this, commandBuffer, execution);
        gpuProfiler.endPass(commandBuffer, passIndex);
        ++passIndex;
    }
    gpuProfiler.endFrame(commandBuffer);
}

RenderGraph::ImageHandle RenderGraph::importImage(Image& image, ImageCreateInfo info, ImageAccess initial)
{
    for (size_t index = 0; index < resources.size(); ++index)
        if (resources[index].data && resources[index].data->importedImage == &image)
            return {static_cast<ResourceHandle>(index)};
    auto data           = std::make_unique<ResourceData>();
    data->imageInfo     = info;
    data->importedImage = &image;
    data->initialState  = initial;
    const ImageHandle handle{static_cast<ResourceHandle>(resources.size())};
    resources.push_back({.data = std::move(data)});
    return handle;
}

RenderGraph::BufferHandle RenderGraph::importBuffer(Buffer& buffer, MemoryAccess initial)
{
    for (size_t index = 0; index < resources.size(); ++index)
        if (resources[index].data && resources[index].data->importedBuffer == &buffer)
            return {static_cast<ResourceHandle>(index)};
    auto data             = std::make_unique<ResourceData>();
    data->type            = ResourceType::Buffer;
    data->bufferInfo.size = buffer.size();
    data->importedBuffer  = &buffer;
    data->initialState    = {.stages = initial.stages, .access = initial.access};
    const BufferHandle handle{static_cast<ResourceHandle>(resources.size())};
    resources.push_back({.data = std::move(data)});
    return handle;
}

RenderGraph::ImageAccess RenderGraph::imageState(ImageHandle image) const
{
    const auto& resource = resolveResource(image.id);
    DCHECK(resource.type == ResourceType::Image && resource.allocation);
    return resource.allocation->instances.at(resource.swapchainIndexed ? currentImage : 0).state;
}

RenderGraph::MemoryAccess RenderGraph::bufferState(BufferHandle buffer, uint32_t frameIndex) const
{
    const auto& resource = resolveResource(buffer.id);
    DCHECK(resource.type == ResourceType::Buffer && resource.allocation);
    const auto& state = resource.allocation->instances.at(instanceIndex(resource, frameIndex)).state;
    return {state.stages, state.access};
}
