#include "render/impl/postprocess/ToneMappingPass/ToneMappingPass.h"
#include "render/graph/RenderPassSettings.h"

#include "core/Logger.h"

#include <slang-rhi.h>
#include "asset/BuiltinAssets.h"
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

bool ToneMappingPass::drawSettings(
    Gui::Widgets& widgets, ToneMappingSettings& settings, const ToneMappingPass&, const PassEditorContext&
)
{
    return widgets.dropdown("Mode", settings.mode, {"Neutral", "Reinhard"});
}

ToneMappingPass::ToneMappingPass(rhi::ComPtr<rhi::IDevice> device) :
    RenderPass(
        std::move(device),
        {
            .type = kType,
            .ports = kPorts,
            .settings = pass_settings::bind<
                &ToneMappingPass::parameters, &RenderPassOptions::toneMapping, &ToneMappingPass::drawSettings
            >(),
        }
    ),
    parameters{}
{
    parameters.validate();
}

const RenderPassReflection::Field ToneMappingPass::kPorts[]{
    {
        .name = "hdrColor",
        .resource = rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::ShaderResource},
        .visibility = RenderPassReflection::Visibility::Input,
    },
    {
        .name = "exposure",
        .resource = rhi::BufferDesc{.usage = rhi::BufferUsage::ShaderResource},
        .visibility = RenderPassReflection::Visibility::Input,
    },
    {
        .name = "color",
        .resource = rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::RenderTarget},
        .visibility = RenderPassReflection::Visibility::Output,
        .textureMatch = RenderPassReflection::TextureMatch::Format,
    },
};


void ToneMappingPass::compile(RenderContext* passContext, const CompileData& compileData)
{
    const auto format = compileData.getTextureDesc("color").format;
    encodeSrgb        = format != rhi::Format::BGRA8UnormSrgb && format != rhi::Format::RGBA8UnormSrgb;
    rhi::ColorTargetDesc color;
    color.format = format;
    rhi::RenderPipelineDesc desc;
    desc.program = passContext->shaderManager->getProgram(
        "render/impl/postprocess/ToneMappingPass/tonemapping.slang",
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
    rhiCheck(root["hdrColor"].setBinding(renderData.getTexture("hdrColor")));
    rhiCheck(root["hdrSampler"].setBinding(renderCommands->resources->sampler(BuiltinAssets::Sampler::linearClamp)));
    rhiCheck(root["exposureState"].setBinding(renderData.getBuffer("exposure")));
    const ToneMappingPushConstants constants{.mode = uint32_t(parameters.mode), .encodeSrgb = encodeSrgb ? 1u : 0u};
    rhiCheck(root["parameters"].getDereferenced().setData(constants));
    pass->setRenderState(fullscreenState(target->getDesc().size));
    pass->draw({3});
    pass->end();
}
