#pragma once
#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "render/resources/ShaderData.h"

struct FrameData
{
    SceneUniforms                  sceneUniforms;
    LightUniforms                  lightUniforms;
    ShadowUniforms                 shadowUniforms{};
    rhi::ComPtr<rhi::IBuffer>      lights;
    rhi::ComPtr<rhi::ITextureView> irradianceMap, prefilteredSpecularMap, radianceMap, brdfLut, sheenLut;
    rhi::ComPtr<rhi::ISampler>     irradianceSampler, prefilteredSampler, radianceSampler, lutSampler;
    rhi::ISampler*                shadowSampler = nullptr;

    void bindEnvironment(
        rhi::ITextureView* irradiance,
        rhi::ISampler*     irradianceSampler,
        rhi::ITextureView* prefiltered,
        rhi::ISampler*     prefilteredSampler,
        rhi::ITextureView* radiance,
        rhi::ISampler*     radianceSampler
    );
    void bindLuts(rhi::ITextureView* brdf, rhi::ITextureView* sheen, rhi::ISampler* sampler);
};
void bindFrameData(rhi::IShaderObject* object, const FrameData& frame);
