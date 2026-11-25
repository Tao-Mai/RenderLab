struct UiParameters
{
    float2 scale;
    float2 translate;
};

ConstantBuffer<UiParameters> parameters;
Texture2D<float4>            sourceImage;
SamplerState                 linearSampler;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD;
    float4 color    : COLOR;
};

[shader("vertex")]
VertexOutput vertMain(float2 position : POSITION, float2 uv : TEXCOORD, float4 color : COLOR)
{
    float2 clipPosition = position * parameters.scale + parameters.translate;

    return {float4(clipPosition, 0.0, 1.0), uv, color};
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    return input.color * sourceImage.Sample(linearSampler, input.uv);
}
