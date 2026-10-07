#ifndef RENDERLAB_SAMPLING_SLANG
#define RENDERLAB_SAMPLING_SLANG

#include "constant.slang"

static const uint kPoissonDisk16SampleCount = 16;
// Independent 16-point distribution, not a prefix of the 32-point table.
static const float2 kPoissonDisk16[kPoissonDisk16SampleCount] = {
    float2(-0.210164, 0.818197),
    float2(-0.496759, -0.645044),
    float2(0.046774, 0.446220),
    float2(0.062539, 0.051239),
    float2(0.874739, 0.099843),
    float2(0.695144, -0.331391),
    float2(-0.442494, 0.539893),
    float2(-0.200152, -0.310107),
    float2(-0.818759, 0.195748),
    float2(0.483114, -0.749171),
    float2(0.081558, -0.795315),
    float2(-0.665359, -0.136182),
    float2(0.373647, 0.696329),
    float2(0.383832, 0.310632),
    float2(0.196809, -0.340345),
    float2(-0.364467, 0.149456),
};

static const uint kPoissonDiskSampleCapacity = 32;
// Unit-disk offsets; each effect supplies its own radius and sample count.
static const float2 kPoissonDisk[kPoissonDiskSampleCapacity] = {
    float2(0.224144, -0.346933),
    float2(-0.506450, 0.581258),
    float2(-0.629531, -0.138053),
    float2(0.330150, 0.432255),
    float2(-0.207142, 0.805141),
    float2(0.863575, -0.221575),
    float2(0.431827, -0.020391),
    float2(-0.850104, 0.148950),
    float2(-0.125521, -0.113802),
    float2(0.019122, -0.848646),
    float2(-0.599167, -0.611514),
    float2(0.175331, 0.806283),
    float2(0.745482, 0.375733),
    float2(-0.171302, 0.348314),
    float2(0.484680, -0.657839),
    float2(-0.185093, -0.539182),
    float2(0.705039, 0.681177),
    float2(-0.022190, 0.559566),
    float2(0.639765, -0.386285),
    float2(-0.823329, -0.312984),
    float2(0.942022, 0.120447),
    float2(-0.656078, 0.375824),
    float2(0.187334, 0.212698),
    float2(-0.294410, -0.861357),
    float2(0.182746, -0.616968),
    float2(0.440251, 0.862071),
    float2(0.333285, -0.884824),
    float2(-0.306205, 0.100302),
    float2(0.501752, 0.231569),
    float2(-0.383366, -0.219401),
    float2(0.171959, -0.048297),
    float2(0.685985, 0.021427),
};

static const uint kRectangle16GridSize = 4;
static const uint kRectangle16SampleCount = kRectangle16GridSize * kRectangle16GridSize;

uint rectangleSampleHash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

// One sample per cell in [-1,1]^2. Jitter stays inside its cell and never rotates the rectangle.
float2 rectangleSample16(uint sampleIndex, uint2 pixel, bool jitterEnabled)
{
    uint2 cell = uint2(sampleIndex % kRectangle16GridSize, sampleIndex / kRectangle16GridSize);
    float2 jitter = float2(0.5);
    if (jitterEnabled)
    {
        uint seed = (pixel.x * 0x9e3779b9u) ^ (pixel.y * 0x85ebca6bu)
            ^ (sampleIndex * 0xc2b2ae35u);
        uint2 hashes = uint2(rectangleSampleHash(seed), rectangleSampleHash(seed ^ 0x68bc21ebu));
        jitter = float2(hashes >> 8) * (1.0 / 16777216.0);
    }
    return (float2(cell) + jitter) * (2.0 / kRectangle16GridSize) - 1.0;
}

// Stable per-pixel rotation, returned as {cos(angle), sin(angle)}.
float2 pixelSampleRotation(uint2 pixel)
{
    uint hash = (pixel.x * 0x9e3779b9u) ^ (pixel.y * 0x85ebca6bu);
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;

    float angle = float(hash >> 8) * (2.0 * PI / 16777216.0);
    float sinAngle;
    float cosAngle;
    sincos(angle, sinAngle, cosAngle);
    return float2(cosAngle, sinAngle);
}

#endif
