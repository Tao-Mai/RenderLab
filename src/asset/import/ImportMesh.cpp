#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include "asset/AssetImporter.h"

#include "asset/AssetManager.h"
#include "asset/AssetDataManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/MeshGeometry.h"
#include "asset/import/ImportId.h"
#include "asset/import/GltfImport.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

namespace
{
constexpr float kMinimumTangentLengthSquared = 1e-12f;

struct GltfBuild
{
    std::vector<MaterialAsset::ID>                              materialIds;
    std::map<std::pair<std::string, ColorSpace>, TextureAsset::ID> textureCache;
};

[[nodiscard]] std::optional<TextureBinding> bindTexture(std::optional<TextureAsset::ID> id)
{
    if (!id)
    {
        return std::nullopt;
    }
    return TextureBinding{*id, BuiltinAssets::Sampler::linearRepeat};
}

[[nodiscard]] std::optional<TextureAsset::ID> textureIdFor(
    GltfBuild&                   build,
    const tinygltf::Model&       model,
    int                          textureIndex,
    const std::filesystem::path& modelDirectory,
    ColorSpace                   colorSpace)
{
    if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size()))
    {
        return std::nullopt;
    }
    const int imageIndex = model.textures[textureIndex].source;
    if (imageIndex < 0 || imageIndex >= static_cast<int>(model.images.size()))
    {
        return std::nullopt;
    }

    const tinygltf::Image& image = model.images[imageIndex];
    std::string            sourcePath;
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
          "embedded glTF textures require external files: {}",
          sourcePath);
    const TextureAsset::ID id = AssetImporter::importTexture({
        .source = std::filesystem::absolute(sourcePath),
        .colorSpace = colorSpace,
    });
    build.textureCache.emplace(key, id);
    return id;
}

