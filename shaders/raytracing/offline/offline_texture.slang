#ifndef RENDERLAB_OFFLINE_TEXTURE_SLANG
#define RENDERLAB_OFFLINE_TEXTURE_SLANG

#include "../../texture_data.slang"
#include "../../../src/render/raytracing/offline/OfflineShaderTypes.h"

static const float kOfflinePi = 3.14159265359;

struct OfflineTextureInstruction
{
    uint4 metadata;
    float4 value;
    float4 uvScaleOffset;
    float4 uAxisKind;
    float4 vAxis;
    float4 settings;
    float4x4 textureFromWorld;
};

[[vk::binding(9, 3)]] StructuredBuffer<OfflineTextureInstruction> textureInstructions;

float noiseHash(float3 p) { return frac(sin(dot(p, float3(127.1, 311.7, 74.7))) * 43758.5453); }
float valueNoise(float3 p)
{
    float3 cell = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = lerp(noiseHash(cell), noiseHash(cell + float3(1, 0, 0)), f.x);
    float b = lerp(noiseHash(cell + float3(0, 1, 0)), noiseHash(cell + float3(1, 1, 0)), f.x);
    float c = lerp(noiseHash(cell + float3(0, 0, 1)), noiseHash(cell + float3(1, 0, 1)), f.x);
    float d = lerp(noiseHash(cell + float3(0, 1, 1)), noiseHash(cell + float3(1, 1, 1)), f.x);
    return lerp(lerp(a, b, f.y), lerp(c, d, f.y), f.z) * 2.0 - 1.0;
}

float2 textureUv(OfflineTextureInstruction node, float2 uv, float3 p)
{
    OfflineTextureMapping mapping = OfflineTextureMapping(uint(node.uAxisKind.w));
    if (mapping == OfflineTextureMapping::Uv) return uv * node.uvScaleOffset.xy + node.uvScaleOffset.zw;
    float3 local = mul(node.textureFromWorld, float4(p, 1.0)).xyz;
    if (mapping == OfflineTextureMapping::Spherical || mapping == OfflineTextureMapping::Cylindrical)
    {
        float phi = atan2(local.y, local.x);
        if (phi < 0.0) phi += 2.0 * kOfflinePi;
        return float2(phi / (2.0 * kOfflinePi), mapping == OfflineTextureMapping::Spherical
            ? acos(clamp(normalize(local).z, -1.0, 1.0)) / kOfflinePi : local.z);
    }
    return float2(dot(local, node.uAxisKind.xyz), dot(local, node.vAxis.xyz)) + node.uvScaleOffset.zw;
}

float3 evaluateTexture(uint program, float2 uv, float3 position, float3 normal, uint primitiveIndex)
{
    float3 values[kOfflineTextureStackSize];
    uint depth = 0;
    uint count = textureInstructions[program].metadata.y;
    for (uint index = 1; index <= count; ++index)
    {
        OfflineTextureInstruction node = textureInstructions[program + index];
        OfflineTextureOp op = OfflineTextureOp(node.metadata.x);
        float3 value = float3(0.0);
        switch (op)
        {
            case OfflineTextureOp::Constant:
                value = node.value.rgb;
                break;
            case OfflineTextureOp::Image:
            {
                float2 st = textureUv(node, uv, position);
                TextureRef image = {node.metadata.z, node.metadata.w};
                value = sampleTextureLevel(image, float2(st.x, 1.0 - st.y)).rgb;
                value *= node.value.x;
                if (node.value.y != 0.0) value = 1.0 - value;
                break;
            }
            case OfflineTextureOp::Multiply:
            {
                float3 second = values[--depth];
                float3 first = values[--depth];
                value = first * second;
                break;
            }
            case OfflineTextureOp::Mix:
            {
                float amount = saturate(values[--depth].x);
                float3 second = values[--depth];
                float3 first = values[--depth];
                value = lerp(first, second, amount);
                break;
            }
            case OfflineTextureOp::Checkerboard:
            {
                float3 second = values[--depth];
                float3 first = values[--depth];
                float2 st = textureUv(node, uv, position);
                float3 local = mul(node.textureFromWorld, float4(position, 1.0)).xyz;
                float parity = node.settings.x == 3.0 ? dot(floor(local), float3(1.0)) : dot(floor(st), float2(1.0));
                value = (int(parity) & 1) == 0 ? first : second;
                break;
            }
            case OfflineTextureOp::Dots:
            {
                float3 second = values[--depth];
                float3 first = values[--depth];
                float2 st = textureUv(node, uv, position);
                float2 cell = floor(st + 0.5);
                float2 center = cell + float2(noiseHash(float3(cell, 0)), noiseHash(float3(cell, 1))) * 0.5 - 0.25;
                value = length(st - center) < 0.35 ? first : second;
                break;
            }
            case OfflineTextureOp::DirectionMix:
            {
                float3 second = values[--depth];
                float3 first = values[--depth];
                value = lerp(first, second, saturate(abs(dot(normal, node.value.xyz))));
                break;
            }
            case OfflineTextureOp::Bilerp:
            {
                float3 v11 = values[--depth], v10 = values[--depth], v01 = values[--depth], v00 = values[--depth];
                float2 st = textureUv(node, uv, position);
                value = lerp(lerp(v00, v01, st.y), lerp(v10, v11, st.y), st.x);
                break;
            }
            case OfflineTextureOp::Fbm:
            case OfflineTextureOp::Wrinkled:
            case OfflineTextureOp::Windy:
            case OfflineTextureOp::Marble:
            {
                float3 p = mul(node.textureFromWorld, float4(position, 1.0)).xyz;
                if (op == OfflineTextureOp::Marble) p *= node.value.x;
                float result = 0.0, amplitude = 1.0;
                for (uint octave = 0; octave < uint(node.settings.y); ++octave)
                {
                    float noise = valueNoise(p);
                    result += amplitude * (op == OfflineTextureOp::Wrinkled ? abs(noise) : noise);
                    p *= 1.99;
                    amplitude *= node.settings.x;
                }
                if (op == OfflineTextureOp::Windy) result = abs(valueNoise(p * 0.1)) * abs(valueNoise(p));
                value = float3(result);
                if (op == OfflineTextureOp::Marble)
                {
                    float t = 0.5 + 0.5 * sin(mul(node.textureFromWorld, float4(position, 1.0)).y * node.value.x
                        + node.value.y * result);
                    value = lerp(float3(0.18, 0.13, 0.1), float3(0.9, 0.9, 0.88), t);
                }
                break;
            }
            case OfflineTextureOp::PtexPreview:
                value = node.value.x * (((primitiveIndex + uint(floor(uv.x * 8)) + uint(floor(uv.y * 8))) & 1) == 0
                    ? float3(0.7) : float3(0.3));
                break;
        }
        if (node.metadata.y != 0) value = float3(dot(value, float3(1.0 / 3.0)));
        values[depth++] = value;
    }
    return values[0];
}
#endif
