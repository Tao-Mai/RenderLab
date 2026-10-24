#include "asset/BuiltinAssets.h"

#include "core/Logger.h"

#include <array>
#include <cmath>
#include <numbers>
#include <span>

#include <glm/geometric.hpp>

namespace
{
void addFace(
    MeshGeometry&    mesh,
    const glm::vec3& normal,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    const glm::vec3& d)
{
    const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(),
                         {
                             {.position = a, .normal = normal, .texcoord = {0.0f, 0.0f}},
                             {.position = b, .normal = normal, .texcoord = {1.0f, 0.0f}},
                             {.position = c, .normal = normal, .texcoord = {1.0f, 1.0f}},
                             {.position = d, .normal = normal, .texcoord = {0.0f, 1.0f}},
                         });
    mesh.indices.insert(mesh.indices.end(),
                        {
                            first, first + 1, first + 2,
                            first, first + 2, first + 3,
                        });
}

void addTriangle(
    MeshGeometry&    mesh,
    const glm::vec3& normal,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c)
{
    const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(),
                         {
                             {.position = a, .normal = normal, .texcoord = {0.0f, 0.0f}},
                             {.position = b, .normal = normal, .texcoord = {1.0f, 0.0f}},
                             {.position = c, .normal = normal, .texcoord = {0.5f, 1.0f}},
                         });
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2});
}

constexpr uint32_t kCubeIndexCount = 36;

[[nodiscard]] MeshGeometry makeCube()
{
    constexpr float    h = 0.5f;
    constexpr uint32_t kCubeVertexCount = 24;

    MeshGeometry mesh;
    mesh.vertices.reserve(kCubeVertexCount);
    mesh.indices.reserve(kCubeIndexCount);

    addFace(mesh,
            {0.0f, 0.0f, 1.0f},
            {-h, -h, h},
            {h, -h, h},
            {h, h, h},
            {-h, h, h});
    addFace(mesh,
            {0.0f, 0.0f, -1.0f},
            {h, -h, -h},
            {-h, -h, -h},
            {-h, h, -h},
            {h, h, -h});
    addFace(mesh,
            {1.0f, 0.0f, 0.0f},
            {h, -h, h},
            {h, -h, -h},
            {h, h, -h},
            {h, h, h});
    addFace(mesh,
            {-1.0f, 0.0f, 0.0f},
            {-h, -h, -h},
            {-h, -h, h},
            {-h, h, h},
            {-h, h, -h});
    addFace(mesh,
            {0.0f, 1.0f, 0.0f},
            {-h, h, h},
            {h, h, h},
            {h, h, -h},
            {-h, h, -h});
    addFace(mesh,
            {0.0f, -1.0f, 0.0f},
            {-h, -h, -h},
            {h, -h, -h},
            {h, -h, h},
            {-h, -h, h});
    generateTangents(mesh, 0, static_cast<uint32_t>(mesh.indices.size()));
    return mesh;
}

constexpr uint32_t kSphereSegments    = 128;
constexpr uint32_t kSphereRings       = 64;
constexpr uint32_t kSphereIndexCount  = kSphereSegments * kSphereRings * 6;

