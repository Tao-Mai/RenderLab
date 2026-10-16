#ifndef RENDERLAB_SCENE_LIGHTING_SLANG
#define RENDERLAB_SCENE_LIGHTING_SLANG

#include "brdf.slang"
#include "scene_data.slang"
#include "sampling.slang"
#include "surface.slang"

#ifndef ENABLE_IBL
#error ENABLE_IBL must be specified when compiling scene lighting
#endif

// Scene shadow resources (bindings 7..12).
[[vk::binding(7, 0)]]
TextureCubeArray<float> pointShadowMap;

[[vk::binding(8, 0)]]
Texture2DArray<float> spotShadowMap;

[[vk::binding(9, 0)]]
Texture2DArray<float> rectAreaShadowMap;

[[vk::binding(10, 0)]]
Texture2DArray<float> cascadeShadowMap;

[[vk::binding(11, 0)]]
SamplerComparisonState shadowSampler;

[[vk::binding(12, 0)]]
SamplerState linearSampler;

// Scene environment image / sampler pairs (bindings 13..20).
[[vk::binding(13, 0)]]
TextureCube<float4> irradianceMap;

[[vk::binding(14, 0)]]
SamplerState irradianceMapSampler;

[[vk::binding(15, 0)]]
TextureCube<float4> prefilteredSpecularMap;

[[vk::binding(16, 0)]]
SamplerState prefilteredSpecularMapSampler;

[[vk::binding(17, 0)]]
Texture2D<float4> brdfLUT;

[[vk::binding(18, 0)]]
SamplerState brdfLUTSampler;

#if ENABLE_SHEEN
[[vk::binding(21, 0)]] Texture2D<float4> sheenLUT;
[[vk::binding(22, 0)]] SamplerState sheenLUTSampler;
#if ENABLE_IBL
[[vk::binding(19, 0)]] TextureCube<float4> radianceMap;
[[vk::binding(20, 0)]] SamplerState radianceSampler;

// Integrate Charlie against the original radiance, not the GGX-prefiltered environment.
// Cosine-weighted sampling cancels NdotL / PDF to PI. No extra Fresnel or E normalization.
static const uint kSheenIblSampleCount = 64;
float3 evaluateSheenIbl(Surface surface, float3 V)
{
    if (!any(surface.sheenColor > 0.0))
        return float3(0.0);
    float3 N = surface.normal;
    float3 axis = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    float3 T = normalize(cross(axis, N));
    float3 B = cross(N, T);
    float3 integral = float3(0.0);
    [loop]
    for (uint sampleIndex = 0; sampleIndex < kSheenIblSampleCount; ++sampleIndex)
    {
        float u = (float(sampleIndex) + 0.5) / kSheenIblSampleCount;
        float angle = float(reversebits(sampleIndex)) * (2.0 * PI / 4294967296.0);
        float radius = sqrt(u);
        float3 L = T * (radius * cos(angle)) + B * (radius * sin(angle)) + N * sqrt(1.0 - u);
        float brdf = evaluateSheenBrdf(surface.sheenRoughness, N, V, L);
        integral += radianceMap.SampleLevel(radianceSampler, L, 0.0).rgb * brdf;
    }
    return surface.sheenColor * integral * (PI / kSheenIblSampleCount);
}
#endif
#endif

float spotAngularAttenuation(LightData light, float3 lightToSurfaceDirection)
{
    float cosine = dot(light.direction.xyz, lightToSurfaceDirection);
    // Equal inner/outer angles describe a hard-edged cone.
    if (light.cosInner <= light.cosOuter)
        return cosine >= light.cosOuter ? 1.0 : 0.0;
    float attenuation = saturate((cosine - light.cosOuter) / (light.cosInner - light.cosOuter));
    return attenuation * attenuation;
}

