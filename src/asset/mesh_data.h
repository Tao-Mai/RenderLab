#pragma once

#include "render/material.h"
#include "render/vertex.h"

#include <cstdint>
#include <vector>

struct SubmeshData
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
};

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Material> materials{Material{}};
    std::vector<SubmeshData> submeshes;
};
