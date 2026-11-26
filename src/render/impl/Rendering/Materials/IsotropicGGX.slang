/***************************************************************************
 # Copyright (c) 2015-24, NVIDIA CORPORATION. All rights reserved.
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
#ifndef RENDERLAB_ISOTROPIC_GGX_SLANG
#define RENDERLAB_ISOTROPIC_GGX_SLANG

#include "math/constant.slang"

static const float kMinCosTheta = 1e-6;
static const float kMinGGXAlpha = 0.0064;

float evalNdfGGX(float alpha, float cosTheta)
{
    float alphaSquared = alpha * alpha;
    float denominator = (cosTheta * alphaSquared - cosTheta) * cosTheta + 1.0;

    return alphaSquared / (denominator * denominator * PI);
}

float evalLambdaGGX(float alphaSquared, float cosTheta)
{
    float cosThetaSquared = cosTheta * cosTheta;
    float tanThetaSquared = max(1.0 - cosThetaSquared, 0.0) / cosThetaSquared;

    return 0.5 * (sqrt(1.0 + alphaSquared * tanThetaSquared) - 1.0);
}

float evalMaskingSmithGGXCorrelated(float alpha, float cosThetaI, float cosThetaO)
{
    return 1.0 / (1.0 + evalLambdaGGX(alpha * alpha, cosThetaI) + evalLambdaGGX(alpha * alpha, cosThetaO));
}

float evalPdfGGX_VNDF(float alpha, float3 wi, float3 halfVector)
{
    float g1 = 1.0 / (1.0 + evalLambdaGGX(alpha * alpha, wi.z));

    return g1 * evalNdfGGX(alpha, halfVector.z) * max(0.0, dot(wi, halfVector)) / wi.z;
}

// Falcor's spherical-cap VNDF sampler (Dupuy and Benyoub, 2023).
float3 sampleGGX_VNDF(float alpha, float3 wi, float2 sampleValue)
{
    float3 stretchedView = normalize(float3(alpha * wi.xy, wi.z));
    float phi = 2.0 * PI * sampleValue.x;
    float z = fma(1.0 - sampleValue.y, 1.0 + stretchedView.z, -stretchedView.z);
    float radius = sqrt(saturate(1.0 - z * z));
    float3 halfVector = float3(radius * cos(phi), radius * sin(phi), z) + stretchedView;

    return normalize(float3(alpha * halfVector.xy, halfVector.z));
}

#endif
