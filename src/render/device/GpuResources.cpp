#include "render/device/GpuResources.h"
#include <algorithm>
#include "render/device/RhiCheck.h"

uint32_t GpuResources::registerResource(rhi::IResource* resource)
{
    if (const auto found = resourceIndices.find(resource); found != resourceIndices.end())
        return found->second;
    const auto index = uint32_t(resources.size());
    resources.push_back({rhi::ComPtr<rhi::IResource>(resource)});
    resourceIndices.emplace(resource, index);
    return index;
}

GpuResources::ImageHandle  GpuResources::registerImage(rhi::ITexture* texture) { return {registerResource(texture)}; }
GpuResources::BufferHandle GpuResources::registerBuffer(rhi::IBuffer* buffer) { return {registerResource(buffer)}; }
rhi::ITexture*             GpuResources::getImage(ImageHandle image) const
{
    return static_cast<rhi::ITexture*>(resources[image.id].resource.get());
}
rhi::IBuffer* GpuResources::getBuffer(BufferHandle buffer) const
{
    return static_cast<rhi::IBuffer*>(resources[buffer.id].resource.get());
}
const rhi::TextureDesc& GpuResources::imageInfo(ImageHandle image) const { return getImage(image)->getDesc(); }
Extent2D                GpuResources::imageExtent(ImageHandle image) const
{
    const auto& info = imageInfo(image);
    return {info.size.width, info.size.height};
}

void GpuResources::ensureBufferCapacity(BufferHandle handle, uint64_t size)
{
    auto* buffer = getBuffer(handle);
    auto  desc   = buffer->getDesc();
    if (desc.size >= size)
        return;

    desc.size = std::max(size, desc.size * 2);
    rhi::ComPtr<rhi::IBuffer> replacement;
    rhiCheck(device.createBuffer(desc, nullptr, replacement.writeRef()));

    resourceIndices.erase(buffer);
    resourceIndices.emplace(replacement.get(), handle.id);
    resources[handle.id].resource = replacement;
    ++resources[handle.id].revision;
}

GpuResources::BufferUpload GpuResources::allocateUpload(BufferHandle buffer, uint64_t size, uint64_t offset)
{
    ensureBufferCapacity(buffer, offset + size);
    auto& upload = uploads.emplace_back(Upload{buffer, offset, std::vector<std::byte>(size)});
    return {offset, upload.bytes};
}

void GpuResources::recordUploads(rhi::ICommandEncoder& encoder)
{
    for (const auto& upload : uploads)
        rhiCheck(encoder.uploadBufferData(
            getBuffer(upload.destination),
            upload.offset,
            upload.bytes.size(),
            upload.bytes.data()
        ));
    uploads.clear();
}