float spotShadowVisibility(LightData light, float3 worldPosition, float2 rotation)
{
    float4 clip = mul(spotShadowData[light.shadowIdx].vp, float4(worldPosition, 1.0));
    if (clip.w <= 0.0)
        return 1.0;
    float3 projected = clip.xyz / clip.w;
    float2 uv = projected.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || projected.z < 0.0 || projected.z > 1.0)
        return 1.0;

    uint width;
    uint height;
    uint layers;
    spotShadowMap.GetDimensions(width, height, layers);
    float2 radiusUv = lightUniforms.spotShadowSampleRadius / float2(width, height);
    float depth = projected.z - lightUniforms.shadowBias;
    float visibility = 0.0;
    [unroll]
    for (uint sampleIndex = 0; sampleIndex < kPoissonDisk16SampleCount; ++sampleIndex)
    {
        float2 offset = kPoissonDisk16[sampleIndex];
        offset = float2(offset.x * rotation.x - offset.y * rotation.y,
                        offset.x * rotation.y + offset.y * rotation.x);
        visibility += spotShadowMap.SampleCmpLevelZero(
            shadowSampler, float3(uv + offset * radiusUv, light.shadowIdx), depth);
    }
    return visibility / kPoissonDisk16SampleCount;
}

float sampleDirectionalCascade(LightData light, uint cascadeIndex, float3 worldPosition, float2 rotation)
{
    CascadeShadowData cascade = directionalShadowData[light.shadowIdx].cascades[cascadeIndex];
    float4 clip = mul(cascade.vp, float4(worldPosition, 1.0));
    float3 projected = clip.xyz / clip.w;
    float2 uv = projected.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || projected.z < 0.0 || projected.z > 1.0)
        return 1.0;

    uint layer = light.shadowIdx * lightUniforms.cascadeCount + cascadeIndex;
    float depth = projected.z - lightUniforms.shadowBias;
    if (lightUniforms.directionalShadowSampleRadius <= 0.0)
        return cascadeShadowMap.SampleCmpLevelZero(shadowSampler, float3(uv, layer), depth);

    uint width;
    uint height;
    uint layers;
    cascadeShadowMap.GetDimensions(width, height, layers);
    float2 radiusUv = lightUniforms.directionalShadowSampleRadius / float2(width, height);
    float visibility = 0.0;
    [unroll]
    for (uint sampleIndex = 0; sampleIndex < kPoissonDisk8SampleCount; ++sampleIndex)
    {
        float2 offset = kPoissonDisk8[sampleIndex];
        offset = float2(offset.x * rotation.x - offset.y * rotation.y,
                        offset.x * rotation.y + offset.y * rotation.x);
        float2 sampleUv = uv + offset * radiusUv;
        // Outside the shadow map there is no recorded occluder; don't repeat the edge texel.
        if (any(sampleUv < 0.0) || any(sampleUv > 1.0))
            visibility += 1.0;
        else
            visibility += cascadeShadowMap.SampleCmpLevelZero(shadowSampler, float3(sampleUv, layer), depth);
    }
    return visibility / kPoissonDisk8SampleCount;
}

float directionalShadowVisibility(LightData light, float3 worldPosition, float2 rotation)
{
    float viewDepth = -mul(viewUniforms.view, float4(worldPosition, 1.0)).z;
    if (viewDepth <= 0.0)
        return 1.0;

    [loop]
    for (uint cascadeIndex = 0; cascadeIndex < lightUniforms.cascadeCount; ++cascadeIndex)
    {
        CascadeShadowData cascade = directionalShadowData[light.shadowIdx].cascades[cascadeIndex];
        if (viewDepth > cascade.depthUpperBound)
            continue;

        float visibility = sampleDirectionalCascade(light, cascadeIndex, worldPosition, rotation);
        float blendWidth = cascade.depthUpperBound - cascade.blendStartDepth;
        if (cascadeIndex + 1 < lightUniforms.cascadeCount && blendWidth > 0.0 &&
            viewDepth > cascade.blendStartDepth)
        {
            float nextVisibility = sampleDirectionalCascade(light, cascadeIndex + 1, worldPosition, rotation);
            float blend = saturate((viewDepth - cascade.blendStartDepth) / blendWidth);
            return lerp(visibility, nextVisibility, blend);
        }
        return visibility;
    }
    // Beyond the configured shadow distance, only direct lighting remains.
    return 1.0;
}


