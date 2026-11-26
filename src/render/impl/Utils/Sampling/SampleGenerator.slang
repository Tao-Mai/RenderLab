#ifndef RENDERLAB_SAMPLE_GENERATOR_SLANG
#define RENDERLAB_SAMPLE_GENERATOR_SLANG

#if !defined(SAMPLE_GENERATOR_TYPE) || SAMPLE_GENERATOR_TYPE != 1
#error Unsupported sample generator type
#endif

#include "Utils/Sampling/UniformSampleGenerator.slang"

typedef UniformSampleGenerator SampleGenerator;

#endif
