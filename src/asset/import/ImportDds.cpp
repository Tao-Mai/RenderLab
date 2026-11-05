#include "asset/AssetImporter.h"

#include "asset/import/ImportId.h"
#include "core/Logger.h"

#include <DirectXTex.h>

#include <limits>
#include <utility>

namespace
{
[[nodiscard]] ImageFormat ddsFormat(DXGI_FORMAT format, const std::filesystem::path& source)
{
    switch (DirectX::MakeLinear(format))
    {
        case DXGI_FORMAT_BC1_UNORM:
            return ImageFormat::BC1;
        case DXGI_FORMAT_BC2_UNORM:
            return ImageFormat::BC2;
        case DXGI_FORMAT_BC3_UNORM:
            return ImageFormat::BC3;
        case DXGI_FORMAT_BC4_UNORM:
            return ImageFormat::BC4Unorm;
        case DXGI_FORMAT_BC4_SNORM:
            return ImageFormat::BC4Snorm;
        case DXGI_FORMAT_BC5_UNORM:
            return ImageFormat::BC5Unorm;
        case DXGI_FORMAT_BC5_SNORM:
            return ImageFormat::BC5Snorm;
        case DXGI_FORMAT_BC6H_UF16:
            return ImageFormat::BC6HUfloat;
        case DXGI_FORMAT_BC6H_SF16:
            return ImageFormat::BC6HSfloat;
        case DXGI_FORMAT_BC7_UNORM:
            return ImageFormat::BC7;
        default:
            LOG_FATAL("unsupported DDS format {} in '{}'; expected a BC format", static_cast<int>(format), source.string());
    }
}
}

TextureAsset::ID AssetImporter::importDdsTexture(const ImportTextureSetting& setting)
{
    const auto& source = setting.source;
    DirectX::TexMetadata metadata{};
    DirectX::ScratchImage image;
    const HRESULT result = DirectX::LoadFromDDSFile(source.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
    CHECK(SUCCEEDED(result), "failed to load DDS '{}': HRESULT {}", source.string(), result);
    CHECK(metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE2D && metadata.arraySize == 1,
          "DDS must contain a single 2D texture: {}", source.string());
    CHECK(metadata.width <= std::numeric_limits<uint32_t>::max() &&
              metadata.height <= std::numeric_limits<uint32_t>::max(),
          "DDS image is too large: {}", source.string());

    TextureAsset asset{};
    asset.id = asset_import::nextId<TextureAsset>(source.stem().string());
    asset.format = ddsFormat(metadata.format, source);
    asset.colorSpace = setting.colorSpace.value_or(
        DirectX::IsSRGB(metadata.format) ? ColorSpace::Srgb : ColorSpace::Linear);
    asset.layout = ImageLayout::Image2D;
    asset.width = static_cast<uint32_t>(metadata.width);
    asset.height = static_cast<uint32_t>(metadata.height);
    asset.mipLevels = static_cast<uint32_t>(metadata.mipLevels);

    // A single 2D ScratchImage stores tight BC mip levels consecutively, starting at mip 0.
    return saveTexture(std::move(asset), source, {image.GetPixels(), image.GetPixelsSize()});
}
