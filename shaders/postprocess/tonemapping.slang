struct ToneMappingParameters
{
    float exposure;
    uint mode;
    uint encodeSrgb;
    uint padding;
};

[[vk::push_constant]]
ConstantBuffer<ToneMappingParameters> parameters;

[[vk::binding(0, 0)]]
Texture2D<float4> hdrColor;

[[vk::binding(1, 0)]]
SamplerState hdrSampler;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertMain(uint vertexId : SV_VertexID)
{
    float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    VertexOutput output;
    output.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    output.uv = corner;
    return output;
}

// Slang adaptation of Khronos PBR Neutral; input and output are linear Rec. 709.
// Copyright 2024 The Khronos Group Inc. SPDX-License-Identifier: CC-BY-4.0
// https://github.com/KhronosGroup/ToneMapping/blob/main/PBR_Neutral/pbrNeutral.glsl
float3 neutralToneMapping(float3 color)
{
    const float kStartCompression = 0.8 - 0.04;
    const float kDesaturation = 0.15;

    float minimum = min(color.r, min(color.g, color.b));
    float offset = minimum < 0.08 ? minimum - 6.25 * minimum * minimum : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < kStartCompression)
        return color;

    float d = 1.0 - kStartCompression;
    float newPeak = 1.0 - d * d / (peak + d - kStartCompression);
    color *= newPeak / peak;

    float desaturation = 1.0 - 1.0 / (kDesaturation * (peak - newPeak) + 1.0);
    return lerp(color, float3(newPeak), desaturation);
}

float linearToSrgb(float value)
{
    return value <= 0.0031308 ? 12.92 * value : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    float3 color = max(hdrColor.Sample(hdrSampler, input.uv).rgb, float3(0.0));
    color *= exp2(parameters.exposure);
    color = parameters.mode == 0 ? neutralToneMapping(color) : color / (1.0 + color);
    color = saturate(color);

    // sRGB attachments encode automatically; UNORM swapchains need explicit encoding.
    if (parameters.encodeSrgb != 0)
        color = float3(linearToSrgb(color.r), linearToSrgb(color.g), linearToSrgb(color.b));

    return float4(color, 1.0);
}