[[nodiscard]] MeshGeometry makeSphere()
{
    constexpr uint32_t kSphereVertexCount = (kSphereSegments + 1) * (kSphereRings + 1);

    MeshGeometry mesh;
    mesh.vertices.reserve(kSphereVertexCount);
    mesh.indices.reserve(kSphereIndexCount);

    for (uint32_t ring = 0; ring <= kSphereRings; ++ring)
    {
        const float v   = static_cast<float>(ring) / static_cast<float>(kSphereRings);
        const float phi = v * std::numbers::pi_v<float>;
        for (uint32_t segment = 0; segment <= kSphereSegments; ++segment)
        {
            const float u = static_cast<float>(segment) /
                static_cast<float>(kSphereSegments);
            const float     theta = u * 2.0f * std::numbers::pi_v<float>;
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

    const uint32_t rowSize = kSphereSegments + 1;
    for (uint32_t ring = 0; ring < kSphereRings; ++ring)
    {
        for (uint32_t segment = 0; segment < kSphereSegments; ++segment)
        {
            const uint32_t a = ring * rowSize + segment;
            const uint32_t b = a + rowSize;
            mesh.indices.insert(mesh.indices.end(),
                                {
                                    a, a + 1, b,
                                    a + 1, b + 1, b,
                                });
        }
    }
    generateTangents(mesh, 0, static_cast<uint32_t>(mesh.indices.size()));
    return mesh;
}

constexpr uint32_t kConeSegments = 64;
constexpr uint32_t kConeSideIndexCount = kConeSegments * 3;
constexpr uint32_t kConeBaseIndexCount = kConeSegments * 3;

[[nodiscard]] MeshGeometry makeCone()
{
    constexpr uint32_t kVerticesPerSegment = 6;

    // Unit radius and height; the apex is at the origin and the base is at Z = -1.
    MeshGeometry mesh;
    mesh.vertices.reserve(kConeSegments * kVerticesPerSegment);
    mesh.indices.reserve(kConeSideIndexCount + kConeBaseIndexCount);
    std::vector<uint32_t> baseIndices;
    baseIndices.reserve(kConeBaseIndexCount);
    for (uint32_t segment = 0; segment < kConeSegments; ++segment)
    {
        const float u0          = static_cast<float>(segment) / kConeSegments;
        const float u1          = static_cast<float>(segment + 1) / kConeSegments;
        const float angle0      = u0 * 2.0f * std::numbers::pi_v<float>;
        const float angle1      = u1 * 2.0f * std::numbers::pi_v<float>;
        const float middleAngle = (angle0 + angle1) * 0.5f;
        const glm::vec3 base0{std::cos(angle0), std::sin(angle0), -1.0f};
        const glm::vec3 base1{std::cos(angle1), std::sin(angle1), -1.0f};
        const glm::vec3 apexNormal = glm::normalize(glm::vec3{std::cos(middleAngle), std::sin(middleAngle), 1.0f});

        const auto first = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.insert(
            mesh.vertices.end(),
            {
                {.position = {0.0f, 0.0f, 0.0f}, .normal = apexNormal, .texcoord = {(u0 + u1) * 0.5f, 0.0f}},
                {.position = base0,
                 .normal   = glm::normalize(glm::vec3{base0.x, base0.y, 1.0f}),
                 .texcoord = {u0, 1.0f}},
                {.position = base1,
                 .normal   = glm::normalize(glm::vec3{base1.x, base1.y, 1.0f}),
                 .texcoord = {u1, 1.0f}},
                {.position = {0.0f, 0.0f, -1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord = {0.5f, 0.5f}},
                {.position = base1, .normal = {0.0f, 0.0f, -1.0f}, .texcoord = glm::vec2{base1} * 0.5f + 0.5f},
                {.position = base0, .normal = {0.0f, 0.0f, -1.0f}, .texcoord = glm::vec2{base0} * 0.5f + 0.5f},
            }
        );
        mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2});
        baseIndices.insert(baseIndices.end(), {first + 3, first + 4, first + 5});
    }
    mesh.indices.insert(mesh.indices.end(), baseIndices.begin(), baseIndices.end());

    generateTangents(mesh, 0, static_cast<uint32_t>(mesh.indices.size()));
    return mesh;
}

constexpr uint32_t kPlaneIndexCount = 6;

[[nodiscard]] MeshGeometry makePlane()
{
    constexpr float    h = 0.5f;
    constexpr uint32_t kPlaneVertexCount = 4;

    MeshGeometry mesh;
    mesh.vertices.reserve(kPlaneVertexCount);
    mesh.indices.reserve(kPlaneIndexCount);
    addFace(mesh,
            {0.0f, 1.0f, 0.0f},
            {-h, 0.0f, -h},
            {-h, 0.0f, h},
            {h, 0.0f, h},
            {h, 0.0f, -h});
    generateTangents(mesh, 0, static_cast<uint32_t>(mesh.indices.size()));
    return mesh;
}

constexpr uint32_t kArrowIndexCount = 48;

[[nodiscard]] MeshGeometry makeArrow()
{
    constexpr float    shaftHalfWidth = 0.025f;
    constexpr float    headHalfWidth  = 0.09f;
    constexpr float    back           = 0.5f;
    constexpr float    headBase       = -0.16f;
    constexpr float    tip            = -0.5f;
    constexpr uint32_t kArrowVertexCount = 36;

    MeshGeometry mesh;
    mesh.vertices.reserve(kArrowVertexCount);
    mesh.indices.reserve(kArrowIndexCount);

    addFace(mesh,
            {0.0f, 1.0f, 0.0f},
            {-shaftHalfWidth, shaftHalfWidth, back},
            {shaftHalfWidth, shaftHalfWidth, back},
            {shaftHalfWidth, shaftHalfWidth, headBase},
            {-shaftHalfWidth, shaftHalfWidth, headBase});
    addFace(mesh,
            {0.0f, -1.0f, 0.0f},
            {-shaftHalfWidth, -shaftHalfWidth, headBase},
            {shaftHalfWidth, -shaftHalfWidth, headBase},
            {shaftHalfWidth, -shaftHalfWidth, back},
            {-shaftHalfWidth, -shaftHalfWidth, back});
    addFace(mesh,
            {1.0f, 0.0f, 0.0f},
            {shaftHalfWidth, -shaftHalfWidth, back},
            {shaftHalfWidth, -shaftHalfWidth, headBase},
            {shaftHalfWidth, shaftHalfWidth, headBase},
            {shaftHalfWidth, shaftHalfWidth, back});
    addFace(mesh,
            {-1.0f, 0.0f, 0.0f},
            {-shaftHalfWidth, -shaftHalfWidth, headBase},
            {-shaftHalfWidth, -shaftHalfWidth, back},
            {-shaftHalfWidth, shaftHalfWidth, back},
            {-shaftHalfWidth, shaftHalfWidth, headBase});
    addFace(mesh,
            {0.0f, 0.0f, 1.0f},
            {-shaftHalfWidth, -shaftHalfWidth, back},
            {shaftHalfWidth, -shaftHalfWidth, back},
            {shaftHalfWidth, shaftHalfWidth, back},
            {-shaftHalfWidth, shaftHalfWidth, back});

    const glm::vec3 topLeft{-headHalfWidth, headHalfWidth, headBase};
    const glm::vec3 topRight{headHalfWidth, headHalfWidth, headBase};
    const glm::vec3 bottomRight{headHalfWidth, -headHalfWidth, headBase};
    const glm::vec3 bottomLeft{-headHalfWidth, -headHalfWidth, headBase};
    const glm::vec3 arrowTip{0.0f, 0.0f, tip};
    addFace(mesh,
            {0.0f, 0.0f, 1.0f},
            bottomLeft,
            bottomRight,
            topRight,
            topLeft);
    addTriangle(mesh,
                glm::normalize(glm::vec3{0.0f, 1.0f, -0.4f}),
                topLeft,
                topRight,
                arrowTip);
    addTriangle(mesh,
                glm::normalize(glm::vec3{1.0f, 0.0f, -0.4f}),
                topRight,
                bottomRight,
                arrowTip);
    addTriangle(mesh,
                glm::normalize(glm::vec3{0.0f, -1.0f, -0.4f}),
                bottomRight,
                bottomLeft,
                arrowTip);
    addTriangle(mesh,
                glm::normalize(glm::vec3{-1.0f, 0.0f, -0.4f}),
                bottomLeft,
                topLeft,
                arrowTip);
    generateTangents(mesh, 0, static_cast<uint32_t>(mesh.indices.size()));
    return mesh;
}

[[nodiscard]] ::MeshAsset makeMeshAsset(
    const ::MeshAsset::ID& id, const MeshGeometry& geometry, std::span<const uint32_t> submeshIndexCounts
)
{
    ::MeshAsset asset{};
    asset.id     = id;
    asset.source = Source::Builtin;
    asset.bounds = calculateBounds(geometry);
    uint32_t firstIndex = 0;
    for (const auto indexCount : submeshIndexCounts)
    {
        asset.submeshes.push_back({
            .firstIndex = firstIndex,
            .indexCount = indexCount,
            .materialId = BuiltinAssets::Material::white,
        });
        firstIndex += indexCount;
    }
    DCHECK(firstIndex == geometry.indices.size());
    return asset;
}
}

