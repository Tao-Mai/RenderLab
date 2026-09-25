#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include "asset/AssetImporter.h"

#include "asset/AssetDescManager.h"
#include "asset/AssetDataManager.h"
#include "asset/MeshGeometry.h"
#include "asset/import/ImportId.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

namespace
{
struct GltfBuild
{
    MeshGeometry geometry;
    std::vector<SubmeshDesc> submeshes;
    std::vector<AssetId> materialIds;
    std::map<std::pair<std::string, ColorSpace>, AssetId> textureCache;
};

[[nodiscard]] AssetId textureIdFor(
    GltfBuild& build,
    const tinygltf::Model& model,
    int textureIndex,
    const std::filesystem::path& modelDirectory,
    ColorSpace colorSpace)
{
    if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size()))
    {
        return kInvalidAssetId;
    }
    const int imageIndex = model.textures[textureIndex].source;
    if (imageIndex < 0 || imageIndex >= static_cast<int>(model.images.size()))
    {
        return kInvalidAssetId;
    }

    const tinygltf::Image& image = model.images[imageIndex];
    std::string sourcePath;
    if (image.uri.empty())
    {
        sourcePath = "embedded://image/" + std::to_string(imageIndex);
    }
    else if (image.uri.starts_with("data:"))
    {
        sourcePath = image.uri;
    }
    else
    {
        sourcePath = (modelDirectory / image.uri).lexically_normal().string();
    }

    const auto key = std::pair{sourcePath, colorSpace};
    if (const auto found = build.textureCache.find(key); found != build.textureCache.end())
    {
        return found->second;
    }

    CHECK(!sourcePath.starts_with("data:") && !sourcePath.starts_with("embedded:"),
        "embedded glTF textures require external files: {}", sourcePath);
    (void)asset_import::sourceRelativeToAssets(sourcePath);

    const AssetId id = AssetImporter::importTexture(
        std::filesystem::absolute(sourcePath), colorSpace);
    build.textureCache.emplace(key, id);
    return id;
}

void registerMaterials(
    GltfBuild& build,
    const tinygltf::Model& model,
    const std::filesystem::path& modelDirectory,
    const std::filesystem::path& meshSource)
{
    MaterialDesc defaultMaterial;
    defaultMaterial.id = asset_import::nextId<MaterialDesc>(meshSource);
    defaultMaterial.baseColorTexture = TextureDesc::white;
    build.materialIds.push_back(
        context().assetManager->save(std::move(defaultMaterial)));

    for (size_t index = 0; index < model.materials.size(); ++index)
    {
        const tinygltf::Material& source = model.materials[index];
        MaterialDesc material;
        material.id = asset_import::nextId<MaterialDesc>(meshSource);

        const auto& pbr = source.pbrMetallicRoughness;
        if (pbr.baseColorFactor.size() == 4)
        {
            material.baseColorFactor = {
                static_cast<float>(pbr.baseColorFactor[0]),
                static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]),
                static_cast<float>(pbr.baseColorFactor[3]),
            };
        }
        material.metallic = static_cast<float>(pbr.metallicFactor);
        material.roughness = static_cast<float>(pbr.roughnessFactor);
        if (source.emissiveFactor.size() == 3)
        {
            material.emissive = {
                static_cast<float>(source.emissiveFactor[0]),
                static_cast<float>(source.emissiveFactor[1]),
                static_cast<float>(source.emissiveFactor[2]),
            };
        }
        material.ao = static_cast<float>(source.occlusionTexture.strength);
        material.normalScale = static_cast<float>(source.normalTexture.scale);
        material.baseColorTexture =
            textureIdFor(build, model, pbr.baseColorTexture.index, modelDirectory,
                         ColorSpace::Srgb);
        material.normalTexture =
            textureIdFor(build, model, source.normalTexture.index, modelDirectory,
                         ColorSpace::Linear);
        const AssetId metallicRoughness =
            textureIdFor(build, model, pbr.metallicRoughnessTexture.index, modelDirectory,
                         ColorSpace::Linear);
        material.metallicTexture = metallicRoughness;
        material.roughnessTexture = metallicRoughness;
        material.aoTexture =
            textureIdFor(build, model, source.occlusionTexture.index, modelDirectory,
                         ColorSpace::Linear);
        material.emissiveTexture =
            textureIdFor(build, model, source.emissiveTexture.index, modelDirectory,
                         ColorSpace::Srgb);
        material.alphaMode = source.alphaMode;
        material.alphaCutoff = static_cast<float>(source.alphaCutoff);
        material.doubleSided = source.doubleSided;
        build.materialIds.push_back(
            context().assetManager->save(std::move(material)));
    }
}

