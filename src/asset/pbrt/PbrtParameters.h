#pragma once

#include "asset/pbrt/PbrtTypes.h"
#include "core/Logger.h"

#include <algorithm>
#include <span>
#include <string_view>

namespace pbrt_import
{
template <class T>
T parameterReference(const PbrtParameter& parameter, uint32_t valueIndex = 0)
{
    const auto found = std::ranges::find_if(parameter.references, [&](const auto& candidate)
    {
        return candidate.valueIndex == valueIndex && std::holds_alternative<T>(candidate.target);
    });
    DCHECK(found != parameter.references.end());
    return std::get<T>(found->target);
}

inline const PbrtParameter* findParameter(const PbrtEntity& entity, std::string_view name)
{
    const auto found = std::ranges::find(entity.parameters, name, &PbrtParameter::name);
    return found == entity.parameters.end() ? nullptr : &*found;
}

template <class Values>
auto arrayParameter(const PbrtEntity& entity, std::string_view name, std::string_view type)
{
    using Scalar = typename decltype(Values{}.values)::value_type;
    const auto* parameter = findParameter(entity, name);
    if (!parameter)
        return std::span<const Scalar>{};

    const auto* values = std::get_if<Values>(&parameter->value);
    CHECK(parameter->type == type && values, "invalid PBRT {} parameter '{}' at {}:{}",
          entity.type, name, parameter->location.file.string(), parameter->location.line);
    return std::span<const Scalar>{values->values};
}

template <class Values>
double scalarParameter(const PbrtEntity& entity, std::string_view name, std::string_view type, double defaultValue)
{
    const auto* parameter = findParameter(entity, name);
    if (!parameter)
        return defaultValue;

    const auto values = arrayParameter<Values>(entity, name, type);
    CHECK(values.size() == 1, "invalid PBRT {} parameter '{}' at {}:{}",
          entity.type, name, parameter->location.file.string(), parameter->location.line);
    return static_cast<double>(values.front());
}
}  // namespace pbrt_import
