#include "asset/builtin_meshes.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace
{
    void addFace(
        MeshData &mesh,
        const glm::vec3 &normal,
        const glm::vec3 &a,
        const glm::vec3 &b,
        const glm::vec3 &c,
        const glm::vec3 &d)
    {
        const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.insert(mesh.vertices.end(), {
            {.position = a, .normal = normal, .texcoord = {0.0f, 0.0f}},
            {.position = b, .normal = normal, .texcoord = {1.0f, 0.0f}},
            {.position = c, .normal = normal, .texcoord = {1.0f, 1.0f}},
            {.position = d, .normal = normal, .texcoord = {0.0f, 1.0f}},
        });
        mesh.indices.insert(mesh.indices.end(), {
            first, first + 1, first + 2,
            first, first + 2, first + 3,
        });
    }
}

MeshData BuiltinMeshes::cube()
{
    constexpr float h = 0.5f;
    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    addFace(mesh, {0.0f, 0.0f, 1.0f},
            {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h});
    addFace(mesh, {0.0f, 0.0f, -1.0f},
            {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h});
    addFace(mesh, {1.0f, 0.0f, 0.0f},
            {h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h});
    addFace(mesh, {-1.0f, 0.0f, 0.0f},
            {-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h});
    addFace(mesh, {0.0f, 1.0f, 0.0f},
            {-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h});
    addFace(mesh, {0.0f, -1.0f, 0.0f},
            {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h});
    mesh.materials[0].name = "Builtin Cube";
    mesh.materials[0].albedo = {0.85f, 0.45f, 0.2f, 1.0f};
    mesh.materials[0].metallic = 0.0f;
    mesh.materials[0].roughness = 0.65f;
    mesh.submeshes.push_back({
        .firstIndex = 0,
        .indexCount = static_cast<uint32_t>(mesh.indices.size()),
        .materialIndex = 0,
    });
    return mesh;
}

MeshData BuiltinMeshes::sphere(uint32_t segments, uint32_t rings)
{
    if (segments < 3 || rings < 2)
    {
        throw std::invalid_argument("sphere requires at least 3 segments and 2 rings");
    }

    MeshData mesh;
    mesh.vertices.reserve((segments + 1) * (rings + 1));
    mesh.indices.reserve(segments * rings * 6);

    for (uint32_t ring = 0; ring <= rings; ++ring)
    {
        const float v = static_cast<float>(ring) / static_cast<float>(rings);
        const float phi = v * std::numbers::pi_v<float>;
        for (uint32_t segment = 0; segment <= segments; ++segment)
        {
            const float u = static_cast<float>(segment) /
                            static_cast<float>(segments);
            const float theta = u * 2.0f * std::numbers::pi_v<float>;
            const glm::vec3 normal{
                std::sin(phi) * std::cos(theta),
                std::cos(phi),
                std::sin(phi) * std::sin(theta),
            };
            mesh.vertices.push_back({
                .position = normal * 0.5f,
                .normal = normal,
                .texcoord = {u, v},
            });
        }
    }

    const uint32_t rowSize = segments + 1;
    for (uint32_t ring = 0; ring < rings; ++ring)
    {
        for (uint32_t segment = 0; segment < segments; ++segment)
        {
            const uint32_t a = ring * rowSize + segment;
            const uint32_t b = a + rowSize;
            mesh.indices.insert(mesh.indices.end(), {
                a, b, a + 1,
                a + 1, b, b + 1,
            });
        }
    }
    mesh.materials[0].name = "Builtin Sphere";
    mesh.materials[0].albedo = {0.25f, 0.65f, 0.9f, 1.0f};
    mesh.materials[0].metallic = 0.0f;
    mesh.materials[0].roughness = 0.4f;
    mesh.submeshes.push_back({
        .firstIndex = 0,
        .indexCount = static_cast<uint32_t>(mesh.indices.size()),
        .materialIndex = 0,
    });
    return mesh;
}
