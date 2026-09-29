#pragma once

#include "asset/Asset.h"
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

    struct Sampler
    {
        // 普通可平铺材质贴图，例如 glTF 的 Base Color。
        static inline const ::Sampler::ID linearRepeat  = "linearRepeat";

        // 环境 Cubemap、BRDF LUT 等需要平滑采样且不应在边缘重复的纹理。
        static inline const ::Sampler::ID linearClamp   = "linearClamp";

        // 可平铺的像素风格纹理，保留清晰的像素边界。
        static inline const ::Sampler::ID nearestRepeat = "nearestRepeat";

        // 掩码、索引或查找纹理，需要精确 texel 且不应在边缘重复。
        static inline const ::Sampler::ID nearestClamp  = "nearestClamp";
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
        else if constexpr (std::is_same_v<T, ::Sampler>)
        {
            return samplerDesc(id);
        }
        else
        {
            return nullptr;
        }
    }

    [[nodiscard]] static const ::Mesh::Desc* meshDesc(const ::Mesh::ID& id);
    [[nodiscard]] static const ::Material::Desc* materialDesc(const ::Material::ID& id);
    [[nodiscard]] static const ::Texture::Desc* textureDesc(const ::Texture::ID& id);
    [[nodiscard]] static const ::Sampler::Desc* samplerDesc(const ::Sampler::ID& id);

    [[nodiscard]] static const MeshGeometry* meshGeometry(const ::Mesh::ID& id);
    [[nodiscard]] static const std::vector<uint8_t>* textureData(const ::Texture::ID& id);
};
