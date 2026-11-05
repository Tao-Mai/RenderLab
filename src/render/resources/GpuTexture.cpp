#include "render/resources/GpuTexture.h"

#include "asset/TextureAsset.h"
#include "asset/TextureMip.h"
#include "render/device/Memory.h"
#include "render/device/VkCheck.h"
#include "render/resources/Buffer.h"

#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "core/Logger.h"

namespace
{
[[nodiscard]] vk::Format vulkanFormat(ImageFormat format, ColorSpace colorSpace)
{
    const bool srgb = colorSpace == ColorSpace::Srgb;
    switch (format)
    {
        case ImageFormat::R8:
            return srgb ? vk::Format::eR8Srgb : vk::Format::eR8Unorm;
        case ImageFormat::RG8:
            return srgb ? vk::Format::eR8G8Srgb : vk::Format::eR8G8Unorm;
        case ImageFormat::RGB8:
        case ImageFormat::RGBA8:
            return srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
        case ImageFormat::RGBA16F:
            return vk::Format::eR16G16B16A16Sfloat;
        case ImageFormat::RGBA32F:
            return vk::Format::eR32G32B32A32Sfloat;
        case ImageFormat::BC1:
            return srgb ? vk::Format::eBc1RgbaSrgbBlock : vk::Format::eBc1RgbaUnormBlock;
        case ImageFormat::BC2:
            return srgb ? vk::Format::eBc2SrgbBlock : vk::Format::eBc2UnormBlock;
        case ImageFormat::BC3:
            return srgb ? vk::Format::eBc3SrgbBlock : vk::Format::eBc3UnormBlock;
        case ImageFormat::BC4Unorm:
            return vk::Format::eBc4UnormBlock;
        case ImageFormat::BC4Snorm:
            return vk::Format::eBc4SnormBlock;
        case ImageFormat::BC5Unorm:
            return vk::Format::eBc5UnormBlock;
        case ImageFormat::BC5Snorm:
            return vk::Format::eBc5SnormBlock;
        case ImageFormat::BC6HUfloat:
            return vk::Format::eBc6HUfloatBlock;
        case ImageFormat::BC6HSfloat:
            return vk::Format::eBc6HSfloatBlock;
        case ImageFormat::BC7:
            return srgb ? vk::Format::eBc7SrgbBlock : vk::Format::eBc7UnormBlock;
    }
    LOG_FATAL("invalid texture data format");
}
}  // namespace

GpuTexture::GpuTexture(GpuUploadContext upload, const TextureAsset& asset, std::span<const uint8_t> bytes)
{
    create(upload, bytes, asset);
}

vk::ImageView GpuTexture::imageView() const { return *view; }

vk::Image                  GpuTexture::imageHandle() const { return *image; }
const vk::ImageCreateInfo& GpuTexture::imageInfo() const { return creationInfo; }

