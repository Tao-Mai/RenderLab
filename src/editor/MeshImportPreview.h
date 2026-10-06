#pragma once

#include "asset/AssetImporter.h"

#include <glm/vec2.hpp>
#include <optional>

// CPU wireframe preview. Preparing it never creates or saves assets.
class MeshImportPreview
{
public:
    void load(const std::filesystem::path& source);
    void clear();
    void draw(float importScale);

    [[nodiscard]] bool                         ready() const { return mesh.has_value(); }
    [[nodiscard]] AssetImporter::PreparedMesh& prepared() { return *mesh; }

private:
    std::optional<AssetImporter::PreparedMesh> mesh;
    glm::vec2                                  orbit{0.7f, 0.3f};
    float                                      zoom = 1.0f;
};
