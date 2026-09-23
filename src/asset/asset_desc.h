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


enum class AssetType
{
    mesh,
    material,
    texture,
    shader,
    scene,
};

template <class T>
inline constexpr AssetType assetTypeOf = AssetType{};

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
};

template <>
inline constexpr AssetType assetTypeOf<MeshDesc> = AssetType::mesh;

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

template <>
inline constexpr AssetType assetTypeOf<MaterialDesc> = AssetType::material;

struct TextureDesc
{
    AssetId id;
    std::string source;
    std::optional<std::filesystem::path> path;
    std::optional<std::array<uint8_t, 4>> rgba;
};

template <>
inline constexpr AssetType assetTypeOf<TextureDesc> = AssetType::texture;

struct ShaderDesc
{
    AssetId id;
    std::filesystem::path binary;
};

template <>
inline constexpr AssetType assetTypeOf<ShaderDesc> = AssetType::shader;

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
    AssetId id;
    int version = 3;
    SceneCameraDesc camera;
    std::vector<Light> lights;
    std::vector<SceneObjectDesc> objects;
};

template <>
inline constexpr AssetType assetTypeOf<SceneDesc> = AssetType::scene;

