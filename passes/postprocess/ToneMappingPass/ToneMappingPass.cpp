#include "passes/postprocess/ToneMappingPass/ToneMappingPass.h"

#include "core/Logger.h"
#include "core/profiling/Profiler.h"
#include "editor/PassEditor.h"

#include <slang-rhi.h>
#include "asset/BuiltinAssets.h"
#include "core/Context.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderData.h"

#include <array>

ToneMappingPass::ToneMappingPass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)),
    parameters(Falcor::deserializeFromProperties<ToneMappingSettings>(properties))
{
    parameters.validate();
}


Falcor::Properties ToneMappingPass::getProperties() const { return Falcor::serializeToProperties(parameters); }


void ToneMappingPass::setProperties(const Falcor::Properties& properties)
{
    ToneMappingSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    parameters = std::move(next);
}


Falcor::RenderPassReflection ToneMappingPass::reflect(const CompileData& compileData)
{
    Falcor::RenderPassReflection reflection;
    reflection.addInput("hdrColor", "HDR scene color").bindFlags(Falcor::ResourceBindFlags::ShaderResource);
    reflection.addInput("exposure", "Exposure state").rawBuffer(0).bindFlags(Falcor::ResourceBindFlags::ShaderResource);
    reflection.addOutput("color", "Tone-mapped color")
        .format(compileData.defaultTexFormat)
        .bindFlags(Falcor::ResourceBindFlags::RenderTarget);
    return reflection;
}


void ToneMappingPass::compile(Falcor::RenderContext* passContext, const CompileData& compileData)
{
    const auto format = compileData.defaultTexFormat;
    encodeSrgb        = format != rhi::Format::BGRA8UnormSrgb && format != rhi::Format::RGBA8UnormSrgb;
    rhi::ColorTargetDesc color;
    color.format = format;
    rhi::RenderPipelineDesc desc;
    desc.program     = passContext->shaderManager->program(passContext->shaderManager->getOrLoad("tonemapping"));
    desc.targets     = &color;
    desc.targetCount = 1;
    desc.depthStencil.depthTestEnable  = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.rasterizer.scissorEnable      = true;
    rhiCheck(passContext->device->createRenderPipeline(desc, pipeline.writeRef()));
}


void ToneMappingPass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
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
    rhiCheck(root["hdrColor"].setBinding(renderData.getTexture("hdrColor")->getDefaultView()));
    rhiCheck(root["hdrSampler"].setBinding(renderCommands->resources->sampler(BuiltinAssets::Sampler::linearClamp)));
    rhiCheck(root["exposureState"].setBinding(renderData.getBuffer("exposure")));
    const ToneMappingPushConstants constants{.mode = uint32_t(parameters.mode), .encodeSrgb = encodeSrgb ? 1u : 0u};
    rhiCheck(root["parameters"].getDereferenced().setData(constants));
    pass->setRenderState(viewport.renderState());
    pass->draw({3});
    recordDraw(renderCommands->profiler, 3);
    pass->end();
}


void ToneMappingPass::renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget)
{
    drawPassEditor(widget, *this, parameters, context->passEditor, "Tone mapping");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, ToneMappingPass>();
}
