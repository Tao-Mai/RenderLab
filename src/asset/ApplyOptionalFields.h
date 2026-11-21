#pragma once

#include "core/Metadata.h"
#include "core/TypeTraits.h"

// Merge src into dst in place: only std::optional fields with values overwrite.
template <class T>
void applyOptionalFields(T& dst, const T& src)
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
