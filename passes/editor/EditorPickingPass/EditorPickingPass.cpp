#include "passes/editor/EditorPickingPass/EditorPickingPass.h"

#include <slang-rhi/shader-cursor.h>
#include "core/Logger.h"
#include "core/Plugin.h"
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/device/RhiCheck.h"
#include "render/editor/EditorShaderData.h"
#include "render/raster/MeshDraw.h"
#include "render/resources/GpuMesh.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderManager.h"

#include <algorithm>

namespace
{
constexpr char        kIdTarget[] = "ids";
constexpr rhi::Format kIdFormat   = rhi::Format::R32Uint;
}  // namespace

RenderPassReflection EditorPickingPass::reflect(const CompileData& compileData)
{
    const auto* input  = compileData.connectedResources.getField("depth");
    const auto  extent = input ? glm::uvec2{input->getWidth(), input->getHeight()} : glm::uvec2{};

    RenderPassReflection reflection;
    reflection.addInput("depth", "Scene depth")
        .format(rhi::Format::D32Float)
        .bindFlags(ResourceBindFlags::DepthStencil);
    reflection.addInternal(kIdTarget, "Object identifiers")
        .format(kIdFormat)
        .bindFlags(ResourceBindFlags::RenderTarget)
        .texture2D(extent.x, extent.y);
    return reflection;
}


void EditorPickingPass::compile(RenderContext* context, const CompileData&)
{
    const auto shader = context->shaderManager->getOrLoad("editor_picking");
    const auto input  = createMeshInputLayout(*context->device);
    rhi::ColorTargetDesc color;
    color.format = kIdFormat;
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

    if (!readback)
    {
        rhi::BufferDesc readbackDesc;
        readbackDesc.size       = sizeof(uint32_t);
        readbackDesc.usage      = rhi::BufferUsage::CopyDestination | rhi::BufferUsage::CopySource;
        readbackDesc.memoryType = rhi::MemoryType::ReadBack;
        rhiCheck(context->device->createBuffer(readbackDesc, nullptr, readback.writeRef()));
    }
}


void EditorPickingPass::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    auto&       execution = *renderCommands;
    const auto& request   = execution.editor->pick;
    if (!request)
        return;

    const auto  target   = renderData.getTexture(kIdTarget);
    const auto& size     = target->getDesc().size;
    const auto  viewport = execution.view->viewport.clamped({size.width, size.height});
    const auto  pixel    = [](float position, float offset, float length)
    {
        return uint32_t(offset) + std::min(uint32_t(position * length), uint32_t(length) - 1);
    };
    const auto x = pixel(request->viewportUv.x, viewport.x, viewport.width);
    const auto y = pixel(request->viewportUv.y, viewport.y, viewport.height);

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
    auto state            = viewport.renderState();
    state.scissorRects[0] = {x, y, x + 1, y + 1};
    for (uint32_t index = 0; index < execution.scene->drawItems.size(); ++index)
    {
        if (!execution.scene->isVisible(index))
            continue;
        const auto& item   = execution.scene->drawItems[index];
        auto*       object = pass->bindPipeline(pipeline);
        CHECK(object);
        const rhi::ShaderCursor root(object);
        rhiCheck(root["editorView"].getDereferenced().setData(execution.view->uniforms.viewProjection));
        const EditorPickingPushConstants constants{.model = item.model, .selectionId = item.selectionId};
        rhiCheck(root["pushConstants"].getDereferenced().setData(constants));
        const auto& mesh        = execution.resources->mesh(item.mesh);
        const auto& submesh     = mesh.submeshes()[item.submeshIndex];
        state.vertexBufferCount = 1;
        state.vertexBuffers[0]  = {mesh.vertices(), 0};
        state.indexBuffer       = {mesh.indices(), 0};
        pass->setRenderState(state);
        pass->drawIndexed({submesh.indexCount, 1, 0, 0, submesh.firstIndex});
    }
    pass->end();

    execution.commands->copyTextureToBuffer(
        readback, 0, sizeof(uint32_t), sizeof(uint32_t), target, 0, 0, {x, y, 0}, {1, 1, 1}
    );
    execution.onGpuComplete.emplace_back([device = mpDevice, buffer = readback, complete = request->complete]
    {
        uint32_t selectionId;
        rhiCheck(device->readBuffer(buffer, 0, sizeof(selectionId), &selectionId));
        complete(selectionId);
    });
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, EditorPickingPass>();
}