void registerMaterials(
    GltfBuild&                   build,
    const tinygltf::Model&       model,
    const std::filesystem::path& modelDirectory,
    const std::filesystem::path& meshSource)
{
    PbrMaterialAsset defaultMaterial;
    defaultMaterial.id               = asset_import::nextId<MaterialAsset>(meshSource.stem().string());
    defaultMaterial.baseColorTexture = TextureBinding{
        BuiltinAssets::Texture::white, BuiltinAssets::Sampler::linearRepeat};
    build.materialIds.push_back(
        context().assetManager->save(std::move(defaultMaterial)));

    for (size_t index = 0; index < model.materials.size(); ++index)
    {
        const tinygltf::Material& source = model.materials[index];
        PbrMaterialAsset              material;
        material.id = asset_import::nextId<MaterialAsset>(meshSource.stem().string());

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
        material.metallic  = static_cast<float>(pbr.metallicFactor);
        material.roughness = static_cast<float>(pbr.roughnessFactor);
        if (source.emissiveFactor.size() == 3)
        {
            material.emissive = {
                static_cast<float>(source.emissiveFactor[0]),
                static_cast<float>(source.emissiveFactor[1]),
                static_cast<float>(source.emissiveFactor[2]),
            };
        }
        material.ao               = static_cast<float>(source.occlusionTexture.strength);
        material.normalScale      = static_cast<float>(source.normalTexture.scale);
        material.baseColorTexture = TextureBinding{
            textureIdFor(build, model, pbr.baseColorTexture.index,
                         modelDirectory, ColorSpace::Srgb)
                .value_or(BuiltinAssets::Texture::white),
            BuiltinAssets::Sampler::linearRepeat};
        material.normalTexture = bindTexture(textureIdFor(
            build, model, source.normalTexture.index,
            modelDirectory, ColorSpace::Linear));
        CHECK(pbr.metallicRoughnessTexture.index < 0 || pbr.metallicRoughnessTexture.texCoord == 0,
              "metallic-roughness textures require TEXCOORD_0: {}", meshSource.string());
        material.metallicTexture = bindTexture(textureIdFor(
            build, model, pbr.metallicRoughnessTexture.index,
            modelDirectory, ColorSpace::Linear));
        material.roughnessTexture = material.metallicTexture;
        material.aoTexture = bindTexture(textureIdFor(
            build, model, source.occlusionTexture.index,
            modelDirectory, ColorSpace::Linear));
        material.emissiveTexture = bindTexture(textureIdFor(
            build, model, source.emissiveTexture.index,
            modelDirectory, ColorSpace::Srgb));
        material.alphaMode   = source.alphaMode;
        material.alphaCutoff = static_cast<float>(source.alphaCutoff);
        material.doubleSided = source.doubleSided;

        if (const auto extension = source.extensions.find("KHR_materials_clearcoat");
            extension != source.extensions.end())
        {
            const auto& value = extension->second;
            CHECK(value.IsObject(), "invalid clearcoat material in '{}'", meshSource.string());
            const auto readFactor = [&](const char* name, float defaultValue, float maximum)
            {
                if (!value.Has(name))
                    return defaultValue;
                const auto& field = value.Get(name);
                CHECK(field.IsNumber(), "invalid clearcoat '{}' in '{}'", name, meshSource.string());
                const float result = static_cast<float>(field.GetNumberAsDouble());
                CHECK(std::isfinite(result) && result >= 0.0f && result <= maximum,
                      "invalid clearcoat '{}' value {} in '{}'", name, result, meshSource.string());
                return result;
            };
            const auto readTexture = [&](const char* name)
            {
                if (!value.Has(name))
                    return std::optional<TextureBinding>{};
                const auto& info = value.Get(name);
                CHECK(info.IsObject() && info.Has("index") && info.Get("index").IsInt(),
                      "invalid clearcoat texture '{}' in '{}'", name, meshSource.string());
                const int index = info.Get("index").GetNumberAsInt();
                CHECK(index >= 0 && index < static_cast<int>(model.textures.size()),
                      "invalid clearcoat texture index {} in '{}'", index, meshSource.string());
                CHECK(!info.Has("texCoord") || (info.Get("texCoord").IsInt() &&
                      info.Get("texCoord").GetNumberAsInt() == 0),
                      "clearcoat textures require TEXCOORD_0: {}", meshSource.string());
                return bindTexture(textureIdFor(build, model, index, modelDirectory, ColorSpace::Linear));
            };

            PbrMaterialAsset::Clearcoat clearcoat{
                .factor = readFactor("clearcoatFactor", 0.0f, 1.0f),
                .roughnessFactor = readFactor("clearcoatRoughnessFactor", 0.0f, 1.0f),
                .texture = readTexture("clearcoatTexture"),
                .roughnessTexture = readTexture("clearcoatRoughnessTexture"),
                .normalTexture = readTexture("clearcoatNormalTexture"),
            };
            if (value.Has("clearcoatNormalTexture"))
            {
                const auto& normal = value.Get("clearcoatNormalTexture");
                if (normal.Has("scale"))
                {
                    CHECK(normal.Get("scale").IsNumber());
                    clearcoat.normalScale = static_cast<float>(normal.Get("scale").GetNumberAsDouble());
                    CHECK(std::isfinite(clearcoat.normalScale) && clearcoat.normalScale >= 0.0f);
                }
            }
            material.clearcoat = std::move(clearcoat);
        }
        build.materialIds.push_back(
            context().assetManager->save(std::move(material)));
    }
}

const unsigned char* accessorData(
    const tinygltf::Model&    model,
    const tinygltf::Accessor& accessor,
    size_t&                   stride)
{
    CHECK(accessor.bufferView >= 0 && !accessor.sparse.isSparse,
          "sparse or missing glTF accessor is not supported");

    const auto& view       = model.bufferViews.at(accessor.bufferView);
    const auto& buffer     = model.buffers.at(view.buffer);
    const int   byteStride = accessor.ByteStride(view);
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
    const tinygltf::Model&    model,
    const tinygltf::Accessor& accessor,
    size_t                    index)
{
    CHECK(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
          accessor.type == TINYGLTF_TYPE_VEC3,
          "glTF VEC3 attribute must use float components");
    size_t               stride = 0;
    const unsigned char* data   = accessorData(model, accessor, stride) + index * stride;
    return {readFloat(data), readFloat(data + 4), readFloat(data + 8)};
}

