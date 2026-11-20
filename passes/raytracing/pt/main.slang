#include "raytracing/offline/path_launch.slang"
#include "pt_parameters.slang"

#include "pt.slang"

[shader("raygeneration")]
void raygenMain()
{
    PathParameters path = parameters.path;
    uint2 pixel = DispatchRaysIndex().xy;
    uint pixelIndex = pixel.x + pixel.y * path.imageSize.x;
    if (!path.containsPixel(pixel))
    {
        accumulation[pixelIndex] = (AccumulatedPixel)0;
        return;
    }

    CameraPathSample sample = sampleCameraPath(pixel, pixelIndex, path);
    accumulateCameraSample(pixelIndex, path.accumulatedSamples, tracePath(sample.ray, sample.randomState));
}
