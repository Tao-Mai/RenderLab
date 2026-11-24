#ifndef RENDERLAB_ACCUMULATION_PATH_PARAMETERS_SLANG
#define RENDERLAB_ACCUMULATION_PATH_PARAMETERS_SLANG

struct PathParameters
{
    float4 viewport;
    uint2 samples; // Completed samples and max bounces.
    uint2 imageSize;

    bool containsPixel(uint2 pixel)
    {
        float2 local = float2(pixel) + 0.5 - viewportOffset;
        return all(local >= 0.0) && all(local < viewportSize);
    }

    property viewportOffset : float2 {
        get { return viewport.xy; }
    }

    property viewportSize : float2 {
        get { return viewport.zw; }
    }

    property accumulatedSamples : uint {
        get { return samples.x; }
    }

    property maxBounces : uint {
        get { return samples.y; }
    }

};

struct AccumulatedPixel
{
    float3 color;
    uint sampleCount;
};

#endif
