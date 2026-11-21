#pragma once

#include "render/RenderConfig.h"
#include "render/resources/Buffer.h"
#include "render/resources/Texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

class VulkanContext;

class VulkanResources
{
public:
    static constexpr uint32_t kInvalidHandle = ~0u;
    struct ImageHandle
    {
        uint32_t id                                   = kInvalidHandle;
        bool     operator==(const ImageHandle&) const = default;
    };
    struct BufferHandle
    {
        uint32_t id                                    = kInvalidHandle;
        bool     operator==(const BufferHandle&) const = default;
    };
    struct ImageAccess
    {
        vk::PipelineStageFlags2 stages;
        vk::AccessFlags2        access;
        vk::ImageLayout         layout = vk::ImageLayout::eUndefined;
    };
    struct MemoryAccess
    {
        vk::PipelineStageFlags2 stages;
        vk::AccessFlags2        access;
    };
    struct Access
    {
        uint32_t    slot;
        ImageAccess access;
    };
    struct BufferUpload
    {
        vk::DeviceSize       offset;
        std::span<std::byte> data;
    };

    explicit VulkanResources(VulkanContext& vulkan);
    ~VulkanResources();

    ImageHandle  registerImage(const Falcor::ref<Falcor::Texture>& texture);
    BufferHandle registerBuffer(const Falcor::ref<Falcor::Buffer>& buffer);
    // Previous submissions and command recording must be finished before releasing tracked allocations.
    void         releaseUnusedResources();
    void         setAcquiredImage(ImageHandle image);

    void writeDescriptor(
        vk::DescriptorSet  set,
        uint32_t           binding,
        ImageHandle        image,
        vk::DescriptorType type = vk::DescriptorType::eSampledImage,
        vk::ImageViewType  view = vk::ImageViewType::e2D
    );
    void writeDescriptor(vk::DescriptorSet set, uint32_t binding, BufferHandle buffer);
    void writeDescriptor(vk::DescriptorSet set, uint32_t binding, vk::Sampler sampler);

    [[nodiscard]] const vk::ImageCreateInfo& imageInfo(ImageHandle image) const;
    [[nodiscard]] vk::Extent2D            imageExtent(ImageHandle image) const;
    [[nodiscard]] vk::PipelineStageFlags2 firstUseStages(ImageHandle image) const;
    [[nodiscard]] Falcor::Texture&                getImage(ImageHandle image);
    [[nodiscard]] Falcor::Buffer&                 getBuffer(BufferHandle buffer);
    [[nodiscard]] uint64_t                bufferRevision(BufferHandle buffer) const;
    void                                  invalidateImageHistory(ImageHandle image);
    [[nodiscard]] uint64_t                imageHistoryRevision(ImageHandle image) const;

    void declareAccess(const Access& use);
    void transition(vk::raii::CommandBuffer& commands, const Access& use);
    void transition(vk::raii::CommandBuffer& commands, ImageHandle image, ImageAccess access);
    void transition(vk::raii::CommandBuffer& commands, BufferHandle buffer, MemoryAccess access);
    void beginFrame(uint32_t frameIndex);
    void recordUploads(vk::raii::CommandBuffer& commands);
    void ensureBufferCapacity(BufferHandle buffer, vk::DeviceSize size);
    [[nodiscard]] BufferUpload
    allocateUpload(BufferHandle buffer, vk::DeviceSize size, vk::DeviceSize destinationOffset = 0);

private:
    struct ResourceData;
    uint32_t registerResource(Falcor::ref<Falcor::Resource> resource, bool isImage);

    struct StagingBlock
    {
        Falcor::Buffer         buffer;
        std::byte*     mapped = nullptr;
        vk::DeviceSize used   = 0;
    };
    struct BufferCopy
    {
        BufferHandle   destination;
        size_t         blockIndex;
        vk::BufferCopy region;
    };
    struct StagingPool
    {
        std::vector<StagingBlock> blocks;
        size_t                    blockIndex = 0;
        std::vector<BufferCopy>   copies;
    };

    VulkanContext*                                deviceContext;
    std::vector<ResourceData>                     resources;
    std::unordered_map<const Falcor::Resource*, uint32_t> resourceIndices;
    std::vector<uint32_t>                         freeSlots;
    std::array<StagingPool, maxFramesInFlight>    stagingPools;
    uint32_t                                      currentFrameIndex = 0;
};
