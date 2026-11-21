#ifndef RENDERLAB_RESTIR_FRAME_PARAMETERS_H
#define RENDERLAB_RESTIR_FRAME_PARAMETERS_H

#ifdef __cplusplus
#include "core/MetadataAnnotations.h"

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#define RENDERLAB_RESTIR_FRAME_ALIGN alignas(16) RL_SHADER_LAYOUT
#define RENDERLAB_RESTIR_FRAME_MAT4 glm::mat4
#define RENDERLAB_RESTIR_FRAME_VEC4 glm::vec4
#define RENDERLAB_RESTIR_FRAME_UINT uint32_t
#else
#define RENDERLAB_RESTIR_FRAME_ALIGN
#define RENDERLAB_RESTIR_FRAME_MAT4 float4x4
#define RENDERLAB_RESTIR_FRAME_VEC4 float4
#define RENDERLAB_RESTIR_FRAME_UINT uint
#endif

struct RENDERLAB_RESTIR_FRAME_ALIGN RestirFrameParameters
{
    RENDERLAB_RESTIR_FRAME_MAT4 previousViewProjection;
    RENDERLAB_RESTIR_FRAME_VEC4 previousViewport;
    RENDERLAB_RESTIR_FRAME_VEC4 previousDepthRow;
    RENDERLAB_RESTIR_FRAME_UINT width;
    RENDERLAB_RESTIR_FRAME_UINT height;
    RENDERLAB_RESTIR_FRAME_UINT writeLayer;
    RENDERLAB_RESTIR_FRAME_UINT historyValid;
    RENDERLAB_RESTIR_FRAME_UINT maxHistoryLength;
    RENDERLAB_RESTIR_FRAME_UINT spatialSampleCount;
    float spatialRadius;
    RENDERLAB_RESTIR_FRAME_UINT temporalReuse;
    RENDERLAB_RESTIR_FRAME_UINT spatialReuse;
    RENDERLAB_RESTIR_FRAME_UINT candidateCount;
    RENDERLAB_RESTIR_FRAME_UINT padding1 = 0;
    RENDERLAB_RESTIR_FRAME_UINT padding2 = 0;

#ifndef __cplusplus
    property float2 previousViewportOffset
    {
        get { return previousViewport.xy; }
    }
    property float2 previousViewportExtent
    {
        get { return previousViewport.zw; }
    }
    property uint pixelCount
    {
        get { return width * height; }
    }
#endif
};

#undef RENDERLAB_RESTIR_FRAME_ALIGN
#undef RENDERLAB_RESTIR_FRAME_MAT4
#undef RENDERLAB_RESTIR_FRAME_VEC4
#undef RENDERLAB_RESTIR_FRAME_UINT

#ifdef __cplusplus
#include RL_METADATA_HEADER("metadata/passes/raytracing/realtime/RestirFrameParameters.generated.h")
#endif
#endif
