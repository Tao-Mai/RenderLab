#ifndef RENDERLAB_ENV_MAP_SLANG
#define RENDERLAB_ENV_MAP_SLANG

#ifndef HAS_ENV
#define HAS_ENV 0
#endif

struct EnvMap
{
#if HAS_ENV
    TextureCube<float4> envMap;      // Environment map cubemap texture.
    SamplerState        envSampler;  // Environment map texture sampler.
#else
    uint _dummy;
#endif

    // Evaluates the radiance coming from world space direction 'dir'.
    float3 eval(float3 dir, float lod = 0.f)
    {
#if HAS_ENV
        return envMap.SampleLevel(envSampler, dir, lod).rgb;
#else
        return float3(0.0);
#endif
    }
};

#endif
