#ifndef RENDERLAB_UNIFORM_SAMPLE_GENERATOR_SLANG
#define RENDERLAB_UNIFORM_SAMPLE_GENERATOR_SLANG

#include "Utils/Sampling/SampleGeneratorInterface.slang"

uint64_t nextSplitMix64(inout uint64_t state)
{
    static const uint64_t kSeedIncrement = 0x9e3779b97f4a7c15ull;
    static const uint64_t kFirstMix = 0xbf58476d1ce4e5b9ull;
    static const uint64_t kSecondMix = 0x94d049bb133111ebull;

    state += kSeedIncrement;
    uint64_t bits = (state ^ (state >> 30u)) * kFirstMix;
    bits = (bits ^ (bits >> 27u)) * kSecondMix;

    return bits ^ (bits >> 31u);
}

struct UniformSampleGenerator : ISampleGenerator
{
    uint4 state;

    __init(uint pixelIndex, uint sampleSeed)
    {
        uint64_t seed = (uint64_t(sampleSeed) << 32u) | uint64_t(pixelIndex);
        uint64_t low = nextSplitMix64(seed);
        uint64_t high = nextSplitMix64(seed);
        state = uint4(uint(low), uint(low >> 32u), uint(high), uint(high >> 32u));
    }

    // xoshiro128** consumes and updates the four words of the uniform stream.
    [mutating]
    uint next()
    {
        uint result = state.y * 5u;
        result = ((result << 7u) | (result >> 25u)) * 9u;

        uint shift = state.y << 9u;
        state.z ^= state.x;
        state.w ^= state.y;
        state.y ^= state.z;
        state.x ^= state.w;
        state.z ^= shift;
        state.w = (state.w << 11u) | (state.w >> 21u);

        return result;
    }
};

#endif
