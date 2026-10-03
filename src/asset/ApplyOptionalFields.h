#pragma once

#include "core/Reflect.h"

// Merge src into dst in place: only std::optional fields with values overwrite.
template <class T>
void applyOptionalFields(T& dst, const T& src)
{
    constexpr auto ctx = std::meta::access_context::current();

    template for (constexpr auto member :
        std::define_static_array(ReflectedDataMembers(^^T, ctx)))
    {
        using Field = std::remove_cvref_t<decltype(dst.[:member:])>;
        if constexpr (IsOptional<Field>)
        {
            if (src.[:member:].has_value())
                dst.[:member:] = src.[:member:];
        }
    }
}
