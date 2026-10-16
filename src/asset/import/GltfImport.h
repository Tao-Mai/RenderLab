#pragma once

#include "asset/import/ModelImport.h"

#include <functional>
#include <glm/mat4x4.hpp>

namespace tinygltf
{
class Model;
}

class GltfImport final : public ModelImport
{
public:
    explicit GltfImport(const std::filesystem::path& source);
    ~GltfImport() override;
    void visitNodes(const std::function<void(const Node&)>& visitor, float importScale = 1.0f) const override;

private:
    [[nodiscard]] AssetImporter::PreparedMesh    readMesh(std::optional<uint32_t> meshIndex) const override;
    [[nodiscard]] std::vector<MaterialAsset::ID> importMaterials() override;
    void visitGltfNodes(const std::function<void(uint32_t, const glm::mat4&)>& visitor, float importScale = 1.0f) const;

    std::unique_ptr<tinygltf::Model> model;
};
