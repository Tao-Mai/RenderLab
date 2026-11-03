#ifndef RENDERLAB_GBUFFER_DATA_SLANG
#define RENDERLAB_GBUFFER_DATA_SLANG

#include "common/surface.slang"

struct GBufferData
{
    float4 diffuseOcclusion : SV_Target0;
    float4 normalRoughness : SV_Target1;
    float4 emissiveSpecularF90 : SV_Target2;
    float4 specularF0DiffuseFresnel : SV_Target3;
    float4 sheenColorRoughness : SV_Target4;

    property PbrBrdf brdf
    {
        get
        {
            PbrBrdf result;
            result.diffuseColor = diffuseOcclusion.rgb;
            result.roughness = normalRoughness.a;
            result.specularF0 = specularF0DiffuseFresnel.rgb;
            result.specularF90 = emissiveSpecularF90.a;
            result.diffuseFresnelWeight = specularF0DiffuseFresnel.a;
            return result;
        }
    }
    property float occlusion { get { return diffuseOcclusion.a; } }
    property float3 normal { get { return normalRoughness.xyz; } }
    property float3 emissive { get { return emissiveSpecularF90.rgb; } }
    property float3 sheenColor { get { return sheenColorRoughness.rgb; } }
    property float sheenRoughness { get { return sheenColorRoughness.a; } }
};

GBufferData packGBuffer(Surface surface)
{
    return {
        float4(surface.brdf.diffuseColor, surface.occlusion),
        float4(surface.normal, surface.brdf.roughness),
        float4(surface.emissive, surface.brdf.specularF90),
        float4(surface.brdf.specularF0, surface.brdf.diffuseFresnelWeight),
        float4(surface.sheenColor, surface.sheenRoughness),
    };
}

#endif
