#pragma once

#include "asset/Asset.h"
#include "asset/ImageFormat.h"
#include "ecs/Camera.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/meta/meta.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

REGISTER_ASSETS(Texture, Mesh, Material, Shader, Scene, EnvironmentMap)

enum class Source
{
    File,
    Builtin
};

struct Mesh::Desc
{
    struct Submesh
    {
        uint32_t     firstIndex = 0;
        uint32_t     indexCount = 0;
        Material::ID materialId;
    };

    ID                    id;
    Source                source;
    std::filesystem::path geometry;
    std::vector<Submesh>  submeshes;
};

struct Texture::Desc
{
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

struct Material::Desc
{
    ID                         id;
    std::optional<Shader::ID>  shaderId;
    std::optional<glm::vec4>   baseColorFactor;
    std::optional<float>       metallic;
    std::optional<float>       roughness;
    std::optional<float>       ao;
    std::optional<glm::vec3>   emissive;
    std::optional<float>       normalScale;
    std::optional<Texture::ID> baseColorTexture;
    std::optional<Texture::ID> normalTexture;
    std::optional<Texture::ID> metallicTexture;
    std::optional<Texture::ID> roughnessTexture;
    std::optional<Texture::ID> aoTexture;
    std::optional<Texture::ID> emissiveTexture;
    std::optional<std::string> alphaMode;
    std::optional<float>       alphaCutoff;
    std::optional<bool>        doubleSided;
};

struct EnvironmentMap::Desc
{
    ID          id;
    Texture::ID radiance;
    Texture::ID irradiance;
    Texture::ID prefilteredSpecular;
};

struct Shader::Desc
{
    ID                    id;
    std::filesystem::path binary;
};

struct Scene::Desc
{
    struct Object
    {
        std::string                                     name;
        std::unordered_map<std::string, entt::meta_any> components;
    };

    ID                  id;
    ecs::Camera         camera;
    std::vector<Object> objects;

    struct Environment
    {
        std::optional<EnvironmentMap::ID> environmentMap;
    } environment;
};
