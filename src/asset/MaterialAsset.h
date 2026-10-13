#pragma once

#include "asset/Asset.h"
#include "core/Logger.h"

struct MaterialAsset : Asset
{
    using ID                              = AssetID<MaterialAsset>;
    static constexpr std::string_view dir = "Material";

    virtual ~MaterialAsset()                    = default;
    bool operator==(const MaterialAsset&) const = default;

    ID id;

    template <std::derived_from<MaterialAsset> T>
    [[nodiscard]] const T& as() const
    {
        const auto* material = dynamic_cast<const T*>(this);
        CHECK(material, "material '{}' has a different material type", id);
        return *material;
    }
};
