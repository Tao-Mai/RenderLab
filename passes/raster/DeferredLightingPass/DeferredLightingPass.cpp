#include "passes/raster/DeferredLightingPass/DeferredLightingPass.h"

#include "core/Logger.h"
#include "core/Plugin.h"
#include "core/profiling/Profiler.h"

#include <slang-rhi.h>
#include "core/Context.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/raster/ShadowResources.h"
#include "render/resources/ShaderData.h"

#include <array>
#include <utility>

namespace
{
constexpr std::string_view kDeferredLightingShader = "deferred_lighting";
constexpr std::array       kGBufferChannels{
    std::pair{"diffuseOcclusion", "diffuseOcclusion"},
    std::pair{"normalRoughness", "normalRoughness"},
    std::pair{"emissiveSpecularF90", "emissiveSpecularF90"},
    std::pair{"specularF0DiffuseFresnel", "specularF0DiffuseFresnel"},
    std::pair{"sheenColorRoughness", "sheenColorRoughness"},
    std::pair{"depth", "sceneDepth"}
};
}  // namespace


DeferredLightingPass::DeferredLightingPass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)),
    parameters(Falcor::deserializeFromProperties<RasterLightingSettings>(properties))
{
}


Falcor::Properties DeferredLightingPass::getProperties() const { return Falcor::serializeToProperties(parameters); }


void DeferredLightingPass::setProperties(const Falcor::Properties& properties)
{
    RasterLightingSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);

    const bool recompile = next.hdr != parameters.hdr;
    parameters           = std::move(next);
    if (recompile)
        requestRecompile();
}


Falcor::RenderPassReflection DeferredLightingPass::reflect(const CompileData& compileData)
{
    const auto* depth  = compileData.connectedResources.getField("depth");
    const auto  extent = depth ? Falcor::uint2{depth->getWidth(), depth->getHeight()} : Falcor::uint2{};

    Falcor::RenderPassReflection reflection;
    addShadowInputs(reflection);
    for (const auto& channel : kGBufferChannels)
        reflection.addInput(channel.first, "G-buffer texture")
            .texture2D(0, 0, 1, 0, 0)
            .bindFlags(Falcor::ResourceBindFlags::ShaderResource);
    reflection.addOutput("color", "Raster lighting result")
        .format(parameters.getColorFormat(compileData.defaultTexFormat))
        .bindFlags(Falcor::ResourceBindFlags::RenderTarget)
        .texture2D(extent.x, extent.y);
    return reflection;
}


void DeferredLightingPass::compile(Falcor::RenderContext* passContext, const CompileData& compileData)
{
    parameters.validateColorFormat(*passContext->device);
    const Falcor::DefineList variant{
        {"ENABLE_IBL", passContext->config->feature.ibl ? "1" : "0"},
        {"ENABLE_CLEARCOAT", "0"},
        {"ENABLE_SHEEN", "1"}
    };
    rhi::ColorTargetDesc color;
    color.format = parameters.getColorFormat(compileData.defaultTexFormat);
    rhi::RenderPipelineDesc desc;
    desc.program =
        passContext->shaderManager->program(passContext->shaderManager->getOrLoad(kDeferredLightingShader, variant));
    desc.targets                       = &color;
    desc.targetCount                   = 1;
    desc.depthStencil.depthTestEnable  = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.rasterizer.scissorEnable      = true;
    rhiCheck(passContext->device->createRenderPipeline(desc, pipeline.writeRef()));
}


void DeferredLightingPass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    const auto target    = renderData.getTexture("color");
    const auto colorView = target->getDefaultView();
    CHECK(colorView);
    rhi::RenderPassColorAttachment color;
    color.view   = colorView;
    color.loadOp = rhi::LoadOp::DontCare;
    auto* pass   = renderCommands->commands->beginRenderPass({&color, 1});
    CHECK(pass);
    auto* object = pass->bindPipeline(pipeline);
    CHECK(object);
    const rhi::ShaderCursor root(object);
    const auto&             size     = target->getDesc().size;
    const auto              viewport = renderCommands->view->viewport.clamped({size.width, size.height});
    bindFrameData(object, *renderCommands->frameData);
    const ShadowBindings shadows{renderData, *renderCommands->frameData, *renderCommands->resources};
    shadows.bind(object);
    for (const auto& [name, parameter] : kGBufferChannels)
        rhiCheck(root[parameter].setBinding(renderData.getTexture(name)->getDefaultView()));
    const DeferredLightingPushConstants constants{{viewport.x, viewport.y, viewport.width, viewport.height}};
    rhiCheck(root["parameters"].getDereferenced().setData(constants));
    pass->setRenderState(viewport.renderState());
    pass->draw({3});
    recordDraw(renderCommands->profiler, 3);
    pass->end();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, DeferredLightingPass>();
}
