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
    float3 normal : NORMAL;
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
};

[[vk::binding(0, 1)]]
Texture2D<float4> albedoMap;

[[vk::binding(1, 1)]]
SamplerState albedoSampler;

[[vk::binding(2, 1)]]
ConstantBuffer<MaterialData> material;

[[vk::binding(0, 0)]]
ConstantBuffer<SceneUniforms> sceneUniforms;

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition = mul(pushConstants.model, float4(input.position, 1.0));
    output.position = mul(sceneUniforms.viewProjection, worldPosition);
    output.normal = input.normal;
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    float3 normal = normalize(input.normal);

    float4 albedo = albedoMap.Sample(albedoSampler, input.uv) * material.baseColorFactor;

    float3 lightDirection = normalize(sceneUniforms.light.position - input.worldPosition);
    float distanceToLight = length(sceneUniforms.light.position - input.worldPosition);
    float lightRange = max(sceneUniforms.light.range, 0.001);
    float distanceAttenuation = saturate(1.0 - distanceToLight / lightRange);
    distanceAttenuation *= distanceAttenuation;

    float3 viewDirection = normalize(sceneUniforms.cameraPosition - input.worldPosition);
    float3 H = normalize(lightDirection + viewDirection);
    float NdotH = saturate(dot(normal, H));
    float NdotL = saturate(dot(normal, lightDirection));
    float NdotV = saturate(dot(normal, viewDirection));
    float VdotH = saturate(dot(viewDirection, H));
    float roughness = material.roughness;
    float metallic = material.metallic;
    float3 F0 = lerp(0.04, albedo.rgb, metallic);
    float3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;
    float3 diffuse = kD * albedo.rgb / 3.14159265358979323846;
    float3 specular = kS * (F0 / (4.0 * (NdotL + roughness * roughness))) * NdotL;
    float3 ambient = 0.04 * albedo.rgb;
    float3 color = diffuse + specular + ambient;

    // float enabled = sceneUniforms.light.enabled != 0 ? 1.0 : 0.0;
    // float3 pointLight = sceneUniforms.light.color
    //     * sceneUniforms.light.intensity
    //     * distanceAttenuation
    //     * enabled;

    // float3 color = albedo.rgb * (pointLight * saturate(dot(normal, lightDirection)));
    return float4(color, albedo.a);
}
