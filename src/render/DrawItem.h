#pragma once

#include "asset/MeshAsset.h"
#include "asset/GltfMaterial.h"

#include <cstdint>
#include <optional>

#include <glm/mat4x4.hpp>

// One submitted instance of a mesh submesh, shared by rasterization and ray tracing.
struct DrawItem
{
    bool operator==(const DrawItem&) const = default;

    MeshAsset::ID                   mesh;
    uint32_t                        submeshIndex = 0;
    MaterialAsset::ID               materialId;
    std::optional<MaterialOverride> materialOverride;
    glm::mat4                       model{1.0f};
    uint32_t                        selectionId   = 0;
    uint32_t                        instanceIndex = 0;
};
