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
TextureCube<float4> environmentMap;

[[vk::binding(2, 0)]]
SamplerState environmentSampler;



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

    float3 F = FresnelSchlick(metallic, baseColor.rgb, VdotH);
    float D = NormalDistributionGGX(roughness, NdotH);
    float G = GeometrySmithGGX(roughness, NdotV, NdotL);


    float3 diffuseBRDF = (1.0 - F) * (1.0 - metallic) * baseColor.rgb * INV_PI;
    float3 specularBRDF = F * D * G / max(4.0 * NdotV * NdotL, 1e-5);
    float3 direct = pointLight * (diffuseBRDF + specularBRDF) * NdotL;
    float3 ambient = 0.04 * baseColor.rgb;
    float3 color = direct + ambient;

    float3 R = reflect(-V, N);
    float3 envColor = environmentMap.Sample(environmentSampler, R).rgb;

    return float4(envColor, baseColor.a);
}
