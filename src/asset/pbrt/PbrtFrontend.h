#pragma once

#include "asset/pbrt/PbrtSceneAsset.h"
#include "asset/pbrt/PbrtTextureAsset.h"

#include <functional>

namespace pbrt_import
{
inline constexpr std::string_view kReaderRevision = "b4ce9687e6c695f5582997c61b0c66cf064bdb4a";

struct MaterialDescription
{
    std::string name;
    PbrtEntity  entity;
};
struct ReadResult
{
    struct MediumDescription
    {
        std::string name;
        PbrtEntity entity;
        PbrtTransform renderFromMedium;
    };
    PbrtSceneAsset                       scene;
    std::vector<MaterialDescription>     materials;
    struct TextureDescription
    {
        PbrtTextureAsset::ID id;
        std::string name;
        PbrtTextureType valueType = PbrtTextureType::Float;
        PbrtEntity entity;
        PbrtTransform renderFromTexture;
    };
    std::vector<TextureDescription>      textures;
    std::vector<MediumDescription>       media;
    std::vector<std::optional<uint32_t>> shapeMaterialIndices;
    std::vector<std::filesystem::path>   sourceFiles;
};

using Diagnostic = std::function<void(bool fatal, const PbrtSourceLocation&, const std::string&)>;
[[nodiscard]] ReadResult                 readScene(const std::filesystem::path& source, const Diagnostic& diagnostic);
[[nodiscard]] bool                       isNamedSpectrum(const std::string& name);
[[nodiscard]] std::optional<std::string> normalizeResourceName(const std::string& name);
}  // namespace pbrt_import
