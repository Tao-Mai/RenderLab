#include "passes/raster/ForwardPbr.h"

#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/graph/PassInputs.h"

void ForwardPbrPipelines::setup(vk::Format colorFormat, PipelineState state, bool ibl, bool clearcoat)
{
    pipelineInfo = {
        .layout       = renderContext().device->meshPipelineLayout,
        .state        = state,
        .colorFormats = {colorFormat},
        .depthFormat  = vk::Format::eD32Sfloat,
    };
    iblEnabled       = ibl;
    clearcoatEnabled = clearcoat;
    shaders          = {};
    pipelines        = {};
}

vk::Pipeline ForwardPbrPipelines::get(const MaterialTable::MaterialInfo& material)
{
    const bool clearcoat = material.clearcoat && clearcoatEnabled;
    const auto variant   = 2 * size_t(clearcoat) + size_t(material.sheen);
    auto&      pipeline  = pipelines[variant][material.doubleSided];
    if (!pipeline)
    {
        auto& ctx    = renderContext();
        auto& shader = shaders[variant];
        if (!shader.index)
            shader = ctx.shaderManager->getOrLoad(
                "forward",
                {{"ENABLE_IBL", iblEnabled ? 1 : 0},
                 {"ENABLE_CLEARCOAT", clearcoat ? 1 : 0},
                 {"ENABLE_SHEEN", material.sheen ? 1 : 0}}
            );

        pipelineInfo.shader         = shader;
        pipelineInfo.state.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        pipeline                    = ctx.device->getOrCreatePipeline(pipelineInfo);
    }
    return pipeline;
}

void bindForwardPbrResources(vk::raii::CommandBuffer& commands, const PassFrame& frame)
{
    const std::array sets{
        frame.data.sceneSetHandle(),
        frame.data.lightSetHandle(),
        frame.renderData.materialSetHandle(),
        frame.data.shadowSetHandle()
    };
    commands.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        renderContext().device->meshPipelineLayout,
        RenderInterface::kSceneSet,
        sets,
        {}
    );
}
