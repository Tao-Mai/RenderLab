#include "passes/editor/EditorPickingPass/EditorPickingPass.h"

#include "core/Plugin.h"
#include "core/profiling/Profiler.h"
#include "render/Visibility.h"
#include "render/raster/MeshDraw.h"

#include <slang-rhi.h>
#include "core/Logger.h"
#include "render/FrameData.h"
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/device/GpuResources.h"
#include "render/editor/EditorFrameInput.h"
#include "render/editor/EditorShaderData.h"
#include "render/resources/GpuMesh.h"
#include "render/resources/RenderResourceManager.h"

#include <array>

Falcor::RenderPassReflection EditorPickingPass::reflect(const CompileData& compileData)
{
    const auto* input  = compileData.connectedResources.getField("depth");
    const auto  extent = input ? Falcor::uint2{input->getWidth(), input->getHeight()} : Falcor::uint2{};

    Falcor::RenderPassReflection reflection;
    reflection.addInput("depth", "Scene depth")
        .format(rhi::Format::D32Float)
        .bindFlags(Falcor::ResourceBindFlags::DepthStencil);
    reflection.addOutput(kPickingImageOutput, "Object identifiers")
        .format(rhi::Format::R32Uint)
        .bindFlags(Falcor::ResourceBindFlags::RenderTarget)
        .texture2D(extent.x, extent.y);
    return reflection;
}


void EditorPickingPass::compile(Falcor::RenderContext* context, const CompileData&)
{
    shader                     = context->shaderManager->getOrLoad("editor_picking");
    const auto           input = createMeshInputLayout(*context->device);
    rhi::ColorTargetDesc color;
    color.format = rhi::Format::R32Uint;
    rhi::RenderPipelineDesc desc;
    desc.program                  = context->shaderManager->program(shader);
    desc.inputLayout              = input;
    desc.targets                  = &color;
    desc.targetCount              = 1;
    desc.rasterizer.scissorEnable = true;
    desc.depthStencil             = {
                    .format           = rhi::Format::D32Float,
                    .depthTestEnable  = true,
                    .depthWriteEnable = false,
                    .depthFunc        = rhi::ComparisonFunc::LessEqual
    };
    rhiCheck(context->device->createRenderPipeline(desc, pipeline.writeRef()));
}


void EditorPickingPass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    const auto& execution = *renderCommands;
    const auto& editor    = *execution.editor;
    if (!editor.requestPick)
        return;
    const auto target    = renderData.getTexture(kPickingImageOutput);
    const auto colorView = target->getDefaultView();
    CHECK(colorView);
    const auto depthView = renderData.getTexture("depth")->getDefaultView();
    CHECK(depthView);
    rhi::RenderPassColorAttachment color;
    color.view = colorView;
    rhi::RenderPassDepthStencilAttachment depth;
    depth.view          = depthView;
    depth.depthLoadOp   = rhi::LoadOp::Load;
    depth.depthReadOnly = true;
    auto* pass          = execution.commands->beginRenderPass({&color, 1, &depth});
    CHECK(pass);
    const auto& size      = target->getDesc().size;
    const auto  viewport  = execution.view->viewport.clamped({size.width, size.height});
    auto        state     = viewport.renderState();
    state.scissorRects[0] = {editor.pickX, editor.pickY, editor.pickX + 1, editor.pickY + 1};
    for (uint32_t index = 0; index < execution.scene->drawItems.size(); ++index)
    {
        if (!execution.scene->isVisible(index))
            continue;
        const auto& item   = execution.scene->drawItems[index];
        auto*       object = pass->bindPipeline(pipeline);
        CHECK(object);
        bindFrameData(object, *execution.frameData);
        const EditorPickingPushConstants constants{.model = item.model, .selectionId = item.selectionId};
        rhiCheck(rhi::ShaderCursor(object)["pushConstants"].getDereferenced().setData(constants));
        const auto& mesh        = execution.resources->mesh(item.mesh);
        const auto& submesh     = mesh.submeshes()[item.submeshIndex];
        state.vertexBufferCount = 1;
        state.vertexBuffers[0]  = {mesh.vertices(), 0};
        state.indexBuffer       = {mesh.indices(), 0};
        pass->setRenderState(state);
        pass->drawIndexed({submesh.indexCount, 1, 0, 0, submesh.firstIndex});
        recordDraw(execution.profiler, submesh.indexCount);
    }
    pass->end();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, EditorPickingPass>();
}
