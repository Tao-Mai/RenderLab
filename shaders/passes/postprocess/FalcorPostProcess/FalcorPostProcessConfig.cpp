/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "shaders/passes/postprocess/FalcorPostProcess/FalcorPostProcessConfig.h"

#include "core/Logger.h"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/matrix.hpp>

namespace
{
glm::vec3 colorTemperatureToXyz(float temperature)
{
    const double t  = temperature;
    const double t2 = t * t;
    const double t3 = t * t * t;
    const double x  = temperature < 4000.0f ? -0.2661239e9 / t3 - 0.2343580e6 / t2 + 0.8776956e3 / t + 0.179910
                                            : -3.0258469e9 / t3 + 2.1070379e6 / t2 + 0.2226347e3 / t + 0.240390;
    const double x2 = x * x;
    const double x3 = x * x * x;
    const double y  = temperature < 2222.0f ? -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683
         : temperature < 4000.0f            ? -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867
                                            : 3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
    const float  chromaticityX = static_cast<float>(x);
    const float  chromaticityY = static_cast<float>(y);
    return {chromaticityX / chromaticityY, 1.0f, (1.0f - chromaticityX - chromaticityY) / chromaticityY};
}
}  // namespace

float FalcorToneMapperConfig::exposureValue() const { return std::log2(shutter * fNumber * fNumber); }

void FalcorToneMapperConfig::setExposureValue(float value)
{
    using namespace falcorPostProcess;
    const float minValue = std::log2(kShutterMin * kFNumberMin * kFNumberMin);
    const float maxValue = std::log2(kShutterMax * kFNumberMax * kFNumberMax);
    value                = std::clamp(value, minValue, maxValue);
    if (exposureMode == FalcorExposureMode::AperturePriority)
        shutter = std::clamp(std::pow(2.0f, value) / (fNumber * fNumber), kShutterMin, kShutterMax);
    else
        fNumber = std::clamp(std::sqrt(std::pow(2.0f, value) / shutter), kFNumberMin, kFNumberMax);
}

glm::mat3 FalcorToneMapperConfig::whiteBalanceTransform() const
{
    if (!whiteBalance)
        return glm::mat3{1.0f};

    // Falcor ColorUtils.h: Rec.709 <-> XYZ <-> CAT02 LMS, specified by rows.
    static const glm::mat3 kRgbToXyz = glm::transpose(
        glm::mat3{
            {0.4123907992659595f, 0.3575843393838780f, 0.1804807884018343f},
            {0.2126390058715104f, 0.7151686787677559f, 0.0721923153607337f},
            {0.0193308187155918f, 0.1191947797946259f, 0.9505321522496608f},
        }
    );
    static const glm::mat3 kXyzToRgb = glm::transpose(
        glm::mat3{
            {3.2409699419045213f, -1.5373831775700935f, -0.4986107602930033f},
            {-0.9692436362808798f, 1.8759675015077206f, 0.0415550574071756f},
            {0.0556300796969936f, -0.2039769588889765f, 1.0569715142428784f},
        }
    );
    static const glm::mat3 kXyzToLms = glm::transpose(
        glm::mat3{
            {0.7328f, 0.4296f, -0.1624f},
            {-0.7036f, 1.6975f, 0.0061f},
            {0.0030f, 0.0136f, 0.9834f},
        }
    );
    static const glm::mat3 kLmsToXyz = glm::transpose(
        glm::mat3{
            {1.096123820835514f, -0.278869000218287f, 0.182745179382773f},
            {0.454369041975359f, 0.473533154307412f, 0.072097803717229f},
            {-0.009627608738429f, -0.005698031216113f, 1.015325639954543f},
        }
    );
    static const glm::mat3 kRgbToLms         = kXyzToLms * kRgbToXyz;
    static const glm::mat3 kLmsToRgb         = kXyzToRgb * kLmsToXyz;
    static const glm::vec3 kDestinationWhite = kXyzToLms * colorTemperatureToXyz(6500.0f);
    const auto             scale             = kDestinationWhite / (kXyzToLms * colorTemperatureToXyz(whitePoint));
    glm::mat3              diagonal{1.0f};
    for (glm::length_t index = 0; index < 3; ++index)
        diagonal[index][index] = scale[index];
    return (kLmsToRgb * diagonal) * kRgbToLms;
}