glm::vec4 readVec4(
    const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t index)
{
    CHECK(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
          accessor.type == TINYGLTF_TYPE_VEC4,
          "glTF VEC4 attribute must use float components");
    size_t stride = 0;
    const unsigned char* data = accessorData(model, accessor, stride) + index * stride;
    return {readFloat(data), readFloat(data + 4), readFloat(data + 8), readFloat(data + 12)};
}

glm::vec2 readVec2(
    const tinygltf::Model&    model,
    const tinygltf::Accessor& accessor,
    size_t                    index)
{
    CHECK(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
          accessor.type == TINYGLTF_TYPE_VEC2,
          "glTF VEC2 attribute must use float components");
    size_t               stride = 0;
    const unsigned char* data   = accessorData(model, accessor, stride) + index * stride;
    return {readFloat(data), readFloat(data + 4)};
}

uint32_t readIndex(
    const tinygltf::Model&    model,
    const tinygltf::Accessor& accessor,
    size_t                    index)
{
    CHECK(accessor.type == TINYGLTF_TYPE_SCALAR, "glTF index accessor must be scalar");

    size_t               stride = 0;
    const unsigned char* data   = accessorData(model, accessor, stride) + index * stride;
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
    matrix           *= glm::mat4_cast(rotation);
    matrix           = glm::scale(matrix, scale);
    return matrix;
}

