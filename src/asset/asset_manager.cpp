#include "asset/asset_manager.h"

#include "asset/builtin_meshes.h"
#include "asset/gltf_loader.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

AssetManager::AssetManager(std::filesystem::path assetRoot) :
    root(std::move(assetRoot))
{
}

const MeshData &AssetManager::loadMesh(const std::string &identifier)
{
    if (const auto cached = meshCache.find(identifier); cached != meshCache.end())
    {
        return cached->second;
    }

    MeshData data;
    if (identifier == "builtin:cube")
    {
        data = BuiltinMeshes::cube();
    }
    else if (identifier == "builtin:sphere")
    {
        data = BuiltinMeshes::sphere();
    }
    else if (identifier == "builtin:arrow")
    {
        data = BuiltinMeshes::arrow();
    }
    else
    {
        const std::filesystem::path assetPath = path(identifier);
        std::string extension = assetPath.extension().string();
        std::ranges::transform(extension, extension.begin(),
                               [](unsigned char character)
                               {
                                   return static_cast<char>(std::tolower(character));
                               });
        if (extension != ".gltf" && extension != ".glb")
        {
            throw std::runtime_error("unsupported mesh asset: " + identifier);
        }
        data = GLTFLoader::load(assetPath);
    }

    return meshCache.emplace(identifier, std::move(data)).first->second;
}

std::filesystem::path AssetManager::path(const std::filesystem::path &relative) const
{
    return root / relative;
}

void AssetManager::clear()
{
    meshCache.clear();
}
