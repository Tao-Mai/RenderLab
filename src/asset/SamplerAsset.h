#pragma once

#include <optional>
#include "asset/Asset.h"

struct SamplerAsset : Asset
{
    using ID                              = AssetID<SamplerAsset>;
    static constexpr std::string_view dir = "Sampler";

    enum class Filter
    {
        Nearest,
        Linear
    };

    enum class MipmapMode
    {
        Nearest,
        Linear
    };

    enum class AddressMode
    {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder
    };

    enum class CompareOp
    {
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always
    };

    enum class BorderColor
    {
        FloatTransparentBlack,
        IntTransparentBlack,
        FloatOpaqueBlack,
        IntOpaqueBlack,
        FloatOpaqueWhite,
        IntOpaqueWhite
    };

    ID          id;
    Filter      magFilter        = Filter::Linear;
    Filter      minFilter        = Filter::Linear;
    MipmapMode  mipmapMode       = MipmapMode::Linear;
    AddressMode addressModeU     = AddressMode::Repeat;
    AddressMode addressModeV     = AddressMode::Repeat;
    AddressMode addressModeW     = AddressMode::Repeat;
    float       mipLodBias       = 0.0f;
    bool        anisotropyEnable = false;
    float       maxAnisotropy    = 1.0f;
    bool        compareEnable    = false;
    CompareOp   compareOp        = CompareOp::Always;
    float       minLod           = 0.0f;
    // 不指定表示不限制最大 LOD；0.0 表示仅使用基础 mip。
    std::optional<float> maxLod;
    BorderColor          borderColor             = BorderColor::FloatTransparentBlack;
    bool                 unnormalizedCoordinates = false;
};