void appendPrimitive(
    AssetImporter::PreparedMesh& build,
    const tinygltf::Model&     model,
    const tinygltf::Primitive& primitive,
    const glm::mat4&           transform)
{
    if (primitive.mode != -1 && primitive.mode != TINYGLTF_MODE_TRIANGLES)
    {
        return;
    }

    const auto positionIt = primitive.attributes.find("POSITION");
    CHECK(positionIt != primitive.attributes.end(),
          "glTF primitive has no POSITION attribute");

    const auto&               positions = model.accessors.at(positionIt->second);
    const auto                normalIt  = primitive.attributes.find("NORMAL");
    const auto                uvIt      = primitive.attributes.find("TEXCOORD_0");
    const auto                tangentIt = primitive.attributes.find("TANGENT");
    const tinygltf::Accessor* normals   = normalIt == primitive.attributes.end()
        ? nullptr
        : &model.accessors.at(normalIt->second);
    const tinygltf::Accessor* uvs = uvIt == primitive.attributes.end()
        ? nullptr
        : &model.accessors.at(uvIt->second);
    const tinygltf::Accessor* tangents = tangentIt == primitive.attributes.end()
        ? nullptr
        : &model.accessors.at(tangentIt->second);
    CHECK((!normals || normals->count == positions.count) &&
          (!uvs || uvs->count == positions.count) &&
          (!tangents || tangents->count == positions.count),
          "glTF vertex attribute counts do not match");
    CHECK(primitive.material >= -1 && primitive.material < static_cast<int>(model.materials.size()),
          "glTF primitive references missing material");
    if (primitive.material >= 0 && model.materials[primitive.material].normalTexture.index >= 0)
    {
        CHECK(uvs && model.materials[primitive.material].normalTexture.texCoord == 0,
              "normal-mapped glTF primitives require TEXCOORD_0");
    }

    const uint32_t  firstVertex  = static_cast<uint32_t>(build.geometry.vertices.size());
    const uint32_t  firstIndex   = static_cast<uint32_t>(build.geometry.indices.size());
    bool regenerateTangents = !tangents || !normals;
    build.geometry.vertices.reserve(build.geometry.vertices.size() + positions.count);
    for (size_t index = 0; index < positions.count; ++index)
    {
        const glm::vec3 position = readVec3(model, positions, index);
        const glm::vec3 normal = normals
            ? glm::normalize(readVec3(model, *normals, index))
            : glm::vec3{0.0f, 0.0f, 1.0f};
        const glm::vec2 uv = uvs ? readVec2(model, *uvs, index) : glm::vec2{0.0f};
        // Unreferenced vertices are not visited by MikkTSpace; their initial frame must still be valid.
        const glm::vec3 axis = std::abs(normal.z) < 0.999f ? glm::vec3{0, 0, 1} : glm::vec3{0, 1, 0};
        const glm::vec4 defaultTangent{glm::normalize(glm::cross(axis, normal)), 1.0f};
        glm::vec4 tangent = tangents ? readVec4(model, *tangents, index) : defaultTangent;
        const glm::vec3 projected = glm::vec3{tangent} - normal * glm::dot(normal, glm::vec3{tangent});
        const float tangentLengthSquared = glm::dot(projected, projected);
        if (!std::isfinite(tangentLengthSquared) || tangentLengthSquared <= kMinimumTangentLengthSquared ||
            (tangent.w != 1.0f && tangent.w != -1.0f))
        {
            regenerateTangents = true;
            tangent = defaultTangent;
        }
        build.geometry.vertices.push_back({position, normal, uv, tangent});
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

    const uint32_t indexCount = static_cast<uint32_t>(build.geometry.indices.size()) - firstIndex;
    CHECK(indexCount % 3 == 0);
    if (!normals)
    {
        // glTF primitives without NORMAL use flat shading. Separate corners
        // before generating tangents so adjacent faces cannot share normals.
        std::vector<Vertex> corners;
        corners.reserve(indexCount);
        for (uint32_t index = firstIndex; index < firstIndex + indexCount; index += 3)
        {
            Vertex a = build.geometry.vertices[build.geometry.indices[index]];
            Vertex b = build.geometry.vertices[build.geometry.indices[index + 1]];
            Vertex c = build.geometry.vertices[build.geometry.indices[index + 2]];
            const glm::vec3 faceNormal = glm::cross(b.position - a.position, c.position - a.position);
            CHECK(glm::dot(faceNormal, faceNormal) > 0.0f);
            a.normal = b.normal = c.normal = glm::normalize(faceNormal);
            corners.insert(corners.end(), {a, b, c});
        }
        build.geometry.vertices.resize(firstVertex);
        build.geometry.vertices.insert(build.geometry.vertices.end(), corners.begin(), corners.end());
        for (uint32_t corner = 0; corner < indexCount; ++corner)
            build.geometry.indices[firstIndex + corner] = firstVertex + corner;
    }
    if (regenerateTangents)
    {
        if (tangents && normals)
            LOG_WARNING(
                "Regenerating degenerate glTF tangents at primitive firstIndex {}: {}",
                firstIndex,
                build.source.string()
            );
        generateTangents(build.geometry, firstIndex, indexCount);
    }

    // Tangents are generated in the source mesh's space, matching its bake.
    // Bake node transforms only afterwards, including the handedness change.
    const glm::mat3 linearTransform{transform};
    const float determinant = glm::determinant(linearTransform);
    CHECK(std::isfinite(determinant) && determinant != 0.0f);
    const glm::mat3 normalMatrix = glm::inverseTranspose(linearTransform);
    const float handedness = determinant < 0.0f ? -1.0f : 1.0f;
    for (size_t index = firstVertex; index < build.geometry.vertices.size(); ++index)
    {
        auto& vertex = build.geometry.vertices[index];
        vertex.position = glm::vec3(transform * glm::vec4(vertex.position, 1.0f));
        vertex.normal = glm::normalize(normalMatrix * vertex.normal);
        glm::vec3 tangent = linearTransform * glm::vec3(vertex.tangent);
        tangent -= vertex.normal * glm::dot(vertex.normal, tangent);
        CHECK(glm::dot(tangent, tangent) > 0.0f,
              "invalid tangent at vertex {}, primitive firstIndex {}", index - firstVertex, firstIndex);
        vertex.tangent = glm::vec4(glm::normalize(tangent), vertex.tangent.w * handedness);
    }

    build.submeshes.push_back({
        .firstIndex = firstIndex,
        .indexCount = indexCount,
        .materialIndex = primitive.material,
    });
}

void appendMesh(
    AssetImporter::PreparedMesh& build,
    const tinygltf::Model& model,
    int                    meshIndex,
    const glm::mat4&       transform)
{
    const auto& mesh = model.meshes.at(meshIndex);
    for (const auto& primitive : mesh.primitives)
    {
        appendPrimitive(build, model, primitive, transform);
    }
}
}

