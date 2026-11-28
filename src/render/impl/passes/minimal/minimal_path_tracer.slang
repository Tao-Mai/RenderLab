#include "Rendering/Lights/EmissiveUniformSampler.slang"
#include "passes/shared/path_tracer_lighting.slang"

struct PathTracingUniforms
{
    uint maxBounces;
    uint sampleIndex;
};

ConstantBuffer<PathTracingUniforms> uniforms;

Texture2D<PackedHitInfo> vbuffer;
[format("rgba32f")] RWTexture2D<float4> outputImage;

float3 tracePath(RayDesc ray, HitInfo hit, inout SampleGenerator state)
{
    EnvMapSampler envMapSampler = {};
    EmissiveUniformSampler emissiveSampler = {};

    float3      radiance        = float3(0.0);
    float3      throughput      = float3(1.0);
    ShadingData previousSd      = {};
    float3      previousNormal  = float3(0.0);
    bool        previousUpper   = true;
    float       previousBrdfPdf = 0.0;

    const uint maxBounces = uniforms.maxBounces;

    for (uint bounce = 0; bounce <= maxBounces; ++bounce)
    {
        if (!hit.isValid())
        {
            const float emissionWeight = bounce == 0 ? 1.0
                : evalEmissionMisWeight(
                    envMapSampler, emissiveSampler, previousSd, previousNormal, previousUpper,
                    ray, hit, ShadingData(), previousBrdfPdf);
            radiance += throughput * envMapSampler.eval(ray.Direction) * emissionWeight;

            break;
        }

        const ShadingData sd = loadShadingData(hit, ray.Origin, ray.Direction);
        const StandardMaterialInstance mi = gScene.materials.getMaterialInstance(sd);
        const BSDFProperties props = mi.getProperties(sd);

        const float emissionWeight = bounce == 0 ? 1.0
            : evalEmissionMisWeight(
                envMapSampler, emissiveSampler, previousSd, previousNormal, previousUpper,
                ray, hit, sd, previousBrdfPdf);
        radiance += throughput * props.emission * emissionWeight;

        // 停止反弹，那接收完emission就可以结束，不再eval表面
        if (bounce == maxBounces)
            break;

        // NEE
        radiance += throughput * evalDirectLighting(envMapSampler, emissiveSampler, sd, mi, state);

        BSDFSample sampled;
        if (!mi.sample(sd, state, sampled) || !any(sampled.weight > 0.0))
            break;

        throughput *= sampled.weight;

        // 轮盘
        if (bounce >= kRussianRouletteStartBounce)
        {
            float survival = saturate(max(throughput.x, max(throughput.y, throughput.z)));
            if (survival == 0.0 || sampleNext1D(state) >= survival)
                break;
            throughput /= survival;
        }

        ray.Origin    = sd.computeRayOrigin(!sampled.isLobe(LobeType::Transmission));
        ray.Direction = sampled.wo;
        ray.TMin      = 0.0;
        ray.TMax      = FLT_MAX;

        previousSd      = sd;
        previousNormal  = props.guideNormal;
        previousUpper   = !props.isTransmissive;
        previousBrdfPdf = sampled.isLobe(LobeType::Delta) ? 0.0 : sampled.pdf;

        hit = traceScatterRay(ray);
    }

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
    SampleGenerator state = createPathSampleGenerator(pixel, frameDim.x, uniforms.sampleIndex);
    RayDesc ray = gScene.camera.computeRayPinhole(pixel, frameDim);
    HitInfo hit = HitInfo(vbuffer.Load(int3(pixel, 0)));

    outputImage[pixel] = float4(tracePath(ray, hit, state), 1.0);
}
