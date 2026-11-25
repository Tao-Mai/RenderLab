#include "scene/scene_data.slang"
#include "scene/light_data.slang"
#include "scene/emissive_light_data.slang"
#include "math/constant.slang"

struct BuildParameters
{
    uint firstNode;
    uint lightCount;
    uint writeLeaves;
    uint powerWeighted;
    float diskArea;
    float environmentPower;
    uint padding0, padding1;
};

ConstantBuffer<BuildParameters> parameters;
ParameterBlock<EmissiveLights> emissiveLights;
RWStructuredBuffer<float> lightDistribution;

float analyticPower(LightData light)
{
    float intensity = max(0.0, max(light.color.x, max(light.color.y, light.color.z)));
    if (light.type == LightType::Directional)
        return parameters.diskArea * intensity;
    if (light.type == LightType::Spot)
    {
        float solidAngle = 2.0 * PI *
            ((1.0 - light.cosInner) + (light.cosInner - light.cosOuter) / (light.spotFalloffExponent + 1.0));
        return solidAngle * intensity;
    }
    if (light.type == LightType::RectArea)
        return PI * rectLightArea(light) * intensity;
    return 4.0 * PI * intensity;
}

float leafWeight(uint index)
{
    if (index >= parameters.lightCount)
        return 0.0;
    if (parameters.powerWeighted == 0)
        return 1.0;

    uint analyticCount = scene.analyticLightCount;
    if (index < analyticCount)
        return analyticPower(scene.analyticLights[index]);
    index -= analyticCount;
    if (index < emissiveLights.triangleCount)
        return emissiveLights.triangles[index].power;
    return parameters.environmentPower;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 threadIndex : SV_DispatchThreadID)
{
    uint index = threadIndex.x + threadIndex.y * kLightDispatchRowWidth;
    if (index >= parameters.firstNode)
        return;
    uint node = parameters.firstNode + index;
    if (parameters.writeLeaves != 0)
        lightDistribution[node] = leafWeight(index);
    else
        lightDistribution[node] = lightDistribution[node * 2] + lightDistribution[node * 2 + 1];
}