MeshAsset::ID AssetImporter::importMesh(const ImportMeshSetting& setting)
{
    CHECK(std::isfinite(setting.importScale) && setting.importScale > 0.0f);
    GltfImport gltf{setting.source};
    return gltf.saveMesh(gltf.prepareMesh(), setting);
}

GltfImport::GltfImport(const std::filesystem::path& path)
{
    CHECK(!path.empty(), "glTF source path is empty");
    const auto start = std::chrono::steady_clock::now();
    LOG_INFO("Import glTF '{}' started", path.string());

    tinygltf::TinyGLTF loader;
    source = std::filesystem::absolute(path).lexically_normal();
    this->model = std::make_shared<tinygltf::Model>();
    auto& model = *this->model;
    std::string warning;
    std::string error;

    std::string extension = path.extension().string();
    std::ranges::transform(
        extension,
        extension.begin(),
        [](unsigned char character) { return std::tolower(character); }
    );
    CHECK(extension == ".gltf" || extension == ".glb", "unsupported mesh source: {}", path.string());
    const bool loaded = extension == ".glb" ? loader.LoadBinaryFromFile(&model, &error, &warning, path.string())
                                            : loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
    if (!warning.empty())
    {
        LOG_WARNING("glTF '{}': {}", path.string(), warning);
    }
    CHECK(loaded, "failed to load glTF '{}': {}", path.string(), error);
    LOG_INFO(
        "glTF parsed: {} meshes, {} materials, {} textures",
        model.meshes.size(),
        model.materials.size(),
        model.textures.size()
    );
    LOG_INFO("glTF parsed in {:.1f}s", std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
}

GltfImport::GltfImport(const AssetImporter::PreparedMesh& prepared)
    : source(prepared.source), model(prepared.model)
{
    DCHECK(model);
}

AssetImporter::PreparedMesh AssetImporter::prepareMesh(
    const std::filesystem::path& path, std::optional<uint32_t> meshIndex)
{
    return GltfImport{path}.prepareMesh(meshIndex);
}

void GltfImport::visitNodes(
    const std::function<void(uint32_t, const glm::mat4&)>& visitor, float importScale) const
{
    CHECK(std::isfinite(importScale) && importScale > 0.0f);
    CHECK(!model->scenes.empty(), "glTF has no scene: {}", source.string());
    const int sceneIndex = model->defaultScene >= 0 ? model->defaultScene : 0;
    CHECK(static_cast<size_t>(sceneIndex) < model->scenes.size());

    std::vector<bool> visited(model->nodes.size());
    const auto visit = [&](this auto&& self, int nodeIndex, const glm::mat4& parent) -> void
    {
        CHECK(nodeIndex >= 0 && static_cast<size_t>(nodeIndex) < model->nodes.size());
        CHECK(!visited[nodeIndex], "glTF scene contains a cycle or duplicate node {}: {}", nodeIndex, source.string());
        visited[nodeIndex] = true;

        const auto& node = model->nodes[nodeIndex];
        const glm::mat4 world = parent * nodeTransform(node);
        visitor(static_cast<uint32_t>(nodeIndex), world);
        for (int child : node.children)
            self(child, world);
    };

    const glm::mat4 root = glm::scale(glm::mat4{1.0f}, glm::vec3{importScale});
    for (int node : model->scenes[sceneIndex].nodes)
        visit(node, root);
}

AssetImporter::PreparedMesh GltfImport::prepareMesh(std::optional<uint32_t> meshIndex) const
{
    const auto start = std::chrono::steady_clock::now();
    const auto& path = source;
    const auto& model = *this->model;
    AssetImporter::PreparedMesh build;
    build.source = source;
    build.model = this->model;
    build.meshIndex = meshIndex;

    if (meshIndex)
    {
        CHECK(*meshIndex < model.meshes.size());
        appendMesh(build, model, static_cast<int>(*meshIndex), glm::mat4{1.0f});
    }
    else if (!model.scenes.empty())
    {
        visitNodes([&](uint32_t nodeIndex, const glm::mat4& transform)
        {
            const auto& node = model.nodes[nodeIndex];
            if (node.mesh >= 0)
                appendMesh(build, model, node.mesh, transform);
        });
    }
    else
    {
        for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex)
        {
            appendMesh(build, model, static_cast<int>(meshIndex), glm::mat4{1.0f});
        }
    }

    CHECK(
        !build.geometry.vertices.empty() && !build.geometry.indices.empty(),
        "glTF contains no triangle mesh data: {}",
        path.string()
    );
    LOG_INFO(
        "Mesh geometry ready: {} vertices, {} indices, {} submeshes",
        build.geometry.vertices.size(),
        build.geometry.indices.size(),
        build.submeshes.size()
    );

    build.boundsMin = build.boundsMax = build.geometry.vertices.front().position;
    for (const auto& vertex : build.geometry.vertices)
    {
        CHECK(
            std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) && std::isfinite(vertex.position.z),
            "non-finite glTF position: {}",
            path.string()
        );
        build.boundsMin = glm::min(build.boundsMin, vertex.position);
        build.boundsMax = glm::max(build.boundsMax, vertex.position);
    }
    LOG_INFO(
        "Mesh preview prepared in {:.1f}s",
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
    );
    return build;
}

