#pragma once

#include "asset/vertex.h"

#include <cstdint>
#include <vector>

struct MeshGeometry
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
