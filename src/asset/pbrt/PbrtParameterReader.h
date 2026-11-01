#pragma once

#include "asset/pbrt/PbrtParameters.h"
#include "asset/pbrt/PbrtSpectrum.h"

#include <utility>

namespace pbrt_import
{
class ParameterReader
{
public:
    explicit ParameterReader(const PbrtEntity& entity) : entity(entity) {}

    std::optional<PbrtFloatInput> optionalFloat(std::string_view name)
    {
        const auto* value = parameter(name);
        if (!value)
            return {};
        if (value->type == "texture")
        {
            checkSingleTexture(*value);
            return parameterReference<PbrtTextureReference>(*value);
        }
        return PbrtFloatConstant{scalarParameter<PbrtFloatValues>(entity, name, "float", 0.0)};
    }

    PbrtFloatInput floatInput(std::string_view name, double defaultValue)
    {
        return optionalFloat(name).value_or(PbrtFloatConstant{defaultValue});
    }

    std::optional<PbrtSpectrumInput> optionalSpectrum(std::string_view name, bool albedo)
    {
        const auto* value = parameter(name);
        if (!value)
            return {};
        if (value->type == "texture")
        {
            checkSingleTexture(*value);
            return parameterReference<PbrtTextureReference>(*value);
        }
        return std::visit([](const auto& spectrum) -> PbrtSpectrumInput { return spectrum; },
                          readSpectrum(*value, albedo, false));
    }

    PbrtSpectrumInput spectrumInput(std::string_view name, PbrtSpectrumInput defaultValue, bool albedo)
    {
        return optionalSpectrum(name, albedo).value_or(std::move(defaultValue));
    }

    PbrtSpectrum spectrum(std::string_view name, PbrtSpectrum defaultValue)
    {
        const auto* value = parameter(name);
        return value ? readSpectrum(*value, false, true) : std::move(defaultValue);
    }

    double number(std::string_view name, double defaultValue)
    {
        parameter(name);
        return scalarParameter<PbrtFloatValues>(entity, name, "float", defaultValue);
    }

    int32_t integer(std::string_view name, int32_t defaultValue)
    {
        parameter(name);
        return static_cast<int32_t>(scalarParameter<PbrtIntegerValues>(entity, name, "integer", defaultValue));
    }

    bool boolean(std::string_view name, bool defaultValue)
    {
        parameter(name);
        return scalarParameter<PbrtBoolValues>(entity, name, "bool", defaultValue) != 0;
    }

    std::string string(std::string_view name, const std::string& defaultValue)
    {
        if (!parameter(name))
            return defaultValue;
        const auto values = arrayParameter<PbrtStringValues>(entity, name, "string");
        CHECK(values.size() == 1, "expected one PBRT string '{}' at {}:{}", name,
              entity.location.file.string(), entity.location.line);
        return values.front();
    }

    std::optional<PbrtFileReference> file(std::string_view name)
    {
        const auto* value = parameter(name);
        if (!value)
            return {};
        const auto values = arrayParameter<PbrtStringValues>(entity, name, "string");
        CHECK(values.size() == 1, "invalid PBRT file '{}' at {}:{}", name,
              entity.location.file.string(), entity.location.line);
        if (values.front().empty())
            return {};
        return parameterReference<PbrtFileReference>(*value);
    }

protected:
    const PbrtEntity& entity;

    const PbrtParameter* parameter(std::string_view name)
    {
        return findParameter(entity, name);
    }

    static void checkSingleTexture(const PbrtParameter& value)
    {
        const auto* names = std::get_if<PbrtStringValues>(&value.value);
        CHECK(names && names->values.size() == 1, "expected one texture for '{}' at {}:{}", value.name,
              value.location.file.string(), value.location.line);
    }
};
}  // namespace pbrt_import
