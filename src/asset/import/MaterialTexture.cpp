#include "asset/import/MaterialTexture.h"

#include "asset/TextureMip.h"
#include "asset/import/ImportId.h"
#include "core/Logger.h"

#include <DirectXTex.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/packing.hpp>
#include <span>
#include <vector>

namespace
{
struct TextureImage
{
    uint32_t               width  = 1;
    uint32_t               height = 1;
    std::vector<glm::vec4> pixels;
    glm::vec4              uniformValue{1.0f};

    glm::vec4 sample(uint32_t x, uint32_t y, uint32_t targetWidth, uint32_t targetHeight) const
    {
        if (pixels.empty())
            return uniformValue;

        const float sourceX = (static_cast<float>(x) + 0.5f) * width / targetWidth - 0.5f;
        const float sourceY = (static_cast<float>(y) + 0.5f) * height / targetHeight - 0.5f;
        const int   left    = static_cast<int>(std::floor(sourceX));
        const int   top     = static_cast<int>(std::floor(sourceY));
        const auto  texel   = [&](int column, int row)
        {
            column = std::clamp(column, 0, static_cast<int>(width) - 1);
            row    = std::clamp(row, 0, static_cast<int>(height) - 1);

            return pixels[static_cast<size_t>(row) * width + column];
        };

        return glm::mix(
            glm::mix(texel(left, top), texel(left + 1, top), sourceX - left),
            glm::mix(texel(left, top + 1), texel(left + 1, top + 1), sourceX - left),
            sourceY - top
        );
    }
};

DXGI_FORMAT textureFormat(ImageFormat format)
{
    switch (format)
    {
        case ImageFormat::R8:
            return DXGI_FORMAT_R8_UNORM;
        case ImageFormat::RG8:
            return DXGI_FORMAT_R8G8_UNORM;
        case ImageFormat::RGBA8:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case ImageFormat::RGBA16F:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case ImageFormat::RGBA32F:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case ImageFormat::BC1:
            return DXGI_FORMAT_BC1_UNORM;
        case ImageFormat::BC2:
            return DXGI_FORMAT_BC2_UNORM;
        case ImageFormat::BC3:
            return DXGI_FORMAT_BC3_UNORM;
        case ImageFormat::BC4Unorm:
            return DXGI_FORMAT_BC4_UNORM;
        case ImageFormat::BC4Snorm:
            return DXGI_FORMAT_BC4_SNORM;
        case ImageFormat::BC5Unorm:
            return DXGI_FORMAT_BC5_UNORM;
        case ImageFormat::BC5Snorm:
            return DXGI_FORMAT_BC5_SNORM;
        case ImageFormat::BC6HUfloat:
            return DXGI_FORMAT_BC6H_UF16;
        case ImageFormat::BC6HSfloat:
            return DXGI_FORMAT_BC6H_SF16;
        case ImageFormat::BC7:
            return DXGI_FORMAT_BC7_UNORM;
        case ImageFormat::RGB8:
            break;
    }

    std::unreachable();
}

TextureImage loadTexture(const std::optional<TextureBinding>& binding, glm::vec4 uniformValue)
{
    if (!binding)
        return {.uniformValue = uniformValue};

    const auto& assets = *context().assetManager;
    const auto& asset  = assets.get(binding->textureID);
    CHECK(asset.layout == ImageLayout::Image2D, "material texture must be 2D: {}", asset.id);
    auto         bytes = assets.readTexture(asset);
    TextureImage result{
        asset.width,
        asset.height,
        std::vector<glm::vec4>(static_cast<size_t>(asset.width) * asset.height)
    };
    if (asset.format == ImageFormat::RGB8)
    {
        for (size_t pixel = 0; pixel < result.pixels.size(); ++pixel)
            result.pixels[pixel] =
                glm::vec4{bytes[pixel * 3], bytes[pixel * 3 + 1], bytes[pixel * 3 + 2], 255} / 255.0f;
    }
    else
    {
        const auto     format = textureFormat(asset.format);
        const auto     block  = imageBlock(asset.format);
        DirectX::Image source{
            asset.width,
            asset.height,
            format,
            static_cast<size_t>(texture_mip::blockCount(asset.width, block.extent)) * block.bytes,
            static_cast<size_t>(texture_mip::levelBytes(asset.width, asset.height, 1, block, 0)),
            bytes.data()
        };
        DirectX::ScratchImage decoded;
        if (format == DXGI_FORMAT_R32G32B32A32_FLOAT)
            std::memcpy(result.pixels.data(), bytes.data(), result.pixels.size() * sizeof(glm::vec4));
        else
        {
            const auto status = DirectX::IsCompressed(format)
                ? DirectX::Decompress(source, DXGI_FORMAT_R32G32B32A32_FLOAT, decoded)
                : DirectX::Convert(source, DXGI_FORMAT_R32G32B32A32_FLOAT, DirectX::TEX_FILTER_DEFAULT, 0.0f, decoded);
            CHECK(SUCCEEDED(status), "cannot decode material texture '{}': HRESULT {}", asset.id, status);
            const auto& image = *decoded.GetImage(0, 0, 0);
            for (size_t row = 0; row < asset.height; ++row)
                std::memcpy(
                    result.pixels.data() + row * asset.width,
                    image.pixels + row * image.rowPitch,
                    asset.width * sizeof(glm::vec4)
                );
        }
    }
    if (asset.colorSpace == ColorSpace::Srgb)
        for (auto& pixel : result.pixels)
            for (glm::length_t channel = 0; channel < 3; ++channel)
                pixel[channel] = pixel[channel] <= 0.04045f ? pixel[channel] / 12.92f
                                                            : std::pow((pixel[channel] + 0.055f) / 1.055f, 2.4f);

    return result;
}

TextureBinding saveTexture(const TextureImage& image, const std::string& name, const SamplerAsset::ID& sampler)
{
    TextureAsset asset{
        .id         = asset_import::nextId<TextureAsset>(name),
        .format     = ImageFormat::RGBA16F,
        .colorSpace = ColorSpace::Linear,
        .layout     = ImageLayout::Image2D,
        .width      = image.width,
        .height     = image.height,
        .mipLevels  = texture_mip::maxLevels(image.width, image.height),
    };
    std::vector<uint16_t> pixels;
    pixels.reserve(image.pixels.size() * 4);
    for (const auto& pixel : image.pixels)
        for (glm::length_t channel = 0; channel < 4; ++channel)
            pixels.push_back(glm::packHalf1x16(pixel[channel]));
    const auto pixelBytes = std::as_bytes(std::span{pixels});
    const auto bytes =
        texture_mip::generateChain(asset, {reinterpret_cast<const uint8_t*>(pixelBytes.data()), pixelBytes.size()});
    asset.binary = context().assetManager->writeTexture(asset, bytes);

    return {context().assetManager->save(std::move(asset)), sampler};
}

SamplerAsset::ID sharedSampler(std::initializer_list<std::optional<TextureBinding>> textures)
{
    std::optional<SamplerAsset::ID> sampler;
    for (const auto& texture : textures)
    {
        if (!texture)
            continue;
        CHECK(!sampler || *sampler == texture->samplerID, "material texture packing requires matching samplers");
        sampler = texture->samplerID;
    }

    return sampler.value_or(BuiltinAssets::Sampler::linearRepeat);
}
}  // namespace

