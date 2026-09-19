
struct VertexInput{
    [[vk::location(0)]]
    float3 position : POSITION;

    [[vk::location(2)]]
    float2 uv : TEXCOORD;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 worldPosition : POSITION0;
};

struct PushConstants
{
    float4x4 model;
    float4 albedo;
};

struct PointLight
{
    float3 position;
    float range;
    float3 color;
    float intensity;
    uint enabled;
};

struct SceneUniforms
{
    float4x4 viewProjection;
    PointLight pointLight;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

[[vk::binding(0, 0)]]
Texture2D<float4> albedoMap;

[[vk::binding(1, 0)]]
SamplerState albedoSampler;

[[vk::binding(0, 1)]]
ConstantBuffer<SceneUniforms> sceneUniforms;

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition = mul(pushConstants.model, float4(input.position, 1.0));
    output.position = mul(sceneUniforms.viewProjection, worldPosition);
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    float4 albedo = albedoMap.Sample(albedoSampler, input.uv) * pushConstants.albedo;
    float distanceToLight = length(
        sceneUniforms.pointLight.position - input.worldPosition);
    float lightRange = max(sceneUniforms.pointLight.range, 0.001);
    float distanceAttenuation = saturate(1.0 - distanceToLight / lightRange);
    distanceAttenuation *= distanceAttenuation;

    float enabled = sceneUniforms.pointLight.enabled != 0 ? 1.0 : 0.0;
    float3 pointLight = sceneUniforms.pointLight.color
        * sceneUniforms.pointLight.intensity
        * distanceAttenuation
        * enabled;

    float3 color = albedo.rgb * (0.04 + pointLight);
    return float4(color, albedo.a);
}
