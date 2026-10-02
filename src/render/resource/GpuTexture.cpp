#include "render/resource/GpuTexture.h"

#include "render/device/Memory.h"
#include "render/device/VkCheck.h"
#include "render/resource/Buffer.h"
#include "asset/AssetDataManager.h"
#include "asset/TextureMip.h"

#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "core/Logger.h"

namespace
{
[[nodiscard]] vk::Format vulkanFormat(ImageFormat format, ColorSpace colorSpace)
{
    switch (format)
    {
        case ImageFormat::R8:
            return colorSpace == ColorSpace::Srgb
                ? vk::Format::eR8Srgb : vk::Format::eR8Unorm;
        case ImageFormat::RG8:
            return colorSpace == ColorSpace::Srgb
                ? vk::Format::eR8G8Srgb : vk::Format::eR8G8Unorm;
        case ImageFormat::RGB8:
            return colorSpace == ColorSpace::Srgb
                ? vk::Format::eR8G8B8Srgb : vk::Format::eR8G8B8Unorm;
        case ImageFormat::RGBA8:
            return colorSpace == ColorSpace::Srgb
                ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
        case ImageFormat::RGBA16F:
            CHECK(colorSpace == ColorSpace::Linear,
                  "floating-point textures require linear color space");
            return vk::Format::eR16G16B16A16Sfloat;
        case ImageFormat::RGBA32F:
            CHECK(colorSpace == ColorSpace::Linear,
                  "floating-point textures require linear color space");
            return vk::Format::eR32G32B32A32Sfloat;
    }
    LOG_FATAL("invalid texture data format");
}
}

GpuTexture::GpuTexture(
    GpuUploadContext         upload, const Texture::Desc& desc,
    std::span<const uint8_t> bytes)
{
    create(upload, bytes, desc, vulkanFormat(desc.format, desc.colorSpace));
}

vk::ImageView GpuTexture::imageView() const
{
    return *view;
}

vk::Image GpuTexture::imageHandle() const { return *image; }
const vk::ImageCreateInfo& GpuTexture::imageInfo() const { return creationInfo; }

void GpuTexture::create(
    GpuUploadContext upload, std::span<const uint8_t> bytes,
    const Texture::Desc& desc, vk::Format format)
{
    const uint32_t       layers         = desc.layout == ImageLayout::Cubemap ? 6 : 1;
    const uint32_t       pixelBytes     = AssetDataManager::bytesPerPixel(desc.format);
    const auto&          physicalDevice = upload.physicalDevice;
    const auto&          device         = upload.device;
    const auto&          commandPool    = upload.commandPool;
    auto&                queue          = upload.queue;
    CHECK(desc.width > 0 && desc.height > 0 && desc.mipLevels > 0 &&
          desc.mipLevels <= texture_mip::maxLevels(desc.width, desc.height),
          "invalid texture mip levels: {}", desc.id);
    CHECK(desc.width <= std::numeric_limits<uint32_t>::max() /
          desc.height / layers / pixelBytes,
          "texture payload is too large: {}", desc.id);

    const vk::DeviceSize byteSize = texture_mip::totalBytes(
        desc.width, desc.height, layers, pixelBytes, desc.mipLevels);
    CHECK(bytes.size() == byteSize, "invalid texture payload size: {}", desc.id);
    Buffer staging(
        physicalDevice,
        device,
        byteSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent);
    staging.upload(bytes.data(), byteSize);

    const vk::ImageCreateInfo imageInfo{
        .flags = desc.layout == ImageLayout::Cubemap
        ? vk::ImageCreateFlags{vk::ImageCreateFlagBits::eCubeCompatible}
        : vk::ImageCreateFlags{},
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {desc.width, desc.height, 1},
        .mipLevels = desc.mipLevels,
        .arrayLayers = layers,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eTransferDst |
        vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    creationInfo = imageInfo;
    image = vkCheck(device.createImage(imageInfo));

    const vk::MemoryRequirements requirements = image.getMemoryRequirements();
    const vk::MemoryAllocateInfo allocationInfo{
        .allocationSize = requirements.size,
        .memoryTypeIndex = vulkan_memory::findType(
            physicalDevice,
            requirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal),
    };
    imageMemory = vkCheck(device.allocateMemory(allocationInfo));
    vkCheck(image.bindMemory(*imageMemory, 0));

    const vk::CommandBufferAllocateInfo commandInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    vk::raii::CommandBuffer copyCommand = std::move(vkCheck(
        device.allocateCommandBuffers(commandInfo)).front());
    vkCheck(
        copyCommand.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}));

    const vk::ImageSubresourceRange subresourceRange{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = desc.mipLevels,
        .baseArrayLayer = 0,
        .layerCount = layers,
    };
    const vk::ImageMemoryBarrier2 toTransfer{
        .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccessMask = {},
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *image,
        .subresourceRange = subresourceRange,
    };
    vk::DependencyInfo dependency{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toTransfer,
    };
    copyCommand.pipelineBarrier2(dependency);

    std::vector<vk::BufferImageCopy> copyRegions;
    copyRegions.reserve(static_cast<size_t>(layers) * desc.mipLevels);
    for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
    {
        const uint32_t mipWidth = texture_mip::extent(desc.width, mip);
        const uint32_t mipHeight = texture_mip::extent(desc.height, mip);
        const vk::DeviceSize levelOffset = texture_mip::offset(
            desc.width, desc.height, layers, pixelBytes, mip);

        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            copyRegions.push_back(vk::BufferImageCopy{
                .bufferOffset = levelOffset +
                    static_cast<vk::DeviceSize>(layer) * mipWidth * mipHeight * pixelBytes,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .mipLevel = mip,
                    .baseArrayLayer = layer,
                    .layerCount = 1,
                },
                .imageOffset = {0, 0, 0},
                .imageExtent = {mipWidth, mipHeight, 1},
            });
        }
    }
    copyCommand.copyBufferToImage(
        staging.handle(),
        *image,
        vk::ImageLayout::eTransferDstOptimal,
        copyRegions);

    const vk::ImageMemoryBarrier2 toShader{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *image,
        .subresourceRange = subresourceRange,
    };
    dependency.pImageMemoryBarriers = &toShader;
    copyCommand.pipelineBarrier2(dependency);
    vkCheck(copyCommand.end());

    const vk::CommandBuffer command = *copyCommand;
    const vk::SubmitInfo    submitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &command,
    };
    vkCheck(queue.submit(submitInfo, nullptr));
    vkCheck(queue.waitIdle());

    const vk::ImageViewCreateInfo viewInfo{
        .image = *image,
        .viewType = desc.layout == ImageLayout::Cubemap
        ? vk::ImageViewType::eCube
        : vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = subresourceRange,
    };
    view = vkCheck(device.createImageView(viewInfo));

}
