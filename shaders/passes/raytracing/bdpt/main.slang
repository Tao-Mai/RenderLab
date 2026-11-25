#include "raytracing/path_launch.slang"
#include "bdpt_parameters.slang"

RWStructuredBuffer<BdptBinding::AccumulatedPixel> cameraHistory;
RWStructuredBuffer<uint4> splats;
#include "bdpt.slang"

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy + parameters.tileOffset;
    uint pixelIndex = pixel.x + pixel.y * parameters.imageSize.x;
    CameraPathSample sample = sampleCameraPath(pixel, parameters.imageSize, parameters.path);
    BdptBinding::AccumulatedPixel history = cameraHistory[pixelIndex];
    history.radiance += tracePath(sample.ray, sample.randomState);
    ++history.sampleCount;
    cameraHistory[pixelIndex] = history;
}
