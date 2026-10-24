#ifndef RENDERLAB_LOW_DISCREPANCY_SLANG
#define RENDERLAB_LOW_DISCREPANCY_SLANG

#include "random.slang"

// Fast Owen scrambling, as described in PBRT's Sobol sampler.
// https://www.pbr-book.org/4ed/Sampling_and_Reconstruction/Sobol_Samplers
uint fastOwenScramble(uint value, uint seed)
{
    static const uint kMix0 = 0x3d20adeau;
    static const uint kMix1 = 0x05526c56u;
    static const uint kMix2 = 0x53a22864u;

    value = reversebits(value);
    value ^= value * kMix0;
    value += seed;
    value *= (seed >> 16u) | 1u;
    value ^= value * kMix1;
    value ^= value * kMix2;
    return reversebits(value);
}

float2 sampleSobol2D(uint sampleIndex, uint scrambleSeed)
{
    static const uint kLeadingBit = 0x80000000u;
    static const float kUint24ToFloat = 1.0 / 16777216.0;

    // First two Sobol generator matrices; no direction-number table is needed.
    uint2 value = uint2(reversebits(sampleIndex), 0u);
    uint direction = kLeadingBit;
    for (uint bits = sampleIndex; bits != 0u; bits >>= 1u)
    {
        if ((bits & 1u) != 0u)
            value.y ^= direction;
        direction ^= direction >> 1u;
    }

    // The scramble is fixed for this pixel across all sample indices.
    uint seedX = randomUint(scrambleSeed);
    uint seedY = randomUint(scrambleSeed);
    value.x = fastOwenScramble(value.x, seedX);
    value.y = fastOwenScramble(value.y, seedY);
    return float2(value >> 8u) * kUint24ToFloat;
}

#endif
