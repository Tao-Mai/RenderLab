#include "render/resource/texture.h"

#include "render/device/memory.h"
#include "render/device/vk_check.h"
#include "render/resource/buffer.h"

#include <memory>
#include <string>
#include <utility>

#include <stb_image.h>
#include "core/logger.h"

namespace
{
    struct StbiDeleter
    {
        void operator()(stbi_uc *pixels) const
        {
            stbi_image_free(pixels);
        }
    };
}

Texture::Texture(GpuUploadContext upload, const std::filesystem::path &path)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::string filename = path.string();
    std::unique_ptr<stbi_uc, StbiDeleter> pixels(
        stbi_load(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha));
    CHECK(pixels && width > 0 && height > 0,
        "failed to load texture '{}': {}", filename, stbi_failure_reason());
    create(
        upload,
        pixels.get(),
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height));
}

Texture::Texture(GpuUploadContext upload, const std::array<uint8_t, 4> &rgba)
{
    create(upload, rgba.data(), 1, 1);
}

vk::ImageView Texture::imageView() const
{
    return *view;
}

vk::Sampler Texture::sampler() const
{
    return *imageSampler;
}

void Texture::create(GpuUploadContext upload, const uint8_t *pixels, uint32_t width, uint32_t height)
{
    const auto& physicalDevice = upload.physicalDevice;
    const auto& device = upload.device;
    const auto& commandPool = upload.commandPool;
    auto& queue = upload.queue;
    const vk::DeviceSize byteSize =
        static_cast<vk::DeviceSize>(width) * height * 4;
    Buffer staging(
        physicalDevice,
        device,
        byteSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent);
    staging.upload(pixels, byteSize);

    const vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR8G8B8A8Srgb,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eTransferDst |
                 vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    image = vkCheck(device.createImage(imageInfo), "vkCreateImage");

    const vk::MemoryRequirements requirements = image.getMemoryRequirements();
    const vk::MemoryAllocateInfo allocationInfo{
        .allocationSize = requirements.size,
        .memoryTypeIndex = vulkan_memory::findType(
            physicalDevice,
            requirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal),
    };
    imageMemory = vkCheck(device.allocateMemory(allocationInfo), "vkAllocateMemory");
    vkCheck(image.bindMemory(*imageMemory, 0), "vkBindImageMemory");

    const vk::CommandBufferAllocateInfo commandInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    vk::raii::CommandBuffer copyCommand = std::move(vkCheck(
        device.allocateCommandBuffers(commandInfo), "vkAllocateCommandBuffers").front());
    vkCheck(
        copyCommand.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}),
        "vkBeginCommandBuffer");

    const vk::ImageSubresourceRange subresourceRange{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
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

    const vk::BufferImageCopy copyRegion{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1},
    };
    copyCommand.copyBufferToImage(
        staging.handle(),
        *image,
        vk::ImageLayout::eTransferDstOptimal,
        copyRegion);

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
    vkCheck(copyCommand.end(), "vkEndCommandBuffer");

    const vk::CommandBuffer command = *copyCommand;
    const vk::SubmitInfo submitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &command,
    };
    vkCheck(queue.submit(submitInfo, nullptr), "vkQueueSubmit");
    vkCheck(queue.waitIdle(), "vkQueueWaitIdle");

    const vk::ImageViewCreateInfo viewInfo{
        .image = *image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR8G8B8A8Srgb,
        .subresourceRange = subresourceRange,
    };
    view = vkCheck(device.createImageView(viewInfo), "vkCreateImageView");

    const vk::SamplerCreateInfo samplerInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .mipLodBias = 0.0f,
        .anisotropyEnable = vk::False,
        .compareEnable = vk::False,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = vk::BorderColor::eIntOpaqueBlack,
        .unnormalizedCoordinates = vk::False,
    };
    imageSampler = vkCheck(device.createSampler(samplerInfo), "vkCreateSampler");
}
