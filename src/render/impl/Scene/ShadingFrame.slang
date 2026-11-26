#ifndef RENDERLAB_SHADING_FRAME_SLANG
#define RENDERLAB_SHADING_FRAME_SLANG

struct ShadingFrame
{
    float3 normal;
    float3 tangent;
    float3 bitangent;

    __init()
    {
        normal = float3(0.0);
        tangent = float3(0.0);
        bitangent = float3(0.0);
    }

    __init(float3 normal)
    {
        this.normal = normal;
        float3 axis = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
        tangent = normalize(cross(axis, normal));
        bitangent = cross(normal, tangent);
    }

    float3 toLocal(float3 direction)
    {
        return float3(dot(direction, tangent), dot(direction, bitangent), dot(direction, normal));
    }

    float3 fromLocal(float3 direction)
    {
        return tangent * direction.x + bitangent * direction.y + normal * direction.z;
    }
};

#endif
