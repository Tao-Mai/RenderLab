#pragma once

#include "core/Logger.h"

#include <cmath>

enum class ExposureMode
{
    Manual,
    Automatic,
};

struct ExposurePassSettings
{
    ExposureMode mode = ExposureMode::Manual;

    float ev100 = 0.0f;

    float exposureCompensation = 0.0f;

    struct Automatic
    {
        float minEv100 = -6.0f;

        float maxEv100 = 20.0f;

        float brightAdaptationSpeed = 3.0f;

        float darkAdaptationSpeed = 1.0f;

        float lowPercentile = 0.02f;

        float highPercentile = 0.98f;

        bool operator==(const Automatic&) const = default;
    };

    Automatic automatic;

    bool operator==(const ExposurePassSettings&) const = default;

    void validate() const
    {
        constexpr float kExposureCalibration = 1.2f;
        const float scale          = std::exp2(exposureCompensation - ev100) / kExposureCalibration;
        const float brightestScale = std::exp2(exposureCompensation - automatic.minEv100) / kExposureCalibration;
        const float darkestScale   = std::exp2(exposureCompensation - automatic.maxEv100) / kExposureCalibration;
        CHECK(
            (mode == ExposureMode::Manual || mode == ExposureMode::Automatic) && std::isfinite(ev100) &&
            std::isfinite(exposureCompensation) && std::isfinite(scale) && scale > 0.0f &&
            std::isfinite(automatic.minEv100) && std::isfinite(automatic.maxEv100) && automatic.minEv100 >= -16.0f &&
            automatic.maxEv100 <= 24.0f && automatic.minEv100 <= automatic.maxEv100 &&
            std::isfinite(automatic.brightAdaptationSpeed) && automatic.brightAdaptationSpeed > 0.0f &&
            std::isfinite(automatic.darkAdaptationSpeed) && automatic.darkAdaptationSpeed > 0.0f &&
            std::isfinite(automatic.lowPercentile) && std::isfinite(automatic.highPercentile) &&
            automatic.lowPercentile >= 0.0f && automatic.lowPercentile < automatic.highPercentile &&
            automatic.highPercentile <= 1.0f && std::isfinite(brightestScale) && darkestScale > 0.0f
        );
    }
};
