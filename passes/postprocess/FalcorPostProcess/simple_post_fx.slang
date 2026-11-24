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

#include "math/color.slang"

// The effect kernels and their numerical quirks are preserved from Falcor SimplePostFX.
#if POST_FX_STAGE == 0
struct Parameters
{
    uint2  resolution;
    float2 invRes;
};
#elif POST_FX_STAGE == 1
struct Parameters
{
    uint2  resolution;
    float2 invRes;
    float2 starDir1;
    float2 starDir2;
    float2 starDir3;
    float  bloomAmount;
    float  star;
    uint   inPlace;
};
#else
struct Parameters
{
    uint2  resolution;
    float2 invRes;
    float  saturationQuadratic;
    float  saturationLinear;
    float  saturationConstant;
    float  vignetteAmount;
    float3 colorOffset;
    float  chromaticAberrationAmount;
    float3 colorScale;
    float  barrelScale;
    float3 colorPower;
    float  barrelAmount;
    float  wipe;
    uint   bypass;
};
#endif

ConstantBuffer<Parameters> parameters;
Texture2D<float4>          sourceImage;
Texture2D<float4>          bloomedImage;
#if POST_FX_STAGE == 2
RWTexture2D<float4> destinationImage;
#else
RWTexture2D<float4> destinationImage;
#endif
SamplerState linearSampler;

#if POST_FX_STAGE != 2
float4 blurFilter2x2(Texture2D<float4> src, const float2 uv, float uscale, float vscale)
{
    // 2x2 bilinear-tap kernel, effective footprint 4x4, with offsets chosen to sample from the binomial 1 3 3 1
    uscale *= 0.75f;
    vscale *= 0.75f;
    // clang-format off
    return 0.25f * (
        src.SampleLevel(linearSampler, uv + float2(-uscale, -vscale), 0.f) +
        src.SampleLevel(linearSampler, uv + float2(uscale, -vscale), 0.f) +
        src.SampleLevel(linearSampler, uv + float2(-uscale, vscale), 0.f) +
        src.SampleLevel(linearSampler, uv + float2(uscale, vscale), 0.f));
    // clang-format on
}

