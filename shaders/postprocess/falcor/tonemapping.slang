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
#include "postprocess/falcor/fullscreen.slang"

enum class ToneMapperOperator : uint
{
    Linear,
    Reinhard,
    ReinhardModified,
    HejiHableAlu,
    HableUc2,
    Aces
};

struct ToneMapperParameters
{
    row_major float3x4 colorTransform;
    float              whiteScale;
    float              whiteMaxLuminance;
    uint               mode;
    uint               autoExposure;
    uint               clampOutput;
};

[[vk::push_constant]] ConstantBuffer<ToneMapperParameters> parameters;
[[vk::binding(0, 0)]] Texture2D<float4>                    sourceImage;
[[vk::binding(1, 0)]] Texture2D<float4>                    luminanceImage;
[[vk::binding(2, 0)]] SamplerState                         linearSampler;
[[vk::binding(3, 0)]] SamplerState                         pointSampler;
static const float                                         kExposureKey  = 0.042;
static const float                                         kLuminanceLod = 16.0;

float calcLuminance(float3 color) { return dot(color, float3(0.299, 0.587, 0.114)); }

// Linear
float3 toneMapLinear(float3 color) { return color; }

// Reinhard
float3 toneMapReinhard(float3 color)
{
    float luminance = calcLuminance(color);
    float reinhard  = luminance / (luminance + 1);
    return color * (reinhard / luminance);
}

// Reinhard with maximum luminance
float3 toneMapReinhardModified(float3 color)
{
    float luminance = calcLuminance(color);
    float reinhard =
        luminance * (1 + luminance / (parameters.whiteMaxLuminance * parameters.whiteMaxLuminance)) * (1 + luminance);
    return color * (reinhard / luminance);
}

// John Hable's ALU approximation of Jim Heji's operator
// http://filmicgames.com/archives/75
float3 toneMapHejiHableAlu(float3 color)
{
    color = max(float(0).rrr, color - 0.004);
    color = (color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7) + 0.06);

    // Result includes sRGB conversion
    return pow(color, float3(2.2));
}

// John Hable's Uncharted 2 filmic tone map
// http://filmicgames.com/archives/75
float3 applyUc2Curve(float3 color)
{
    const float kA = 0.22;  // Shoulder Strength
    const float kB = 0.3;   // Linear Strength
    const float kC = 0.1;   // Linear Angle
    const float kD = 0.2;   // Toe Strength
    const float kE = 0.01;  // Toe Numerator
    const float kF = 0.3;   // Toe Denominator

    color = ((color * (kA * color + kC * kB) + kD * kE) / (color * (kA * color + kB) + kD * kF)) - (kE / kF);
    return color;
}

float3 toneMapHableUc2(float3 color)
{
    float exposureBias = 2.0f;
    color              = applyUc2Curve(exposureBias * color);
    float whiteScale   = 1 / applyUc2Curve(float3(parameters.whiteScale)).x;
    color              = color * whiteScale;

    return color;
}

float3 toneMapAces(float3 color)
{
    // Cancel out the pre-exposure mentioned in
    // https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
    color *= 0.6;

    const float kA = 2.51;
    const float kB = 0.03;
    const float kC = 2.43;
    const float kD = 0.59;
    const float kE = 0.14;

    color = saturate((color * (kA * color + kB)) / (color * (kC * color + kD) + kE));
    return color;
}

float3 toneMap(float3 color)
{
    switch (ToneMapperOperator(parameters.mode))
    {
        case ToneMapperOperator::Linear:
            return toneMapLinear(color);
        case ToneMapperOperator::Reinhard:
            return toneMapReinhard(color);
        case ToneMapperOperator::ReinhardModified:
            return toneMapReinhardModified(color);
        case ToneMapperOperator::HejiHableAlu:
            return toneMapHejiHableAlu(color);
        case ToneMapperOperator::HableUc2:
            return toneMapHableUc2(color);
        case ToneMapperOperator::Aces:
            return toneMapAces(color);
        default:
            return color;
    }
}

[shader("fragment")] float4 fragMain(VertexOutput input) : SV_Target
{
    float4 color      = sourceImage.Sample(pointSampler, input.uv);
    float3 finalColor = color.rgb;

    if (parameters.autoExposure != 0)
    {
        // apply auto exposure
        float avgLuminance   = exp2(luminanceImage.SampleLevel(linearSampler, input.uv, kLuminanceLod).r);
        float pixelLuminance = calcLuminance(finalColor);
        finalColor *= (kExposureKey / avgLuminance);
    }

    // apply color grading
    finalColor = mul((float3x3)parameters.colorTransform, finalColor);

    // apply tone mapping
    finalColor = toneMap(finalColor);

    if (parameters.clampOutput != 0)
    {
        finalColor = saturate(finalColor);
    }

    return float4(finalColor, color.a);
}
