#include "shaders/passes/raster/RasterPbr.h"

#include <slang-rhi/shader-cursor.h>
#include "render/GpuScene.h"
#include "render/resources/ShaderManager.h"
#include "render/device/RhiCheck.h"
#include "render/raster/MeshDraw.h"
#include "render/raster/ShadowResources.h"
#include "render/resources/RenderResourceManager.h"

void bindPbrTextures(const rhi::ShaderCursor& root, const GpuScene& scene, bool enableSheen)
{
    auto* sampler = scene.lightingLutSampler();
    if (enableSheen)
    {
        rhiCheck(root["sheenLUT"].setBinding(scene.sheenLut()));
        rhiCheck(root["sheenLUTSampler"].setBinding(sampler));
    }

    if (const auto* environment = scene.environmentMap())
    {
        rhiCheck(root["irradianceMap"].setBinding(environment->irradiance));
        rhiCheck(root["irradianceMapSampler"].setBinding(environment->irradianceSampler));
        rhiCheck(root["prefilteredSpecularMap"].setBinding(environment->prefilteredSpecular));
        rhiCheck(root["prefilteredSpecularMapSampler"].setBinding(environment->prefilteredSampler));
        rhiCheck(root["brdfLUT"].setBinding(scene.brdfLut()));
        rhiCheck(root["brdfLUTSampler"].setBinding(sampler));
        if (enableSheen)
            environment->bindRadiance(root);
    }
}

void ForwardPbrPipelines::setup(
    rhi::IDevice& targetDevice, rhi::Format colorFormat, const rhi::DepthStencilDesc& depthState, bool blend
)
{
    device            = &targetDevice;
    color             = {};
    color.format      = colorFormat;
    color.enableBlend = blend;
    color.color       = {rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::InvSrcAlpha, rhi::BlendOp::Add};
    color.alpha       = {rhi::BlendFactor::One, rhi::BlendFactor::InvSrcAlpha, rhi::BlendOp::Add};
    depth             = depthState;
    input             = createMeshInputLayout(targetDevice);
    pipelines         = {};
}

rhi::IShaderObject& ForwardPbrPipelines::bind(
    rhi::IRenderPassEncoder&           pass,
    ShaderManager&                     shaders,
    const GpuScene&                    scene,
    const MaterialTable::MaterialInfo& material,
    const ShadowBindings&              shadows,
    float                              ambientCoefficient,
    bool                               enableClearcoat
)
{
    const bool  hasEnvironment = scene.environmentMap() != nullptr;
    const bool  clearcoat      = material.clearcoat && enableClearcoat;
    const auto  variant        = 2 * size_t(clearcoat) + size_t(material.sheen);
    auto&       pipeline       = pipelines[hasEnvironment][variant][material.doubleSided];
    if (!pipeline)
    {
        const auto shader = shaders.getProgram(
            "shaders/raster/forward.slang", {"vertMain", "fragMain"},
            {{"HAS_ENV", hasEnvironment ? "1" : "0"},
             {"ENABLE_CLEARCOAT", clearcoat ? "1" : "0"},
             {"ENABLE_SHEEN", material.sheen ? "1" : "0"}}
        );
        rhi::RenderPipelineDesc desc;
        desc.program                  = shader;
        desc.inputLayout              = input;
        desc.targets                  = &color;
        desc.targetCount              = 1;
        desc.depthStencil             = depth;
        desc.rasterizer.cullMode      = material.doubleSided ? rhi::CullMode::None : rhi::CullMode::Back;
        desc.rasterizer.frontFace     = rhi::FrontFaceMode::CounterClockwise;
        desc.rasterizer.scissorEnable = true;
        rhiCheck(device->createRenderPipeline(desc, pipeline.writeRef()));
    }

    auto* object = pass.bindPipeline(pipeline);
    CHECK(object);
    const rhi::ShaderCursor root(object);
    if (!hasEnvironment)
        rhiCheck(root["lightingParameters"].getDereferenced()["ambientCoefficient"].setData(ambientCoefficient));
    bindPbrTextures(root, scene, material.sheen);
    shadows.bind(root);
    return *object;
}
