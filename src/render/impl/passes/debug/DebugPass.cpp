#include "render/impl/passes/debug/DebugPass.h"

#include "render/GpuScene.h"
#include "render/graph/RenderPassSettings.h"

#include <slang-rhi/shader-cursor.h>
#include <utility>

namespace
{
constexpr char kSource[] = "source";
constexpr char kVBuffer[] = "vbuffer";
constexpr char kColor[] = "color";
constexpr uint32_t kMarkerVertexCount = 6;
constexpr rhi::SubresourceRange kColorSubresource{.layer = 0, .layerCount = 1, .mip = 0, .mipCount = 1};

using Visibility = RenderPassReflection::Visibility;
using TextureMatch = RenderPassReflection::TextureMatch;

const RenderPassReflection::Field kPorts[]{
    {
        .name = kSource,
        .resource = rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::CopySource},
        .visibility = Visibility::Input,
    },
    {
        .name = kVBuffer,
        .resource = rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::ShaderResource},
        .visibility = Visibility::Input,
    },
    {
        .name = kColor,
        .resource = rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::RenderTarget},
        .visibility = Visibility::Output,
        .matchInput = kSource,
        .textureMatch = TextureMatch::Extent | TextureMatch::Format,
    },
};
}  // namespace

DebugPass::DebugPass(rhi::ComPtr<rhi::IDevice> device) :
    RenderPass(
        std::move(device),
        {
            .type = kType,
            .ports = kPorts,
            .settings = pass_settings::bind<
                &DebugPass::showAnalyticLights, &RenderPassOptions::showAnalyticLights, &DebugPass::drawSettings
            >(),
        }
    )
{
}

bool DebugPass::drawSettings(Gui::Widgets& widgets, bool& enabled, const DebugPass&, const PassEditorContext&)
{
    return widgets.checkbox("Show analytic lights", enabled);
}

void DebugPass::compile(RenderContext* context, const CompileData& compileData)
{
    const rhi::ColorTargetDesc color{.format = compileData.getTextureDesc(kColor).format};
    rhi::RenderPipelineDesc desc;
    desc.program = context->shaderManager->getProgram("passes/debug/debug.slang", {"vertMain", "fragMain"});
    desc.targets = &color;
    desc.targetCount = 1;
    desc.rasterizer.cullMode = rhi::CullMode::None;
    desc.rasterizer.scissorEnable = true;
    desc.depthStencil.depthTestEnable = false;
    desc.depthStencil.depthWriteEnable = false;
    rhiCheck(mpDevice->createRenderPipeline(desc, pipeline.writeRef()));
}

void DebugPass::execute(RenderContext* context, const RenderData& renderData)
{
    auto& commands = *context->commands;
    auto* output = renderData.getTexture(kColor);
    const auto extent = output->getDesc().size;
    commands.copyTexture(
        output, kColorSubresource, {}, renderData.getTexture(kSource), kColorSubresource, {}, extent
    );

    if (!showAnalyticLights || !mpScene || mpScene->analyticLightCount() == 0)
        return;

    auto* tlas = mpScene->prepareRayTracing(commands);
    const auto view = output->getDefaultView();
    CHECK(view);
    rhi::RenderPassColorAttachment color{.view = view, .loadOp = rhi::LoadOp::Load};
    auto* pass = commands.beginRenderPass({&color, 1});
    CHECK(pass);
    auto* object = pass->bindPipeline(pipeline);
    CHECK(object);

    mpScene->bindSceneData(*object, tlas);
    rhiCheck(rhi::ShaderCursor(object)[kVBuffer].setBinding(renderData.getTexture(kVBuffer)));
    pass->setRenderState(fullscreenState(extent));
    pass->draw({.vertexCount = kMarkerVertexCount, .instanceCount = mpScene->analyticLightCount()});
    pass->end();
}
