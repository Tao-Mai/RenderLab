#include "math/color.slang"
#include "raster/fullscreen.slang"

struct PresentParameters
{
    uint encodeSrgb;
};

[[vk::push_constant]] ConstantBuffer<PresentParameters> parameters;
[[vk::binding(0, 0)]] Texture2D<float4>                 sourceImage;
[[vk::binding(3, 0)]] SamplerState                      pointSampler;

[shader("fragment")] float4 fragMain(FullscreenVertexOutput input) : SV_Target
{
    float4 color = sourceImage.Sample(pointSampler, input.uv);
    if (parameters.encodeSrgb != 0)
        color.rgb = float3(linearToSrgb(color.r), linearToSrgb(color.g), linearToSrgb(color.b));
    return color;
}