glm::mat3 FalcorToneMapperConfig::colorTransform() const
{
    const float exposureScale       = std::pow(2.0f, exposureCompensation);
    const float manualExposureScale = autoExposure ? 1.0f : (0.01f * filmSpeed) / (shutter * fNumber * fNumber);
    return whiteBalanceTransform() * exposureScale * manualExposureScale;
}

bool FalcorPostFxConfig::bypass() const
{
    // Preserve SimplePostFX's identity predicate, including its omission of vignetteAmount.
    return !enabled || wipe >= 1.0f ||
        (bloomAmount == 0.0f && chromaticAberrationAmount == 0.0f && barrelDistortAmount == 0.0f &&
         saturationCurve == glm::vec3{1.0f} && colorOffset == glm::vec3{0.5f} && colorScale == glm::vec3{0.5f} &&
         colorPower == glm::vec3{0.5f} && colorOffsetScalar == 0.0f && colorScaleScalar == 0.0f &&
         colorPowerScalar == 0.0f);
}

bool FalcorPostFxConfig::usesBloom() const { return !bypass() && bloomAmount > 0.0f; }

void FalcorPostProcessConfig::validate() const
{
    using namespace falcorPostProcess;
    const auto inRange = [](float value, float minValue, float maxValue)
    { return std::isfinite(value) && value >= minValue && value <= maxValue; };

    CHECK(inRange(toneMapper.exposureCompensation, kExposureCompensationMin, kExposureCompensationMax));
    CHECK(inRange(toneMapper.filmSpeed, kFilmSpeedMin, kFilmSpeedMax));
    CHECK(inRange(toneMapper.fNumber, kFNumberMin, kFNumberMax));
    CHECK(inRange(toneMapper.shutter, kShutterMin, kShutterMax));
    CHECK(inRange(toneMapper.whitePoint, kWhitePointMin, kWhitePointMax));
    CHECK(std::isfinite(toneMapper.whiteMaxLuminance) && toneMapper.whiteMaxLuminance > 0.0f);
    CHECK(std::isfinite(toneMapper.whiteScale) && toneMapper.whiteScale >= 0.001f);
    CHECK(toneMapper.exposureMode == FalcorExposureMode::AperturePriority ||
          toneMapper.exposureMode == FalcorExposureMode::ShutterPriority);
    CHECK(toneMapper.mode >= FalcorToneMappingOperator::Linear && toneMapper.mode <= FalcorToneMappingOperator::Aces);

    const auto& postFx = simplePostFx;
    CHECK(inRange(postFx.wipe, 0.0f, 1.0f));
    CHECK(inRange(postFx.bloomAmount, 0.0f, 1.0f));
    CHECK(inRange(postFx.starAmount, 0.0f, 1.0f));
    CHECK(inRange(postFx.starAngle, 0.0f, 1.0f));
    CHECK(inRange(postFx.vignetteAmount, 0.0f, 1.0f));
    CHECK(inRange(postFx.chromaticAberrationAmount, 0.0f, 1.0f));
    CHECK(inRange(postFx.barrelDistortAmount, 0.0f, 1.0f));
    CHECK(inRange(postFx.colorOffsetScalar, -1.0f, 1.0f));
    CHECK(inRange(postFx.colorScaleScalar, -1.0f, 1.0f));
    CHECK(inRange(postFx.colorPowerScalar, -1.0f, 1.0f));
    for (glm::length_t index = 0; index < 3; ++index)
    {
        CHECK(inRange(postFx.saturationCurve[index], 0.0f, 2.0f));
        CHECK(inRange(postFx.colorOffset[index], 0.0f, 1.0f));
        CHECK(inRange(postFx.colorScale[index], 0.0f, 1.0f));
        CHECK(inRange(postFx.colorPower[index], 0.0f, 1.0f));
    }
}
