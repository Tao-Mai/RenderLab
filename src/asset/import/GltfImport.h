#pragma once

#include "asset/AssetImporter.h"

#include <functional>
#include <glm/mat4x4.hpp>

// One import session per source file: parsed data and imported materials are shared by its meshes.
class GltfImport
{
public:
    explicit GltfImport(const std::filesystem::path& source);
    explicit GltfImport(const AssetImporter::PreparedMesh& prepared);

    [[nodiscard]] AssetImporter::PreparedMesh prepareMesh(std::optional<uint32_t> meshIndex = std::nullopt) const;
    MeshAsset::ID saveMesh(AssetImporter::PreparedMesh&& prepared, const ImportMeshSetting& setting);
    void visitNodes(const std::function<void(uint32_t, const glm::mat4&)>& visitor, float importScale = 1.0f) const;

    std::filesystem::path            source;
    std::shared_ptr<tinygltf::Model> model;

private:
    std::vector<AssetID<MaterialAsset>> materialIds;
};
