#pragma once

#include "core/MetadataAnnotations.h"

#include <cmath>

inline constexpr float kExposureCalibration = 1.2f;

enum class RL_ENUM ExposureMode
{
    Manual RL_ENUM_VALUE("Manual"),
    Automatic RL_ENUM_VALUE("Automatic"),
};

struct RL_RECORD ExposureConfig
{
    RL_FIELD("mode") ExposureMode mode                 = ExposureMode::Manual;
    RL_FIELD("ev100") float        ev100                = 0.0f;
    RL_FIELD("exposureCompensation") float        exposureCompensation = 0.0f;  // Stops: +1 doubles brightness.

    struct RL_RECORD Automatic
    {
        RL_FIELD("minEv100") float minEv100              = -6.0f;
        RL_FIELD("maxEv100") float maxEv100              = 20.0f;
        RL_FIELD("brightAdaptationSpeed") float brightAdaptationSpeed = 3.0f;
        RL_FIELD("darkAdaptationSpeed") float darkAdaptationSpeed   = 1.0f;
        RL_FIELD("lowPercentile") float lowPercentile         = 0.02f;
        RL_FIELD("highPercentile") float highPercentile        = 0.98f;

        bool operator==(const Automatic&) const = default;
    } automatic RL_FIELD("automatic");

    bool operator==(const ExposureConfig&) const = default;

    [[nodiscard]] float exposureScale() const { return std::exp2(exposureCompensation - ev100) / kExposureCalibration; }

    [[nodiscard]] bool isValid() const
    {
        const float scale          = exposureScale();
        const float brightestScale = std::exp2(exposureCompensation - automatic.minEv100) / kExposureCalibration;
        const float darkestScale   = std::exp2(exposureCompensation - automatic.maxEv100) / kExposureCalibration;
        return (mode == ExposureMode::Manual || mode == ExposureMode::Automatic) && std::isfinite(ev100) &&
            std::isfinite(exposureCompensation) && std::isfinite(scale) && scale > 0.0f &&
            std::isfinite(automatic.minEv100) && std::isfinite(automatic.maxEv100) && automatic.minEv100 >= -16.0f &&
            automatic.maxEv100 <= 24.0f && automatic.minEv100 <= automatic.maxEv100 &&
            std::isfinite(automatic.brightAdaptationSpeed) && automatic.brightAdaptationSpeed > 0.0f &&
            std::isfinite(automatic.darkAdaptationSpeed) && automatic.darkAdaptationSpeed > 0.0f &&
            std::isfinite(automatic.lowPercentile) && std::isfinite(automatic.highPercentile) &&
            automatic.lowPercentile >= 0.0f && automatic.lowPercentile < automatic.highPercentile &&
            automatic.highPercentile <= 1.0f && std::isfinite(brightestScale) && darkestScale > 0.0f;
    }
};

#include RL_METADATA_HEADER("metadata/src/render/ExposureConfig.generated.h")
