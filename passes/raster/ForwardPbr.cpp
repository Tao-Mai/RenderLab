#include "passes/raster/ForwardPbr.h"
#include "render/RenderContext.h"
#include "render/device/RhiCheck.h"
#include "render/raster/MeshDraw.h"
void ForwardPbrPipelines::setup(
    rhi::IDevice&                device,
    rhi::Format                  colorFormat,
    const rhi::DepthStencilDesc& depthState,
    bool                         blend,
    bool                         clearcoat
)
{
    color             = {};
    color.format      = colorFormat;
    color.enableBlend = blend;
    color.color       = {rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::InvSrcAlpha, rhi::BlendOp::Add};
    color.alpha       = {rhi::BlendFactor::One, rhi::BlendFactor::InvSrcAlpha, rhi::BlendOp::Add};
    depth             = depthState;
    input             = createMeshInputLayout(device);
    clearcoatEnabled  = clearcoat;
    pipelines         = {};
}
rhi::IRenderPipeline*
ForwardPbrPipelines::get(const RenderContext& context, const MaterialTable::MaterialInfo& material)
{
    const bool hasEnvironment = context.scene->environmentMap != nullptr;
    const bool clearcoat      = material.clearcoat && clearcoatEnabled;
    const auto variant        = 2 * size_t(clearcoat) + size_t(material.sheen);
    auto& pipeline            = pipelines[hasEnvironment][variant][material.doubleSided];
    if (!pipeline)
    {
        const auto shader = context.shaderManager->getOrCompile(
            "forward",
            {{"HAS_ENV", hasEnvironment ? "1" : "0"},
             {"ENABLE_CLEARCOAT", clearcoat ? "1" : "0"},
             {"ENABLE_SHEEN", material.sheen ? "1" : "0"}}
        );
        rhi::RenderPipelineDesc desc;
        desc.program                  = context.shaderManager->program(shader);
        desc.inputLayout              = input;
        desc.targets                  = &color;
        desc.targetCount              = 1;
        desc.depthStencil             = depth;
        desc.rasterizer.cullMode      = material.doubleSided ? rhi::CullMode::None : rhi::CullMode::Back;
        desc.rasterizer.frontFace     = rhi::FrontFaceMode::CounterClockwise;
        desc.rasterizer.scissorEnable = true;
        rhiCheck(context.device->createRenderPipeline(desc, pipeline.writeRef()));
    }
    return pipeline;
}
