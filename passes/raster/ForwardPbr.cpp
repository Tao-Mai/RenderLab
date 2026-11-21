#include "passes/raster/ForwardPbr.h"

#include "render/FrameData.h"
#include "render/PreparedScene.h"
#include "render/RenderContext.h"

void ForwardPbrPipelines::setup(const Falcor::Device& device, vk::Format colorFormat, PipelineState state, bool ibl, bool clearcoat)
{
    pipelineInfo = {
        .layout       = device.meshPipelineLayout,
        .state        = state,
        .colorFormats = {colorFormat},
        .depthFormat  = vk::Format::eD32Sfloat,
    };
    iblEnabled       = ibl;
    clearcoatEnabled = clearcoat;
    shaders          = {};
    pipelines        = {};
}

vk::Pipeline ForwardPbrPipelines::get(const Falcor::RenderContext& context, const MaterialTable::MaterialInfo& material)
{
    const bool clearcoat = material.clearcoat && clearcoatEnabled;
    const auto variant   = 2 * size_t(clearcoat) + size_t(material.sheen);
    auto&      pipeline  = pipelines[variant][material.doubleSided];
    if (!pipeline)
    {
        auto& shader = shaders[variant];
        if (!shader.index)
            shader = context.shaderManager->getOrLoad(
                "forward",
                {{"ENABLE_IBL", iblEnabled ? "1" : "0"},
                 {"ENABLE_CLEARCOAT", clearcoat ? "1" : "0"},
                 {"ENABLE_SHEEN", material.sheen ? "1" : "0"}}
            );

        pipelineInfo.shader         = shader;
        pipelineInfo.state.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        pipeline                    = context.getDevice()->getOrCreatePipeline(pipelineInfo);
    }
    return pipeline;
}

void bindForwardPbrResources(vk::raii::CommandBuffer& commands, const Falcor::RenderContext& frame)
{
    const std::array sets{
        frame.frameData->sceneSetHandle(),
        frame.frameData->lightSetHandle(),
        frame.scene->materialSetHandle(),
        frame.frameData->shadowSetHandle()
    };
    commands.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        frame.getDevice()->meshPipelineLayout,
        RenderInterface::kSceneSet,
        sets,
        {}
    );
}
