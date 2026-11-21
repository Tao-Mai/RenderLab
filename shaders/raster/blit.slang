#include "raster/fullscreen.slang"

#ifdef BLIT_SRGB_CONTROL
#include "math/color.slang"

struct PresentParameters
{
    uint encodeSrgb;
};

[[vk::push_constant]] ConstantBuffer<PresentParameters> parameters;
#endif

[[vk::binding(0, 0)]] Texture2D<float4>                 sourceImage;
[[vk::binding(3, 0)]] SamplerState                      pointSampler;

[shader("fragment")] float4 fragMain(FullscreenVertexOutput input) : SV_Target
{
    float4 color = sourceImage.Sample(pointSampler, input.uv);
#ifdef BLIT_SRGB_CONTROL
    if (parameters.encodeSrgb != 0)
        color.rgb = float3(linearToSrgb(color.r), linearToSrgb(color.g), linearToSrgb(color.b));
#endif
    return color;
}
