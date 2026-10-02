#include "render/pass/EditorPickingPass.h"

#include "core/Logger.h"
#include "ecs/component/LightComponent.h"
#include "ecs/component/TransformComponent.h"
#include "render/Renderer.h"
#include "render/pass/RenderGraph.h"
#include "render/pass/ScenePass.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/ShaderData.h"

#include <algorithm>
#include <array>

void EditorPickingPass::init(RenderGraph& targetGraph)
{
    graph = &targetGraph;
    pipelines = &graph->pipelines();
    shader = graph->shaders().getOrLoad("picking");
}

void EditorPickingPass::registerPass(RenderGraph& graph)
{
    std::tie(inputSlots, outputSlots) = graph.registerPass("editor_picking", InputCount, OutputCount,
        [this](RenderGraph& g) { setupPass(g); },
        [this](RenderGraph& g) { executePass(g); });
}

void EditorPickingPass::setupPass(RenderGraph& graph)
{
    graph.bindInput(inputSlots[Depth], graph.outputSlot("scene", ScenePass::DepthResult), {
        .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
        .stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
            vk::PipelineStageFlagBits2::eLateFragmentTests,
        .access = vk::AccessFlagBits2::eDepthStencilAttachmentRead,
        .layout = vk::ImageLayout::eDepthReadOnlyOptimal,
    });
    const auto extent = graph.swapchain().extent();
    graph.createResource(inputSlots[PickingImage], {
        .format = vk::Format::eR32Uint,
        .extent = {extent.width, extent.height, 1},
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .access = vk::AccessFlagBits2::eColorAttachmentWrite,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    });
    graph.createResource(inputSlots[Readback], {
        .type = RenderGraph::ResourceType::Buffer,
        .size = sizeof(uint32_t),
        .memoryProperties = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        .bufferUsage = vk::BufferUsageFlagBits::eTransferDst,
        .stages = vk::PipelineStageFlagBits2::eTransfer,
        .access = vk::AccessFlagBits2::eTransferWrite,
    });
    graph.bindOutput(outputSlots[ImageResult], inputSlots[PickingImage], {
        .imageUsage = vk::ImageUsageFlagBits::eTransferSrc,
        .stages = vk::PipelineStageFlagBits2::eTransfer,
        .access = vk::AccessFlagBits2::eTransferRead,
        .layout = vk::ImageLayout::eTransferSrcOptimal,
    });
    graph.bindOutput(outputSlots[ReadbackResult], inputSlots[Readback], {
        .type = RenderGraph::ResourceType::Buffer,
        .stages = vk::PipelineStageFlagBits2::eHost,
        .access = vk::AccessFlagBits2::eHostRead,
    });

    pipeline = pipelines->getOrCreate({
        .shader = shader,
        .layout = PipelineLayoutPreset::SceneOnly,
        .state = PipelineState::preset(RenderMode::Picking),
        .colorFormat = vk::Format::eR32Uint,
        .depthFormat = graph.depthFormat(),
    });
    pipelineLayout = pipelines->layout(PipelineLayoutPreset::SceneOnly);
}

void EditorPickingPass::draw(vk::raii::CommandBuffer& commandBuffer,
    GpuMesh& mesh, const glm::mat4& model, uint32_t selectionId) const
{
    const EditorPickingPushConstants pushConstants{
        .model = model,
        .selectionId = selectionId,
    };
    commandBuffer.pushConstants<EditorPickingPushConstants>(pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, pushConstants);
    mesh.bind(commandBuffer);
    for (const Submesh& submesh : mesh.submeshes())
    {
        commandBuffer.drawIndexed(submesh.indexCount, 1, submesh.firstIndex, 0, 0);
    }
}

void EditorPickingPass::executePass(RenderGraph& graph) const
{
    const auto& editor = graph.editorInput();
    if (!editor.requestPick)
    {
        return;
    }
    auto& commandBuffer = graph.commands();
    const auto extent = graph.swapchain().extent();
    CHECK(editor.pickX < extent.width && editor.pickY < extent.height,
        "picking coordinate is outside the render target");
    const uint32_t x = editor.pickX;
    const uint32_t y = editor.pickY;

    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = graph.imageView(inputSlots[PickingImage]),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearColorValue(std::array<uint32_t, 4>{0u, 0u, 0u, 0u}),
    };
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView = graph.imageView(inputSlots[Depth]),
        .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eNone,
    };
    commandBuffer.beginRendering({
        .renderArea = {{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {1, 1}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    });
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sets = {graph.frameContext(graph.frameIndex()).sceneSetHandle()};
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
        pipelineLayout, RenderInterface::sceneSet, sets, {});

    // Match ScenePass's viewport; only the clicked pixel needs rasterization.
    const auto& viewport = editor.viewport;
    const uint32_t viewportX = std::min(
        static_cast<uint32_t>(std::max(viewport.x, 0.0f)), extent.width - 1);
    const uint32_t viewportY = std::min(
        static_cast<uint32_t>(std::max(viewport.y, 0.0f)), extent.height - 1);
    const uint32_t width = std::max(1u, std::min(
        static_cast<uint32_t>(viewport.width), extent.width - viewportX));
    const uint32_t height = std::max(1u, std::min(
        static_cast<uint32_t>(viewport.height), extent.height - viewportY));
    commandBuffer.setViewport(0, vk::Viewport(static_cast<float>(viewportX),
        static_cast<float>(viewportY), static_cast<float>(width),
        static_cast<float>(height), 0.0f, 1.0f));
    commandBuffer.setScissor(0, vk::Rect2D({static_cast<int32_t>(x), static_cast<int32_t>(y)}, {1, 1}));

    for (const SceneRenderItem& item : graph.scene().renderItems())
    {
        const auto* transform = graph.registry().try_get<ecs::TransformComponent>(item.entity);
        CHECK(transform != nullptr, "picking object is missing Transform");
        draw(commandBuffer, *item.mesh, transform->matrix(), item.selectionId);
    }
    for (const LightRenderItem& item : graph.scene().lightRenderItems())
    {
        const auto* light = graph.registry().try_get<ecs::LightComponent>(item.entity);
        const auto* transform = graph.registry().try_get<ecs::TransformComponent>(item.entity);
        CHECK(light != nullptr && transform != nullptr,
            "picking light requires Light and Transform components");
        graph.scene().lightMarkers().recordPicking(commandBuffer,
            pipelineLayout, *transform, *light, item.selectionId);
    }
    commandBuffer.endRendering();

    graph.useOutput(outputSlots[ImageResult]);
    const vk::BufferImageCopy copyRegion{
        .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
        .imageOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0},
        .imageExtent = {1, 1, 1},
    };
    commandBuffer.copyImageToBuffer(graph.image(inputSlots[PickingImage]),
        vk::ImageLayout::eTransferSrcOptimal, graph.buffer(inputSlots[Readback]).handle(), copyRegion);
}

uint32_t EditorPickingPass::readSelectionId(uint32_t frameIndex)
{
    uint32_t selectionId = 0;
    graph->buffer(inputSlots[Readback], frameIndex).download(&selectionId, sizeof(selectionId));
    return selectionId;
}
