#include "asset/BuiltinMeshes.h"

#include "core/Logger.h"

#include <cmath>
#include <numbers>

#include <glm/geometric.hpp>

namespace
{
void addFace(
    MeshGeometry& mesh,
    const glm::vec3& normal,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    const glm::vec3& d)
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

void addTriangle(
    MeshGeometry& mesh,
    const glm::vec3& normal,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c)
{
    const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), {
        {.position = a, .normal = normal, .texcoord = {0.0f, 0.0f}},
        {.position = b, .normal = normal, .texcoord = {1.0f, 0.0f}},
        {.position = c, .normal = normal, .texcoord = {0.5f, 1.0f}},
    });
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2});
}
}

MeshGeometry BuiltinMeshes::cube()
{
    constexpr float h = 0.5f;
    MeshGeometry mesh;
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
    return mesh;
}

MeshGeometry BuiltinMeshes::sphere(uint32_t segments, uint32_t rings)
{
    CHECK(segments >= 3 && rings >= 2,
        "sphere requires at least 3 segments and 2 rings");

    MeshGeometry mesh;
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
    return mesh;
}

MeshGeometry BuiltinMeshes::plane()
{
    constexpr float h = 0.5f;
    MeshGeometry mesh;
    addFace(mesh, {0.0f, 1.0f, 0.0f},
        {-h, 0.0f, -h}, {-h, 0.0f, h}, {h, 0.0f, h}, {h, 0.0f, -h});
    return mesh;
}

MeshGeometry BuiltinMeshes::arrow()
{
    constexpr float shaftHalfWidth = 0.025f;
    constexpr float headHalfWidth = 0.09f;
    constexpr float back = 0.5f;
    constexpr float headBase = -0.16f;
    constexpr float tip = -0.5f;

    MeshGeometry mesh;
    mesh.vertices.reserve(36);
    mesh.indices.reserve(48);

    addFace(mesh, {0.0f, 1.0f, 0.0f},
        {-shaftHalfWidth, shaftHalfWidth, back},
        {shaftHalfWidth, shaftHalfWidth, back},
        {shaftHalfWidth, shaftHalfWidth, headBase},
        {-shaftHalfWidth, shaftHalfWidth, headBase});
    addFace(mesh, {0.0f, -1.0f, 0.0f},
        {-shaftHalfWidth, -shaftHalfWidth, headBase},
        {shaftHalfWidth, -shaftHalfWidth, headBase},
        {shaftHalfWidth, -shaftHalfWidth, back},
        {-shaftHalfWidth, -shaftHalfWidth, back});
    addFace(mesh, {1.0f, 0.0f, 0.0f},
        {shaftHalfWidth, -shaftHalfWidth, back},
        {shaftHalfWidth, -shaftHalfWidth, headBase},
        {shaftHalfWidth, shaftHalfWidth, headBase},
        {shaftHalfWidth, shaftHalfWidth, back});
    addFace(mesh, {-1.0f, 0.0f, 0.0f},
        {-shaftHalfWidth, -shaftHalfWidth, headBase},
        {-shaftHalfWidth, -shaftHalfWidth, back},
        {-shaftHalfWidth, shaftHalfWidth, back},
        {-shaftHalfWidth, shaftHalfWidth, headBase});
    addFace(mesh, {0.0f, 0.0f, 1.0f},
        {-shaftHalfWidth, -shaftHalfWidth, back},
        {shaftHalfWidth, -shaftHalfWidth, back},
        {shaftHalfWidth, shaftHalfWidth, back},
        {-shaftHalfWidth, shaftHalfWidth, back});

    const glm::vec3 topLeft{-headHalfWidth, headHalfWidth, headBase};
    const glm::vec3 topRight{headHalfWidth, headHalfWidth, headBase};
    const glm::vec3 bottomRight{headHalfWidth, -headHalfWidth, headBase};
    const glm::vec3 bottomLeft{-headHalfWidth, -headHalfWidth, headBase};
    const glm::vec3 arrowTip{0.0f, 0.0f, tip};
    addFace(mesh, {0.0f, 0.0f, 1.0f},
        bottomLeft, bottomRight, topRight, topLeft);
    addTriangle(mesh, glm::normalize(glm::vec3{0.0f, 1.0f, -0.4f}),
        topLeft, topRight, arrowTip);
    addTriangle(mesh, glm::normalize(glm::vec3{1.0f, 0.0f, -0.4f}),
        topRight, bottomRight, arrowTip);
    addTriangle(mesh, glm::normalize(glm::vec3{0.0f, -1.0f, -0.4f}),
        bottomRight, bottomLeft, arrowTip);
    addTriangle(mesh, glm::normalize(glm::vec3{-1.0f, 0.0f, -0.4f}),
        bottomLeft, topLeft, arrowTip);
    return mesh;
}