float pcf(float3 dir, uint shadowIndex, float depth, float2 rotation)
{
    float3 direction = normalize(dir);
    float3 referenceAxis = abs(direction.z) < 0.999
        ? float3(0.0, 0.0, 1.0)
        : float3(0.0, 1.0, 0.0);
    float3 tangent = normalize(cross(direction, referenceAxis));
    float3 bitangent = cross(direction, tangent);
    float3 rotatedTangent = tangent * rotation.x + bitangent * rotation.y;
    float3 rotatedBitangent = bitangent * rotation.x - tangent * rotation.y;

    float totalVisibility = 0.0;
    [unroll]
    for (uint sampleIndex = 0; sampleIndex < kPoissonDisk16SampleCount; ++sampleIndex)
    {
        float2 offset = kPoissonDisk16[sampleIndex] * lightUniforms.shadowSampleRadius;
        float3 sampleDirection = normalize(
            direction + rotatedTangent * offset.x + rotatedBitangent * offset.y);
        totalVisibility += pointShadowMap.SampleCmpLevelZero(
            shadowSampler, float4(sampleDirection, shadowIndex), depth - lightUniforms.shadowBias);
    }

    return totalVisibility / kPoissonDisk16SampleCount;
}


float rectAreaShadowPCSS(LightData light, float3 worldPos, uint2 pixel)
{
    uint shadowIndex = light.shadowIdx;
    RectAreaShadowData shadow = rectAreaShadowData[shadowIndex];
    float4 pos = mul(shadow.vp, float4(worldPos, 1.0));
    if (pos.w <= 0.0)
        return 1.0;

    float3 projected = pos.xyz / pos.w;
    float2 uv = projected.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || projected.z < 0.0 || projected.z > 1.0)
        return 1.0;

    // clip.w is the positive, linear depth along the light's viewing direction.
    float receiverLinearDepth = pos.w;
    float receiverDepth = max(projected.z - lightUniforms.shadowBias, 0.0);
    float2 lightHalfSize = light.areaSize * 0.5;

    // The blocker depth is unknown: project the search footprint onto the near plane.
    float2 searchSize = lightHalfSize * max(receiverLinearDepth - shadow.nearPlane, 0.0)
        / receiverLinearDepth;
    float2 searchRadiusUv = searchSize * shadow.worldToUvScale / shadow.nearPlane;
    uint blockerCount = 0;
    float avgBlockerLinearDepth = 0.0;
    [unroll]
    for (uint sampleIndex = 0; sampleIndex < kRectangle16SampleCount; ++sampleIndex)
    {
        float2 offset = rectangleSample16(sampleIndex, pixel, lightUniforms.shadowPixelRotation);
        float2 sampleUv = uv + offset * searchRadiusUv;
        if (any(sampleUv < 0.0) || any(sampleUv > 1.0))
            continue;

        float blockerDepth = rectAreaShadowMap.SampleLevel(
            linearSampler, float3(sampleUv, shadowIndex), 0.0);
        // Keep bias and comparison in the same non-linear 0..1 depth space as PCF.
        if (blockerDepth < receiverDepth)
        {
            float blockerLinearDepth = shadow.nearPlane * shadow.farPlane
                / (shadow.farPlane + (shadow.nearPlane - shadow.farPlane) * blockerDepth);
            avgBlockerLinearDepth += blockerLinearDepth;
            ++blockerCount;
        }
    }

    if (blockerCount == 0)
        return 1.0;
    avgBlockerLinearDepth /= blockerCount;

    // Small-triangle footprint on the blocker plane; convert using blocker depth.
    float ratio = max(receiverLinearDepth - avgBlockerLinearDepth, 0.0) / receiverLinearDepth;
    float2 filterSize = lightHalfSize * ratio;
    float2 filterRadiusUv = filterSize * shadow.worldToUvScale / avgBlockerLinearDepth;
    float totalVisibility = 0.0;
    [unroll]
    for (uint sampleIndex = 0; sampleIndex < kRectangle16SampleCount; ++sampleIndex)
    {
        float2 offset = rectangleSample16(sampleIndex, pixel, lightUniforms.shadowPixelRotation);
        float2 sampleUv = uv + offset * filterRadiusUv;
        if (any(sampleUv < 0.0) || any(sampleUv > 1.0))
        {
            totalVisibility += 1.0;
            continue;
        }

        totalVisibility += rectAreaShadowMap.SampleCmpLevelZero(
            shadowSampler, float3(sampleUv, shadowIndex), receiverDepth);
    }
    return totalVisibility / kRectangle16SampleCount;
}

