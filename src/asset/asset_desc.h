#pragma once

#include "asset/asset_id.h"
#include "ecs/light.h"
#include "ecs/transform.h"

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
#include <utility>
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
    using type = T;
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
    AssetId materialId;
};

struct MeshSourceDesc
{
    std::string kind;
    std::optional<std::filesystem::path> path;
};

struct MeshDesc
{
    AssetId id;
    MeshSourceDesc source;
    std::filesystem::path geometry;
    std::vector<SubmeshDesc> submeshes;

    static inline const AssetId cube = "cube";
    static inline const AssetId sphere = "sphere";
    static inline const AssetId arrow = "arrow";
    static inline const std::array builtins{&cube, &sphere, &arrow};
};

struct MaterialDesc
{
    AssetId id;
    glm::vec4 baseColorFactor{1.0f};
    float metallic = 1.0f;
    float roughness = 1.0f;
    float ao = 1.0f;
    glm::vec3 emissive{0.0f};
    float normalScale = 1.0f;
    AssetId baseColorTexture;
    AssetId normalTexture;
    AssetId metallicTexture;
    AssetId roughnessTexture;
    AssetId aoTexture;
    AssetId emissiveTexture;
    std::string alphaMode = "OPAQUE";
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};

struct TextureDesc
{
    AssetId id;
    std::string source;
    std::optional<std::filesystem::path> path;
    std::optional<std::array<uint8_t, 4>> rgba;

    static inline const AssetId white = "white";
    static inline const std::array builtins{&white};
};

struct ShaderDesc
{
    AssetId id;
    std::filesystem::path binary;
};

using ComponentMap = std::unordered_map<std::string, entt::meta_any>;

struct SceneObjectDesc
{
    std::string name;
    AssetId meshId;
    ComponentMap components;
};

struct SceneCameraDesc
{
    glm::vec3 position{0.0f, 1.5f, 6.0f};
    glm::vec3 target{0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fieldOfView = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float movementSpeed = 3.0f;
    float sprintMultiplier = 3.0f;
};

struct SceneDesc
{
    AssetId id;
    SceneCameraDesc camera;
    std::vector<ecs::Light> lights;
    std::vector<SceneObjectDesc> objects;
};

template <class T>
[[nodiscard]] bool isBuiltin(const AssetId& id)
{
    if constexpr (requires { T::builtins; })
    {
        for (const AssetId* builtin : T::builtins)
        {
            if (id == *builtin)
            {
                return true;
            }
        }
    }
    return false;
}

// Register new asset types here only.
using AssetTypes = TypeList<
    AssetEntry<MeshDesc, "mesh">,
    AssetEntry<MaterialDesc, "material">,
    AssetEntry<TextureDesc, "texture">,
    AssetEntry<ShaderDesc, "shader">,
    AssetEntry<SceneDesc, "scene">>;
