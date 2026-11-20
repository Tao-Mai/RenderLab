#include "passes/raster/SkyboxPass/SkyboxPass.h"

#include "core/Context.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/resources/ShaderData.h"

#include <array>

SkyboxPass::Output SkyboxPass::setup(
    RenderGraph&             graph,
    const PassSetup&         setup,
    RenderGraph::ImageHandle colorHandle,
    RenderGraph::ImageHandle depthHandle
)
{
    const auto color      = graph.write(colorHandle);
    attachments.colors[0] = {color, vk::AttachmentLoadOp::eLoad};
    attachments.depth     = RasterDepthAttachment{depthHandle};
    attachments.declare(graph);

    auto&            device = *renderContext().device;
    const std::array descriptorSetLayouts{&device.sceneDescriptorSetLayout, &device.lightDescriptorSetLayout};
    const std::array pushConstantRanges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(SkyboxPushConstants),
    }};
    pipelineLayout = device.getOrCreatePipelineLayout(descriptorSetLayouts, pushConstantRanges);

    pipeline = device.getOrCreatePipeline({
        .shader = renderContext().shaderManager->getOrLoad("skybox"),
        .layout = pipelineLayout,
        .state =
            PipelineState{
                .vertexLayout = VertexLayoutPreset::None,
                .depthCompare = vk::CompareOp::eLessOrEqual,
                .depthWrite   = false
            },
        .colorFormats = {graph.imageInfo(color).format},
        .depthFormat  = vk::Format::eD32Sfloat,
    });
    return {color};
}

void SkyboxPass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const
{
    if (!execution.renderData.environmentMap)
        return;

    const auto viewport = attachments.begin(graph, commandBuffer, execution.view.viewport).toScissor();
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array descriptorSets = {execution.data.sceneSetHandle(), execution.data.lightSetHandle()};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        RenderInterface::kSceneSet,
        descriptorSets,
        {}
    );
    const SkyboxPushConstants parameters{
        .viewport = {
            static_cast<float>(viewport.offset.x),
            static_cast<float>(viewport.offset.y),
            static_cast<float>(viewport.extent.width),
            static_cast<float>(viewport.extent.height),
        },
    };
    commandBuffer.pushConstants<SkyboxPushConstants>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        parameters
    );
    commandBuffer.draw(3, 1, 0, 0);
    recordDraw(context().profiler, 3);
    commandBuffer.endRendering();
}
