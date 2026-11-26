#include "postprocess/fullscreen.slang"

#ifdef BLIT_SRGB_CONTROL
#include "math/color.slang"

struct PresentParameters
{
    uint encodeSrgb;
};

ConstantBuffer<PresentParameters> parameters;
#endif

Texture2D<float4>                 sourceImage;
SamplerState                      pointSampler;

[shader("fragment")] float4 fragMain(FullscreenVertexOutput input) : SV_Target
{
    float4 color = sourceImage.Sample(pointSampler, input.uv);
#ifdef BLIT_SRGB_CONTROL
    if (parameters.encodeSrgb != 0)
        color.rgb = float3(linearToSrgb(color.r), linearToSrgb(color.g), linearToSrgb(color.b));
#endif
    return color;
}