MeshAsset::ID AssetImporter::saveMesh(PreparedMesh&& prepared, const ImportMeshSetting& setting)
{
    GltfImport gltf{prepared};
    return gltf.saveMesh(std::move(prepared), setting);
}

MeshAsset::ID GltfImport::saveMesh(AssetImporter::PreparedMesh&& prepared, const ImportMeshSetting& setting)
{
    const float importScale = setting.importScale;
    CHECK(std::isfinite(importScale) && importScale > 0.0f);
    DCHECK(prepared.model == model);
    const auto          start = std::chrono::steady_clock::now();
    const auto&         path  = prepared.source;
    const MeshAsset::ID meshId =
        asset_import::nextId<MeshAsset>(setting.id.empty() ? path.stem().string() : setting.id.value);
    if (materialIds.empty())
    {
        GltfBuild materials;
        registerMaterials(materials, *model, path.parent_path(), path);
        materialIds = std::move(materials.materialIds);
    }

    for (auto& vertex : prepared.geometry.vertices)
    {
        vertex.position *= importScale;
        CHECK(
            std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) && std::isfinite(vertex.position.z),
            "scaled mesh position overflows: {}",
            path.string()
        );
    }

    MeshAsset mesh;
    mesh.id          = meshId;
    mesh.source      = Source::File;
    mesh.path        = asset_import::sourcePathForAsset(path);
    mesh.meshIndex   = prepared.meshIndex;
    mesh.importScale = importScale;
    mesh.geometry    = context().assetDataManager->writeGeometry(meshId, prepared.geometry);
    mesh.submeshes.reserve(prepared.submeshes.size());
    for (const auto& submesh : prepared.submeshes)
    {
        const auto slot = static_cast<size_t>(submesh.materialIndex + 1);
        CHECK(slot < materialIds.size());
        mesh.submeshes.push_back({submesh.firstIndex, submesh.indexCount, materialIds[slot]});
    }
    const MeshAsset::ID id = context().assetManager->save<MeshAsset>(std::move(mesh));
    LOG_INFO(
        "Import mesh '{}' completed as '{}' in {:.1f}s",
        path.string(),
        id.value,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
    );
    return id;
}