std::optional<TextureBinding>
asset_import::bakeMaterialTexture(const std::optional<TextureBinding>& texture, glm::vec4 factor)
{
    if (!texture || factor == glm::vec4{1.0f})
        return texture;

    auto image = loadTexture(texture, glm::vec4{1.0f});
    for (auto& pixel : image.pixels)
        pixel *= factor;

    return saveTexture(image, texture->textureID.value + "_scaled", texture->samplerID);
}

std::optional<TextureBinding> asset_import::packMetalRoughTextures(
    const std::optional<TextureBinding>& metallic,
    const std::optional<TextureBinding>& roughness,
    float                                metallicFactor,
    float                                roughnessFactor
)
{
    if (!metallic && !roughness)
        return std::nullopt;
    if (metallic && roughness && *metallic == *roughness)
        return bakeMaterialTexture(metallic, {1.0f, roughnessFactor, metallicFactor, 1.0f});

    const auto   sampler    = sharedSampler({metallic, roughness});
    const auto   metalImage = loadTexture(metallic, glm::vec4{1.0f});
    const auto   roughImage = loadTexture(roughness, glm::vec4{1.0f});
    TextureImage result{std::max(metalImage.width, roughImage.width), std::max(metalImage.height, roughImage.height)};
    result.pixels.reserve(static_cast<size_t>(result.width) * result.height);
    for (uint32_t y = 0; y < result.height; ++y)
        for (uint32_t x = 0; x < result.width; ++x)
            result.pixels.emplace_back(
                0.0f,
                roughImage.sample(x, y, result.width, result.height).g * roughnessFactor,
                metalImage.sample(x, y, result.width, result.height).b * metallicFactor,
                1.0f
            );

    return saveTexture(result, (metallic ? metallic : roughness)->textureID.value + "_metalRough", sampler);
}

