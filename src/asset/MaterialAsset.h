#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/Asset.h"
#include "core/Logger.h"

struct RL_TYPE("MaterialAsset") MaterialAsset : Asset
{
    using ID                              = AssetID<MaterialAsset>;
    static constexpr std::string_view dir = "Material";

    virtual ~MaterialAsset()                    = default;
    bool operator==(const MaterialAsset&) const = default;

    RL_FIELD("id") ID id;

    template <std::derived_from<MaterialAsset> T>
    [[nodiscard]] const T& as() const
    {
        const auto* material = dynamic_cast<const T*>(this);
        CHECK(material, "material '{}' has a different material type", id);
        return *material;
    }
};

#include RL_METADATA_HEADER("metadata/src/asset/MaterialAsset.generated.h")
