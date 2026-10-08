#pragma once

#include "core/Vertex.h"

#include <cstdint>
#include <vector>

struct MeshGeometry
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

void generateTangents(MeshGeometry& mesh, uint32_t firstIndex, uint32_t indexCount);
