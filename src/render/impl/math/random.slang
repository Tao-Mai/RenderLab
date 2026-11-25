#ifndef RENDERLAB_RANDOM_SLANG
#define RENDERLAB_RANDOM_SLANG

// PCG RXS-M-XS: advance the state, then permute it into a random uint.
uint randomUint(inout uint state)
{
    static const uint kStateMultiplier = 747796405u;
    static const uint kStateIncrement = 2891336453u;
    static const uint kOutputMultiplier = 277803737u;

    state = state * kStateMultiplier + kStateIncrement;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * kOutputMultiplier;
    return (word >> 22u) ^ word;
}

float randomUintToFloat(uint sample)
{
    // Use the upper 24 bits so float rounding cannot produce 1.0.
    static const float kUint24ToFloat = 1.0f / 16777216.0f;
    return float(sample >> 8u) * kUint24ToFloat;
}

float randomFloat(inout uint state) { return randomUintToFloat(randomUint(state)); }

#if defined(SAMPLE_GENERATOR_TYPE)
#if SAMPLE_GENERATOR_TYPE != 1
#error Unsupported sample generator type
#endif
#include "render/impl/math/uniform_sample_generator.slang"
typedef UniformSampleGenerator SampleGenerator;
float randomFloat(inout UniformSampleGenerator generator) { return randomUintToFloat(randomUint(generator)); }
#else
typedef uint SampleGenerator;
#endif

#endif
