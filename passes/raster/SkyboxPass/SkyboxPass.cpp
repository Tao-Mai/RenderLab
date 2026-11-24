#include "passes/raster/SkyboxPass/SkyboxPass.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/plugins/Plugin.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderData.h"

#include <array>

namespace
{
struct SkyboxPushConstants
{
    glm::vec4 viewport{0.0f};
};
static_assert(sizeof(SkyboxPushConstants) == 16);
}

const RenderPassReflection::Field SkyboxPass::kPorts[]{
    {
        .name = "color",
        .resource = rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::RenderTarget},
        .visibility = RenderPassReflection::Visibility::Input | RenderPassReflection::Visibility::Output,
    },
    {
        .name = "depth",
        .resource = rhi::TextureDesc{
            .size = {0, 0, 1},
            .format = rhi::Format::D32Float,
            .usage = rhi::TextureUsage::DepthStencil,
        },
        .visibility = RenderPassReflection::Visibility::Input,
    },
};


void SkyboxPass::compile(RenderContext* context, const CompileData& compileData)
{
    rhi::ColorTargetDesc color;
    color.format = compileData.getTextureDesc("color").format;
    rhi::RenderPipelineDesc desc;
    desc.program =
        context->shaderManager->getProgram("passes/raster/SkyboxPass/skybox.slang", {"vertMain", "fragMain"});
    desc.targets                       = &color;
    desc.targetCount                   = 1;
    desc.depthStencil.depthTestEnable  = true;
    desc.depthStencil.depthWriteEnable = false;
    desc.rasterizer.scissorEnable      = true;
    desc.depthStencil.format           = rhi::Format::D32Float;
    desc.depthStencil.depthFunc        = rhi::ComparisonFunc::LessEqual;
    rhiCheck(mpDevice->createRenderPipeline(desc, pipeline.writeRef()));
}


void SkyboxPass::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    const auto* environment = mpScene->environmentMap();
    if (!environment)
        return;

    const auto target    = renderData.getTexture("color");
    const auto colorView = target->getDefaultView();
    CHECK(colorView);
    rhi::RenderPassColorAttachment color;
    color.view   = colorView;
    color.loadOp = rhi::LoadOp::Load;

    const auto depthView = renderData.getTexture("depth")->getDefaultView();
    CHECK(depthView);
    rhi::RenderPassDepthStencilAttachment depth;
    depth.view          = depthView;
    depth.depthLoadOp   = rhi::LoadOp::Load;
    depth.depthReadOnly = true;
    auto* pass          = renderCommands->commands->beginRenderPass({&color, 1, &depth});
    CHECK(pass);
    auto* object = pass->bindPipeline(pipeline);
    CHECK(object);
    const rhi::ShaderCursor root(object);
    const auto&             size     = target->getDesc().size;
    const auto              viewport = mpScene->viewport().clamped({size.width, size.height});
    mpScene->bindShaderData(*object);
    environment->bindRadiance(root);
    const SkyboxPushConstants constants{{viewport.x, viewport.y, viewport.width, viewport.height}};
    rhiCheck(root["pushConstants"].getDereferenced().setData(constants));
    pass->setRenderState(viewport.renderState());
    pass->draw({3});
    pass->end();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, SkyboxPass>();
}
