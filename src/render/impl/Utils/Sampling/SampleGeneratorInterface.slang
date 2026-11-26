#ifndef RENDERLAB_SAMPLE_GENERATOR_INTERFACE_SLANG
#define RENDERLAB_SAMPLE_GENERATOR_INTERFACE_SLANG

interface ISampleGenerator
{
    [mutating]
    uint next();
};

float sampleNext1D<S : ISampleGenerator>(inout S generator)
{
    // Use the upper 24 bits so float rounding cannot produce 1.0.
    static const float kUint24ToFloat = 1.0f / 16777216.0f;

    return float(generator.next() >> 8u) * kUint24ToFloat;
}

float2 sampleNext2D<S : ISampleGenerator>(inout S generator)
{
    float2 sample;
    sample.x = sampleNext1D(generator);
    sample.y = sampleNext1D(generator);

    return sample;
}

float3 sampleNext3D<S : ISampleGenerator>(inout S generator)
{
    float3 sample;
    sample.x = sampleNext1D(generator);
    sample.y = sampleNext1D(generator);
    sample.z = sampleNext1D(generator);

    return sample;
}

#endif
