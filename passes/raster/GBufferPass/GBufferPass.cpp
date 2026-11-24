#include "passes/raster/GBufferPass/GBufferPass.h"

#include "core/Logger.h"
#include "core/Plugin.h"

#include <slang-rhi.h>
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/resources/MaterialTable.h"

namespace
{
constexpr std::string_view kGBufferShader = "gbuffer";
}


Falcor::RenderPassReflection GBufferPass::reflect(const CompileData& compileData)
{
    const auto* input  = compileData.connectedResources.getField("depth");
    const auto  extent = input ? Falcor::uint2{input->getWidth(), input->getHeight()} : Falcor::uint2{};

    Falcor::RenderPassReflection reflection;
    reflection.addInput("depth", "Scene depth")
        .format(rhi::Format::D32Float)
        .bindFlags(Falcor::ResourceBindFlags::DepthStencil);
    for (size_t index = 0; index < kColorNames.size(); ++index)
        reflection.addOutput(std::string{kColorNames[index]}, "G-buffer channel")
            .format(kColorFormats[index])
            .bindFlags(Falcor::ResourceBindFlags::RenderTarget)
            .texture2D(extent.x, extent.y);
    return reflection;
}


void GBufferPass::compile(Falcor::RenderContext* passContext, const CompileData&)
{
    std::array<rhi::ColorTargetDesc, kColorFormats.size()> colors;
    for (size_t index = 0; index < colors.size(); ++index)
        colors[index].format = kColorFormats[index];
    rhi::RenderPipelineDesc desc;
    desc.program      = passContext->shaderManager->program(passContext->shaderManager->getOrLoad(kGBufferShader));
    desc.targets      = colors.data();
    desc.targetCount  = uint32_t(colors.size());
    desc.depthStencil = {
        .format           = rhi::Format::D32Float,
        .depthTestEnable  = true,
        .depthWriteEnable = false,
        .depthFunc        = rhi::ComparisonFunc::Equal
    };
    pipelines = createMeshCullPipelines(*passContext->device, desc);
}


void GBufferPass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    std::array<rhi::RenderPassColorAttachment, kColorFormats.size()> colors;
    std::array<rhi::ComPtr<rhi::ITextureView>, kColorFormats.size()> views;
    for (size_t index = 0; index < colors.size(); ++index)
    {
        views[index] = renderData.getTexture(kColorNames[index])->getDefaultView();
        CHECK(views[index]);
        colors[index].view          = views[index];
        colors[index].clearValue[3] = 1.0f;
    }
    const auto target = renderData.getTexture("depth");
    const auto view   = target->getDefaultView();
    CHECK(view);
    rhi::RenderPassDepthStencilAttachment depth;
    depth.view          = view;
    depth.depthLoadOp   = rhi::LoadOp::Load;
    depth.depthReadOnly = true;
    auto* pass          = renderCommands->commands->beginRenderPass({colors.data(), uint32_t(colors.size()), &depth});
    CHECK(pass);
    const auto&      size     = target->getDesc().size;
    const auto       viewport = renderCommands->view->viewport.clamped({size.width, size.height});
    MeshDrawRecorder recorder{*pass, viewport.renderState(), *renderCommands};
    recorder.drawOpaque([&](const auto& material) { return pipelines.get(material); });
    pass->end();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, GBufferPass>();
}
