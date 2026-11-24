#include "render/FrameData.h"
#include "render/device/RhiCheck.h"

void FrameData::bindEnvironment(
    rhi::ITextureView* irradiance,
    rhi::ISampler*     irradianceSampling,
    rhi::ITextureView* prefiltered,
    rhi::ISampler*     prefilteredSampling,
    rhi::ITextureView* radiance,
    rhi::ISampler*     radianceSampling
)
{
    irradianceMap          = irradiance;
    irradianceSampler      = irradianceSampling;
    prefilteredSpecularMap = prefiltered;
    prefilteredSampler     = prefilteredSampling;
    radianceMap            = radiance;
    radianceSampler        = radianceSampling;
}
void FrameData::bindLuts(rhi::ITextureView* brdf, rhi::ITextureView* sheen, rhi::ISampler* sampler)
{
    brdfLut    = brdf;
    sheenLut   = sheen;
    lutSampler = sampler;
}

void bindFrameData(rhi::IShaderObject* object, const FrameData& frame)
{
    const rhi::ShaderCursor root(object);
    // Programs consume different subsets of the renderer's scene data.
    const auto bind = [&](const char* name, const rhi::Binding& resource)
    {
        const auto cursor = root[name];
        if (cursor.isValid())
            rhiCheck(cursor.setBinding(resource));
    };
    if (const auto field = root["sceneUniforms"]; field.isValid())
        rhiCheck(field.getDereferenced().setData(frame.sceneUniforms));
    if (const auto field = root["lightUniforms"]; field.isValid())
        rhiCheck(field.getDereferenced().setData(frame.lightUniforms));
    if (const auto field = root["editorView"]; field.isValid())
        rhiCheck(field.getDereferenced().setData(frame.sceneUniforms.viewProjection));

    bind("lights", frame.lights);
    bind("irradianceMap", frame.irradianceMap);
    bind("irradianceMapSampler", frame.irradianceSampler);
    bind("prefilteredSpecularMap", frame.prefilteredSpecularMap);
    bind("prefilteredSpecularMapSampler", frame.prefilteredSampler);
    bind("radianceMap", frame.radianceMap);
    bind("radianceSampler", frame.radianceSampler);
    bind("brdfLUT", frame.brdfLut);
    bind("brdfLUTSampler", frame.lutSampler);
    bind("sheenLUT", frame.sheenLut);
    bind("sheenLUTSampler", frame.lutSampler);
}
