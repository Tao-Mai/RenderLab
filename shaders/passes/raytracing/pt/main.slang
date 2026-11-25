#include "raytracing/path_launch.slang"

struct PtParameters { PathTracingParameters path; };
ConstantBuffer<PtParameters> parameters;
[format("rgba32f")] RWTexture2D<float4> sampleOutput;

#include "pt.slang"

[shader("raygeneration")]
void raygenMain()
{
    PathTracingParameters path = parameters.path;
    uint2 pixel = DispatchRaysIndex().xy;
    CameraPathSample sample = sampleCameraPath(pixel, DispatchRaysDimensions().xy, path);
    sampleOutput[pixel] = float4(tracePath(sample.ray, sample.randomState), 1.0);
}
