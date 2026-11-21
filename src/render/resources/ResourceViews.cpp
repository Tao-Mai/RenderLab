#include "render/resources/ResourceViews.h"

#include "core/Logger.h"
#include "render/device/Device.h"
#include "render/resources/Buffer.h"
#include "render/resources/NativeHandleTraits.h"
#include "render/resources/Texture.h"

namespace Falcor
{
ref<UnorderedAccessView> UnorderedAccessView::create(
    Device* device, Texture* texture, uint32_t mipLevel, uint32_t firstArraySlice, uint32_t arraySize
)
{
    DCHECK(device && texture);
    CHECK(texture->getDevice().get() == device);
    CHECK(is_set(texture->getBindFlags(), ResourceBindFlags::UnorderedAccess), "texture requires UnorderedAccess");
    CHECK(mipLevel < texture->getMipCount() && firstArraySlice < texture->getArraySize());
    CHECK(arraySize && arraySize <= texture->getArraySize() - firstArraySlice);

    const auto& info = texture->imageInfo();
    const uint32_t faces = texture->getType() == Resource::Type::TextureCube ? 6u : 1u;
    const uint32_t layers = arraySize * faces;
    const auto viewType = [&]
    {
        switch (info.imageType)
        {
        case vk::ImageType::e1D: return layers > 1 ? vk::ImageViewType::e1DArray : vk::ImageViewType::e1D;
        case vk::ImageType::e3D: return vk::ImageViewType::e3D;
        default: return layers > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
        }
    }();
    const auto handle = texture->getOrCreateImageView({
        .viewType = viewType,
        .format = info.format,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, mipLevel, 1, firstArraySlice * faces, layers},
    });
    return ref<UnorderedAccessView>(new UnorderedAccessView(
        texture, NativeHandle(static_cast<VkImageView>(handle)), ResourceViewInfo{mipLevel, 1, firstArraySlice, arraySize}
    ));
}

ref<UnorderedAccessView> UnorderedAccessView::create(Device* device, Buffer* buffer, uint64_t offset, uint64_t size)
{
    DCHECK(device && buffer);
    CHECK(buffer->getDevice().get() == device);
    CHECK(is_set(buffer->getBindFlags(), ResourceBindFlags::UnorderedAccess), "buffer requires UnorderedAccess");
    const auto bufferSize = buffer->getSize();
    CHECK(offset < bufferSize && size && (size == kEntireBuffer || size <= bufferSize - offset));

    return ref<UnorderedAccessView>(new UnorderedAccessView(buffer, buffer->getNativeHandle(), ResourceViewInfo{offset, size}));
}
}  // namespace Falcor
