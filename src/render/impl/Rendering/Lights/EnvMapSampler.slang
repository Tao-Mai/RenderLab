#ifndef RENDERLAB_ENV_MAP_SAMPLER_SLANG
#define RENDERLAB_ENV_MAP_SAMPLER_SLANG

#include "Scene/Lights/EnvMap.slang"

struct EnvMapSample
{
    float3 direction;
    float3 radiance;
    float  pdf;  // Solid-angle PDF for the sampled direction.
};

struct EnvMapSampler
{
    float3 eval(float3 direction)
    {
        return environmentRadiance(direction);
    }

    float evalPdf(float3 direction)
    {
        return 1.0 / (4.0 * PI);
    }

    bool sample(float2 sampleValue, out EnvMapSample result)
    {
        float elevation = 1.0 - 2.0 * sampleValue.x;
        float radius = sqrt(max(0.0, 1.0 - elevation * elevation));
        float angle = 2.0 * PI * sampleValue.y;
        result.direction = float3(radius * cos(angle), elevation, radius * sin(angle));
        result.radiance = eval(result.direction);
        result.pdf = evalPdf(result.direction);

        return true;
    }
};

#endif
