#pragma once

#include "asset/asset_id.h"
#include "scene/light.h"
#include "scene/transform.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <rfl/Flatten.hpp>

enum class AssetType
{
    Mesh,
    Material,
    Texture,
    Shader,
    Scene,
};

[[nodiscard]] constexpr std::string_view assetTypeDir(AssetType type)
{
    switch (type)
    {
    case AssetType::Mesh:
        return "mesh";
    case AssetType::Material:
        return "material";
    case AssetType::Texture:
        return "texture";
    case AssetType::Shader:
        return "shader";
    case AssetType::Scene:
        return "scene";
    }
    return {};
}

struct AssetDesc
{
    AssetId id;
    std::string name;
    AssetType type{};
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
    rfl::Flatten<AssetDesc> asset{};
    MeshSourceDesc source;
    std::filesystem::path geometry;
    std::vector<SubmeshDesc> submeshes;
};

struct MaterialDesc
{
    rfl::Flatten<AssetDesc> asset{};
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
    rfl::Flatten<AssetDesc> asset{};
    std::string source;
    std::optional<std::filesystem::path> path;
    std::optional<std::array<uint8_t, 4>> rgba;
};

struct ShaderDesc
{
    rfl::Flatten<AssetDesc> asset{};
    std::filesystem::path binary;
};

struct SceneObjectDesc
{
    std::string name;
    AssetId meshId;
    Transform transform;
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
    rfl::Flatten<AssetDesc> asset{};
    int version = 2;
    SceneCameraDesc camera;
    std::vector<Light> lights;
    std::vector<SceneObjectDesc> objects;
};
