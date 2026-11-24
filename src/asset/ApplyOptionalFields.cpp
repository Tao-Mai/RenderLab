#include "asset/ApplyOptionalFields.h"

#include "metadata/RenderLabCore.schema.inc"

void applyOptionalFields(GltfMaterial& dst, const GltfMaterial& src)
{
    metadata::visitFields(dst, [&](const auto& field, auto& value)
    {
        using Field = std::remove_cvref_t<decltype(value)>;
        if constexpr (IsOptional<Field>)
        {
            const auto& override = field.get(src);
            if (override)
                value = override;
        }
    });
}