float3 evaluateDirectContribution(
    Surface surface, float3 V, float3 lightDirection, float3 incidentIrradiance, float visibility)
{
    if (visibility <= 0.0)
        return float3(0.0);

    float nDotL = saturate(dot(surface.normal, lightDirection));
    float3 brdf = evaluateDirectBrdf(
        surface.baseColor.rgb, surface.metallic, surface.roughness, surface.normal, V, lightDirection);
    float3 response = brdf * nDotL;
#if ENABLE_SHEEN
    float sheenStrength = max(surface.sheenColor.x, max(surface.sheenColor.y, surface.sheenColor.z));
    float nDotV = saturate(dot(surface.normal, V));
    float sheenViewAlbedo = sheenLUT.SampleLevel(sheenLUTSampler, float2(nDotV, surface.sheenRoughness), 0.0).r;
    float sheenLightAlbedo = sheenLUT.SampleLevel(sheenLUTSampler, float2(nDotL, surface.sheenRoughness), 0.0).r;
    float baseScale = saturate(1.0 - sheenStrength * max(sheenViewAlbedo, sheenLightAlbedo));
    response = response * baseScale + surface.sheenColor
        * evaluateSheenBrdf(surface.sheenRoughness, surface.normal, V, lightDirection) * nDotL;
#endif
#if ENABLE_CLEARCOAT
    float clearcoatNdotV = saturate(dot(surface.clearcoatNormal, V));
    float clearcoatWeight = surface.clearcoatFactor * fresnelSchlick(float3(kClearcoatF0), clearcoatNdotV).x;
    float clearcoatNdotL = saturate(dot(surface.clearcoatNormal, lightDirection));
    float clearcoatResponse = evaluateClearcoatBrdf(
        surface.clearcoatRoughness, surface.clearcoatNormal, V, lightDirection) * clearcoatNdotL;
    response = lerp(response, float3(clearcoatResponse), clearcoatWeight);
#endif
    return incidentIrradiance * response * visibility;
}

