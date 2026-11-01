#include "asset/pbrt/PbrtImage.h"

#include "asset/AssetDataManager.h"
#include "asset/AssetImporter.h"
#include "asset/AssetManager.h"
#include "asset/TextureMip.h"
#include "core/Context.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstring>
#include <utility>

namespace pbrt_import
{
PbrtImageEncoding imageEncoding(std::string_view name, const PbrtSourceLocation& location)
{
    if (name == "linear") return {};
    if (name == "sRGB") return {PbrtImageEncoding::Kind::Srgb, 1.0};
    if (name.starts_with("gamma "))
    {
        double gamma = 0;
        const auto text = name.substr(6);
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), gamma);
        CHECK(error == std::errc{} && end == text.data() + text.size() && std::isfinite(gamma) && gamma > 0,
              "invalid PBRT encoding '{}' at {}:{}", name, location.file.string(), location.line);
        return {PbrtImageEncoding::Kind::Gamma, gamma};
    }
    CHECK(false, "unknown PBRT encoding '{}' at {}:{}", name, location.file.string(), location.line);
    std::unreachable();
}

PbrtImageEncoding defaultImageEncoding(const PbrtFileReference& file)
{
    auto extension = std::filesystem::path(file.originalName).extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".png" ? PbrtImageEncoding{PbrtImageEncoding::Kind::Srgb, 1.0} : PbrtImageEncoding{};
}

ImageImporter::ImageImporter(std::string sceneId, std::filesystem::path sourceRoot) :
    sceneId(std::move(sceneId)), sourceRoot(std::move(sourceRoot)) {}

TextureBinding ImageImporter::texture(const PbrtFileReference& file, PbrtImageEncoding encoding,
    PbrtTextureAsset::ImageMap::Filter filter, PbrtTextureAsset::ImageMap::Wrap wrap, double maxAnisotropy)
{
    CHECK(std::isfinite(maxAnisotropy) && maxAnisotropy >= 1.0, "invalid PBRT anisotropy for {}", file.originalName);
    const auto imageKey = std::tuple{file.path, encoding.kind, encoding.gamma};
    auto image = imageIds.find(imageKey);
    if (image == imageIds.end())
    {
        const auto path = sourceRoot / file.originalName;
        const auto loaded = AssetImporter::loadImage(path);
        const auto pixelCount = size_t(loaded.width) * loaded.height;
        std::vector<float> rgba(pixelCount * 4, 1.0f);
        if (const auto* bytes = std::get_if<std::vector<uint8_t>>(&loaded.pixels))
        {
            const size_t channels = AssetDataManager::bytesPerPixel(loaded.format);
            for (size_t pixel = 0; pixel < pixelCount; ++pixel)
            {
                for (size_t channel = 0; channel < 3; ++channel)
                    rgba[pixel * 4 + channel] = (*bytes)[pixel * channels + (channels <= 2 ? 0 : channel)] / 255.0f;
                if (channels == 2 || channels == 4)
                    rgba[pixel * 4 + 3] = (*bytes)[pixel * channels + channels - 1] / 255.0f;
            }
        }
        else
            rgba = std::get<std::vector<float>>(loaded.pixels);

        for (size_t pixel = 0; pixel < pixelCount; ++pixel)
            for (size_t channel = 0; channel < 4; ++channel)
            {
                auto& value = rgba[pixel * 4 + channel];
                CHECK(std::isfinite(value), "non-finite PBRT image pixel in {}", file.originalName);
                if (channel == 3) continue;
                if (encoding.kind == PbrtImageEncoding::Kind::Srgb)
                    value = value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
                else if (encoding.kind == PbrtImageEncoding::Kind::Gamma)
                {
                    CHECK(value >= 0.0f, "negative gamma-encoded pixel in {}", file.originalName);
                    value = static_cast<float>(std::pow(value, encoding.gamma));
                }
                CHECK(std::isfinite(value), "PBRT image decoding exceeds float range: {}", file.originalName);
            }

        PreparedImage prepared;
        prepared.asset.id = sceneId + "_image" + std::to_string(images.size());
        CHECK(!context().assetManager->find(prepared.asset.id), "PBRT image ID already exists: {}", prepared.asset.id);
        prepared.asset.source = Source::File;
        prepared.asset.path = file.path;
        prepared.asset.format = ImageFormat::RGBA32F;
        prepared.asset.colorSpace = ColorSpace::Linear;
        prepared.asset.chromaticities = loaded.chromaticities.value_or(RgbChromaticities{});
        prepared.asset.layout = ImageLayout::Image2D;
        prepared.asset.width = loaded.width;
        prepared.asset.height = loaded.height;
        prepared.asset.mipLevels = texture_mip::maxLevels(loaded.width, loaded.height);
        const auto pixels = std::span{reinterpret_cast<const uint8_t*>(rgba.data()), rgba.size() * sizeof(float)};
        prepared.bytes = texture_mip::generateChain(prepared.asset, pixels);
        image = imageIds.emplace(imageKey, prepared.asset.id).first;
        images.push_back(std::move(prepared));
    }

    const auto samplerKey = std::tuple{filter, wrap, maxAnisotropy};
    auto sampler = samplerIds.find(samplerKey);
    if (sampler == samplerIds.end())
    {
        SamplerAsset asset;
        asset.id = sceneId + "_sampler" + std::to_string(samplers.size());
        CHECK(!context().assetManager->find(asset.id), "PBRT sampler ID already exists: {}", asset.id);
        asset.magFilter = asset.minFilter = filter == PbrtTextureAsset::ImageMap::Filter::Point
            ? SamplerAsset::Filter::Nearest : SamplerAsset::Filter::Linear;
        asset.mipmapMode = filter == PbrtTextureAsset::ImageMap::Filter::Trilinear || filter == PbrtTextureAsset::ImageMap::Filter::Ewa
            ? SamplerAsset::MipmapMode::Linear : SamplerAsset::MipmapMode::Nearest;
        asset.addressModeU = asset.addressModeV = wrap == PbrtTextureAsset::ImageMap::Wrap::Repeat
            ? SamplerAsset::AddressMode::Repeat : wrap == PbrtTextureAsset::ImageMap::Wrap::Black
            ? SamplerAsset::AddressMode::ClampToBorder : SamplerAsset::AddressMode::ClampToEdge;
        asset.anisotropyEnable = filter == PbrtTextureAsset::ImageMap::Filter::Ewa;
        asset.maxAnisotropy = static_cast<float>(maxAnisotropy);
        sampler = samplerIds.emplace(samplerKey, asset.id).first;
        samplers.push_back(std::move(asset));
    }
    return {image->second, sampler->second};
}

void ImageImporter::save()
{
    for (auto& image : images)
    {
        image.asset.binary = context().assetDataManager->writeTexture(image.asset, image.bytes);
        context().assetManager->save(std::move(image.asset));
    }
    for (auto& sampler : samplers)
        context().assetManager->save(std::move(sampler));
}
}
