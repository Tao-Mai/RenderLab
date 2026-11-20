#ifndef RENDERLAB_OFFLINE_PATH_PARAMETERS_SLANG
#define RENDERLAB_OFFLINE_PATH_PARAMETERS_SLANG

struct PathParameters
{
    float4 viewport;
    uint2 samples; // Completed samples and max bounces.
    uint2 imageSize;
    uint4 lightCounts; // Points, spots, directionals, triangles.
    uint4 mappedLightCounts; // Environment, projections, goniometrics, portal vertices.

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

    property pointLightCount : uint {
        get { return lightCounts.x; }
    }

    property spotLightCount : uint {
        get { return lightCounts.y; }
    }

    property directionalLightCount : uint {
        get { return lightCounts.z; }
    }

    property triangleLightCount : uint {
        get { return lightCounts.w; }
    }

    property hasEnvironmentLight : bool {
        get { return mappedLightCounts.x != 0; }
    }
};

struct AccumulatedPixel
{
    float3 color;
    uint sampleCount;
};

#endif
