#pragma once

#include "asset/material_data.h"
#include "asset/vertex.h"

#include <cstdint>
#include <vector>

struct SubmeshData
{
    uint32_t firstIndex    = 0;
    uint32_t indexCount    = 0;
    uint32_t materialIndex = 0;
};

struct MeshData
{
    std::vector<Vertex>        vertices;
    std::vector<uint32_t>      indices;
    std::vector<MaterialData>  materials{MaterialData{}};
    std::vector<SubmeshData>   submeshes;
};
