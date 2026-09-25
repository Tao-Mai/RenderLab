#include "render/resource/Texture.h"

#include "render/device/Memory.h"
#include "render/device/VkCheck.h"
#include "render/resource/Buffer.h"
#include "asset/AssetDataManager.h"

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

Texture::Texture(
    GpuUploadContext         upload, const TextureDesc& desc,
    std::span<const uint8_t> bytes)
{
    create(upload,
           bytes.data(),
           desc.width,
           desc.height,
           vulkanFormat(desc.format, desc.colorSpace),
           AssetDataManager::bytesPerPixel(desc.format),
           desc.layout);
}

vk::ImageView Texture::imageView() const
{
    return *view;
}

vk::Sampler Texture::sampler() const
{
    return *imageSampler;
}

void Texture::create(
    GpuUploadContext upload, const void* pixels, uint32_t           width, uint32_t height,
    vk::Format       format, uint32_t    bytesPerPixel, ImageLayout layout)
{
    const uint32_t       layers         = layout == ImageLayout::Cubemap ? 6 : 1;
    const auto&          physicalDevice = upload.physicalDevice;
    const auto&          device         = upload.device;
    const auto&          commandPool    = upload.commandPool;
    auto&                queue          = upload.queue;
    const vk::DeviceSize byteSize       =
        static_cast<vk::DeviceSize>(width) * height * layers * bytesPerPixel;
    Buffer staging(
        physicalDevice,
        device,
        byteSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent);
    staging.upload(pixels, byteSize);

    const vk::ImageCreateInfo imageInfo{
        .flags = layout == ImageLayout::Cubemap
        ? vk::ImageCreateFlags{vk::ImageCreateFlagBits::eCubeCompatible}
        : vk::ImageCreateFlags{},
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = layers,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eTransferDst |
        vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
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
        .levelCount = 1,
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
    copyRegions.reserve(layers);
    for (uint32_t layer = 0; layer < layers; ++layer)
    {
        copyRegions.push_back(vk::BufferImageCopy{
            .bufferOffset = static_cast<vk::DeviceSize>(layer) * width * height * bytesPerPixel,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = layer,
                .layerCount = 1,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {width, height, 1},
        });
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
        .viewType = layout == ImageLayout::Cubemap
        ? vk::ImageViewType::eCube
        : vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = subresourceRange,
    };
    view = vkCheck(device.createImageView(viewInfo));

    const vk::SamplerCreateInfo samplerInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = layout == ImageLayout::Cubemap
        ? vk::SamplerAddressMode::eClampToEdge
        : vk::SamplerAddressMode::eRepeat,
        .addressModeV = layout == ImageLayout::Cubemap
        ? vk::SamplerAddressMode::eClampToEdge
        : vk::SamplerAddressMode::eRepeat,
        .addressModeW = layout == ImageLayout::Cubemap
        ? vk::SamplerAddressMode::eClampToEdge
        : vk::SamplerAddressMode::eRepeat,
        .mipLodBias = 0.0f,
        .anisotropyEnable = vk::False,
        .compareEnable = vk::False,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = vk::BorderColor::eIntOpaqueBlack,
        .unnormalizedCoordinates = vk::False,
    };
    imageSampler = vkCheck(device.createSampler(samplerInfo));
}
