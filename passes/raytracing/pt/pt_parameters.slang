#ifndef RENDERLAB_PT_PARAMETERS_SLANG
#define RENDERLAB_PT_PARAMETERS_SLANG

#include "raytracing/offline/path_parameters.slang"

struct PtParameters { PathParameters path; };
ConstantBuffer<PtParameters> parameters;

#endif
