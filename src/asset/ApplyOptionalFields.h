#pragma once

#include <optional>
#include <type_traits>
#include <utility>

#include <rfl/get.hpp>
#include <rfl/to_view.hpp>

namespace apply_optional_fields_detail
{
template <class T>
inline constexpr bool isOptional = false;

template <class T>
inline constexpr bool isOptional<std::optional<T>> = true;
}

// Merge src into dst in place: only std::optional fields with values overwrite.
template <class T>
void applyOptionalFields(T& dst, const T& src)
{
    auto       dstView = rfl::to_view(dst);
    const auto srcView = rfl::to_view(src);

    using View = std::remove_cvref_t<decltype(dstView)>;
    [&]<int... Is>(std::integer_sequence<int, Is...>)
    {
        auto applyOne = [](auto* dstField, const auto* srcField)
        {
            using Field = std::remove_cvref_t<decltype(*dstField)>;
            if constexpr (apply_optional_fields_detail::isOptional<Field>)
            {
                if (srcField->has_value())
                {
                    *dstField = *srcField;
                }
            }
        };
        (applyOne(rfl::get<Is>(dstView), rfl::get<Is>(srcView)), ...);
    }(std::make_integer_sequence<int, static_cast<int>(View::size())>());
}