const unsigned char* accessorData(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    size_t& stride)
{
    CHECK(accessor.bufferView >= 0 && !accessor.sparse.isSparse,
        "sparse or missing glTF accessor is not supported");

    const auto& view = model.bufferViews.at(accessor.bufferView);
    const auto& buffer = model.buffers.at(view.buffer);
    const int byteStride = accessor.ByteStride(view);
    CHECK(byteStride > 0, "invalid glTF accessor stride");
    stride = static_cast<size_t>(byteStride);

    const size_t offset = view.byteOffset + accessor.byteOffset;
    CHECK(offset < buffer.data.size(), "glTF accessor points outside its buffer");
    return buffer.data.data() + offset;
}

float readFloat(const unsigned char* data)
{
    float value = 0.0f;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

glm::vec3 readVec3(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    size_t index)
{
    CHECK(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
          accessor.type == TINYGLTF_TYPE_VEC3,
        "glTF VEC3 attribute must use float components");
    size_t stride = 0;
    const unsigned char* data = accessorData(model, accessor, stride) + index * stride;
    return {readFloat(data), readFloat(data + 4), readFloat(data + 8)};
}

glm::vec2 readVec2(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    size_t index)
{
    CHECK(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
          accessor.type == TINYGLTF_TYPE_VEC2,
        "glTF VEC2 attribute must use float components");
    size_t stride = 0;
    const unsigned char* data = accessorData(model, accessor, stride) + index * stride;
    return {readFloat(data), readFloat(data + 4)};
}

uint32_t readIndex(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    size_t index)
{
    CHECK(accessor.type == TINYGLTF_TYPE_SCALAR, "glTF index accessor must be scalar");

    size_t stride = 0;
    const unsigned char* data = accessorData(model, accessor, stride) + index * stride;
    switch (accessor.componentType)
    {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return *data;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
    {
        uint16_t value = 0;
        std::memcpy(&value, data, sizeof(value));
        return value;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    {
        uint32_t value = 0;
        std::memcpy(&value, data, sizeof(value));
        return value;
    }
    default:
        LOG_FATAL("unsupported glTF index component type");
    }
}

glm::mat4 nodeTransform(const tinygltf::Node& node)
{
    if (node.matrix.size() == 16)
    {
        glm::mat4 matrix{1.0f};
        for (size_t column = 0; column < 4; ++column)
        {
            for (size_t row = 0; row < 4; ++row)
            {
                matrix[column][row] =
                    static_cast<float>(node.matrix[column * 4 + row]);
            }
        }
        return matrix;
    }

    glm::vec3 translation{0.0f};
    glm::vec3 scale{1.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    if (node.translation.size() == 3)
    {
        translation = {node.translation[0], node.translation[1], node.translation[2]};
    }
    if (node.scale.size() == 3)
    {
        scale = {node.scale[0], node.scale[1], node.scale[2]};
    }
    if (node.rotation.size() == 4)
    {
        rotation = glm::quat{
            static_cast<float>(node.rotation[3]),
            static_cast<float>(node.rotation[0]),
            static_cast<float>(node.rotation[1]),
            static_cast<float>(node.rotation[2]),
        };
    }

    glm::mat4 matrix = glm::translate(glm::mat4{1.0f}, translation);
    matrix *= glm::mat4_cast(rotation);
    matrix = glm::scale(matrix, scale);
    return matrix;
}

void appendPrimitive(
    GltfBuild& build,
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive,
    const glm::mat4& transform)
{
    if (primitive.mode != -1 && primitive.mode != TINYGLTF_MODE_TRIANGLES)
    {
        return;
    }

    const auto positionIt = primitive.attributes.find("POSITION");
    CHECK(positionIt != primitive.attributes.end(),
        "glTF primitive has no POSITION attribute");

    const auto& positions = model.accessors.at(positionIt->second);
    const auto normalIt = primitive.attributes.find("NORMAL");
    const auto uvIt = primitive.attributes.find("TEXCOORD_0");
    const tinygltf::Accessor* normals = normalIt == primitive.attributes.end()
        ? nullptr
        : &model.accessors.at(normalIt->second);
    const tinygltf::Accessor* uvs = uvIt == primitive.attributes.end()
        ? nullptr
        : &model.accessors.at(uvIt->second);
    CHECK((!normals || normals->count == positions.count) &&
          (!uvs || uvs->count == positions.count),
        "glTF vertex attribute counts do not match");

    const uint32_t firstVertex = static_cast<uint32_t>(build.geometry.vertices.size());
    const uint32_t firstIndex = static_cast<uint32_t>(build.geometry.indices.size());
    const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(transform));
    build.geometry.vertices.reserve(build.geometry.vertices.size() + positions.count);
    for (size_t index = 0; index < positions.count; ++index)
    {
        const glm::vec3 position = glm::vec3(transform *
            glm::vec4(readVec3(model, positions, index), 1.0f));
        glm::vec3 normal = normals
            ? glm::normalize(normalMatrix * readVec3(model, *normals, index))
            : glm::vec3{0.0f, 0.0f, 1.0f};
        const glm::vec2 uv = uvs ? readVec2(model, *uvs, index) : glm::vec2{0.0f};
        build.geometry.vertices.push_back({position, normal, uv});
    }

    if (primitive.indices >= 0)
    {
        const auto& indices = model.accessors.at(primitive.indices);
        build.geometry.indices.reserve(build.geometry.indices.size() + indices.count);
        for (size_t index = 0; index < indices.count; ++index)
        {
            const uint32_t localIndex = readIndex(model, indices, index);
            CHECK(localIndex < positions.count,
                "glTF index is outside the vertex accessor");
            build.geometry.indices.push_back(firstVertex + localIndex);
        }
    }
    else
    {
        for (uint32_t index = 0; index < positions.count; ++index)
        {
            build.geometry.indices.push_back(firstVertex + index);
        }
    }

    uint32_t materialSlot = 0;
    if (primitive.material >= 0 &&
        primitive.material < static_cast<int>(model.materials.size()))
    {
        materialSlot = static_cast<uint32_t>(primitive.material) + 1;
    }
    CHECK(materialSlot < build.materialIds.size(),
        "glTF primitive references missing material");
    build.submeshes.push_back({
        .firstIndex = firstIndex,
        .indexCount = static_cast<uint32_t>(build.geometry.indices.size()) - firstIndex,
        .materialId = build.materialIds[materialSlot],
    });
}

void appendMesh(
    GltfBuild& build,
    const tinygltf::Model& model,
    int meshIndex,
    const glm::mat4& transform)
{
    const auto& mesh = model.meshes.at(meshIndex);
    for (const auto& primitive : mesh.primitives)
    {
        appendPrimitive(build, model, primitive, transform);
    }
}
}

AssetId AssetImporter::importMesh(const std::filesystem::path& path)
{
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string warning;
    std::string error;

    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
        [](unsigned char character) { return std::tolower(character); });
    CHECK(extension == ".gltf" || extension == ".glb",
        "unsupported mesh source: {}", path.string());
    const bool loaded = extension == ".glb"
        ? loader.LoadBinaryFromFile(&model, &error, &warning, path.string())
        : loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
    if (!warning.empty())
    {
        std::cerr << "TinyGLTF warning: " << warning << '\n';
    }
    CHECK(loaded, "failed to load glTF '{}': {}", path.string(), error);

    const AssetId meshId = asset_import::nextId<MeshDesc>(path);
    GltfBuild build;
    registerMaterials(build, model, path.parent_path(), path);

    std::function<void(int, const glm::mat4&)> visitNode;
    visitNode = [&](int nodeIndex, const glm::mat4& parentTransform)
    {
        const auto& node = model.nodes.at(nodeIndex);
        const glm::mat4 transform = parentTransform * nodeTransform(node);
        if (node.mesh >= 0)
        {
            appendMesh(build, model, node.mesh, transform);
        }
        for (const int child : node.children)
        {
            visitNode(child, transform);
        }
    };

    if (!model.scenes.empty())
    {
        const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
        for (const int node : model.scenes.at(sceneIndex).nodes)
        {
            visitNode(node, glm::mat4{1.0f});
        }
    }
    else
    {
        for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex)
        {
            appendMesh(build, model, static_cast<int>(meshIndex), glm::mat4{1.0f});
        }
    }

    CHECK(!build.geometry.vertices.empty() && !build.geometry.indices.empty(),
        "glTF contains no triangle mesh data: {}", path.string());

    MeshDesc mesh;
    mesh.id = meshId;
    mesh.geometry = context().assetDataManager->writeGeometry(meshId, build.geometry);
    mesh.submeshes = std::move(build.submeshes);
    return context().assetManager->save(std::move(mesh));
}
