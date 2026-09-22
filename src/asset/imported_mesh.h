#pragma once

#include "asset/vertex.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct ImportedMaterial
{
    std::string name = "Default";
    glm::vec4 baseColorFactor{1.0f};
    float metallic = 1.0f;
    float roughness = 1.0f;
    float ao = 1.0f;
    glm::vec3 emissive{0.0f};
    float normalScale = 1.0f;
    std::string albedoMap;
    std::string normalMap;
    std::string metallicMap;
    std::string roughnessMap;
    std::string aoMap;
    std::string emissiveMap;
    std::string alphaMode = "OPAQUE";
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};

struct ImportedSubmesh
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
};

struct ImportedMesh
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<ImportedMaterial> materials{ImportedMaterial{}};
    std::vector<ImportedSubmesh> submeshes;
};

struct MeshGeometry
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
