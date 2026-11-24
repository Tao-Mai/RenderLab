#include "passes/raster/SkyboxPass/SkyboxPass.h"

#include "core/Plugin.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderData.h"

#include <array>

RenderPassReflection SkyboxPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflection;
    auto& color = reflection.addInputOutput("color", "Color target").bindFlags(ResourceBindFlags::RenderTarget);
    if (const auto* input = compileData.connectedResources.getField("color"))
        color.texture2D(input->getWidth(), input->getHeight()).format(input->getFormat());
    reflection.addInput("depth", "Scene depth")
        .format(rhi::Format::D32Float)
        .bindFlags(ResourceBindFlags::DepthStencil);
    return reflection;
}


void SkyboxPass::compile(RenderContext* context, const CompileData& compileData)
{
    rhi::ColorTargetDesc color;
    color.format = compileData.connectedResources.getField("color")->getFormat();
    rhi::RenderPipelineDesc desc;
    desc.program                       = context->shaderManager->program(context->shaderManager->getOrLoad("skybox"));
    desc.targets                       = &color;
    desc.targetCount                   = 1;
    desc.depthStencil.depthTestEnable  = true;
    desc.depthStencil.depthWriteEnable = false;
    desc.rasterizer.scissorEnable      = true;
    desc.depthStencil.format           = rhi::Format::D32Float;
    desc.depthStencil.depthFunc        = rhi::ComparisonFunc::LessEqual;
    rhiCheck(context->device->createRenderPipeline(desc, pipeline.writeRef()));
}


void SkyboxPass::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    const auto* environment = renderCommands->scene->environmentMap;
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
    const auto              viewport = renderCommands->view->viewport.clamped({size.width, size.height});
    rhiCheck(root["sceneGlobals"].setBinding(renderCommands->scene->globalsBuffer()));
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
