#pragma once

#include "asset/SamplerAsset.h"
#include "asset/StandardMaterial.h"
#include "asset/TextureAsset.h"

#include <cstdint>
#include <type_traits>
#include <vector>

// Builtin asset IDs and on-demand data for AssetManager.
class BuiltinAssets
{
public:
    struct Material
    {
        static inline const ::StandardMaterial::ID white = "white";
    };

    struct Texture
    {
        static inline const ::TextureAsset::ID white        = "white";
        static inline const ::TextureAsset::ID kBlackCube   = "blackCube";
        static inline const ::TextureAsset::ID flatNormal   = "flatNormal";
        static inline const ::TextureAsset::ID kWhiteLinear = "whiteLinear";
    };

    struct Sampler
    {
        // 普通可平铺材质贴图，例如 glTF 的 Base Color。
        static inline const ::SamplerAsset::ID linearRepeat = "linearRepeat";

        // 环境 Cubemap 等需要平滑采样且不应在边缘重复的纹理。
        static inline const ::SamplerAsset::ID linearClamp = "linearClamp";

        // 可平铺的像素风格纹理，保留清晰的像素边界。
        static inline const ::SamplerAsset::ID nearestRepeat = "nearestRepeat";

        // 掩码、索引或查找纹理，需要精确 texel 且不应在边缘重复。
        static inline const ::SamplerAsset::ID nearestClamp = "nearestClamp";
    };

private:
    friend class AssetManager;

    template <AssetType T>
    [[nodiscard]] static const T* findAsset(const typename T::ID& id)
    {
        if constexpr (std::is_same_v<T, ::StandardMaterial>)
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

    [[nodiscard]] static const ::StandardMaterial* materialAsset(const ::StandardMaterial::ID& id);
    [[nodiscard]] static const ::TextureAsset*  textureAsset(const ::TextureAsset::ID& id);
    [[nodiscard]] static const ::SamplerAsset*  samplerAsset(const ::SamplerAsset::ID& id);

    [[nodiscard]] static const std::vector<uint8_t>* textureData(const ::TextureAsset::ID& id);
};
