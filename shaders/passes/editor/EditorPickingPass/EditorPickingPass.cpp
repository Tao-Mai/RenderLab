#include "shaders/passes/editor/EditorPickingPass/EditorPickingPass.h"

#include <slang-rhi/shader-cursor.h>
#include "core/Logger.h"
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/device/RhiCheck.h"
#include "render/plugins/Plugin.h"
#include "render/raster/MeshDraw.h"
#include "render/resources/GpuMesh.h"
#include "render/resources/ShaderManager.h"

#include <algorithm>

namespace
{
struct EditorPickingPushConstants
{
    glm::mat4 model{1.0f};
    uint32_t  selectionId = 0;
    uint32_t  padding0    = 0;
    uint32_t  padding1    = 0;
    uint32_t  padding2    = 0;
};

static_assert(sizeof(EditorPickingPushConstants) == 80);

constexpr char        kIdTarget[] = "ids";
constexpr rhi::Format kIdFormat   = rhi::Format::R32Uint;
}  // namespace

const RenderPassReflection::Field EditorPickingPass::kPorts[]{
    {
        .name = "depth",
        .resource = rhi::TextureDesc{
            .size = {0, 0, 1},
            .format = rhi::Format::D32Float,
            .usage = rhi::TextureUsage::DepthStencil,
        },
        .visibility = RenderPassReflection::Visibility::Input,
    },
    {
        .name = kIdTarget,
        .resource = rhi::TextureDesc{.size = {0, 0, 1}, .format = kIdFormat, .usage = rhi::TextureUsage::RenderTarget},
        .visibility = RenderPassReflection::Visibility::Internal,
        .matchInput = "depth",
        .textureMatch = RenderPassReflection::TextureMatch::Extent,
    },
};


void EditorPickingPass::compile(RenderContext* context, const CompileData&)
{
    const auto shader = context->shaderManager->getProgram(
        "shaders/passes/editor/EditorPickingPass/picking.slang", {"vertMain", "fragMain"}
    );
    const auto input  = createMeshInputLayout(*mpDevice);
    rhi::ColorTargetDesc color;
    color.format = kIdFormat;
    rhi::RenderPipelineDesc desc;
    desc.program                  = shader;
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
    rhiCheck(mpDevice->createRenderPipeline(desc, pipeline.writeRef()));

    if (!readback)
    {
        rhi::BufferDesc readbackDesc;
        readbackDesc.size       = sizeof(uint32_t);
        readbackDesc.usage      = rhi::BufferUsage::CopyDestination | rhi::BufferUsage::CopySource;
        readbackDesc.memoryType = rhi::MemoryType::ReadBack;
        rhiCheck(mpDevice->createBuffer(readbackDesc, nullptr, readback.writeRef()));
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
    const ViewportRect viewport{.width = float(size.width), .height = float(size.height)};
    const auto pixel = [](float position, uint32_t length)
    {
        return std::min(uint32_t(position * float(length)), length - 1);
    };
    const auto x = pixel(request->viewportUv.x, size.width);
    const auto y = pixel(request->viewportUv.y, size.height);

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
    for (const uint32_t index : mpScene->visibleDraws())
    {
        const auto& item   = mpScene->drawCalls()[index];
        auto*       object = pass->bindPipeline(pipeline);
        CHECK(object);
        mpScene->bindShaderData(*object);
        const rhi::ShaderCursor root(object);
        const EditorPickingPushConstants constants{.model = item.model, .selectionId = item.selectionId};
        rhiCheck(root["pushConstants"].getDereferenced().setData(constants));
        const auto& mesh        = *item.mesh;
        state.vertexBufferCount = 1;
        state.vertexBuffers[0]  = {mesh.vertices(), 0};
        state.indexBuffer       = {mesh.indices(), 0};
        pass->setRenderState(state);
        pass->drawIndexed({item.indexCount, 1, 0, 0, item.firstIndex});
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
