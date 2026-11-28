#ifndef RENDERLAB_ENV_MAP_SAMPLER_SLANG
#define RENDERLAB_ENV_MAP_SAMPLER_SLANG

#include "Scene/Scene.slang"
#include "Utils/Math/MathConstants.slangh"

struct EnvMapSample
{
    float3 dir;  // Sampled direction towards the light in world space.
    float  pdf;  // Probability density function for the sampled direction with respect to solid angle.
    float3 Le;   // Emitted radiance.
};

struct EnvMapSampler
{
    float3 eval(float3 dir, float lod = 0.f)
    {
        return gScene.envMap.eval(dir, lod);
    }

    bool sample(const float2 rnd, out EnvMapSample result, float lod = 0.f)
    {
        float cosTheta = 1.0f - 2.0f * rnd.x;
        float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
        float phi = M_2PI * rnd.y;

        result.dir = float3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));
        result.pdf = evalPdf(result.dir);
        result.Le = eval(result.dir, lod);

        return true;
    }

    float evalPdf(float3 dir)
    {
        return M_1_4PI;
    }
};

#endif
