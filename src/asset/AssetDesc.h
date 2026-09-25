#pragma once

#include "asset/AssetId.h"
#include "asset/ImageFormat.h"
#include "ecs/Camera.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <entt/meta/meta.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

template <std::size_t N>
struct FixedString
{
    char data[N]{};

    constexpr FixedString(const char (&text)[N])
    {
        for (std::size_t index = 0; index < N; ++index)
        {
            data[index] = text[index];
        }
    }

    [[nodiscard]] constexpr std::string_view view() const
    {
        return std::string_view{data, N - 1};
    }
};

template <class T, FixedString Dir>
struct AssetEntry
{
    using type                            = T;
    static constexpr std::string_view dir = Dir.view();
};

template <class... Entries>
struct TypeList
{
    template <class F>
    static constexpr void forEach(F&& f)
    {
        (f.template operator()<Entries>(), ...);
    }

    template <template <class> class W>
    using wrapTypes = std::tuple<W<typename Entries::type>...>;

    template <class T>
    static consteval std::string_view dir()
    {
        return dirOf<T, Entries...>();
    }

private:
    template <class T, class Entry, class... Rest>
    static consteval std::string_view dirOf()
    {
        if constexpr (std::is_same_v<T, typename Entry::type>)
        {
            return Entry::dir;
        }
        else
        {
            static_assert(sizeof...(Rest) > 0, "type is not registered in AssetTypes");
            return dirOf<T, Rest...>();
        }
    }
};

struct SubmeshDesc
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    AssetId  materialId;
};

inline constexpr std::string_view kBuiltinGeometryDir   = "builtin";
inline constexpr std::string_view kBuiltinTextureSource = "builtin";

struct MeshDesc
{
    AssetId                  id;
    std::filesystem::path    geometry;
    std::vector<SubmeshDesc> submeshes;
};

struct MaterialDesc
{
    AssetId                    id;
    std::optional<AssetId>     shaderId;
    std::optional<glm::vec4>   baseColorFactor;
    std::optional<float>       metallic;
    std::optional<float>       roughness;
    std::optional<float>       ao;
    std::optional<glm::vec3>   emissive;
    std::optional<float>       normalScale;
    std::optional<AssetId>     baseColorTexture;
    std::optional<AssetId>     normalTexture;
    std::optional<AssetId>     metallicTexture;
    std::optional<AssetId>     roughnessTexture;
    std::optional<AssetId>     aoTexture;
    std::optional<AssetId>     emissiveTexture;
    std::optional<std::string> alphaMode;
    std::optional<float>       alphaCutoff;
    std::optional<bool>        doubleSided;

    static inline const AssetId white = "white";
};

void applyMaterialFields(MaterialDesc& dst, const MaterialDesc& src);

struct TextureDesc
{
    AssetId                               id;
    std::string                           source;
    ImageFormat                           format;
    ColorSpace                            colorSpace;
    ImageLayout                           layout;
    uint32_t                              width;
    uint32_t                              height;
    std::optional<std::filesystem::path>  path;
    std::optional<std::filesystem::path>  binary;
    std::optional<std::array<uint8_t, 4>> rgba;

    static inline const AssetId white     = "white";
    static inline const AssetId whiteCube = "whiteCube";
};

struct EnvironmentMapDesc
{
    AssetId id;
    AssetId radiance;
    AssetId irradiance;
    AssetId prefilteredSpecular;
};

struct ShaderDesc
{
    AssetId               id;
    std::filesystem::path binary;
};

struct SceneObjectDesc
{
    std::string                                     name;
    std::unordered_map<std::string, entt::meta_any> components;
};

struct SceneDesc
{
    AssetId                      id;
    ecs::Camera                  camera;
    std::vector<SceneObjectDesc> objects;

    struct Environment
    {
        std::optional<AssetId> environmentMap;
    } environment;
};

// Register new asset types here only.
using AssetTypes = TypeList<
    AssetEntry<MeshDesc, "mesh">,
    AssetEntry<MaterialDesc, "material">,
    AssetEntry<TextureDesc, "texture">,
    AssetEntry<ShaderDesc, "shader">,
    AssetEntry<SceneDesc, "scene">,
    AssetEntry<EnvironmentMapDesc, "environment_map">
>;
