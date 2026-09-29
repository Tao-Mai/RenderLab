#include "brdf.slang"

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

struct Light
{
    float3 color;
    float intensity;
    float3 position;
    float range;
    float3 direction;
    float padding0;
    float2 areaSize;
    float cosInner;
    float cosOuter;
    uint type;
    uint enabled;
    uint castShadow;
    uint padding1;
};

struct SceneUniforms
{
    float4x4 viewProjection;
    Light light;
    float3 cameraPosition;
    float padding2;
    float4 iblParameters;
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

[[vk::binding(0, 0)]]
ConstantBuffer<SceneUniforms> sceneUniforms;

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


[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition = mul(pushConstants.model, float4(input.position, 1.0));
    output.position = mul(sceneUniforms.viewProjection, worldPosition);
    output.worldNormal = mul(pushConstants.model, float4(input.normal, 0.0)).xyz; // 没有平移
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    return output;
}

// [shader("fragment")]
// float4 fragMain(VertexOutput input) : SV_Target
// {
//     float3 N = normalize(input.worldNormal);
//     float3 V = normalize(sceneUniforms.cameraPosition - input.worldPosition);
//     float3 R = reflect(-V, N);

//     float lod = 7.0;

//     float3 c = prefilteredSpecularMap.SampleLevel(
//         prefilteredSpecularMapSampler, R, lod).rgb;

//     return float4(c, 1.0);
// }

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    float3 N = normalize(input.worldNormal);

    float4 baseColor = baseColorTexture.Sample(baseColorSampler, input.uv) * material.baseColorFactor;
    if (material.alphaMode == 1 && baseColor.a < material.alphaCutoff)
        discard;

    float3 L = normalize(sceneUniforms.light.position - input.worldPosition);
    float distanceToLight = length(sceneUniforms.light.position - input.worldPosition);
    float lightRange = max(sceneUniforms.light.range, 0.001);
    float distanceAttenuation = saturate(1.0 - distanceToLight / lightRange);
    distanceAttenuation *= distanceAttenuation;
    float3 pointLight = sceneUniforms.light.color
        * sceneUniforms.light.intensity
        * distanceAttenuation
        * sceneUniforms.light.enabled;

    float3 V = normalize(sceneUniforms.cameraPosition - input.worldPosition);
    float3 H = normalize(L + V);
    float NdotH = saturate(dot(N, H));
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float VdotH = saturate(dot(V, H));
    float roughness = material.roughness;
    float metallic = material.metallic;

    float3 F0 = lerp(0.04, baseColor.rgb, float3(metallic));
    float3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
    float D = NormalDistributionGGX(roughness, NdotH);
    float G = GeometrySmithGGX(roughness, NdotV, NdotL);

    float3 diffuseBRDF = (1.0 - F) * (1.0 - metallic) * baseColor.rgb * INV_PI;
    float3 specularBRDF = F * D * G / max(4.0 * NdotV * NdotL, 1e-5);

    float3 direct = pointLight * (diffuseBRDF + specularBRDF) * NdotL;

    // 环境光照
    float3 R = reflect(-V, N);

    // 漫反射环境光照
    float3 irradiance = irradianceMap.Sample(irradianceMapSampler, N).rgb;
    float3 ambient_diffuse = irradiance * baseColor.rgb * INV_PI * (1.0 - metallic);

    // 镜面反射环境光照 Split Sum Approximation
    float3 env_term = prefilteredSpecularMap.SampleLevel(
        prefilteredSpecularMapSampler, R, roughness * sceneUniforms.iblParameters.x).rgb;
    float2 scale_bias = brdfLUT.Sample(brdfLUTSampler, float2(NdotV, roughness)).rg;
    float3 brdf_term = scale_bias.x * F0 + scale_bias.y;
    float3 ambient_specular = env_term * brdf_term;

    float3 ambient = ambient_diffuse + ambient_specular;

    float3 color = direct + ambient;

    return float4(color, baseColor.a);
}
