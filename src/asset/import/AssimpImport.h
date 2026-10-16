#pragma once

#include "asset/import/ModelImport.h"

namespace Assimp
{
class Importer;
}
struct aiScene;

class AssimpImport final : public ModelImport
{
public:
    explicit AssimpImport(const std::filesystem::path& source);
    ~AssimpImport() override;
    void visitNodes(const std::function<void(const Node&)>& visitor, float importScale = 1.0f) const override;

    [[nodiscard]] static bool        supports(const std::filesystem::path& source);
    [[nodiscard]] static std::string extensions();

private:
    [[nodiscard]] AssetImporter::PreparedMesh    readMesh(std::optional<uint32_t> meshIndex) const override;
    [[nodiscard]] std::vector<MaterialAsset::ID> importMaterials() override;

    std::unique_ptr<Assimp::Importer> importer;
    const aiScene*                    scene = nullptr;
};
