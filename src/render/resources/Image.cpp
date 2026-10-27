//
// Created by 24500 on 2026/10/6.
//

#include "render/resources/Image.h"

#include "core/Logger.h"
#include "render/device/VkCheck.h"

#include <utility>

Image::Image(const vk::raii::Device& device, vk::Image image) : device(&device), image(image)
{
    DCHECK(*device);
    DCHECK(image);
}

vk::Image Image::handle() const { return image; }

vk::ImageView Image::getOrCreateImageView(const vk::ImageViewCreateInfo& viewInfo)
{
    DCHECK(device && **device);
    DCHECK(image);
    CHECK(!viewInfo.pNext);

    const auto&   components = viewInfo.components;
    const auto&   range      = viewInfo.subresourceRange;
    const ViewKey key{
        viewInfo.flags,
        viewInfo.viewType,
        viewInfo.format,
        components.r,
        components.g,
        components.b,
        components.a,
        range.aspectMask,
        range.baseMipLevel,
        range.levelCount,
        range.baseArrayLayer,
        range.layerCount,
    };
    if (const auto found = views.find(key); found != views.end())
        return *found->second;

    vk::ImageViewCreateInfo creationInfo = viewInfo;
    creationInfo.image                   = image;
    auto                view             = vkCheck(device->createImageView(creationInfo));
    const vk::ImageView handle           = *view;
    views.emplace(key, std::move(view));
    return handle;
}
