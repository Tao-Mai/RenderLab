#pragma once
#include <slang-rhi.h>
#include <span>
#include <unordered_map>
#include <vector>
#include "render/ViewportRect.h"

// Graph-lifetime resource registration, capacity growth, uploads and temporal history.
class GpuResources
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
    struct BufferUpload
    {
        uint64_t             offset;
        std::span<std::byte> data;
    };

    explicit GpuResources(rhi::IDevice& device) : device(device) {}
    ImageHandle                           registerImage(rhi::ITexture* texture);
    BufferHandle                          registerBuffer(rhi::IBuffer* buffer);
    [[nodiscard]] const rhi::TextureDesc& imageInfo(ImageHandle image) const;
    [[nodiscard]] Extent2D                imageExtent(ImageHandle image) const;
    [[nodiscard]] rhi::ITexture*          getImage(ImageHandle image) const;
    [[nodiscard]] rhi::IBuffer*           getBuffer(BufferHandle buffer) const;
    [[nodiscard]] uint64_t bufferRevision(BufferHandle buffer) const { return resources[buffer.id].revision; }
    void                   invalidateImageHistory(ImageHandle image) { ++resources[image.id].historyRevision; }
    [[nodiscard]] uint64_t imageHistoryRevision(ImageHandle image) const { return resources[image.id].historyRevision; }
    void                   recordUploads(rhi::ICommandEncoder& encoder);
    void                   ensureBufferCapacity(BufferHandle buffer, uint64_t size);
    [[nodiscard]] BufferUpload allocateUpload(BufferHandle buffer, uint64_t size, uint64_t destinationOffset = 0);

private:
    uint32_t registerResource(rhi::IResource* resource);
    struct ResourceData
    {
        rhi::ComPtr<rhi::IResource> resource;
        uint64_t                    revision        = 1;
        uint64_t                    historyRevision = 0;
    };
    struct Upload
    {
        BufferHandle           destination;
        uint64_t               offset;
        std::vector<std::byte> bytes;
    };
    rhi::IDevice&                                 device;
    std::vector<ResourceData>                     resources;
    std::unordered_map<rhi::IResource*, uint32_t> resourceIndices;
    std::vector<Upload>                           uploads;
};