float4 blurFilter3x3(Texture2D<float4> src, const float2 uv, const float uscale, const float vscale)
{
    // 3x3 bilinear-tap kernel, effective footprint 6x6, with offsets chosen to sample from the binomial 1 5 10 10 5 1
    const float kW0 = (10.f + 10.f) / 32.f;
    const float kX1 = 1.5f + 1.f / (1.f + 5.f), kW1 = (1.f + 5.f) / 32.f;
    const float kX1u = kX1 * uscale;
    const float kX1v = kX1 * vscale;
    // clang-format off
    return
        (src.SampleLevel(linearSampler, uv + float2(-kX1u, -kX1v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(kX1u, -kX1v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(-kX1u, kX1v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(kX1u, kX1v), 0.f)) * (kW1 * kW1) +
        (src.SampleLevel(linearSampler, uv + float2(0.f, -kX1v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(0.f, kX1v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(-kX1u, 0.f), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(kX1u, 0.f), 0.f)) * (kW1 * kW0) +
        (src.SampleLevel(linearSampler, uv + float2(0.f, 0.f), 0.f)) * (kW0 * kW0);
    // clang-format on
}

float4 blurFilter5x5(Texture2D<float4> src, const float2 uv, const float uscale, const float vscale)
{
    // 5x5 bilinear-tap kernel, effective footprint 10x10, with offsets chosen to sample from the binomial 1 9 36 84 126
    // 126 84 32 9 1
    const float kW0 = (126.f + 126.f) / 512.f;
    const float kX1 = 1.5f + 36.f / (36.f + 84.f), kW1 = (36.f + 84.f) / 512.f;
    const float kX2 = 3.5f + 1.f / (1.f + 9.f), kW2 = (1.f + 10.f) / 512.f;
    const float kX1u = kX1 * uscale, kX2u = kX2 * uscale;
    const float kX1v = kX1 * vscale, kX2v = kX2 * vscale;
    // clang-format off
    float4 s0 =
        (src.SampleLevel(linearSampler, uv + float2(-kX2u, -kX2v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(kX2u, -kX2v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(-kX2u, kX2v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(kX2u, kX2v), 0.f)) * (kW2 * kW2) +
        (src.SampleLevel(linearSampler, uv + float2(-kX1u, -kX2v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(kX1u, -kX2v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(-kX1u, kX2v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(kX1u, kX2v), 0.f)) * (kW1 * kW2) +
        (src.SampleLevel(linearSampler, uv + float2(0.f, -kX2v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(0.f, kX2v), 0.f)) * (kW0 * kW2);
    float4 s1 =
        (src.SampleLevel(linearSampler, uv + float2(-kX2u, -kX1v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(kX2u, -kX1v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(-kX2u, kX1v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(kX2u, kX1v), 0.f)) * (kW2 * kW1) +
        (src.SampleLevel(linearSampler, uv + float2(-kX1u, -kX1v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(kX1u, -kX1v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(-kX1u, kX1v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(kX1u, kX1v), 0.f)) * (kW1 * kW1) +
        (src.SampleLevel(linearSampler, uv + float2(0.f, -kX1v), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(0.f, kX1v), 0.f)) * (kW0 * kW1);
    float4 s2 =
        (src.SampleLevel(linearSampler, uv + float2(-kX2u, 0.f), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(kX2u, 0.f), 0.f)) * (kW2 * kW0) +
        (src.SampleLevel(linearSampler, uv + float2(-kX1u, 0.f), 0.f) +
         src.SampleLevel(linearSampler, uv + float2(kX1u, 0.f), 0.f)) * (kW1 * kW0) +
        (src.SampleLevel(linearSampler, uv + float2(0.f, 0.f), 0.f)) * (kW0 * kW0);
    // clang-format on
    return s0 + s1 + s2;
}

#endif

#if POST_FX_STAGE == 0
[shader("compute")][numthreads(16, 16, 1)] void computeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= parameters.resolution))
        return;
    int2   pixelPos            = dispatchThreadId.xy;
    float2 uv                  = (pixelPos + 0.5f) * parameters.invRes;
    destinationImage[pixelPos] = blurFilter5x5(sourceImage, uv, parameters.invRes.x * 0.5f, parameters.invRes.y * 0.5f);
}

#elif POST_FX_STAGE == 1
[shader("compute")][numthreads(16, 16, 1)] void computeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= parameters.resolution))
        return;
    int2   pixelPos  = dispatchThreadId.xy;
    float2 uv        = (pixelPos + 0.5f) * parameters.invRes;
    float4 centertap = blurFilter3x3(bloomedImage, uv, parameters.invRes.x, parameters.invRes.y);
    if (parameters.star > 0.f)
    {
        float4 star = 0.f;
        for (int i = -20; i <= 20; ++i)
        {
            float  bri = 1.f / (10 + i * i);
            float4 c   = bloomedImage.SampleLevel(linearSampler, uv + parameters.starDir1 * i * 2, 0.f) +
                bloomedImage.SampleLevel(linearSampler, uv + parameters.starDir2 * i * 2, 0.f) +
                bloomedImage.SampleLevel(linearSampler, uv + parameters.starDir3 * i * 2, 0.f);
            star += c * bri;
        }
        star *= 1.f / 1.793358534742959f;  // normalize by the total of the weights of star taps
        destinationImage[pixelPos] =
            lerp(lerp(destinationImage[pixelPos], centertap, parameters.bloomAmount), star, parameters.star);
    }
    else
    {
        if (parameters.inPlace != 0)
        {
            destinationImage[pixelPos] = lerp(destinationImage[pixelPos], centertap, parameters.bloomAmount);
        }
        else
        {
            destinationImage[pixelPos] = lerp(sourceImage[pixelPos], centertap, parameters.bloomAmount);
        }
    }
}

#else
[shader("compute")][numthreads(16, 16, 1)] void computeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= parameters.resolution))
        return;
    const uint2 pixelPos = dispatchThreadId.xy;
    if (parameters.bypass != 0)
    {
        destinationImage[pixelPos] = sourceImage[pixelPos];
        return;
    }

    float2 circularUv = ((pixelPos + 0.5f) * 2.f - parameters.resolution.xy) *
        parameters.invRes.y;  // -1 to 1 vertically; more than -1 to 1 horizontally.
    float radiusSquared = dot(circularUv, circularUv);

    float barrel = (1.f + radiusSquared * parameters.barrelAmount) * parameters.barrelScale;
    circularUv *= barrel;

    float3 col;
    float2 ndc = float2(parameters.resolution.y * parameters.invRes.x * 0.5f, 0.5f);
    float  ca  = 1.f + parameters.chromaticAberrationAmount;
    if (ca != 1.f)
    {
        col.x = bloomedImage.SampleLevel(linearSampler, circularUv * ndc / ca + 0.5f, 0.f).x;
        col.y = bloomedImage.SampleLevel(linearSampler, circularUv * ndc + 0.5f, 0.f).y;
        col.z = bloomedImage.SampleLevel(linearSampler, circularUv * ndc * ca + 0.5f, 0.f).z;
    }
    else
    {
        col = bloomedImage.SampleLevel(linearSampler, circularUv * ndc + 0.5f, 0.f).xyz;
    }

    // eyeballing the illuminance graphs with radius in
    // https://grail.cs.washington.edu/projects/vignette/vign.iccv05.pdf, it seems like there's quite a variety of
    // falloffs with radius. let's go with a cheap circular falloff for now.
    float vignette = max(0.f, 1.f - radiusSquared * parameters.vignetteAmount);
    col *= vignette;

    float luma = rec709Luminance(col);
    // squashedLuma is used to partition the luminance range into a shadow-mid-bright, which we then fit a quadratic
    // curve thru the desired saturation
    float squashedLuma = saturate(luma / (luma + 0.5f));
    float saturation =
        max(0.f,
            parameters.saturationConstant + squashedLuma * parameters.saturationLinear +
                squashedLuma * squashedLuma * parameters.saturationQuadratic);
    col = lerp(luma, col, saturation);

    // ASC-CDL style offset,power,slope (as implemented in blender, see
    // https://blender.stackexchange.com/questions/55231/what-is-the-the-asc-cdl-node)
    col = pow(max(0.f, col * parameters.colorScale + parameters.colorOffset), parameters.colorPower);

    float4 output = float4(col, 1.);
    if (pixelPos.x < parameters.wipe)
    {
        output = sourceImage[pixelPos];
    }
    destinationImage[pixelPos] = output;
}
#endif
