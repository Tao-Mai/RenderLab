#include "Rendering/Lights/EmissiveUniformSampler.slang"
#include "passes/shared/path_tracing.slang"
#include "Rendering/Lights/LightHelpers.slang"
#include "Rendering/Lights/EnvMapSampler.slang"
#include "Utils/Color/ColorHelpers.slang"

struct PathTracingUniforms
{
    uint sampleSeed;
};

struct ReservoirHistory
{
    int selectedLightIndex;
    float3 selectedRadiance = float3(0.0);
    float selectedTarget;
    float weightSum;
    uint sampleCount;
};

static const uint kCandidateCount = 5;

ConstantBuffer<PathTracingUniforms> uniforms;
RWStructuredBuffer<ReservoirHistory> reservoirHistory;

Texture2D<PackedHitInfo> vbuffer;

[format("rgba32f")]
RWTexture2D<float4> outputImage;

void sampleAnalyticLight(out uint lightIndex, out float pdf, inout SampleGenerator state)
{
    uint lightCount = gScene.getLightCount();
    lightIndex = min(uint(sampleNext1D(state) * lightCount), lightCount - 1u);
    pdf = 1.0f / float(lightCount);
}

enum LightGroup
{
    Emissive,
    Analytic
};

LightGroup sampleLightGroup(inout SampleGenerator state) {}

float3 evalDL<MaterialInstance : IMaterialInstance>(
    const ShadingData sd, const MaterialInstance mi, inout SampleGenerator state)
{
    // TODO 选灯pdf感觉可以用分层采样

    uint2 pixel = DispatchRaysIndex().xy;
    uint historyIndex = pixel.x + pixel.y * DispatchRaysDimensions().x;

    ReservoirHistory history = {};
    if (uniforms.sampleSeed > 0)
    {
        ReservoirHistory history = reservoirHistory[historyIndex];
    }

    const BSDFProperties props = mi.getProperties(sd);
    const bool upperHemisphere = !props.isTransmissive;

    EmissiveUniformSampler emissive_sampler = {};

    for (uint i = 0; i < kCandidateCount; i++)
    {
        TriangleLightSample tls = {};

        if (!emissive_sampler.sampleLight(sd.posW, props.guideNormal, upperHemisphere, state, tls))
        {
            continue;
        }

        float3 radiance = mi.eval(sd, tls.dir, state) * tls.Le;
        float3 shadowOrigin = computeRayOrigin(sd.posW, dot(sd.faceN, tls.dir) >= 0.f ? sd.faceN : -sd.faceN);
        if (!any(radiance > 0.0) || !traceShadowRay(shadowOrigin, tls.dir, tls.distance))
        {
            continue;
        }

        float target = luminance(radiance);

        float w = 1.0 * target / tls.pdf;

        history.weightSum += w;

        if (sampleNext1D(state) * history.weightSum < w)
        {
            history.selectedLightIndex = tls.triangleIndex;
            history.selectedRadiance = radiance;
            history.selectedTarget = target;
        }
    }

    history.sampleCount += kCandidateCount;
    reservoirHistory[historyIndex] = history;

    return history.selectedRadiance / history.selectedTarget * history.weightSum / float(history.sampleCount);
}

float3 tracePath(RayDesc ray, HitInfo hit, inout SampleGenerator state)
{
    float3 radiance = float3(0.0);

    EnvMapSampler envMapSampler = {};

    if (!hit.isValid())
    {
        return envMapSampler.eval(ray.Direction);
    }

    const ShadingData sd = loadShadingData(hit, ray.Origin, ray.Direction);
    const StandardMaterialInstance mi = gScene.materials.getMaterialInstance(sd);
    radiance += mi.getProperties(sd).emission;

    radiance += evalDL(sd, mi, state);

    return radiance;
}

[shader("anyhit")]
void scatterAnyHitMain(inout ScatterRayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (alphaTest(getGeometryInstanceID(), PrimitiveIndex(), attributes))
        IgnoreHit();
}

[shader("anyhit")]
void visibilityAnyHitMain(inout ShadowRayData payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (alphaTest(getGeometryInstanceID(), PrimitiveIndex(), attributes))
        IgnoreHit();
}

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;
    SampleGenerator state = createPathSampleGenerator(pixel, frameDim.x, uniforms.sampleSeed);
    RayDesc ray = gScene.camera.computeRayPinhole(pixel, frameDim);
    HitInfo hit = HitInfo(vbuffer.Load(int3(pixel, 0)));

    outputImage[pixel] = float4(tracePath(ray, hit, state), 1.0);
}
