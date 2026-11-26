#include "raytracing/alpha_test.slang"
#include "passes/shared/path_tracer_lighting.slang"
#include "Rendering/Lights/EmissiveUniformSampler.slang"
#include "raytracing/visibility_buffer.slang"

struct PathTracingUniforms
{
    uint maxBounces;
    uint sampleIndex;
};

ConstantBuffer<PathTracingUniforms> uniforms;

Texture2D<uint4> vbuffer;
[format("rgba32f")] RWTexture2D<float4> outputImage;

float3 tracePath(RayDesc ray, PathHit hit, inout SampleGenerator state)
{
    EnvMapSampler envMapSampler = {};
    EmissiveUniformSampler emissiveSampler = {};

    float3  radiance         = float3(0.0);
    float3  throughput       = float3(1.0);
    Surface previousSurface  = {};
    float   previousBrdfPdf  = 0.0;

    const uint maxBounces = uniforms.maxBounces;
    
    for (uint bounce = 0; bounce <= maxBounces; ++bounce)
    {
        const float emissionWeight = bounce == 0 ? 1.0
            : evalEmissionMisWeight(
                envMapSampler, emissiveSampler, previousSurface, ray, hit, previousBrdfPdf);
        
        if (hit.event == PathEvent::Miss)
        {
            radiance += throughput * envMapSampler.eval(ray.Direction) * emissionWeight;

            break;
        }

        Surface surface = hit.surface;
        radiance += throughput * surface.emissive * emissionWeight;

        // 停止反弹，那接收完emission就可以结束，不再eval表面
        if (bounce == maxBounces)
            break;

        // NEE 
        radiance += throughput * evalDirectLighting(envMapSampler, emissiveSampler, surface, state);
        
        BSDFSample sampled = surface.sample(state);

        if (!sampled.valid || !any(sampled.weight > 0.0))
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
        
        ray.Origin    = surface.rayOrigin(sampled.direction);
        ray.Direction = sampled.direction;
        ray.TMin      = kRayEpsilon;
        ray.TMax      = kMaxRayDistance;

        previousSurface = surface;
        previousBrdfPdf = sampled.isDelta ? 0.0 : sampled.pdf;

        traceSceneRay(ray, RayType::Scatter, RAY_FLAG_FORCE_NON_OPAQUE, hit);
    }

    return radiance;
}

[shader("anyhit")]
void scatterAnyHitMain(inout PathHit hit, BuiltInTriangleIntersectionAttributes attributes)
{
    if (isMaskedTriangle(InstanceID(), PrimitiveIndex(), attributes.barycentrics))
        IgnoreHit();
}

[shader("anyhit")]
void visibilityAnyHitMain(inout VisibilityPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (isMaskedTriangle(InstanceID(), PrimitiveIndex(), attributes.barycentrics))
        IgnoreHit();
}

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;
    SampleGenerator state = createPathSampleGenerator(pixel, frameDim.x, uniforms.sampleIndex);
    RayDesc ray = createCameraRay(pixel, frameDim, float2(0.5));
    VBufferHit primary = {vbuffer.Load(int3(pixel, 0))};

    PathHit hit = {};
    if (primary.isValid())
    {
        hit.event = PathEvent::Surface;
        hit.surface = surfaceAt(primary.instanceIndex, primary.primitiveIndex, primary.barycentrics, ray.Direction);
    }

    outputImage[pixel] = float4(tracePath(ray, hit, state), 1.0);
}
