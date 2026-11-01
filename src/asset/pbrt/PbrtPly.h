#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <vector>

namespace pbrt_import
{
struct PlyMesh
{
    std::vector<glm::dvec3> p;
    std::vector<glm::dvec3> n;
    std::vector<glm::dvec2> uv;
    std::vector<int> triIndices;
    std::vector<int> quadIndices;
    std::vector<int> faceIndices;
    std::vector<int> quadFaceIndices;

    [[nodiscard]] static PlyMesh read(const std::string& filename);
    void triangulate();
};
}  // namespace pbrt_import
