#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include "asset/gltf_loader.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

namespace
{
    const unsigned char *accessorData(
        const tinygltf::Model &model,
        const tinygltf::Accessor &accessor,
        size_t &stride)
    {
        if (accessor.bufferView < 0 || accessor.sparse.isSparse)
        {
            throw std::runtime_error("sparse or missing glTF accessor is not supported");
        }

        const auto &view = model.bufferViews.at(accessor.bufferView);
        const auto &buffer = model.buffers.at(view.buffer);
        const int byteStride = accessor.ByteStride(view);
        if (byteStride <= 0)
        {
            throw std::runtime_error("invalid glTF accessor stride");
        }
        stride = static_cast<size_t>(byteStride);

        const size_t offset = view.byteOffset + accessor.byteOffset;
        if (offset >= buffer.data.size())
        {
            throw std::runtime_error("glTF accessor points outside its buffer");
        }
        return buffer.data.data() + offset;
    }

    float readFloat(const unsigned char *data)
    {
        float value = 0.0f;
        std::memcpy(&value, data, sizeof(value));
        return value;
    }

    glm::vec3 readVec3(
        const tinygltf::Model &model,
        const tinygltf::Accessor &accessor,
        size_t index)
    {
        if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
            accessor.type != TINYGLTF_TYPE_VEC3)
        {
            throw std::runtime_error("glTF VEC3 attribute must use float components");
        }
        size_t stride = 0;
        const unsigned char *data = accessorData(model, accessor, stride) + index * stride;
        return {readFloat(data), readFloat(data + 4), readFloat(data + 8)};
    }

    glm::vec2 readVec2(
        const tinygltf::Model &model,
        const tinygltf::Accessor &accessor,
        size_t index)
    {
        if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
            accessor.type != TINYGLTF_TYPE_VEC2)
        {
            throw std::runtime_error("glTF VEC2 attribute must use float components");
        }
        size_t stride = 0;
        const unsigned char *data = accessorData(model, accessor, stride) + index * stride;
        return {readFloat(data), readFloat(data + 4)};
    }

    uint32_t readIndex(
        const tinygltf::Model &model,
        const tinygltf::Accessor &accessor,
        size_t index)
    {
        if (accessor.type != TINYGLTF_TYPE_SCALAR)
        {
            throw std::runtime_error("glTF index accessor must be scalar");
        }

        size_t stride = 0;
        const unsigned char *data = accessorData(model, accessor, stride) + index * stride;
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
                throw std::runtime_error("unsupported glTF index component type");
        }
    }

    glm::mat4 nodeTransform(const tinygltf::Node &node)
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
        MeshData &result,
        const tinygltf::Model &model,
        const tinygltf::Primitive &primitive,
        const glm::mat4 &transform)
    {
        if (primitive.mode != -1 && primitive.mode != TINYGLTF_MODE_TRIANGLES)
        {
            return;
        }

        const auto positionIt = primitive.attributes.find("POSITION");
        if (positionIt == primitive.attributes.end())
        {
            throw std::runtime_error("glTF primitive has no POSITION attribute");
        }

        const auto &positions = model.accessors.at(positionIt->second);
        const auto normalIt = primitive.attributes.find("NORMAL");
        const auto uvIt = primitive.attributes.find("TEXCOORD_0");
        const tinygltf::Accessor *normals = normalIt == primitive.attributes.end()
            ? nullptr
            : &model.accessors.at(normalIt->second);
        const tinygltf::Accessor *uvs = uvIt == primitive.attributes.end()
            ? nullptr
            : &model.accessors.at(uvIt->second);
        if ((normals && normals->count != positions.count) ||
            (uvs && uvs->count != positions.count))
        {
            throw std::runtime_error("glTF vertex attribute counts do not match");
        }

        const uint32_t firstVertex = static_cast<uint32_t>(result.vertices.size());
        const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(transform));
        result.vertices.reserve(result.vertices.size() + positions.count);
        for (size_t index = 0; index < positions.count; ++index)
        {
            const glm::vec3 position = glm::vec3(transform *
                                                 glm::vec4(readVec3(model, positions, index), 1.0f));
            glm::vec3 normal = normals
                ? glm::normalize(normalMatrix * readVec3(model, *normals, index))
                : glm::vec3{0.0f, 0.0f, 1.0f};
            const glm::vec2 uv = uvs
                ? readVec2(model, *uvs, index)
                : glm::vec2{0.0f};
            result.vertices.push_back({position, normal, uv});
        }

        if (primitive.indices >= 0)
        {
            const auto &indices = model.accessors.at(primitive.indices);
            result.indices.reserve(result.indices.size() + indices.count);
            for (size_t index = 0; index < indices.count; ++index)
            {
                const uint32_t localIndex = readIndex(model, indices, index);
                if (localIndex >= positions.count)
                {
                    throw std::runtime_error("glTF index is outside the vertex accessor");
                }
                result.indices.push_back(firstVertex + localIndex);
            }
        }
        else
        {
            for (uint32_t index = 0; index < positions.count; ++index)
            {
                result.indices.push_back(firstVertex + index);
            }
        }
    }

    void appendMesh(
        MeshData &result,
        const tinygltf::Model &model,
        int meshIndex,
        const glm::mat4 &transform)
    {
        const auto &mesh = model.meshes.at(meshIndex);
        for (const auto &primitive : mesh.primitives)
        {
            appendPrimitive(result, model, primitive, transform);
        }
    }
}

MeshData GLTFLoader::load(const std::filesystem::path &path)
{
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string warning;
    std::string error;

    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char character) { return std::tolower(character); });
    const bool loaded = extension == ".glb"
        ? loader.LoadBinaryFromFile(&model, &error, &warning, path.string())
        : loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
    if (!warning.empty())
    {
        std::cerr << "TinyGLTF warning: " << warning << '\n';
    }
    if (!loaded)
    {
        throw std::runtime_error("failed to load glTF '" + path.string() + "': " + error);
    }

    MeshData result;
    std::function<void(int, const glm::mat4 &)> visitNode;
    visitNode = [&](int nodeIndex, const glm::mat4 &parentTransform)
    {
        const auto &node = model.nodes.at(nodeIndex);
        const glm::mat4 transform = parentTransform * nodeTransform(node);
        if (node.mesh >= 0)
        {
            appendMesh(result, model, node.mesh, transform);
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
            appendMesh(result, model, static_cast<int>(meshIndex), glm::mat4{1.0f});
        }
    }

    if (result.vertices.empty() || result.indices.empty())
    {
        throw std::runtime_error("glTF contains no triangle mesh data: " + path.string());
    }
    return result;
}
