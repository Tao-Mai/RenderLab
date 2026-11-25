#pragma once

#include <cstdint>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

enum class FalcorToneMappingOperator
{
    Linear,
    Reinhard,
    ReinhardModified,
    HejiHableAlu,
    HableUc2,
    Aces,
};

enum class FalcorExposureMode
{
    AperturePriority,
    ShutterPriority,
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

struct FalcorToneMapperConfig
{
    float                     exposureCompensation = 0.0f;

    bool                      autoExposure         = false;

    float                     filmSpeed            = 100.0f;

    float                     fNumber              = 1.0f;

    float                     shutter              = 1.0f;  // Reciprocal shutter time, as in Falcor.
    FalcorExposureMode        exposureMode         = FalcorExposureMode::AperturePriority;

    bool                      whiteBalance         = false;

    float                     whitePoint           = 6500.0f;

    FalcorToneMappingOperator mode                 = FalcorToneMappingOperator::Aces;

    bool                      clampOutput          = true;

    float                     whiteMaxLuminance    = 1.0f;

    float                     whiteScale           = 11.2f;

    bool operator==(const FalcorToneMapperConfig&) const = default;

    [[nodiscard]] float     exposureValue() const;
    void                    setExposureValue(float value);
    [[nodiscard]] glm::mat3 whiteBalanceTransform() const;
    [[nodiscard]] glm::mat3 colorTransform() const;
};

struct FalcorPostFxConfig
{
    bool      enabled                   = true;

    float     wipe                      = 0.0f;

    float     bloomAmount               = 0.0f;

    float     starAmount                = 0.0f;

    float     starAngle                 = 0.1f;

    float     vignetteAmount            = 0.0f;

    float     chromaticAberrationAmount = 0.0f;

    float     barrelDistortAmount       = 0.0f;

    glm::vec3 saturationCurve{1.0f};

    glm::vec3 colorOffset{0.5f};

    glm::vec3 colorScale{0.5f};

    glm::vec3 colorPower{0.5f};

    float     colorOffsetScalar = 0.0f;

    float     colorScaleScalar  = 0.0f;

    float     colorPowerScalar  = 0.0f;

    bool operator==(const FalcorPostFxConfig&) const = default;

    [[nodiscard]] bool bypass() const;
    [[nodiscard]] bool usesBloom() const;
};

struct FalcorPostProcessConfig
{
    FalcorToneMapperConfig toneMapper;

    FalcorPostFxConfig     simplePostFx;

    bool operator==(const FalcorPostProcessConfig&) const = default;

    void validate() const;
};
