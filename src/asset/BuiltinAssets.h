#pragma once

#include "asset/AssetDesc.h"
#include "asset/MeshGeometry.h"

#include <cstdint>
#include <type_traits>
#include <vector>

// Passive builtin asset set: expose typed IDs; Managers generate desc/data on demand.
class BuiltinAssets
{
public:
    struct Mesh
    {
        static inline const ::Mesh::ID cube   = "cube";
        static inline const ::Mesh::ID sphere = "sphere";
        static inline const ::Mesh::ID plane  = "plane";
        static inline const ::Mesh::ID arrow  = "arrow";
    };

    struct Material
    {
        static inline const ::Material::ID white = "white";
    };

    struct Texture
    {
        static inline const ::Texture::ID white     = "white";
        static inline const ::Texture::ID whiteCube = "whiteCube";
    };

private:
    friend class AssetDescManager;
    friend class AssetDataManager;

    template <AssetType T>
    [[nodiscard]] static const typename T::Desc* makeDesc(const typename T::ID& id)
    {
        if constexpr (std::is_same_v<T, ::Mesh>)
        {
            return meshDesc(id);
        }
        else if constexpr (std::is_same_v<T, ::Material>)
        {
            return materialDesc(id);
        }
        else if constexpr (std::is_same_v<T, ::Texture>)
        {
            return textureDesc(id);
        }
        else
        {
            return nullptr;
        }
    }

    [[nodiscard]] static const ::Mesh::Desc* meshDesc(const ::Mesh::ID& id);
    [[nodiscard]] static const ::Material::Desc* materialDesc(const ::Material::ID& id);
    [[nodiscard]] static const ::Texture::Desc* textureDesc(const ::Texture::ID& id);

    [[nodiscard]] static const MeshGeometry* meshGeometry(const ::Mesh::ID& id);
    [[nodiscard]] static const std::vector<uint8_t>* textureData(const ::Texture::ID& id);
};