void GpuTexture::create(GpuUploadContext upload, std::span<const uint8_t> bytes, const TextureAsset& asset)
{
    const uint32_t layers = asset.layout == ImageLayout::Cubemap ? 6 : 1;
    const ImageFormat uploadFormat = asset.format == ImageFormat::RGB8 ? ImageFormat::RGBA8 : asset.format;
    const ImageBlock block = imageBlock(uploadFormat);
    const vk::Format format = vulkanFormat(uploadFormat, asset.colorSpace);
    const auto& physicalDevice = upload.physicalDevice;
    const auto& device = upload.device;
    const auto& commandPool = upload.commandPool;
    auto& queue = upload.queue;

    std::vector<uint8_t> rgbaPixels;
    if (asset.format == ImageFormat::RGB8)
    {
        const auto byteSize = texture_mip::totalBytes(asset.width, asset.height, layers, block, asset.mipLevels);
        CHECK(byteSize <= std::numeric_limits<uint32_t>::max(), "texture payload is too large: {}", asset.id);

        // The payload packs all mip levels and cube faces consecutively.
        // Expand every RGB texel; GPU copies then use a four-byte pixel stride.
        rgbaPixels.resize(static_cast<size_t>(byteSize));
        for (size_t sourceOffset = 0, destinationOffset = 0; sourceOffset < bytes.size();
             sourceOffset += 3, destinationOffset += 4)
        {
            rgbaPixels[destinationOffset]     = bytes[sourceOffset];
            rgbaPixels[destinationOffset + 1] = bytes[sourceOffset + 1];
            rgbaPixels[destinationOffset + 2] = bytes[sourceOffset + 2];
            rgbaPixels[destinationOffset + 3] = 255;
        }
        bytes = rgbaPixels;
    }

    // Packed asset mips can start at odd offsets for R8/RG8 NPOT images.
    // BC offsets are aligned to the size of a whole compressed block.
    const vk::DeviceSize             copyOffsetAlignment = std::max(4u, block.bytes);
    vk::DeviceSize                   stagingSize          = 0;
    std::vector<vk::BufferImageCopy> copyRegions;
    copyRegions.reserve(static_cast<size_t>(layers) * asset.mipLevels);
    for (uint32_t mip = 0; mip < asset.mipLevels; ++mip)
    {
        const uint32_t       mipWidth   = texture_mip::extent(asset.width, mip);
        const uint32_t       mipHeight  = texture_mip::extent(asset.height, mip);
        const vk::DeviceSize layerBytes = texture_mip::levelBytes(asset.width, asset.height, 1, block, mip);
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            stagingSize = (stagingSize + copyOffsetAlignment - 1) & ~(copyOffsetAlignment - 1);
            copyRegions.push_back({
                .bufferOffset = stagingSize,
                .imageSubresource =
                    {
                        .aspectMask     = vk::ImageAspectFlagBits::eColor,
                        .mipLevel       = mip,
                        .baseArrayLayer = layer,
                        .layerCount     = 1,
                    },
                .imageExtent = {mipWidth, mipHeight, 1},
            });
            stagingSize += layerBytes;
        }
    }

    std::vector<uint8_t> alignedPixels;
    if (stagingSize != bytes.size())
    {
        alignedPixels.resize(static_cast<size_t>(stagingSize));
        vk::DeviceSize sourceOffset = 0;
        for (const auto& region : copyRegions)
        {
            const auto layerBytes = texture_mip::levelBytes(
                asset.width, asset.height, 1, block, region.imageSubresource.mipLevel);
            std::memcpy(alignedPixels.data() + region.bufferOffset, bytes.data() + sourceOffset, layerBytes);
            sourceOffset += layerBytes;
        }
        bytes = alignedPixels;
    }

    Buffer staging(
        physicalDevice,
        device,
        stagingSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    );
    staging.upload(bytes.data(), stagingSize);

    const vk::ImageCreateInfo imageInfo{
        .flags = asset.layout == ImageLayout::Cubemap ? vk::ImageCreateFlags{vk::ImageCreateFlagBits::eCubeCompatible}
                                                      : vk::ImageCreateFlags{},
        .imageType     = vk::ImageType::e2D,
        .format        = format,
        .extent        = {asset.width, asset.height, 1},
        .mipLevels     = asset.mipLevels,
        .arrayLayers   = layers,
        .samples       = vk::SampleCountFlagBits::e1,
        .tiling        = vk::ImageTiling::eOptimal,
        .usage         = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        .sharingMode   = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    creationInfo = imageInfo;
    image        = vkCheck(device.createImage(imageInfo));

    const vk::MemoryRequirements requirements = image.getMemoryRequirements();
    const vk::MemoryAllocateInfo allocationInfo{
        .allocationSize  = requirements.size,
        .memoryTypeIndex = vulkan_memory::findType(
            physicalDevice,
            requirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        ),
    };
    imageMemory = vkCheck(device.allocateMemory(allocationInfo));
    vkCheck(image.bindMemory(*imageMemory, 0));

    const vk::CommandBufferAllocateInfo commandInfo{
        .commandPool        = commandPool,
        .level              = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    vk::raii::CommandBuffer copyCommand = std::move(vkCheck(device.allocateCommandBuffers(commandInfo)).front());
    vkCheck(copyCommand.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}));

    const vk::ImageSubresourceRange subresourceRange{
        .aspectMask     = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel   = 0,
        .levelCount     = asset.mipLevels,
        .baseArrayLayer = 0,
        .layerCount     = layers,
    };
    const vk::ImageMemoryBarrier2 toTransfer{
        .srcStageMask        = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccessMask       = {},
        .dstStageMask        = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask       = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout           = vk::ImageLayout::eUndefined,
        .newLayout           = vk::ImageLayout::eTransferDstOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = *image,
        .subresourceRange    = subresourceRange,
    };
    vk::DependencyInfo dependency{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &toTransfer,
    };
    copyCommand.pipelineBarrier2(dependency);

    copyCommand.copyBufferToImage(staging.handle(), *image, vk::ImageLayout::eTransferDstOptimal, copyRegions);

    const vk::ImageMemoryBarrier2 toShader{
        .srcStageMask        = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask       = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask        = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask       = vk::AccessFlagBits2::eShaderSampledRead,
        .oldLayout           = vk::ImageLayout::eTransferDstOptimal,
        .newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = *image,
        .subresourceRange    = subresourceRange,
    };
    dependency.pImageMemoryBarriers = &toShader;
    copyCommand.pipelineBarrier2(dependency);
    vkCheck(copyCommand.end());

    const vk::CommandBuffer command = *copyCommand;
    const vk::SubmitInfo    submitInfo{
           .commandBufferCount = 1,
           .pCommandBuffers    = &command,
    };
    vkCheck(queue.submit(submitInfo, nullptr));
    vkCheck(queue.waitIdle());

    const vk::ImageViewCreateInfo viewInfo{
        .image            = *image,
        .viewType         = asset.layout == ImageLayout::Cubemap ? vk::ImageViewType::eCube : vk::ImageViewType::e2D,
        .format           = format,
        .subresourceRange = subresourceRange,
    };
    view = vkCheck(device.createImageView(viewInfo));
}
