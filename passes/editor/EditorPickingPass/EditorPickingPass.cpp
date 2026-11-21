#include "render/device/ResourceBarriers.h"
#include "passes/editor/EditorPickingPass/EditorPickingPass.h"

#include "core/profiling/Profiler.h"
#include "core/Plugin.h"
#include "render/Visibility.h"

#include "core/Logger.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/PreparedScene.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/device/VulkanResources.h"
#include "render/editor/EditorFrameInput.h"
#include "render/editor/EditorShaderData.h"
#include "render/resources/GpuMesh.h"
#include "render/resources/RenderResourceManager.h"

#include <array>

Falcor::RenderPassReflection EditorPickingPass::reflect(const CompileData& compileData)
{
    const auto* input = compileData.connectedResources.getField("depth");
    const auto extent = input ? Falcor::uint2{input->getWidth(), input->getHeight()} : Falcor::uint2{};

    Falcor::RenderPassReflection reflection;
    reflection.addInput("depth", "Scene depth").format(Falcor::ResourceFormat::D32Float).bindFlags(Falcor::ResourceBindFlags::DepthStencil);
    reflection.addOutput(kPickingImageOutput, "Object identifiers")
        .format(Falcor::ResourceFormat::R32Uint)
        .bindFlags(Falcor::ResourceBindFlags::RenderTarget)
        .texture2D(extent.x, extent.y);
    return reflection;
}

void EditorPickingPass::compile(Falcor::RenderContext* context, const CompileData&)
{
    shader = context->shaderManager->getOrLoad("editor_picking");

    auto& device = *context->getDevice();
    pipelineLayout = device.editorPipelineLayout;
    auto state     = PipelineState{
            .vertexLayout = VertexLayoutPreset::PositionOnly,
            .depthCompare = vk::CompareOp::eLessOrEqual,
            .depthWrite   = false
    };
    pipeline = device.getOrCreatePipeline({
        .shader       = shader,
        .layout       = pipelineLayout,
        .state        = state,
        .colorFormats = {vk::Format::eR32Uint},
        .depthFormat  = vk::Format::eD32Sfloat,
    });

    barriers.clear();
}

void EditorPickingPass::draw(const Falcor::RenderContext& context, const DrawItem& item) const
{
    auto& commandBuffer = *context.commands;
    const EditorPickingPushConstants parameters{
        .model       = item.model,
        .selectionId = item.selectionId,
    };
    commandBuffer.pushConstants<EditorPickingPushConstants>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        parameters
    );
    auto&       mesh    = context.resources->mesh(item.mesh);
    const auto& submesh = mesh.submeshes()[item.submeshIndex];
    mesh.bind(commandBuffer);
    commandBuffer.drawIndexed(submesh.indexCount, 1, submesh.firstIndex, 0, 0);
    recordDraw(context.profiler, submesh.indexCount);
}

void EditorPickingPass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    const auto& editor = *renderCommands->editor;
    if (!editor.requestPick)
        return;

    auto&       cache         = *renderCommands->vulkanResources;
    auto&       commandBuffer = *renderCommands->commands;
    const auto& execution     = *renderCommands;
    if (barriers.empty())
    {
        depth = cache.registerImage(renderData.getTexture("depth"));
        pickingImage = cache.registerImage(renderData.getTexture(kPickingImageOutput));
        barriers.add(depth, vulkan_access::kDepthRead);
        barriers.add(pickingImage, vulkan_access::kColorWrite);
        barriers.declare(cache);
    }

    barriers.transition(cache, commandBuffer);

    const auto extent = cache.imageExtent(pickingImage);
    DCHECK(
        editor.pickX < extent.width && editor.pickY < extent.height,
        "picking coordinate is outside the render target"
    );
    const uint32_t x = editor.pickX;
    const uint32_t y = editor.pickY;

    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = cache.getImage(pickingImage)
                         .getOrCreateImageView({
                             .viewType = vk::ImageViewType::e2D,
                             .format   = vk::Format::eR32Uint,
                             .subresourceRange =
                                 {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
                         }),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eClear,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = vk::ClearColorValue(std::array<uint32_t, 4>{0u, 0u, 0u, 0u}),
    };
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView   = cache.getImage(depth).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = vk::Format::eD32Sfloat,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1},
        }),
        .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        .loadOp      = vk::AttachmentLoadOp::eLoad,
        .storeOp     = vk::AttachmentStoreOp::eNone,
    };
    commandBuffer.beginRendering({
        .renderArea           = {{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {1, 1}},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &colorAttachment,
        .pDepthAttachment     = &depthAttachment,
    });
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sets = {execution.frameData->viewSetHandle()};
    commandBuffer
        .bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, EditorInterface::kViewSet, sets, {});

    // Match ScenePass's viewport; only the clicked pixel needs rasterization.
    const auto viewport = execution.view->viewport.clamped(extent);
    commandBuffer.setViewport(0, viewport.toViewport());
    commandBuffer.setScissor(0, vk::Rect2D({static_cast<int32_t>(x), static_cast<int32_t>(y)}, {1, 1}));

    for (uint32_t index = 0; index < execution.scene->drawItems.size(); ++index)
    {
        if (!execution.scene->isVisible(index))
            continue;

        draw(execution, execution.scene->drawItems[index]);
    }
    commandBuffer.endRendering();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, EditorPickingPass>();
}
