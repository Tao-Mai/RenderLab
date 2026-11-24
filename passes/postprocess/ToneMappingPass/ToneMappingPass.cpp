#include "passes/postprocess/ToneMappingPass/ToneMappingPass.h"

#include "core/Logger.h"
#include "editor/PassEditor.h"

#include <slang-rhi.h>
#include "asset/BuiltinAssets.h"
#include "render/RenderConfig.h"
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderData.h"

#include <array>

namespace
{
struct ToneMappingPushConstants
{
    uint32_t mode       = 0;
    uint32_t encodeSrgb = 0;
    uint32_t padding[2]{};
};
static_assert(sizeof(ToneMappingPushConstants) == 16);
static_assert(offsetof(ToneMappingPushConstants, mode) == 0);
static_assert(offsetof(ToneMappingPushConstants, encodeSrgb) == 4);
}

ToneMappingPass::ToneMappingPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
    RenderPass(std::move(device)),
    parameters(deserializeFromProperties<ToneMappingSettings>(properties))
{
    parameters.validate();
}


Properties ToneMappingPass::getProperties() const { return serializeToProperties(parameters); }


void ToneMappingPass::setProperties(const Properties& properties)
{
    ToneMappingSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    parameters = std::move(next);
}


RenderPassReflection ToneMappingPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflection;
    reflection.addInput("hdrColor", rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::ShaderResource});
    reflection.addInput("exposure", rhi::BufferDesc{.usage = rhi::BufferUsage::ShaderResource});
    reflection.addOutput("color", rhi::TextureDesc{
        .size = {0, 0, 1},
        .format = compileData.defaultTexFormat,
        .usage = rhi::TextureUsage::RenderTarget,
    });
    return reflection;
}


void ToneMappingPass::compile(RenderContext* passContext, const CompileData& compileData)
{
    const auto format = compileData.defaultTexFormat;
    encodeSrgb        = format != rhi::Format::BGRA8UnormSrgb && format != rhi::Format::RGBA8UnormSrgb;
    rhi::ColorTargetDesc color;
    color.format = format;
    rhi::RenderPipelineDesc desc;
    desc.program = passContext->shaderManager->getProgram(
        "passes/postprocess/ToneMappingPass/tonemapping.slang",
        {"vertMain", "fragMain"}
    );
    desc.targets     = &color;
    desc.targetCount = 1;
    desc.depthStencil.depthTestEnable  = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.rasterizer.scissorEnable      = true;
    rhiCheck(mpDevice->createRenderPipeline(desc, pipeline.writeRef()));
}


void ToneMappingPass::execute(RenderContext* renderCommands, const RenderData& renderData)
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
    const auto              viewport = mpScene->viewport().clamped({size.width, size.height});
    rhiCheck(root["hdrColor"].setBinding(renderData.getTexture("hdrColor")));
    rhiCheck(root["hdrSampler"].setBinding(renderCommands->resources->sampler(BuiltinAssets::Sampler::linearClamp)));
    rhiCheck(root["exposureState"].setBinding(renderData.getBuffer("exposure")));
    const ToneMappingPushConstants constants{.mode = uint32_t(parameters.mode), .encodeSrgb = encodeSrgb ? 1u : 0u};
    rhiCheck(root["parameters"].getDereferenced().setData(constants));
    pass->setRenderState(viewport.renderState());
    pass->draw({3});
    pass->end();
}


void ToneMappingPass::renderUI(const PassEditorContext& context, Gui::Widgets& widget)
{
    drawPassEditor(widget, *this, parameters, context, "Tone mapping");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, ToneMappingPass>();
}
