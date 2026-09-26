#pragma once

#include "asset/AssetDesc.h"
#include "asset/MeshGeometry.h"

#include <cstdint>
#include <type_traits>
#include <vector>

// Passive builtin asset set: expose AssetIds; Managers generate desc/data on demand.
class BuiltinAssets
{
public:
    struct Mesh
    {
        static inline const AssetId cube   = "cube";
        static inline const AssetId sphere = "sphere";
        static inline const AssetId plane  = "plane";
        static inline const AssetId arrow  = "arrow";
    };

    struct Material
    {
        static inline const AssetId white = "white";
    };

    struct Texture
    {
        static inline const AssetId white     = "white";
        static inline const AssetId whiteCube = "whiteCube";
    };

private:
    friend class AssetDescManager;
    friend class AssetDataManager;

    template <class T>
    [[nodiscard]] static const T* makeDesc(const AssetId& id)
    {
        if constexpr (std::is_same_v<T, MeshDesc>)
        {
            return meshDesc(id);
        }
        else if constexpr (std::is_same_v<T, MaterialDesc>)
        {
            return materialDesc(id);
        }
        else if constexpr (std::is_same_v<T, TextureDesc>)
        {
            return textureDesc(id);
        }
        else
        {
            return nullptr;
        }
    }

    [[nodiscard]] static const MeshDesc* meshDesc(const AssetId& id);
    [[nodiscard]] static const MaterialDesc* materialDesc(const AssetId& id);
    [[nodiscard]] static const TextureDesc* textureDesc(const AssetId& id);

    [[nodiscard]] static const MeshGeometry* meshGeometry(const AssetId& id);
    [[nodiscard]] static const std::vector<uint8_t>* textureData(const AssetId& id);
};
