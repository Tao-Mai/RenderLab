#pragma once

#include "asset/asset_desc.h"
#include "asset/imported_mesh.h"

#include <filesystem>
#include <string>
#include <unordered_map>

class AssetManager
{
public:
    explicit AssetManager(std::filesystem::path assetRoot = "assets");

    [[nodiscard]] const MeshDesc& meshDesc(const AssetId& id) const;
    [[nodiscard]] const MaterialDesc& materialDesc(const AssetId& id) const;
    [[nodiscard]] const TextureDesc& textureDesc(const AssetId& id) const;
    [[nodiscard]] const ShaderDesc& shaderDesc(const AssetId& id) const;
    [[nodiscard]] MeshGeometry loadGeometry(const AssetId& id);
    [[nodiscard]] std::filesystem::path path(const std::filesystem::path& relative) const;

    void importMesh(const AssetId& id, MeshSourceDesc source);
    void reload();
    void clear();

private:
    std::filesystem::path root;
    std::unordered_map<AssetId, MeshDesc> meshes;
    std::unordered_map<AssetId, MaterialDesc> materials;
    std::unordered_map<AssetId, TextureDesc> textures;
    std::unordered_map<AssetId, ShaderDesc> shaders;
    std::unordered_map<AssetId, std::filesystem::path> descFiles;

    void loadAll();
    void storeDescFile(const AssetId& id, const std::filesystem::path& file);
    [[nodiscard]] std::filesystem::path descFile(AssetType type, const AssetId& id) const;
    [[nodiscard]] MeshGeometry readGeometryFile(const std::filesystem::path& file) const;
};
