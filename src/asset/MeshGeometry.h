#pragma once

#include "core/Vertex.h"
#include "core/math/Bounds.h"

#include <cstdint>
#include <vector>

struct MeshGeometry
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

void generateTangents(MeshGeometry& mesh, uint32_t firstIndex, uint32_t indexCount);
[[nodiscard]] Aabb calculateBounds(const MeshGeometry& mesh);
