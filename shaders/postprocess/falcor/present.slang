#include "postprocess/falcor/fullscreen.slang"

struct PresentParameters
{
    uint encodeSrgb;
};

[[vk::push_constant]] ConstantBuffer<PresentParameters> parameters;
[[vk::binding(0, 0)]] Texture2D<float4>                 sourceImage;
[[vk::binding(3, 0)]] SamplerState                      pointSampler;

float linearToSrgb(float value) { return value <= 0.0031308 ? 12.92 * value : 1.055 * pow(value, 1.0 / 2.4) - 0.055; }

[shader("fragment")] float4 fragMain(VertexOutput input) : SV_Target
{
    float4 color = sourceImage.Sample(pointSampler, input.uv);
    if (parameters.encodeSrgb != 0)
        color.rgb = float3(linearToSrgb(color.r), linearToSrgb(color.g), linearToSrgb(color.b));
    return color;
}
