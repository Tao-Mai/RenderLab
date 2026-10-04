#include "render/pass/EditorUiPass.h"

#include "render/pass/RenderGraph.h"
#include "render/pass/ScenePass.h"
#include "render/raytracing/RayTracingPass.h"
#include "render/Renderer.h"
#include "render/present/Swapchain.h"

void EditorUiPass::registerPass(RenderGraph& graph)
{
    std::tie(inputSlots, outputSlots) = graph.registerPass("editor_ui", 1, 1,
        [this](RenderGraph& g) { setupPass(g); },
        [this](RenderGraph& g) { executePass(g); });
}

void EditorUiPass::setupPass(RenderGraph& graph)
{
    const RenderGraph::ResourceDesc colorUse{
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .access = vk::AccessFlagBits2::eColorAttachmentRead |
            vk::AccessFlagBits2::eColorAttachmentWrite,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };
    const auto source = graph.passEnabled("raytracing")
        ? graph.outputSlot("raytracing", RayTracingPass::ColorResult)
        : graph.outputSlot("scene", ScenePass::ColorResult);
    graph.bindInput(inputSlots[0], source, colorUse);
    graph.bindOutput(outputSlots[0], inputSlots[0], colorUse);
}

void EditorUiPass::executePass(RenderGraph& graph) const
{
    const auto& editor = graph.editorInput();
    if (!editor.recordUi) return;

    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = graph.imageView(inputSlots[0]),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    graph.commands().beginRendering({
        .renderArea = {{0, 0}, graph.swapchain().extent()},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    });
    editor.recordUi(*graph.commands());
    graph.commands().endRendering();
}
