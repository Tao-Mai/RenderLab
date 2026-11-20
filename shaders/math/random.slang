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

// Use the upper 24 bits so float rounding cannot produce 1.0.
float randomFloat(inout uint state)
{
    static const float kUint24ToFloat = 1.0f / 16777216.0f;
    return float(randomUint(state) >> 8u) * kUint24ToFloat;
}

#endif
