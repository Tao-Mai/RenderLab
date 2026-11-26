#include "raytracing/geometry.slang"
#include "raytracing/visibility_buffer.slang"
#include "scene/scene_data.slang"

Texture2D<uint4> vbuffer;

static const float kPointRadius = 5.0;
static const float kArrowRadius = 9.0;
static const float kDepthBias = 0.000001;
static const float2 kQuadVertices[6] = {
    float2(-1, -1), float2(1, -1), float2(-1, 1),
    float2(-1, 1), float2(1, -1), float2(1, 1),
};

struct MarkerVertex
{
    float4 position : SV_Position;
    float2 markerPosition : TEXCOORD0;
    nointerpolation float2 arrowDirection : TEXCOORD1;
    nointerpolation uint lightType : TEXCOORD2;
    nointerpolation float3 color : COLOR0;
};

[shader("vertex")]
MarkerVertex vertMain(uint vertexIndex : SV_VertexID, uint lightIndex : SV_InstanceID)
{
    const LightData light = scene.analyticLights[lightIndex];
    const float2 corner = kQuadVertices[vertexIndex];

    MarkerVertex vertex;
    vertex.markerPosition = corner;
    vertex.lightType = uint(light.type);
    const float peak = max(light.color.r, max(light.color.g, light.color.b));
    vertex.color = peak > 0.0 ? light.color / peak : float3(1.0);
    vertex.arrowDirection = float2(0.0, 1.0);

    if (light.type == LightType::RectArea)
    {
        const float3 worldPosition = sampleRectLightPosition(light, corner * 0.5 + 0.5);
        vertex.position = mul(scene.camera.viewProjection, float4(worldPosition, 1.0));
    }
    else
    {
        vertex.position = mul(scene.camera.viewProjection, float4(light.position, 1.0));

        uint2 extent;
        vbuffer.GetDimensions(extent.x, extent.y);
        const float radius = light.type == LightType::Point ? kPointRadius : kArrowRadius;
        vertex.position.xy += corner * (2.0 * radius / float2(extent)) * vertex.position.w;

        if (light.type != LightType::Point)
        {
            const float3 viewDirection = mul((float3x3)scene.camera.view, light.direction.xyz);
            if (dot(viewDirection.xy, viewDirection.xy) > 0.000001)
                vertex.arrowDirection = normalize(viewDirection.xy);
        }
    }

    return vertex;
}

float hitDepth(VBufferHit hit)
{
    const HitData object = scene.objects[hit.instanceIndex];
    const uint firstIndex = object.firstIndex + hit.primitiveIndex * 3;
    TriangleVertices triangle = loadTriangleVertices(object.vertexAddress, object.indexAddress, firstIndex);
    const float3 position = triangle.interpolate(hit.barycentrics).position;
    const float4 clip = mul(scene.camera.viewProjection, mul(object.objectToWorld, float4(position, 1.0)));

    return clip.z / clip.w;
}

[shader("fragment")]
float4 fragMain(MarkerVertex vertex) : SV_Target
{
    float3 color = vertex.color;
    switch (LightType(vertex.lightType))
    {
        case LightType::Point:
        {
            const float radiusSquared = dot(vertex.markerPosition, vertex.markerPosition);
            if (radiusSquared > 1.0)
                discard;
            if (radiusSquared > 0.64)
                color *= 0.2;
            break;
        }
        case LightType::RectArea:
            if (max(abs(vertex.markerPosition.x), abs(vertex.markerPosition.y)) < 0.86)
                discard;
            break;
        case LightType::Spot:
        case LightType::Directional:
        {
            const float2 axis = vertex.arrowDirection;
            const float along = dot(vertex.markerPosition, axis);
            const float across = abs(dot(vertex.markerPosition, float2(-axis.y, axis.x)));
            const bool shaft = along >= -0.85 && along <= 0.25 && across < 0.18;
            const bool head = along >= 0.1 && along <= 0.9 && across < (0.9 - along) * 0.7;
            if (!shaft && !head)
                discard;
            break;
        }
    }

    VBufferHit hit = {vbuffer.Load(int3(int2(vertex.position.xy), 0))};
    if (hit.isValid())
    {
        if (vertex.position.z > hitDepth(hit) + kDepthBias)
            discard;
    }

    return float4(color, 1.0);
}
