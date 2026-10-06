#include "brdf.slang"
#include "scene_data.slang"
#include "sampling.slang"

#ifndef ENABLE_IBL
#error ENABLE_IBL must be specified when compiling the scene shader
#endif

struct VertexInput{
    [[vk::location(0)]]
    float3 position : POSITION;

    [[vk::location(1)]]
    float3 normal : NORMAL;

    [[vk::location(2)]]
    float2 uv : TEXCOORD;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldNormal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 worldPosition : POSITION0;
};

struct PushConstants
{
    float4x4 model;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;


struct MaterialData {
    float4 baseColorFactor;
    float roughness;
    float metallic;
    float alphaCutoff;
    uint alphaMode;
};

[[vk::binding(0, 1)]]
Texture2D<float4> baseColorTexture;

[[vk::binding(1, 1)]]
SamplerState baseColorSampler;

[[vk::binding(2, 1)]]
ConstantBuffer<MaterialData> material;

[[vk::binding(1, 0)]]
TextureCube<float4> irradianceMap;

[[vk::binding(2, 0)]]
SamplerState irradianceMapSampler;

[[vk::binding(3, 0)]]
TextureCube<float4> prefilteredSpecularMap;

[[vk::binding(4, 0)]]
SamplerState prefilteredSpecularMapSampler;

[[vk::binding(5, 0)]]
Texture2D<float4> brdfLUT;

[[vk::binding(6, 0)]]
SamplerState brdfLUTSampler;

[[vk::binding(9, 0)]]
TextureCubeArray<float> pointShadowMap;

[[vk::binding(10, 0)]]
SamplerComparisonState pointShadowSampler;



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
            pointShadowSampler, float4(sampleDirection, shadowIndex), depth);
    }

    return totalVisibility / kPoissonDisk16SampleCount;
}

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition = mul(pushConstants.model, float4(input.position, 1.0));
    output.position = mul(viewUniforms.viewProjection, worldPosition);
    output.worldNormal = mul(pushConstants.model, float4(input.normal, 0.0)).xyz; // 没有平移
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    float3 N = normalize(input.worldNormal);

    float4 baseColor = baseColorTexture.Sample(baseColorSampler, input.uv) * material.baseColorFactor;
    if (material.alphaMode == 1 && baseColor.a < material.alphaCutoff)
        discard;

    float3 V = normalize(viewUniforms.cameraPosition.xyz - input.worldPosition);
    float NdotV = saturate(dot(N, V));
    float roughness = material.roughness;
    float metallic = material.metallic;
    float3 F0 = lerp(0.04, baseColor.rgb, float3(metallic));

    float2 shadowRotation = lightUniforms.shadowPixelRotation
        ? pixelSampleRotation(uint2(input.position.xy))
        : float2(1.0, 0.0);
    float3 direct = float3(0.0);
    for (uint index = 0; index < lightUniforms.lightCount; ++index)
    {
        LightData light = lights[index];
        if (!light.enabled) continue;

        if (light.type == LightType.Point) {
            float3 L = normalize(light.position - input.worldPosition);
            float distanceToLight = length(light.position - input.worldPosition);
            if (distanceToLight > light.range) continue;

            float visibility = 1.0;
            if (light.castShadow) {
                uint shadowidx = light.shadowIdx;
                float depth = distanceToLight / light.range - lightUniforms.shadowBias;

                // depth <= shadowMapDepth，用的less or equal
                visibility = pcf(-L, shadowidx, depth, shadowRotation);

                if (visibility == 0.0) continue;
            }

            float3 pointLight = light.color * light.intensity / (distanceToLight * distanceToLight);

            float3 H = normalize(L + V);
            float NdotH = saturate(dot(N, H));
            float NdotL = saturate(dot(N, L));
            float VdotH = saturate(dot(V, H));
            float3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
            float D = NormalDistributionGGX(roughness, NdotH);
            float G = GeometrySmithGGX(roughness, NdotV, NdotL);

            float3 diffuseBRDF = (1.0 - F) * (1.0 - metallic) * baseColor.rgb * INV_PI;
            float3 specularBRDF = F * D * G / max(4.0 * NdotV * NdotL, 1e-5);
            direct += pointLight * (diffuseBRDF + specularBRDF) * NdotL * visibility;
        }
        else if (light.type == LightType.Directional) {
            // TODO
            float NdotL = saturate(dot(N, -light.direction.xyz));
            direct += light.color * light.intensity * NdotL * baseColor.rgb;
        }
    }

    float3 ambient = float3(0.0);
#if ENABLE_IBL
    // 环境光照
    float3 R = reflect(-V, N);

    // 漫反射环境光照
    float3 irradiance = irradianceMap.Sample(irradianceMapSampler, N).rgb;
    float3 ambient_diffuse = irradiance * baseColor.rgb * INV_PI * (1.0 - metallic);

    // 镜面反射环境光照 Split Sum Approximation
    float3 env_term = prefilteredSpecularMap.SampleLevel(
        prefilteredSpecularMapSampler, R, roughness * lightUniforms.iblPrefilteredMaxMipLevel).rgb;
    float2 scale_bias = brdfLUT.Sample(brdfLUTSampler, float2(NdotV, roughness)).rg;
    float3 brdf_term = scale_bias.x * F0 + scale_bias.y;
    float3 ambient_specular = env_term * brdf_term;

    ambient = ambient_diffuse + ambient_specular;
#else
    ambient = lightUniforms.ambientCoefficient * baseColor.rgb;
#endif
    float3 color = direct + ambient;

    return float4(color, baseColor.a);
}
