#include "asset/BuiltinAssetManager.h"

#include "asset/AssetDescManager.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <cmath>
#include <numbers>
#include <utility>

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

constexpr uint32_t kCubeVertexCount = 24;
constexpr uint32_t kCubeIndexCount  = 36;

[[nodiscard]] MeshGeometry makeCube()
{
    constexpr float h = 0.5f;
    MeshGeometry mesh;
    mesh.vertices.reserve(kCubeVertexCount);
    mesh.indices.reserve(kCubeIndexCount);

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

constexpr uint32_t kSphereSegments    = 32;
constexpr uint32_t kSphereRings       = 16;
constexpr uint32_t kSphereVertexCount = (kSphereSegments + 1) * (kSphereRings + 1);
constexpr uint32_t kSphereIndexCount  = kSphereSegments * kSphereRings * 6;

[[nodiscard]] MeshGeometry makeSphere()
{
    MeshGeometry mesh;
    mesh.vertices.reserve(kSphereVertexCount);
    mesh.indices.reserve(kSphereIndexCount);

    for (uint32_t ring = 0; ring <= kSphereRings; ++ring)
    {
        const float v = static_cast<float>(ring) / static_cast<float>(kSphereRings);
        const float phi = v * std::numbers::pi_v<float>;
        for (uint32_t segment = 0; segment <= kSphereSegments; ++segment)
        {
            const float u = static_cast<float>(segment) /
                static_cast<float>(kSphereSegments);
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

    const uint32_t rowSize = kSphereSegments + 1;
    for (uint32_t ring = 0; ring < kSphereRings; ++ring)
    {
        for (uint32_t segment = 0; segment < kSphereSegments; ++segment)
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

constexpr uint32_t kPlaneVertexCount = 4;
constexpr uint32_t kPlaneIndexCount  = 6;

[[nodiscard]] MeshGeometry makePlane()
{
    constexpr float h = 0.5f;
    MeshGeometry mesh;
    mesh.vertices.reserve(kPlaneVertexCount);
    mesh.indices.reserve(kPlaneIndexCount);
    addFace(mesh, {0.0f, 1.0f, 0.0f},
        {-h, 0.0f, -h}, {-h, 0.0f, h}, {h, 0.0f, h}, {h, 0.0f, -h});
    return mesh;
}

constexpr uint32_t kArrowVertexCount = 36;
constexpr uint32_t kArrowIndexCount  = 48;

[[nodiscard]] MeshGeometry makeArrow()
{
    constexpr float shaftHalfWidth = 0.025f;
    constexpr float headHalfWidth = 0.09f;
    constexpr float back = 0.5f;
    constexpr float headBase = -0.16f;
    constexpr float tip = -0.5f;

    MeshGeometry mesh;
    mesh.vertices.reserve(kArrowVertexCount);
    mesh.indices.reserve(kArrowIndexCount);

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

[[nodiscard]] MeshGeometry builtinMesh(const AssetId& name)
{
    if (name == BuiltinAssetManager::cube)
    {
        return makeCube();
    }
    if (name == BuiltinAssetManager::sphere)
    {
        return makeSphere();
    }
    if (name == BuiltinAssetManager::plane)
    {
        return makePlane();
    }
    CHECK(name == BuiltinAssetManager::arrow, "unknown builtin mesh: {}", name);
    return makeArrow();
}

[[nodiscard]] TextureDesc builtinTexture(const AssetId& id, ImageLayout layout)
{
    TextureDesc desc{};
    desc.id         = id;
    desc.source     = kBuiltinTextureSource;
    desc.format     = ImageFormat::RGBA8;
    desc.colorSpace = ColorSpace::Srgb;
    desc.layout     = layout;
    desc.width      = 1;
    desc.height     = 1;
    return desc;
}
}

void BuiltinAssetManager::init()
{
    AssetDescManager* manager = context().assetManager;
    CHECK(manager != nullptr,
          "AssetDescManager must exist before BuiltinAssetManager");

    const auto addMesh = [manager](const AssetId& id, uint32_t indexCount)
    {
        MeshDesc desc{};
        desc.id       = id;
        desc.geometry = std::filesystem::path{kBuiltinGeometryDir} / id;
        desc.submeshes.push_back({
            .firstIndex = 0,
            .indexCount = indexCount,
            .materialId = MaterialDesc::white,
        });
        manager->add(std::move(desc));
    };

    addMesh(cube, kCubeIndexCount);
    addMesh(sphere, kSphereIndexCount);
    addMesh(plane, kPlaneIndexCount);
    addMesh(arrow, kArrowIndexCount);

    MaterialDesc whiteMaterial{};
    whiteMaterial.id               = MaterialDesc::white;
    whiteMaterial.metallic         = 0.0f;
    whiteMaterial.roughness        = 0.4f;
    whiteMaterial.baseColorTexture = TextureDesc::white;
    manager->add(std::move(whiteMaterial));

    manager->add(builtinTexture(TextureDesc::white, ImageLayout::Image2D));
    manager->add(builtinTexture(TextureDesc::whiteCube, ImageLayout::Cubemap));
}

MeshGeometry BuiltinAssetManager::meshGeometry(
    const std::filesystem::path& geometry) const
{
    return builtinMesh(geometry.filename().string());
}

std::vector<uint8_t> BuiltinAssetManager::textureData(const AssetId& id) const
{
    if (id == TextureDesc::white)
    {
        return std::vector<uint8_t>(4, 255);
    }
    CHECK(id == TextureDesc::whiteCube, "unknown builtin texture: {}", id);
    return std::vector<uint8_t>(6 * 4, 255);
}
