#pragma once

#include "core/MetadataAnnotations.h"

#include <optional>
#include "asset/Asset.h"

struct RL_RECORD SamplerAsset : Asset
{
    using ID                              = AssetID<SamplerAsset>;
    static constexpr std::string_view dir = "Sampler";

    enum class RL_ENUM Filter
    {
        Nearest RL_ENUM_VALUE("Nearest"),
        Linear RL_ENUM_VALUE("Linear")
    };

    enum class RL_ENUM MipmapMode
    {
        Nearest RL_ENUM_VALUE("Nearest"),
        Linear RL_ENUM_VALUE("Linear")
    };

    enum class RL_ENUM AddressMode
    {
        Repeat RL_ENUM_VALUE("Repeat"),
        MirroredRepeat RL_ENUM_VALUE("MirroredRepeat"),
        ClampToEdge RL_ENUM_VALUE("ClampToEdge"),
        ClampToBorder RL_ENUM_VALUE("ClampToBorder")
    };

    enum class RL_ENUM CompareOp
    {
        Never RL_ENUM_VALUE("Never"),
        Less RL_ENUM_VALUE("Less"),
        Equal RL_ENUM_VALUE("Equal"),
        LessOrEqual RL_ENUM_VALUE("LessOrEqual"),
        Greater RL_ENUM_VALUE("Greater"),
        NotEqual RL_ENUM_VALUE("NotEqual"),
        GreaterOrEqual RL_ENUM_VALUE("GreaterOrEqual"),
        Always RL_ENUM_VALUE("Always")
    };

    enum class RL_ENUM BorderColor
    {
        FloatTransparentBlack RL_ENUM_VALUE("FloatTransparentBlack"),
        IntTransparentBlack RL_ENUM_VALUE("IntTransparentBlack"),
        FloatOpaqueBlack RL_ENUM_VALUE("FloatOpaqueBlack"),
        IntOpaqueBlack RL_ENUM_VALUE("IntOpaqueBlack"),
        FloatOpaqueWhite RL_ENUM_VALUE("FloatOpaqueWhite"),
        IntOpaqueWhite RL_ENUM_VALUE("IntOpaqueWhite")
    };

    RL_FIELD("id") ID          id;
    RL_FIELD("magFilter") Filter      magFilter        = Filter::Linear;
    RL_FIELD("minFilter") Filter      minFilter        = Filter::Linear;
    RL_FIELD("mipmapMode") MipmapMode  mipmapMode       = MipmapMode::Linear;
    RL_FIELD("addressModeU") AddressMode addressModeU     = AddressMode::Repeat;
    RL_FIELD("addressModeV") AddressMode addressModeV     = AddressMode::Repeat;
    RL_FIELD("addressModeW") AddressMode addressModeW     = AddressMode::Repeat;
    RL_FIELD("mipLodBias") float       mipLodBias       = 0.0f;
    RL_FIELD("anisotropyEnable") bool        anisotropyEnable = false;
    RL_FIELD("maxAnisotropy") float       maxAnisotropy    = 1.0f;
    RL_FIELD("compareEnable") bool        compareEnable    = false;
    RL_FIELD("compareOp") CompareOp   compareOp        = CompareOp::Always;
    RL_FIELD("minLod") float       minLod           = 0.0f;
    // 不指定表示不限制最大 LOD；0.0 表示仅使用基础 mip。
    RL_FIELD("maxLod") std::optional<float> maxLod;
    RL_FIELD("borderColor") BorderColor          borderColor             = BorderColor::FloatTransparentBlack;
    RL_FIELD("unnormalizedCoordinates") bool                 unnormalizedCoordinates = false;
};

#include RL_METADATA_HEADER("metadata/src/asset/SamplerAsset.generated.h")
