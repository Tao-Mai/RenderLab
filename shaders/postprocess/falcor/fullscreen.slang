struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")] VertexOutput vertMain(uint vertexId : SV_VertexID)
{
    float2       corner = float2((vertexId << 1) & 2, vertexId & 2);
    VertexOutput output;
    output.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    output.uv       = corner;
    return output;
}
