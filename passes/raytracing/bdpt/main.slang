#include "raytracing/accumulation/path_launch.slang"
#include "bdpt_parameters.slang"

RWStructuredBuffer<uint4> splats;
#include "bdpt.slang"

[shader("raygeneration")]
void raygenMain()
{
    PathParameters path = parameters.path;
    uint2 pixel = DispatchRaysIndex().xy + parameters.tileOffset;
    uint pixelIndex = pixel.x + pixel.y * path.imageSize.x;
    if (!path.containsPixel(pixel))
    {
        accumulation[pixelIndex] = (AccumulatedPixel)0;
        return;
    }

    CameraPathSample sample = sampleCameraPath(pixel, pixelIndex, path);
    accumulateCameraSample(pixelIndex, path.accumulatedSamples, tracePath(sample.ray, sample.randomState));
}
