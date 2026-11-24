#include "passes/raster/SkyboxPass/SkyboxPass.h"

#include "core/Plugin.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "render/GpuScene.h"
#include "render/RenderContext.h"
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

RenderPassReflection SkyboxPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflection;
    const auto* input = compileData.connectedResources.getField("color");
    reflection.addInputOutput("color", rhi::TextureDesc{
        .size = input ? input->texture().size : rhi::Extent3D{0, 0, 1},
        .format = input ? input->texture().format : rhi::Format::Undefined,
        .usage = rhi::TextureUsage::RenderTarget,
    });
    reflection.addInput("depth", rhi::TextureDesc{
        .size = {0, 0, 1},
        .format = rhi::Format::D32Float,
        .usage = rhi::TextureUsage::DepthStencil,
    });
    return reflection;
}


void SkyboxPass::compile(RenderContext* context, const CompileData& compileData)
{
    rhi::ColorTargetDesc color;
    color.format = compileData.connectedResources.getField("color")->texture().format;
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
