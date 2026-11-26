#ifndef RENDERLAB_RANDOM_SLANG
#define RENDERLAB_RANDOM_SLANG

#include "Utils/Sampling/SampleGeneratorInterface.slang"

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

uint randomIndex<S : ISampleGenerator>(uint count, inout S generator)
{
    uint threshold = (0u - count) % count;
    uint value;
    do
    {
        value = generator.next();
    }
    while (value < threshold);

    return value % count;
}

#endif
