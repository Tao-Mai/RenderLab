#include "passes/raster/ScenePass/ScenePass.h"

#include "core/Logger.h"
#include "core/Plugin.h"

#include <slang-rhi.h>
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/raster/MeshDraw.h"
#include "render/raster/ShadowResources.h"
#include "render/resources/MaterialTable.h"

ScenePass::ScenePass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)),
    parameters(Falcor::deserializeFromProperties<RasterLightingSettings>(properties))
{
}


Falcor::Properties ScenePass::getProperties() const { return Falcor::serializeToProperties(parameters); }


void ScenePass::setProperties(const Falcor::Properties& properties)
{
    RasterLightingSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);

    const bool recompile = next.hdr != parameters.hdr;
    parameters           = std::move(next);
    if (recompile)
        requestRecompile();
}


Falcor::RenderPassReflection ScenePass::reflect(const CompileData& compileData)
{
    const auto* depth  = compileData.connectedResources.getField("depth");
    const auto  extent = depth ? Falcor::uint2{depth->getWidth(), depth->getHeight()} : Falcor::uint2{};

    Falcor::RenderPassReflection reflection;
    addShadowInputs(reflection);
    reflection.addInput("depth", "Scene depth")
        .format(rhi::Format::D32Float)
        .bindFlags(Falcor::ResourceBindFlags::DepthStencil);
    reflection.addOutput("color", "Raster lighting result")
        .format(parameters.getColorFormat(compileData.defaultTexFormat))
        .bindFlags(Falcor::ResourceBindFlags::RenderTarget)
        .texture2D(extent.x, extent.y);
    return reflection;
}


void ScenePass::compile(Falcor::RenderContext* passContext, const CompileData& compileData)
{
    const auto& config = *passContext->config;
    parameters.validateColorFormat(*passContext->device);
    const rhi::DepthStencilDesc depth{
        .format           = rhi::Format::D32Float,
        .depthTestEnable  = true,
        .depthWriteEnable = false,
        .depthFunc        = rhi::ComparisonFunc::Equal
    };
    pipelines.setup(
        *passContext->device,
        parameters.getColorFormat(compileData.defaultTexFormat),
        depth,
        false,
        config.feature.ibl,
        config.renderPath != RenderPath::Deferred
    );
}


void ScenePass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    const auto target    = renderData.getTexture("color");
    const auto colorView = target->getDefaultView();
    CHECK(colorView);
    const auto depthView = renderData.getTexture("depth")->getDefaultView();
    CHECK(depthView);
    rhi::RenderPassColorAttachment color;
    color.view          = colorView;
    color.loadOp        = rhi::LoadOp::Clear;
    color.clearValue[3] = 1.0f;
    rhi::RenderPassDepthStencilAttachment depth;
    depth.view          = depthView;
    depth.depthLoadOp   = rhi::LoadOp::Load;
    depth.depthReadOnly = true;
    auto* pass          = renderCommands->commands->beginRenderPass({&color, 1, &depth});
    CHECK(pass);
    const auto&      size     = target->getDesc().size;
    const auto       viewport = renderCommands->view->viewport.clamped({size.width, size.height});
    const ShadowBindings shadows{renderData, *renderCommands->frameData, *renderCommands->resources};
    MeshDrawRecorder recorder{*pass, viewport.renderState(), *renderCommands, &shadows};
    recorder.drawOpaque([&](const auto& material) { return pipelines.get(*renderCommands, material); });
    pass->end();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, ScenePass>();
}
