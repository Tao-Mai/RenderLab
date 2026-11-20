#ifndef RENDERLAB_FULLSCREEN_SLANG
#define RENDERLAB_FULLSCREEN_SLANG

struct FullscreenVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
FullscreenVertexOutput vertMain(uint vertexId : SV_VertexID)
{
    float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return {float4(corner * 2.0 - 1.0, 0.0, 1.0), corner};
}

#endif
