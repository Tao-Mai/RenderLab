#pragma once

#include "core/MetadataAnnotations.h"

#include <cstdint>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

enum class RL_ENUM FalcorToneMappingOperator
{
    Linear RL_ENUM_VALUE("Linear"),
    Reinhard RL_ENUM_VALUE("Reinhard"),
    ReinhardModified RL_ENUM_VALUE("ReinhardModified"),
    HejiHableAlu RL_ENUM_VALUE("HejiHableAlu"),
    HableUc2 RL_ENUM_VALUE("HableUc2"),
    Aces RL_ENUM_VALUE("Aces"),
};

enum class RL_ENUM FalcorExposureMode
{
    AperturePriority RL_ENUM_VALUE("AperturePriority"),
    ShutterPriority RL_ENUM_VALUE("ShutterPriority"),
};

namespace falcorPostProcess
{
inline constexpr uint32_t kBloomLevels             = 8;
inline constexpr float    kExposureCompensationMin = -12.0f;
inline constexpr float    kExposureCompensationMax = 12.0f;
inline constexpr float    kFilmSpeedMin            = 1.0f;
inline constexpr float    kFilmSpeedMax            = 6400.0f;
inline constexpr float    kFNumberMin              = 0.1f;
inline constexpr float    kFNumberMax              = 100.0f;
inline constexpr float    kShutterMin              = 0.1f;
inline constexpr float    kShutterMax              = 10000.0f;
inline constexpr float    kWhitePointMin           = 1905.0f;
inline constexpr float    kWhitePointMax           = 25000.0f;
}  // namespace falcorPostProcess

struct RL_RECORD FalcorToneMapperConfig
{
    RL_FIELD("exposureCompensation") float                     exposureCompensation = 0.0f;
    RL_FIELD("autoExposure") bool                      autoExposure         = false;
    RL_FIELD("filmSpeed") float                     filmSpeed            = 100.0f;
    RL_FIELD("fNumber") float                     fNumber              = 1.0f;
    RL_FIELD("shutter") float                     shutter              = 1.0f;  // Reciprocal shutter time, as in Falcor.
    RL_FIELD("exposureMode") FalcorExposureMode        exposureMode         = FalcorExposureMode::AperturePriority;
    RL_FIELD("whiteBalance") bool                      whiteBalance         = false;
    RL_FIELD("whitePoint") float                     whitePoint           = 6500.0f;
    RL_FIELD("mode") FalcorToneMappingOperator mode                 = FalcorToneMappingOperator::Aces;
    RL_FIELD("clampOutput") bool                      clampOutput          = true;
    RL_FIELD("whiteMaxLuminance") float                     whiteMaxLuminance    = 1.0f;
    RL_FIELD("whiteScale") float                     whiteScale           = 11.2f;

    [[nodiscard]] float     exposureValue() const;
    void                    setExposureValue(float value);
    [[nodiscard]] glm::mat3 whiteBalanceTransform() const;
    [[nodiscard]] glm::mat3 colorTransform() const;
};

struct RL_RECORD FalcorPostFxConfig
{
    RL_FIELD("enabled") bool      enabled                   = true;
    RL_FIELD("wipe") float     wipe                      = 0.0f;
    RL_FIELD("bloomAmount") float     bloomAmount               = 0.0f;
    RL_FIELD("starAmount") float     starAmount                = 0.0f;
    RL_FIELD("starAngle") float     starAngle                 = 0.1f;
    RL_FIELD("vignetteAmount") float     vignetteAmount            = 0.0f;
    RL_FIELD("chromaticAberrationAmount") float     chromaticAberrationAmount = 0.0f;
    RL_FIELD("barrelDistortAmount") float     barrelDistortAmount       = 0.0f;
    RL_FIELD("saturationCurve") glm::vec3 saturationCurve{1.0f};
    RL_FIELD("colorOffset") glm::vec3 colorOffset{0.5f};
    RL_FIELD("colorScale") glm::vec3 colorScale{0.5f};
    RL_FIELD("colorPower") glm::vec3 colorPower{0.5f};
    RL_FIELD("colorOffsetScalar") float     colorOffsetScalar = 0.0f;
    RL_FIELD("colorScaleScalar") float     colorScaleScalar  = 0.0f;
    RL_FIELD("colorPowerScalar") float     colorPowerScalar  = 0.0f;

    [[nodiscard]] bool bypass() const;
    [[nodiscard]] bool usesBloom() const;
};

struct RL_RECORD FalcorPostProcessConfig
{
    RL_FIELD("toneMapper") FalcorToneMapperConfig toneMapper;
    RL_FIELD("simplePostFx") FalcorPostFxConfig     simplePostFx;

    void validate() const;
};
