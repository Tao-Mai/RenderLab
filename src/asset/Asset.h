#pragma once

#include "asset/AssetID.h"
#include "asset/ImageFormat.h"
#include "scene/Actor.h"

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct Asset
{
};

template <class T>
concept AssetType = std::derived_from<T, Asset> && requires(T& value)
{
    typename T::ID;
    requires std::same_as<typename T::ID, AssetID<T>>;
    { value.id } -> std::same_as<AssetID<T>&>;
    { T::dir } -> std::convertible_to<std::string_view>;
};

template <AssetType... Assets>
struct AssetTypeList
{
    template <class F>
    static constexpr void forEach(F&& function)
    {
        (function.template operator()<Assets>(), ...);
    }

    template <template <class> class Wrapper>
    using wrapTypes = std::tuple<Wrapper<Assets>...>;
};

enum class Source
{
    File,
    Builtin
};

struct TextureAsset : Asset
{
    using ID                              = AssetID<TextureAsset>;
    static constexpr std::string_view dir = "Texture";

    ID                                   id;
    Source                               source = Source::File;
    ImageFormat                          format;
    ColorSpace                           colorSpace;
    ImageLayout                          layout;
    uint32_t                             width{};
    uint32_t                             height{};
    uint32_t                             mipLevels{};
    std::optional<std::filesystem::path> path;
    std::optional<std::filesystem::path> binary;
};

struct SamplerAsset : Asset
{
    using ID                              = AssetID<SamplerAsset>;
    static constexpr std::string_view dir = "Sampler";

    enum class Filter
    {
        Nearest,
        Linear
    };

    enum class MipmapMode
    {
        Nearest,
        Linear
    };

    enum class AddressMode
    {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder
    };

    enum class CompareOp
    {
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always
    };

    enum class BorderColor
    {
        FloatTransparentBlack,
        IntTransparentBlack,
        FloatOpaqueBlack,
        IntOpaqueBlack,
        FloatOpaqueWhite,
        IntOpaqueWhite
    };

    ID                   id;
    Filter               magFilter        = Filter::Linear;
    Filter               minFilter        = Filter::Linear;
    MipmapMode           mipmapMode       = MipmapMode::Linear;
    AddressMode          addressModeU     = AddressMode::Repeat;
    AddressMode          addressModeV     = AddressMode::Repeat;
    AddressMode          addressModeW     = AddressMode::Repeat;
    float                mipLodBias       = 0.0f;
    bool                 anisotropyEnable = false;
    float                maxAnisotropy    = 1.0f;
    bool                 compareEnable    = false;
    CompareOp            compareOp        = CompareOp::Always;
    float                minLod           = 0.0f;
    // 不指定表示不限制最大 LOD；0.0 表示仅使用基础 mip。
    std::optional<float> maxLod;
    BorderColor          borderColor             = BorderColor::FloatTransparentBlack;
    bool                 unnormalizedCoordinates = false;
};

struct TextureBinding
{
    TextureAsset::ID textureID;
    SamplerAsset::ID samplerID;

    bool operator==(const TextureBinding&) const = default;
};

struct ShaderAsset : Asset
{
    using ID                              = AssetID<ShaderAsset>;
    static constexpr std::string_view dir = "Shader";

    ID                    id;
    std::filesystem::path binary;
};

struct MaterialAsset : Asset
{
    using ID                              = AssetID<MaterialAsset>;
    static constexpr std::string_view dir = "Material";

    ID                             id;
    std::optional<ShaderAsset::ID> shaderId;
    std::optional<glm::vec4>       baseColorFactor;
    std::optional<float>           metallic;
    std::optional<float>           roughness;
    std::optional<float>           ao;
    std::optional<glm::vec3>       emissive;
    std::optional<float>           normalScale;
    std::optional<TextureBinding>  baseColorTexture;
    std::optional<TextureBinding>  normalTexture;
    std::optional<TextureBinding>  metallicTexture;
    std::optional<TextureBinding>  roughnessTexture;
    std::optional<TextureBinding>  aoTexture;
    std::optional<TextureBinding>  emissiveTexture;
    std::optional<std::string>     alphaMode;
    std::optional<float>           alphaCutoff;
    std::optional<bool>            doubleSided;
};

using MaterialOverride = MaterialAsset;

struct MeshAsset : Asset
{
    using ID                              = AssetID<MeshAsset>;
    static constexpr std::string_view dir = "Mesh";

    struct Submesh
    {
        uint32_t          firstIndex = 0;
        uint32_t          indexCount = 0;
        MaterialAsset::ID materialId;
    };

    ID                    id;
    Source                source;
    std::filesystem::path geometry;
    std::vector<Submesh>  submeshes;
};

struct EnvironmentMapAsset : Asset
{
    using ID                              = AssetID<EnvironmentMapAsset>;
    static constexpr std::string_view dir = "EnvironmentMap";

    ID             id;
    TextureBinding radiance;
    TextureBinding irradiance;
    TextureBinding prefilteredSpecular;
};

struct SceneAsset : Asset
{
    using ID                              = AssetID<SceneAsset>;
    static constexpr std::string_view dir = "Scene";

    ID                                  id;
    std::vector<std::unique_ptr<Actor>> actors;

    struct Environment
    {
        std::optional<EnvironmentMapAsset::ID> environmentMap;
        glm::vec3                              up{0.0f, 1.0f, 0.0f};
    } environment;
};

using AssetTypes = AssetTypeList<TextureAsset, MeshAsset, MaterialAsset, ShaderAsset,
                                 SceneAsset, EnvironmentMapAsset, SamplerAsset>;
