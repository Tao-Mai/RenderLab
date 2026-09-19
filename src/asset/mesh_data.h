#pragma once

#include "render/vertex.h"

#include <cstdint>
#include <vector>

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
