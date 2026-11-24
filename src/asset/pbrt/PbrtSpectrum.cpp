#include "asset/pbrt/PbrtSpectrum.h"
#include "asset/pbrt/PbrtData.h"
#include "asset/pbrt/PbrtFrontend.h"
#include "asset/pbrt/PbrtParameters.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace pbrt_import
{
PbrtSpectrum readSpectrum(const PbrtParameter& parameter, bool albedo, bool allowFloat)
{
    const auto invalid = [&]
    {
        CHECK(
            false,
            "invalid PBRT spectrum '{}' at {}:{}",
            parameter.name,
            parameter.location.file.string(),
            parameter.location.line
        );
    };
    if (parameter.type == "spectrum")
    {
        if (const auto* values = std::get_if<PbrtStringValues>(&parameter.value))
        {
            CHECK(
                values->values.size() == 1,
                "expected one spectrum for '{}' at {}:{}",
                parameter.name,
                parameter.location.file.string(),
                parameter.location.line
            );
            if (isNamedSpectrum(values->values.front()))
                return PbrtNamedSpectrum{values->values.front()};
            return PbrtSpectrumFile{parameterReference<PbrtFileReference>(parameter)};
        }
        if (const auto* values = std::get_if<PbrtFloatValues>(&parameter.value))
            return PbrtSampledSpectrum{*values};
        if (const auto* array = std::get_if<PbrtArrayReference>(&parameter.value))
        {

            return PbrtSampledSpectrum{*array};
        }
    }
    const auto* values = std::get_if<PbrtFloatValues>(&parameter.value);
    if (!values)
    {
        invalid();
        std::unreachable();
    }
    if (parameter.type == "rgb" && values->values.size() == 3)
    {
        const glm::dvec3 rgb{values->values[0], values->values[1], values->values[2]};
        CHECK(
            rgb.x >= 0 && rgb.y >= 0 && rgb.z >= 0 && (!albedo || (rgb.x <= 1 && rgb.y <= 1 && rgb.z <= 1)),
            "invalid PBRT RGB spectrum '{}' at {}:{}",
            parameter.name,
            parameter.location.file.string(),
            parameter.location.line
        );
        return PbrtRgbSpectrum{rgb, parameter.colorSpace};
    }
    if (parameter.type == "blackbody" && values->values.size() == 1)
        return PbrtBlackbodySpectrum{values->values[0]};
    if (allowFloat && parameter.type == "float" && values->values.size() == 1)
        return PbrtConstantSpectrum{values->values[0]};
    invalid();
    std::unreachable();
}

}  // namespace pbrt_import

glm::vec3 pbrtPreviewRgb(const PbrtSpectrum& spectrum, const AssetDataManager& assetData)
{
    return std::visit(
        [&]<class T>(const T& value) -> glm::vec3
        {
            if constexpr (std::same_as<T, PbrtConstantSpectrum>)
                return glm::vec3(value.value);
            else if constexpr (std::same_as<T, PbrtRgbSpectrum>)
                return glm::vec3(value.value);
            else if constexpr (std::same_as<T, PbrtBlackbodySpectrum>)
            {
                if (value.temperature <= 0.0)
                    return glm::vec3{0.0f};
                constexpr double kMinimumPreviewTemperature = 100.0;
                constexpr double kMaximumPreviewTemperature = 1e9;
                const double     temperature =
                    std::clamp(value.temperature, kMinimumPreviewTemperature, kMaximumPreviewTemperature);
                constexpr double     kSecondRadiationConstantNm = 1.438776877e7;
                constexpr glm::dvec3 kPreviewWavelengths{610, 550, 460};
                glm::dvec3           rgb;
                for (int channel = 0; channel < 3; ++channel)
                {
                    const auto wavelength = kPreviewWavelengths[channel];
                    rgb[channel]          = 1.0 /
                        (std::pow(wavelength, 5) * std::expm1(kSecondRadiationConstantNm / (wavelength * temperature)));
                }
                return glm::vec3(rgb / std::max({rgb.x, rgb.y, rgb.z}));
            }
            else if constexpr (std::same_as<T, PbrtSampledSpectrum>)
            {
                const auto samples = std::visit(
                    [&]<class Array>(const Array& data) -> PbrtFloatValues
                    {
                        if constexpr (std::same_as<Array, PbrtFloatValues>)
                            return data;
                        else
                            return std::get<PbrtFloatValues>(readPbrtArray(assetData, data));
                    },
                    value.samples
                );
                CHECK(samples.values.size() >= 2 && samples.values.size() % 2 == 0);
                if (samples.values.size() == 2)
                    return glm::vec3{static_cast<float>(samples.values[1])};
                const auto evaluate = [&](double wavelength)
                {
                    if (wavelength < samples.values.front() || wavelength > samples.values[samples.values.size() - 2])
                        return 0.0;
                    for (size_t index = 2; index < samples.values.size(); index += 2)
                        if (wavelength <= samples.values[index])
                        {
                            const auto t = (wavelength - samples.values[index - 2]) /
                                (samples.values[index] - samples.values[index - 2]);
                            return std::lerp(samples.values[index - 1], samples.values[index + 1], t);
                        }
                    std::unreachable();
                };
                return {
                    static_cast<float>(evaluate(610)),
                    static_cast<float>(evaluate(550)),
                    static_cast<float>(evaluate(460))
                };
            }
            // Named spectra and external SPDs retain their complete assets; the RGB preview uses neutral white.
            else
                return glm::vec3{1.0f};
        },
        spectrum
    );
}
