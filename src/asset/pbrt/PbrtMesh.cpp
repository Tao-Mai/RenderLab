#include "asset/pbrt/PbrtMesh.h"
#include <numeric>

#include "asset/builtin/PrimitiveMesh.h"
#include "asset/import/ImportId.h"
#include "asset/pbrt/PbrtParameters.h"
#include "asset/pbrt/PbrtPly.h"

#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <span>

namespace pbrt_import
{
namespace
{
std::vector<glm::dvec3> vector3Parameter(const PbrtEntity& entity, std::string_view name, std::string_view type)
{
    const auto values = arrayParameter<PbrtFloatValues>(entity, name, type);
    std::vector<glm::dvec3> result;
    result.reserve(values.size() / 3);
    for (size_t index = 0; index < values.size(); index += 3)
        result.emplace_back(values[index], values[index + 1], values[index + 2]);
    return result;
}

MeshGeometry convertMesh(
    const PbrtEntity& entity, std::span<const glm::dvec3> positions, std::span<const int> indices,
    std::span<const glm::dvec3> normals, std::span<const glm::dvec2> texcoords,
    std::span<const glm::dvec3> tangents, std::vector<int32_t>& faceIndices)
{
    CHECK(!positions.empty() && !indices.empty() && indices.size() % 3 == 0,
          "invalid PBRT triangle mesh at {}:{}", entity.location.file.string(), entity.location.line);
    CHECK(positions.size() <= std::numeric_limits<uint32_t>::max() &&
          indices.size() <= std::numeric_limits<uint32_t>::max(),
          "PBRT mesh exceeds engine index limits at {}:{}", entity.location.file.string(), entity.location.line);
    for (const auto count : {normals.size(), texcoords.size(), tangents.size()})
        CHECK(count == 0 || count == positions.size(), "PBRT vertex attribute count mismatch at {}:{}",
              entity.location.file.string(), entity.location.line);
    for (const auto& position : positions)
        for (glm::length_t axis = 0; axis < 3; ++axis)
            CHECK(std::isfinite(position[axis]) && std::abs(position[axis]) <= std::numeric_limits<float>::max(),
                  "invalid PBRT vertex at {}:{}", entity.location.file.string(), entity.location.line);
    for (const auto index : indices)
        CHECK(index >= 0 && size_t(index) < positions.size(), "PBRT mesh index {} out of range at {}:{}",
              index, entity.location.file.string(), entity.location.line);
    for (const auto& normal : normals)
        CHECK(std::isfinite(glm::dot(normal, normal)) && glm::dot(normal, normal) > 0.0,
              "invalid PBRT normal at {}:{}", entity.location.file.string(), entity.location.line);
    for (const auto& uv : texcoords)
        CHECK(std::isfinite(uv.x) && std::isfinite(uv.y) &&
              std::abs(uv.x) <= std::numeric_limits<float>::max() && std::abs(uv.y) <= std::numeric_limits<float>::max(),
              "invalid PBRT UV at {}:{}", entity.location.file.string(), entity.location.line);

    // PBRT supplies a geometric normal and these UVs when a triangle has no attributes.
    if (faceIndices.empty())
    {
        faceIndices.resize(indices.size() / 3);
        std::iota(faceIndices.begin(), faceIndices.end(), 0);
    }
    CHECK(faceIndices.size() == indices.size() / 3, "PBRT face-index count mismatch at {}:{}",
          entity.location.file.string(), entity.location.line);
    std::vector<int32_t> survivingFaces;
    constexpr std::array<glm::vec2, 3> kDefaultUv{{{0, 0}, {1, 0}, {1, 1}}};
    MeshGeometry mesh;
    const bool shareVertices = !normals.empty() && !texcoords.empty();
    std::vector<int> tangentIndices;
    mesh.vertices.reserve(shareVertices ? positions.size() : indices.size());
    mesh.indices.reserve(indices.size());
    if (!tangents.empty())
        tangentIndices.reserve(indices.size());
    if (shareVertices)
        for (size_t index = 0; index < positions.size(); ++index)
            mesh.vertices.push_back({.position = glm::vec3(positions[index]),
                                     .normal = glm::vec3(glm::normalize(normals[index])),
                                     .texcoord = glm::vec2(texcoords[index])});
    for (size_t first = 0; first < indices.size(); first += 3)
    {
        const auto geometricNormal = glm::cross(positions[indices[first + 1]] - positions[indices[first]],
                                                 positions[indices[first + 2]] - positions[indices[first]]);
        if (glm::dot(geometricNormal, geometricNormal) == 0.0)
            continue;
        survivingFaces.push_back(faceIndices[first / 3]);
        for (size_t corner = 0; corner < 3; ++corner)
        {
            const auto index = indices[first + corner];
            if (shareVertices)
                mesh.indices.push_back(static_cast<uint32_t>(index));
            else
            {
                mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
                mesh.vertices.push_back({
                    .position = glm::vec3(positions[index]),
                    .normal = glm::vec3(glm::normalize(normals.empty() ? geometricNormal : normals[index])),
                    .texcoord = texcoords.empty() ? kDefaultUv[corner] : glm::vec2(texcoords[index]),
                });
            }
            if (!tangents.empty())
                tangentIndices.push_back(index);
        }
    }
    CHECK(!mesh.indices.empty(), "PBRT mesh has no nondegenerate triangles at {}:{}",
          entity.location.file.string(), entity.location.line);
    generateTangents(mesh, 0, static_cast<uint32_t>(mesh.indices.size()));
    if (!tangents.empty())
        for (size_t corner = 0; corner < mesh.indices.size(); ++corner)
        {
            auto& vertex = mesh.vertices[mesh.indices[corner]];
            const auto normal = glm::dvec3(vertex.normal);
            const auto sourceTangent = tangents[tangentIndices[corner]];
            const auto tangent = sourceTangent - normal * glm::dot(normal, sourceTangent);
            CHECK(std::isfinite(glm::dot(tangent, tangent)) && glm::dot(tangent, tangent) > 0.0,
                  "invalid PBRT tangent at {}:{}", entity.location.file.string(), entity.location.line);
            vertex.tangent = glm::vec4(glm::vec3(glm::normalize(tangent)), 1.0f);
        }
    faceIndices = std::move(survivingFaces);
    return mesh;
}

MeshGeometry triangleMesh(const PbrtEntity& entity, std::vector<int32_t>& faceIndices)
{
    const auto positions = vector3Parameter(entity, "P", "point3");
    const auto normals = vector3Parameter(entity, "N", "normal");
    const auto tangents = vector3Parameter(entity, "S", "vector3");
    const auto values = arrayParameter<PbrtFloatValues>(entity, "uv", "point2");
    std::vector<glm::dvec2> texcoords;
    texcoords.reserve(values.size() / 2);
    for (size_t index = 0; index < values.size(); index += 2)
        texcoords.emplace_back(values[index], values[index + 1]);
    auto indices = arrayParameter<PbrtIntegerValues>(entity, "indices", "integer");
    constexpr std::array<int32_t, 3> kSingleTriangle{0, 1, 2};
    if (indices.empty() && positions.size() == 3)
        indices = kSingleTriangle;
    const auto faces = arrayParameter<PbrtIntegerValues>(entity, "faceIndices", "integer");
    faceIndices.assign(faces.begin(), faces.end());
    return convertMesh(entity, positions, indices, normals, texcoords, tangents, faceIndices);
}

MeshAsset::Primitive primitiveParameters(const PbrtEntity& entity)
{
    const auto number = [&](std::string_view name, double defaultValue)
    {
        return scalarParameter<PbrtFloatValues>(entity, name, "float", defaultValue);
    };
    const double radius = number("radius", MeshAsset::Sphere{}.radius);
    CHECK(radius > 0.0 && radius <= std::numeric_limits<float>::max(), "invalid PBRT radius at {}:{}",
          entity.location.file.string(), entity.location.line);
    if (entity.type == "sphere")
    {
        MeshAsset::Sphere parameters;
        parameters.radius = radius;
        const double a = number("zmin", -parameters.radius);
        const double b = number("zmax", parameters.radius);
        parameters.zMin = std::clamp(std::min(a, b), -parameters.radius, parameters.radius);
        parameters.zMax = std::clamp(std::max(a, b), -parameters.radius, parameters.radius);
        parameters.phiMax = std::clamp(number("phimax", parameters.phiMax), 0.0, 360.0);
        return parameters;
    }
    if (entity.type == "cylinder")
    {
        MeshAsset::Cylinder parameters;
        parameters.radius = radius;
        const double a = number("zmin", parameters.zMin);
        const double b = number("zmax", parameters.zMax);
        parameters.zMin = std::min(a, b);
        parameters.zMax = std::max(a, b);
        parameters.phiMax = std::clamp(number("phimax", parameters.phiMax), 0.0, 360.0);
        return parameters;
    }
    DCHECK(entity.type == "disk");
    MeshAsset::Disk parameters;
    parameters.radius = radius;
    parameters.innerRadius = number("innerradius", parameters.innerRadius);
    parameters.height = number("height", parameters.height);
    parameters.phiMax = std::clamp(number("phimax", parameters.phiMax), 0.0, 360.0);
    return parameters;
}
}  // namespace

std::vector<PreparedMesh> prepareMeshes(PbrtSceneAsset& scene, const std::filesystem::path& sourceRoot)
{
    std::vector<PreparedMesh> result;
    result.reserve(scene.shapes.size());
    for (size_t index = 0; index < scene.shapes.size(); ++index)
    {
        auto& shape = scene.shapes[index];
        const auto& entity = shape.entity;
        PreparedMesh mesh;
        std::vector<int32_t> faceIndices;
        if (entity.type == "trianglemesh")
            mesh.geometry = triangleMesh(entity, faceIndices);
        else if (entity.type == "plymesh")
        {
            const auto names = arrayParameter<PbrtStringValues>(entity, "filename", "string");
            CHECK(names.size() == 1 && !names.front().empty(), "missing PBRT PLY filename at {}:{}",
                  entity.location.file.string(), entity.location.line);
            auto ply = PlyMesh::read((sourceRoot / names.front()).lexically_normal().string());
            ply.triangulate();
            faceIndices.assign(ply.faceIndices.begin(), ply.faceIndices.end());
            mesh.geometry = convertMesh(entity, ply.p, ply.triIndices, ply.n, ply.uv, {}, faceIndices);
        }
        else
        {
            CHECK(entity.type == "sphere" || entity.type == "cylinder" || entity.type == "disk",
                  "unsupported PBRT shape '{}' at {}:{}", entity.type,
                  entity.location.file.string(), entity.location.line);
            mesh.asset.primitive = primitiveParameters(entity);
            mesh.geometry = generatePrimitiveMesh(*mesh.asset.primitive);
            faceIndices.resize(mesh.geometry.indices.size() / 3, 0);
        }
        mesh.asset.id = asset_import::nextId<MeshAsset>(scene.id.value + "_shape" + std::to_string(index));
        mesh.asset.source = mesh.asset.primitive ? Source::Builtin : Source::File;
        mesh.asset.path = scene.source;
        CHECK(index <= std::numeric_limits<uint32_t>::max());
        mesh.asset.meshIndex = static_cast<uint32_t>(index);
        mesh.asset.bounds = calculateBounds(mesh.geometry);
        DCHECK(shape.material);
        mesh.asset.submeshes.push_back({0, static_cast<uint32_t>(mesh.geometry.indices.size()), *shape.material});
        shape.mesh = mesh.asset.id;
        shape.faceIndices = PbrtIntegerValues{std::move(faceIndices)};
        if (mesh.asset.primitive)
            mesh.geometry = {};
        result.push_back(std::move(mesh));
    }
    return result;
}
}  // namespace pbrt_import
