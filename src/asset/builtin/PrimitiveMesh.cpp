#include "asset/builtin/PrimitiveMesh.h"

#include "core/Logger.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <limits>
#include <type_traits>

namespace
{
template <class Sample>
MeshGeometry makeGrid(uint32_t segments, uint32_t rings, Sample&& sample)
{
    CHECK(segments >= 3 && rings >= 1);
    const uint64_t vertexCount = (uint64_t(segments) + 1) * (uint64_t(rings) + 1);
    CHECK(vertexCount <= std::numeric_limits<uint32_t>::max());
    CHECK(uint64_t(segments) * rings * 6 <= std::numeric_limits<uint32_t>::max());

    MeshGeometry mesh;
    mesh.vertices.reserve(static_cast<size_t>(vertexCount));
    mesh.indices.reserve(size_t(segments) * rings * 6);
    for (uint32_t ring = 0; ring <= rings; ++ring)
        for (uint32_t segment = 0; segment <= segments; ++segment)
            mesh.vertices.push_back(sample(double(segment) / segments, double(ring) / rings));

    const auto addTriangle = [&](uint32_t a, uint32_t b, uint32_t c)
    {
        const auto normal = glm::cross(
            glm::dvec3(mesh.vertices[b].position) - glm::dvec3(mesh.vertices[a].position),
            glm::dvec3(mesh.vertices[c].position) - glm::dvec3(mesh.vertices[a].position));
        if (glm::dot(normal, normal) == 0.0)
            return;
        if (glm::dot(normal, glm::dvec3(mesh.vertices[a].normal)) < 0.0)
            std::swap(b, c);
        mesh.indices.insert(mesh.indices.end(), {a, b, c});
    };
    const uint32_t rowSize = segments + 1;
    for (uint32_t ring = 0; ring < rings; ++ring)
        for (uint32_t segment = 0; segment < segments; ++segment)
        {
            const uint32_t a = ring * rowSize + segment;
            const uint32_t b = a + rowSize;
            addTriangle(a, a + 1, b);
            addTriangle(a + 1, b + 1, b);
        }
    CHECK(!mesh.indices.empty());
    generateTangents(mesh, 0, static_cast<uint32_t>(mesh.indices.size()));
    return mesh;
}

void validateRadiusAndAngle(double radius, double phiMax)
{
    CHECK(std::isfinite(radius) && radius > 0.0 && radius <= std::numeric_limits<float>::max());
    CHECK(std::isfinite(phiMax) && phiMax > 0.0 && phiMax <= 360.0);
}

MeshGeometry makeSphere(const MeshAsset::Sphere& sphere)
{
    validateRadiusAndAngle(sphere.radius, sphere.phiMax);
    CHECK(std::isfinite(sphere.zMin) && std::isfinite(sphere.zMax) &&
          sphere.zMin >= -sphere.radius && sphere.zMax <= sphere.radius && sphere.zMin < sphere.zMax);
    const double thetaMin = std::acos(sphere.zMax / sphere.radius);
    const double thetaMax = std::acos(sphere.zMin / sphere.radius);
    return makeGrid(sphere.segments, sphere.rings, [&](double u, double v)
    {
        const double theta = std::lerp(thetaMin, thetaMax, v);
        const double phi = u * glm::radians(sphere.phiMax);
        const bool pole = (v == 0.0 && sphere.zMax == sphere.radius) ||
            (v == 1.0 && sphere.zMin == -sphere.radius);
        const double radial = pole ? 0.0 : std::sin(theta);
        const glm::dvec3 normal{radial * std::cos(phi), radial * std::sin(phi), std::cos(theta)};
        return Vertex{.position = glm::vec3(normal * sphere.radius),
                      .normal = glm::vec3(normal), .texcoord = {u, v}};
    });
}

MeshGeometry makeCylinder(const MeshAsset::Cylinder& cylinder)
{
    validateRadiusAndAngle(cylinder.radius, cylinder.phiMax);
    CHECK(std::isfinite(cylinder.zMin) && std::isfinite(cylinder.zMax) && cylinder.zMin < cylinder.zMax);
    CHECK(std::abs(cylinder.zMin) <= std::numeric_limits<float>::max() &&
          std::abs(cylinder.zMax) <= std::numeric_limits<float>::max());
    return makeGrid(cylinder.segments, 1, [&](double u, double v)
    {
        const double phi = u * glm::radians(cylinder.phiMax);
        const glm::dvec3 normal{std::cos(phi), std::sin(phi), 0.0};
        return Vertex{.position = {normal.x * cylinder.radius, normal.y * cylinder.radius,
                                   std::lerp(cylinder.zMin, cylinder.zMax, v)},
                      .normal = glm::vec3(normal), .texcoord = {u, v}};
    });
}

MeshGeometry makeDisk(const MeshAsset::Disk& disk)
{
    validateRadiusAndAngle(disk.radius, disk.phiMax);
    CHECK(std::isfinite(disk.height) && std::abs(disk.height) <= std::numeric_limits<float>::max());
    CHECK(std::isfinite(disk.innerRadius) && disk.innerRadius >= 0.0 && disk.innerRadius < disk.radius);
    return makeGrid(disk.segments, 1, [&](double u, double v)
    {
        const double radius = std::lerp(disk.radius, disk.innerRadius, v);
        const double phi = u * glm::radians(disk.phiMax);
        return Vertex{.position = {radius * std::cos(phi), radius * std::sin(phi), disk.height},
                      .normal = {0, 0, 1}, .texcoord = {u, v}};
    });
}
}  // namespace

MeshGeometry generatePrimitiveMesh(const MeshAsset::Primitive& primitive)
{
    return std::visit([]<class T>(const T& parameters)
    {
        if constexpr (std::is_same_v<T, MeshAsset::Sphere>)
            return makeSphere(parameters);
        else if constexpr (std::is_same_v<T, MeshAsset::Cylinder>)
            return makeCylinder(parameters);
        else
            return makeDisk(parameters);
    }, primitive);
}
