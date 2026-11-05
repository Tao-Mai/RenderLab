#include <tiny_gltf.h>

#include "asset/AssetImporter.h"

#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/MeshGeometry.h"
#include "asset/import/ImportId.h"
#include "asset/import/GltfImport.h"
#include "asset/import/GltfMaterialExtensions.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <limits>
#include <optional>
#include <string>
#include <vector>

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
    const auto& texture = model.textures[textureIndex];
    int imageIndex = texture.source;
    if (const auto dds = texture.extensions.find("MSFT_texture_dds"); dds != texture.extensions.end())
    {
        const auto& extension = dds->second;
        CHECK(extension.IsObject() && extension.Has("source") && extension.Get("source").IsInt(),
              "invalid MSFT_texture_dds source on texture {} in '{}'", textureIndex, modelDirectory.string());
        imageIndex = extension.Get("source").GetNumberAsInt();
        CHECK(imageIndex >= 0 && static_cast<size_t>(imageIndex) < model.images.size(),
              "MSFT_texture_dds image index {} is out of range in '{}'", imageIndex, modelDirectory.string());
    }
    else if (imageIndex < 0 || static_cast<size_t>(imageIndex) >= model.images.size())
    {
        return std::nullopt;
    }

    const auto& image = model.images[imageIndex];
    CHECK(!image.uri.empty() && !image.uri.starts_with("data:"),
          "glTF image {} in '{}' requires an external file", imageIndex, modelDirectory.string());
    std::string decodedUri;
    CHECK(tinygltf::URIDecode(image.uri, &decodedUri, nullptr), "invalid glTF image URI '{}'", image.uri);
    const auto sourcePath = (modelDirectory / decodedUri).lexically_normal();

    const auto key = std::pair{sourcePath.string(), colorSpace};
    if (const auto found = build.textureCache.find(key); found != build.textureCache.end())
    {
        return found->second;
    }

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
    GltfMaterial defaultMaterial;
    defaultMaterial.workflow         = MaterialWorkflow::MetallicRoughness;
    defaultMaterial.id               = asset_import::nextId<MaterialAsset>(meshSource.stem().string());
    defaultMaterial.baseColorTexture = TextureBinding{
        BuiltinAssets::Texture::white, BuiltinAssets::Sampler::linearRepeat};
    build.materialIds.push_back(
        context().assetManager->save(std::move(defaultMaterial)));

    for (size_t index = 0; index < model.materials.size(); ++index)
    {
        const tinygltf::Material& source = model.materials[index];
        GltfMaterial              material;
        material.id = asset_import::nextId<MaterialAsset>(meshSource.stem().string());

        material.workflow = source.extensions.contains("KHR_materials_pbrSpecularGlossiness")
            ? MaterialWorkflow::SpecularGlossiness
            : MaterialWorkflow::MetallicRoughness;
        if (*material.workflow == MaterialWorkflow::MetallicRoughness)
        {
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

            material.baseColorTexture = TextureBinding{
                textureIdFor(build, model, pbr.baseColorTexture.index, modelDirectory, ColorSpace::Srgb)
                    .value_or(BuiltinAssets::Texture::white),
                BuiltinAssets::Sampler::linearRepeat
            };
            CHECK(
                pbr.metallicRoughnessTexture.index < 0 || pbr.metallicRoughnessTexture.texCoord == 0,
                "metallic-roughness textures require TEXCOORD_0: {}",
                meshSource.string()
            );
            material.metallicTexture = bindTexture(
                textureIdFor(build, model, pbr.metallicRoughnessTexture.index, modelDirectory, ColorSpace::Linear)
            );
            material.roughnessTexture = material.metallicTexture;
        }

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
        material.normalTexture = bindTexture(textureIdFor(
            build, model, source.normalTexture.index,
            modelDirectory, ColorSpace::Linear));
        material.aoTexture = bindTexture(textureIdFor(
            build, model, source.occlusionTexture.index,
            modelDirectory, ColorSpace::Linear));
        material.emissiveTexture = bindTexture(textureIdFor(
            build, model, source.emissiveTexture.index,
            modelDirectory, ColorSpace::Srgb));
        material.alphaMode   = source.alphaMode;
        material.alphaCutoff = static_cast<float>(source.alphaCutoff);
        material.doubleSided = source.doubleSided;

        importMaterialExtensions(material, source, meshSource, [&](int textureIndex, ColorSpace colorSpace)
        {
            CHECK(textureIndex >= 0 && textureIndex < static_cast<int>(model.textures.size()),
                  "invalid material texture index {} in '{}'", textureIndex, meshSource.string());
            return bindTexture(textureIdFor(build, model, textureIndex, modelDirectory, colorSpace));
        });
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
    asset_import::transformVertices(build.geometry, firstVertex, transform);

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

GltfImport::GltfImport(const std::filesystem::path& path)
    : ModelImport(path)
{
    CHECK(!path.empty(), "glTF source path is empty");
    const auto start = std::chrono::steady_clock::now();
    LOG_INFO("Import glTF '{}' started", path.string());

    tinygltf::TinyGLTF loader;
    this->model = std::make_unique<tinygltf::Model>();
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

GltfImport::~GltfImport() = default;

void GltfImport::visitGltfNodes(
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

AssetImporter::PreparedMesh GltfImport::readMesh(std::optional<uint32_t> meshIndex) const
{
    const auto& model = *this->model;
    AssetImporter::PreparedMesh build;
    build.source = source;
    build.meshIndex = meshIndex;

    if (meshIndex)
    {
        CHECK(*meshIndex < model.meshes.size());
        appendMesh(build, model, static_cast<int>(*meshIndex), glm::mat4{1.0f});
    }
    else if (!model.scenes.empty())
    {
        visitGltfNodes([&](uint32_t nodeIndex, const glm::mat4& transform)
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

    return build;
}

std::vector<MaterialAsset::ID> GltfImport::importMaterials()
{
    GltfBuild build;
    registerMaterials(build, *model, source.parent_path(), source);
    return std::move(build.materialIds);
}

void GltfImport::visitNodes(const std::function<void(const Node&)>& visitor, float importScale) const
{
    visitGltfNodes([&](uint32_t nodeIndex, const glm::mat4& world)
    {
        const auto& node = model->nodes[nodeIndex];
        Node imported{.index = nodeIndex, .name = node.name, .world = world};
        if (node.mesh >= 0)
        {
            CHECK(static_cast<size_t>(node.mesh) < model->meshes.size());
            CHECK(node.skin < 0, "scene import supports static meshes, not skin on node {}: {}", nodeIndex, source.string());
            imported.meshes.push_back(static_cast<uint32_t>(node.mesh));
            if (imported.name.empty())
                imported.name = model->meshes[node.mesh].name;
        }
        if (node.camera >= 0)
        {
            CHECK(static_cast<size_t>(node.camera) < model->cameras.size());
            const auto& camera = model->cameras[node.camera];
            CHECK(camera.type == "perspective", "editor camera cannot represent camera type '{}': {}", camera.type, source.string());
            const auto& projection = camera.perspective;
            CHECK(std::isfinite(projection.yfov) && projection.yfov > 0 && projection.yfov < glm::pi<double>());
            CHECK(std::isfinite(projection.znear) && projection.znear > 0);
            imported.camera = Camera{
                .position = glm::vec3{world[3]},
                .direction = glm::normalize(glm::vec3{world * glm::vec4{0, 0, -1, 0}}),
                .fieldOfView = glm::degrees(static_cast<float>(projection.yfov)),
                .nearPlane = static_cast<float>(projection.znear) * importScale,
                .farPlane = static_cast<float>(projection.zfar) * importScale,
            };
        }

        if (const auto found = node.extensions.find("KHR_lights_punctual"); found != node.extensions.end())
        {
            const auto& extension = found->second;
            CHECK(extension.IsObject() && extension.Has("light") && extension.Get("light").IsInt(),
                  "invalid glTF light on node {}: {}", nodeIndex, source.string());
            const int index = extension.Get("light").GetNumberAsInt();
            CHECK(index >= 0 && static_cast<size_t>(index) < model->lights.size(),
                  "invalid glTF light index {}: {}", index, source.string());

            const auto& light = model->lights[index];
            CHECK(light.type == "directional" || light.type == "point" || light.type == "spot",
                  "unsupported glTF light type '{}': {}", light.type, source.string());
            CHECK(std::isfinite(light.intensity) && light.intensity >= 0.0 &&
                      light.intensity <= std::numeric_limits<float>::max(),
                  "invalid glTF light intensity on node {}: {}", nodeIndex, source.string());
            CHECK(std::isfinite(light.range) && light.range >= 0.0 &&
                      light.range * importScale <= std::numeric_limits<float>::max(),
                  "invalid glTF light range on node {}: {}", nodeIndex, source.string());
            CHECK(light.color.empty() || light.color.size() == 3,
                  "invalid glTF light color on node {}: {}", nodeIndex, source.string());

            glm::vec3 color{1.0f};
            for (size_t channel = 0; channel < light.color.size(); ++channel)
            {
                CHECK(std::isfinite(light.color[channel]) && light.color[channel] >= 0.0 && light.color[channel] <= 1.0,
                      "invalid glTF light color on node {}: {}", nodeIndex, source.string());
                color[channel] = static_cast<float>(light.color[channel]);
            }

            if (light.type == "spot")
            {
                CHECK(std::isfinite(light.spot.innerConeAngle) && std::isfinite(light.spot.outerConeAngle) &&
                          light.spot.innerConeAngle >= 0.0 && light.spot.innerConeAngle < light.spot.outerConeAngle &&
                          light.spot.outerConeAngle <= glm::half_pi<double>(),
                      "invalid glTF spot cone on node {}: {}", nodeIndex, source.string());
            }

            imported.light = Light{
                .type = light.type == "directional" ? Light::Type::Directional :
                    light.type == "point" ? Light::Type::Point : Light::Type::Spot,
                .color = color,
                .intensity = static_cast<float>(light.intensity),
                .range = light.range > 0.0 ? static_cast<float>(light.range * importScale) :
                    std::numeric_limits<float>::max(),
                .cosInner = static_cast<float>(std::cos(light.spot.innerConeAngle)),
                .cosOuter = static_cast<float>(std::cos(light.spot.outerConeAngle)),
            };
        }

        visitor(imported);
    }, importScale);
}
