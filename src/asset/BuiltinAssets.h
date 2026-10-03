#pragma once

#include "asset/Asset.h"
#include "asset/MeshGeometry.h"

#include <cstdint>
#include <type_traits>
#include <vector>

// Passive builtin asset set: expose typed IDs; Managers generate asset/data on demand.
class BuiltinAssets
{
public:
    struct Mesh
    {
        static inline const ::MeshAsset::ID cube   = "cube";
        static inline const ::MeshAsset::ID sphere = "sphere";
        static inline const ::MeshAsset::ID plane  = "plane";
        static inline const ::MeshAsset::ID arrow  = "arrow";
    };

    struct Material
    {
        static inline const ::MaterialAsset::ID white = "white";
    };

    struct Texture
    {
        static inline const ::TextureAsset::ID white     = "white";
        static inline const ::TextureAsset::ID whiteCube = "whiteCube";
    };

    struct Sampler
    {
        // 普通可平铺材质贴图，例如 glTF 的 Base Color。
        static inline const ::SamplerAsset::ID linearRepeat  = "linearRepeat";

        // 环境 Cubemap、BRDF LUT 等需要平滑采样且不应在边缘重复的纹理。
        static inline const ::SamplerAsset::ID linearClamp   = "linearClamp";

        // 可平铺的像素风格纹理，保留清晰的像素边界。
        static inline const ::SamplerAsset::ID nearestRepeat = "nearestRepeat";

        // 掩码、索引或查找纹理，需要精确 texel 且不应在边缘重复。
        static inline const ::SamplerAsset::ID nearestClamp  = "nearestClamp";
    };

private:
    friend class AssetManager;
    friend class AssetDataManager;

    template <AssetType T>
    [[nodiscard]] static const T* findAsset(const typename T::ID& id)
    {
        if constexpr (std::is_same_v<T, ::MeshAsset>)
        {
            return meshAsset(id);
        }
        else if constexpr (std::is_same_v<T, ::MaterialAsset>)
        {
            return materialAsset(id);
        }
        else if constexpr (std::is_same_v<T, ::TextureAsset>)
        {
            return textureAsset(id);
        }
        else if constexpr (std::is_same_v<T, ::SamplerAsset>)
        {
            return samplerAsset(id);
        }
        else
        {
            return nullptr;
        }
    }

    [[nodiscard]] static const ::MeshAsset* meshAsset(const ::MeshAsset::ID& id);
    [[nodiscard]] static const ::MaterialAsset* materialAsset(const ::MaterialAsset::ID& id);
    [[nodiscard]] static const ::TextureAsset* textureAsset(const ::TextureAsset::ID& id);
    [[nodiscard]] static const ::SamplerAsset* samplerAsset(const ::SamplerAsset::ID& id);

    [[nodiscard]] static const MeshGeometry* meshGeometry(const ::MeshAsset::ID& id);
    [[nodiscard]] static const std::vector<uint8_t>* textureData(const ::TextureAsset::ID& id);
};