float3 evaluateLighting(Surface surface, uint2 pixel)
{
    float3 V = normalize(viewUniforms.cameraPosition.xyz - surface.worldPosition);
    float NdotV = saturate(dot(surface.normal, V));
    float3 f0 = lerp(0.04, surface.baseColor.rgb, float3(surface.metallic));
#if ENABLE_SHEEN
    float sheenStrength = max(surface.sheenColor.x, max(surface.sheenColor.y, surface.sheenColor.z));
    float sheenViewAlbedo = sheenLUT.SampleLevel(sheenLUTSampler, float2(NdotV, surface.sheenRoughness), 0.0).r;
    float sheenAmbientScale = saturate(1.0 - sheenStrength * sheenViewAlbedo);
#endif
#if ENABLE_CLEARCOAT
    float3 clearcoatNormal = surface.clearcoatNormal;
    float clearcoatRoughness = surface.clearcoatRoughness;
    float clearcoatNdotV = saturate(dot(clearcoatNormal, V));
    float clearcoatWeight = surface.clearcoatFactor * fresnelSchlick(float3(kClearcoatF0), clearcoatNdotV).x;
#endif

    float2 shadowRotation = lightUniforms.shadowPixelRotation
        ? pixelSampleRotation(pixel)
        : float2(1.0, 0.0);
    LightRanges ranges = lightUniforms.lightRanges;
    float3 direct = float3(0.0);
    for (uint index = 0; index < ranges.directionalEnd; ++index)
    {
        LightData light = lights[index];
        if (!light.enabled)
            continue;

        float3 lightDirection = -light.direction.xyz;
        float visibility = 1.0;
        if (light.castShadow)
            visibility = directionalShadowVisibility(light, surface.worldPosition, shadowRotation);
        direct += evaluateDirectContribution(surface, V, lightDirection, light.color, visibility);
    }

    for (uint index = ranges.directionalEnd; index < ranges.pointEnd; ++index)
    {
        LightData light = lights[index];
        if (!light.enabled)
            continue;

        float distanceToLight = length(light.position - surface.worldPosition);
        if (distanceToLight <= 0.0 || distanceToLight > light.range)
            continue;

        float3 lightDirection = (light.position - surface.worldPosition) / distanceToLight;
        float3 incidentIrradiance = light.color / (distanceToLight * distanceToLight);
        float visibility = 1.0;
        if (light.castShadow)
            visibility = pcf(-lightDirection, light.shadowIdx, distanceToLight / light.range, shadowRotation);
        direct += evaluateDirectContribution(surface, V, lightDirection, incidentIrradiance, visibility);
    }

    for (uint index = ranges.pointEnd; index < ranges.rectAreaEnd; ++index)
    {
        LightData light = lights[index];
        if (!light.enabled)
            continue;

        float distanceToLight = length(light.position - surface.worldPosition);
        if (distanceToLight <= 0.0)
            continue;

        // Center-direction approximation; finite-area specular integration is not included.
        float3 lightDirection = (light.position - surface.worldPosition) / distanceToLight;
        float projectedArea = light.areaSize.x * light.areaSize.y
            * saturate(dot(-lightDirection, light.direction.xyz));
        float solidAngle = projectedArea / (distanceToLight * distanceToLight);
        float3 incidentIrradiance = light.color * solidAngle;
        float visibility = 1.0;
        if (light.castShadow)
            visibility = rectAreaShadowPCSS(light, surface.worldPosition, pixel);
        direct += evaluateDirectContribution(surface, V, lightDirection, incidentIrradiance, visibility);
    }

    for (uint index = ranges.rectAreaEnd; index < ranges.spotEnd; ++index)
    {
        LightData light = lights[index];
        if (!light.enabled)
            continue;

        float distanceToLight = length(light.position - surface.worldPosition);
        if (distanceToLight <= 0.0 || distanceToLight > light.range)
            continue;

        float3 lightDirection = (light.position - surface.worldPosition) / distanceToLight;
        float attenuation = spotAngularAttenuation(light, -lightDirection);
        if (attenuation <= 0.0)
            continue;

        float3 incidentIrradiance = light.color * attenuation / (distanceToLight * distanceToLight);
        float visibility = 1.0;
        if (light.castShadow)
            visibility = spotShadowVisibility(light, surface.worldPosition, shadowRotation);
        direct += evaluateDirectContribution(surface, V, lightDirection, incidentIrradiance, visibility);
    }

    float3 ambient = float3(0.0);
#if ENABLE_IBL
    // 环境光照
    float3 R = reflect(-V, surface.normal);

    // 镜面反射环境光照 Split Sum Approximation
    float3 environmentTerm = prefilteredSpecularMap.SampleLevel(
        prefilteredSpecularMapSampler, R, surface.roughness * lightUniforms.iblPrefilteredMaxMipLevel).rgb;
    float2 scaleBias = brdfLUT.Sample(brdfLUTSampler, float2(NdotV, surface.roughness)).rg;
    float3 brdfTerm = scaleBias.x * f0 + scaleBias.y;
    float3 ambientSpecular = environmentTerm * brdfTerm;

    // The LUT integrates Fresnel and visibility; allocate the remaining energy to diffuse.
    float3 diffuseWeight = (1.0 - saturate(brdfTerm)) * (1.0 - surface.metallic);
    float3 irradiance = irradianceMap.Sample(irradianceMapSampler, surface.normal).rgb;
    float3 ambientDiffuse = irradiance * surface.baseColor.rgb * INV_PI * diffuseWeight;

    ambient = ambientDiffuse + ambientSpecular;
#else
    ambient = lightUniforms.ambientCoefficient * surface.baseColor.rgb;
#endif
#if ENABLE_SHEEN
    ambient *= sheenAmbientScale;
#if ENABLE_IBL
    ambient += evaluateSheenIbl(surface, V);
#endif
#endif
#if ENABLE_CLEARCOAT
    ambient *= 1.0 - clearcoatWeight;
#if ENABLE_IBL
    float3 clearcoatReflection = reflect(-V, clearcoatNormal);
    float3 clearcoatEnvironment = prefilteredSpecularMap.SampleLevel(
        prefilteredSpecularMapSampler,
        clearcoatReflection,
        clearcoatRoughness * lightUniforms.iblPrefilteredMaxMipLevel).rgb;
    float2 clearcoatScaleBias = brdfLUT.Sample(brdfLUTSampler, float2(clearcoatNdotV, clearcoatRoughness)).rg;
    // The layer weight already includes Fresnel. Use the LUT's unit-Fresnel
    // response (scale + bias), avoiding a second Fresnel multiplication.
    ambient += clearcoatEnvironment * (clearcoatScaleBias.x + clearcoatScaleBias.y) * clearcoatWeight;
#endif
#endif
    ambient *= surface.occlusion;

    float3 emissive = surface.emissive;
#if ENABLE_CLEARCOAT
    emissive *= 1.0 - clearcoatWeight;
#endif
    float3 color = direct + ambient + emissive;

    return color;
}

#endif
