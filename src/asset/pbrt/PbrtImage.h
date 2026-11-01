#pragma once

#include "asset/TextureAsset.h"
#include "asset/SamplerAsset.h"
#include "asset/pbrt/PbrtTextureAsset.h"

#include <map>
#include <tuple>

namespace pbrt_import
{
[[nodiscard]] PbrtImageEncoding imageEncoding(std::string_view name, const PbrtSourceLocation& location);
[[nodiscard]] PbrtImageEncoding defaultImageEncoding(const PbrtFileReference& file);

class ImageImporter
{
public:
    ImageImporter(std::string sceneId, std::filesystem::path sourceRoot);
    [[nodiscard]] TextureBinding texture(const PbrtFileReference& file, PbrtImageEncoding encoding,
        PbrtTextureAsset::ImageMap::Filter filter = PbrtTextureAsset::ImageMap::Filter::Bilinear,
        PbrtTextureAsset::ImageMap::Wrap wrap = PbrtTextureAsset::ImageMap::Wrap::Repeat,
        double maxAnisotropy = 8.0);
    void save();

private:
    struct PreparedImage { TextureAsset asset; std::vector<uint8_t> bytes; };
    std::string sceneId;
    std::filesystem::path sourceRoot;
    std::vector<PreparedImage> images;
    std::vector<SamplerAsset> samplers;
    std::map<std::tuple<std::filesystem::path, PbrtImageEncoding::Kind, double>, TextureAsset::ID> imageIds;
    std::map<std::tuple<PbrtTextureAsset::ImageMap::Filter, PbrtTextureAsset::ImageMap::Wrap, double>, SamplerAsset::ID> samplerIds;
};
}
