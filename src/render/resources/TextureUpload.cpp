#include "render/resources/TextureUpload.h"
#include <cstring>
#include <vector>
#include "asset/TextureAsset.h"
#include "asset/TextureMip.h"
#include "render/RhiUtils.h"
namespace
{
[[nodiscard]] rhi::Format textureFormat(ImageFormat format, ColorSpace colorSpace)
{
    const bool srgb = colorSpace == ColorSpace::Srgb;
    switch (format)
    {
        case ImageFormat::R8:
            return rhi::Format::R8Unorm;
        case ImageFormat::RG8:
            return rhi::Format::RG8Unorm;
        case ImageFormat::RGB8:
        case ImageFormat::RGBA8:
            return srgb ? rhi::Format::RGBA8UnormSrgb : rhi::Format::RGBA8Unorm;
        case ImageFormat::RGBA16F:
            return rhi::Format::RGBA16Float;
        case ImageFormat::RGBA32F:
            return rhi::Format::RGBA32Float;
        case ImageFormat::BC1:
            return srgb ? rhi::Format::BC1UnormSrgb : rhi::Format::BC1Unorm;
        case ImageFormat::BC2:
            return srgb ? rhi::Format::BC2UnormSrgb : rhi::Format::BC2Unorm;
        case ImageFormat::BC3:
            return srgb ? rhi::Format::BC3UnormSrgb : rhi::Format::BC3Unorm;
        case ImageFormat::BC4Unorm:
            return rhi::Format::BC4Unorm;
        case ImageFormat::BC4Snorm:
            return rhi::Format::BC4Snorm;
        case ImageFormat::BC5Unorm:
            return rhi::Format::BC5Unorm;
        case ImageFormat::BC5Snorm:
            return rhi::Format::BC5Snorm;
        case ImageFormat::BC6HUfloat:
            return rhi::Format::BC6HUfloat;
        case ImageFormat::BC6HSfloat:
            return rhi::Format::BC6HSfloat;
        case ImageFormat::BC7:
            return srgb ? rhi::Format::BC7UnormSrgb : rhi::Format::BC7Unorm;
    }
    LOG_FATAL("invalid texture data format");
}
}  // namespace

rhi::ComPtr<rhi::ITexture>
uploadTexture(rhi::IDevice& device, rhi::ICommandEncoder& commands, const TextureAsset& asset,
    std::span<const uint8_t> bytes)
{
    const uint32_t layers = asset.layout == ImageLayout::Cubemap ? 6 : 1;
    const bool     expand = asset.format == ImageFormat::RGB8 ||
        (asset.colorSpace == ColorSpace::Srgb && (asset.format == ImageFormat::R8 || asset.format == ImageFormat::RG8));
    const auto           uploadFormat = expand ? ImageFormat::RGBA8 : asset.format;
    const auto           block        = imageBlock(uploadFormat);
    std::vector<uint8_t> rgba;
    if (expand)
    {
        const size_t channels = asset.format == ImageFormat::RGB8 ? 3 : asset.format == ImageFormat::RG8 ? 2 : 1;
        rgba.resize(bytes.size() / channels * 4);
        for (size_t src = 0, dst = 0; src < bytes.size(); src += channels, dst += 4)
        {
            std::memcpy(rgba.data() + dst, bytes.data() + src, channels);
            rgba[dst + 3] = 255;
        }
        bytes = rgba;
    }
    std::vector<rhi::SubresourceData> subresources(layers * asset.mipLevels);
    size_t                            offset = 0;
    for (uint32_t mip = 0; mip < asset.mipLevels; ++mip)
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            const auto width = texture_mip::extent(asset.width, mip);
            const auto size  = texture_mip::levelBytes(asset.width, asset.height, 1, block, mip);
            subresources[layer * asset.mipLevels + mip] =
                {bytes.data() + offset, ((width + block.extent - 1) / block.extent) * block.bytes, size};
            offset += size;
        }
    CHECK(offset == bytes.size(), "invalid texture payload: {}", asset.id);
    rhi::TextureDesc desc;
    desc.type     = layers == 6 ? rhi::TextureType::TextureCube : rhi::TextureType::Texture2D;
    desc.size     = {asset.width, asset.height, 1};
    desc.mipCount = asset.mipLevels;
    desc.format   = textureFormat(uploadFormat, asset.colorSpace);
    desc.usage    = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::CopyDestination;
    rhi::ComPtr<rhi::ITexture> image;
    rhiCheck(device.createTexture(desc, nullptr, image.writeRef()));
    rhiCheck(commands.uploadTextureData(image, {0, layers, 0, desc.mipCount}, {0, 0, 0},
        rhi::Extent3D::kWholeTexture, subresources.data(), static_cast<uint32_t>(subresources.size())));
    return image;
}
