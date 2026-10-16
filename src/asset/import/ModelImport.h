#pragma once

#include "asset/AssetImporter.h"
#include "asset/MaterialAsset.h"

#include <functional>
#include <glm/mat4x4.hpp>
#include <string>

// A source-file session owns parsed data and shares materials across its meshes.
class ModelImport : public std::enable_shared_from_this<ModelImport>
{
public:
    struct Camera
    {
        glm::vec3 position;
        glm::vec3 direction;
        float     fieldOfView;
        float     nearPlane;
        float     farPlane;  // Zero denotes an infinite source projection.
    };

    struct Node
    {
        uint32_t              index;
        std::string           name;
        glm::mat4             world;
        std::vector<uint32_t> meshes;
        std::optional<Camera> camera;
    };

    virtual ~ModelImport() = default;
    [[nodiscard]] static std::shared_ptr<ModelImport> open(const std::filesystem::path& source);
    [[nodiscard]] const std::filesystem::path&        sourcePath() const { return source; }

    [[nodiscard]] AssetImporter::PreparedMesh prepareMesh(std::optional<uint32_t> meshIndex = std::nullopt);
    MeshAsset::ID saveMesh(AssetImporter::PreparedMesh&& prepared, const ImportMeshSetting& setting);
    virtual void  visitNodes(const std::function<void(const Node&)>& visitor, float importScale = 1.0f) const = 0;

protected:
    explicit ModelImport(const std::filesystem::path& path);
    [[nodiscard]] virtual AssetImporter::PreparedMesh readMesh(std::optional<uint32_t> meshIndex) const = 0;
    // Slot zero is the default material; source material i is stored at i + 1.
    [[nodiscard]] virtual std::vector<MaterialAsset::ID> importMaterials() = 0;

    std::filesystem::path source;

private:
    std::vector<MaterialAsset::ID> materialIds;
};

namespace asset_import
{
void transformVertices(MeshGeometry& geometry, uint32_t firstVertex, const glm::mat4& transform);
}