void asset_import::convertToSpecGloss(
    StandardMaterial&                    material,
    float                                specularFactor,
    glm::vec3                            specularColor,
    const std::optional<TextureBinding>& specularTexture,
    const std::optional<TextureBinding>& specularColorTexture
)
{
    const auto sampler =
        sharedSampler({material.baseColorTexture, material.specularTexture, specularTexture, specularColorTexture});
    const auto   base   = loadTexture(material.baseColorTexture, material.baseColor);
    const auto   params = loadTexture(material.specularTexture, material.specularParams);
    const auto   weight = loadTexture(specularTexture, glm::vec4{1.0f});
    const auto   color  = loadTexture(specularColorTexture, glm::vec4{1.0f});
    TextureImage diffuse{
        std::max({base.width, params.width, weight.width, color.width}),
        std::max({base.height, params.height, weight.height, color.height})
    };
    TextureImage specular{diffuse.width, diffuse.height};
    const float  ratio   = (material.indexOfRefraction - 1.0f) / (material.indexOfRefraction + 1.0f);
    const auto   convert = [&](uint32_t x, uint32_t y)
    {
        const auto baseColor  = base.sample(x, y, diffuse.width, diffuse.height);
        const auto parameters = params.sample(x, y, diffuse.width, diffuse.height);
        const auto dielectric =
            glm::min(
                glm::vec3{ratio * ratio} * specularColor * glm::vec3{color.sample(x, y, diffuse.width, diffuse.height)},
                glm::vec3{1.0f}
            ) *
            specularFactor * weight.sample(x, y, diffuse.width, diffuse.height).a;
        diffuse.pixels.emplace_back(glm::vec3{baseColor} * (1.0f - parameters.b), baseColor.a);
        specular.pixels.emplace_back(glm::mix(dielectric, glm::vec3{baseColor}, parameters.b), 1.0f - parameters.g);
    };
    for (uint32_t y = 0; y < diffuse.height; ++y)
        for (uint32_t x = 0; x < diffuse.width; ++x)
            convert(x, y);

    material.shadingModel   = ShadingModel::SpecGloss;
    material.baseColor      = diffuse.pixels.front();
    material.specularParams = specular.pixels.front();
    if (material.baseColorTexture || material.specularTexture || specularTexture || specularColorTexture)
    {
        material.baseColorTexture = saveTexture(diffuse, material.id.value + "_diffuse", sampler);
        material.specularTexture  = saveTexture(specular, material.id.value + "_specGloss", sampler);
    }
}
