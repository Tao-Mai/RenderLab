#include "passes/postprocess/PresentColorPass/PresentColorPass.h"
#include "render/plugins/PluginManager.h"

#include "core/Logger.h"
#include "render/device/VulkanContext.h"

#include <array>

RenderPassReflection PresentColorPass::reflect(const PassSetup&) const
{
    RenderPassReflection reflection;
    reflection.input("source", PortKind::Image);
    reflection.input("target", PortKind::Image);
    reflection.output("color", PortKind::Image);
    return reflection;
}

void PresentColorPass::compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports)
{
    const auto source = ports.image("source");
    const auto target = ports.image("target");

    const auto color = graph.sharesImage(source, target) ? source :
        graph.addStage("Present blit", blit, setup, source, target).color;
    ports.setOutput("color", color);
}

PresentColorPass::Output PresentColorPass::BlitPass::compile(
    RenderGraph&             graph,
    const PassSetup&         setup,
    RenderGraph::ImageHandle sourceHandle,
    RenderGraph::ImageHandle targetHandle
)
{
    const auto& device = setup.vulkan.physicalDeviceHandle();
    CHECK(
        device.getFormatProperties(graph.imageInfo(targetHandle).format).optimalTilingFeatures &
            vk::FormatFeatureFlagBits::eBlitDst,
        "target format does not support blit destinations"
    );
    CHECK(
        device.getFormatProperties(graph.imageInfo(sourceHandle).format).optimalTilingFeatures &
            vk::FormatFeatureFlagBits::eBlitSrc,
        "scene color format does not support blit sources"
    );

    source = sourceHandle;
    target = graph.write(targetHandle);
    graph.use(
        source,
        {
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferRead,
            .layout = vk::ImageLayout::eTransferSrcOptimal,
        }
    );
    graph.use(
        target,
        {
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
            .layout = vk::ImageLayout::eTransferDstOptimal,
        }
    );
    return {target};
}

void PresentColorPass::BlitPass::execute(
    RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame&
) const
{
    const auto          sourceExtent = graph.imageExtent(source);
    const auto          targetExtent = graph.imageExtent(target);
    const vk::ImageBlit region{
        .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .srcOffsets =
            std::array{
                vk::Offset3D{0, 0, 0},
                vk::Offset3D{static_cast<int32_t>(sourceExtent.width), static_cast<int32_t>(sourceExtent.height), 1}
            },
        .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .dstOffsets     = std::array{
            vk::Offset3D{0, 0, 0},
            vk::Offset3D{static_cast<int32_t>(targetExtent.width), static_cast<int32_t>(targetExtent.height), 1}
        },
    };
    commandBuffer.blitImage(
        graph.getImage(source).handle(),
        vk::ImageLayout::eTransferSrcOptimal,
        graph.getImage(target).handle(),
        vk::ImageLayout::eTransferDstOptimal,
        region,
        vk::Filter::eNearest
    );
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<PresentColorPass>();
}
