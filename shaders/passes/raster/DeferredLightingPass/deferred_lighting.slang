#include "raster/scene_lighting.slang"
#include "raster/gbuffer_data.slang"
#include "raster/fullscreen.slang"

// Pass set 2; bindings match RenderInterface's G-buffer bindings.
Texture2D<float4> diffuseOcclusion;
Texture2D<float4> normalRoughness;
Texture2D<float4> emissiveSpecularF90;
Texture2D<float4> specularF0DiffuseFresnel;
Texture2D<float4> sheenColorRoughness;
Texture2D<float> sceneDepth;

[shader("fragment")]
float4 fragMain(FullscreenVertexOutput input) : SV_Target
{
    uint2 pixel = uint2(input.position.xy);
    float depth = sceneDepth.Load(int3(pixel, 0));
    if (depth >= 1.0)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float2 ndc = uvToClip(input.uv);
    float4 worldPosition = mul(scene.camera.inverseViewProjection, float4(ndc, depth, 1.0));
    GBufferData data = {
        diffuseOcclusion.Load(int3(pixel, 0)),
        normalRoughness.Load(int3(pixel, 0)),
        emissiveSpecularF90.Load(int3(pixel, 0)),
        specularF0DiffuseFresnel.Load(int3(pixel, 0)),
        sheenColorRoughness.Load(int3(pixel, 0)),
    };

    Surface surface;
    surface.worldPosition = worldPosition.xyz / worldPosition.w;
    surface.normal = normalize(data.normal);
    surface.brdf = data.brdf;
    surface.opacity = 1.0;
    surface.occlusion = data.occlusion;
    surface.emissive = data.emissive;
    surface.sheenColor = data.sheenColor;
    surface.sheenRoughness = data.sheenRoughness;
    return float4(evaluateLighting(surface, pixel), 1.0);
}