const ::MeshAsset* BuiltinAssets::meshAsset(const ::MeshAsset::ID& id)
{
    static const ::MeshAsset assets[] = {
        makeMeshAsset(Mesh::cube, *meshGeometry(Mesh::cube), std::array{kCubeIndexCount}),
        makeMeshAsset(Mesh::sphere, *meshGeometry(Mesh::sphere), std::array{kSphereIndexCount}),
        makeMeshAsset(Mesh::kCone, *meshGeometry(Mesh::kCone), std::array{kConeSideIndexCount, kConeBaseIndexCount}),
        makeMeshAsset(Mesh::plane, *meshGeometry(Mesh::plane), std::array{kPlaneIndexCount}),
        makeMeshAsset(Mesh::arrow, *meshGeometry(Mesh::arrow), std::array{kArrowIndexCount}),
    };

    for (const ::MeshAsset& asset : assets)
    {
        if (asset.id == id)
        {
            return &asset;
        }
    }
    return nullptr;
}

const MeshGeometry* BuiltinAssets::meshGeometry(const ::MeshAsset::ID& id)
{
    struct Entry
    {
        ::MeshAsset::ID     id;
        MeshGeometry geometry;
    };

    static const Entry entries[] = {
        {Mesh::cube, makeCube()},
        {Mesh::sphere, makeSphere()},
        {Mesh::kCone, makeCone()},
        {Mesh::plane, makePlane()},
        {Mesh::arrow, makeArrow()},
    };

    for (const Entry& entry : entries)
    {
        if (entry.id == id)
        {
            return &entry.geometry;
        }
    }
    return nullptr;
}
