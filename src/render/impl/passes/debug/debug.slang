#include "Scene/Scene.slang"
#include "Utils/Math/MathConstants.slangh"

Texture2D<PackedHitInfo> vbuffer;

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
    nointerpolation bool isOmnidirectional : TEXCOORD3;
    nointerpolation float3 color : COLOR0;
};

[shader("vertex")]
MarkerVertex vertMain(uint vertexIndex : SV_VertexID, uint lightIndex : SV_InstanceID)
{
    const LightData light = gScene.getLight(lightIndex);
    const LightType type = LightType(light.type);
    const float2 corner = kQuadVertices[vertexIndex];

    MarkerVertex vertex;
    vertex.markerPosition = corner;
    vertex.lightType = light.type;
    vertex.isOmnidirectional = type == LightType::Point && light.openingAngle == M_PI;
    const float peak = max(light.intensity.r, max(light.intensity.g, light.intensity.b));
    vertex.color = peak > 0.0 ? light.intensity / peak : float3(1.0);
    vertex.arrowDirection = float2(0.0, 1.0);

    if (type == LightType::Rect)
    {
        const float3 worldPosition = mul(light.transMat, float4(corner, 0.0, 1.0)).xyz;
        vertex.position = mul(gScene.camera.getViewProj(), float4(worldPosition, 1.0));
    }
    else
    {
        vertex.position = mul(gScene.camera.getViewProj(), float4(light.posW, 1.0));

        uint2 extent;
        vbuffer.GetDimensions(extent.x, extent.y);
        const float radius = vertex.isOmnidirectional ? kPointRadius : kArrowRadius;
        vertex.position.xy += corner * (2.0 * radius / float2(extent)) * vertex.position.w;

        if (!vertex.isOmnidirectional)
        {
            const float3 viewDirection = mul((float3x3)gScene.camera.data.viewMat, light.dirW);
            if (dot(viewDirection.xy, viewDirection.xy) > 0.000001)
                vertex.arrowDirection = normalize(viewDirection.xy);
        }
    }

    return vertex;
}

float hitDepth(const HitInfo hit)
{
    const VertexData v = gScene.getVertexData(hit.getTriangleHit());
    const float4 clip = mul(gScene.camera.getViewProj(), float4(v.posW, 1.0));

    return clip.z / clip.w;
}

[shader("fragment")]
float4 fragMain(MarkerVertex vertex) : SV_Target
{
    float3 color = vertex.color;
    if (vertex.isOmnidirectional)
    {
        const float radiusSquared = dot(vertex.markerPosition, vertex.markerPosition);
        if (radiusSquared > 1.0)
            discard;
        if (radiusSquared > 0.64)
            color *= 0.2;
    }
    else if (LightType(vertex.lightType) == LightType::Rect)
    {
        if (max(abs(vertex.markerPosition.x), abs(vertex.markerPosition.y)) < 0.86)
            discard;
    }
    else
    {
        const float2 axis = vertex.arrowDirection;
        const float along = dot(vertex.markerPosition, axis);
        const float across = abs(dot(vertex.markerPosition, float2(-axis.y, axis.x)));
        const bool shaft = along >= -0.85 && along <= 0.25 && across < 0.18;
        const bool head = along >= 0.1 && along <= 0.9 && across < (0.9 - along) * 0.7;
        if (!shaft && !head)
            discard;
    }

    const HitInfo hit = HitInfo(vbuffer.Load(int3(int2(vertex.position.xy), 0)));
    if (hit.isValid())
    {
        if (vertex.position.z > hitDepth(hit) + kDepthBias)
            discard;
    }

    return float4(color, 1.0);
}
