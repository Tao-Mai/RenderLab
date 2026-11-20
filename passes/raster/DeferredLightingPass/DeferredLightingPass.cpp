#include "passes/raster/DeferredLightingPass/DeferredLightingPass.h"
#include "render/plugins/PluginManager.h"
#include "render/graph/PassPorts.h"

#include "core/Context.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/resources/ShaderData.h"

#include <array>

namespace
{
constexpr std::string_view kDeferredLightingShader = "deferred_lighting";
}

RenderPassReflection DeferredLightingPass::reflect(const PassSetup&) const
{
    RenderPassReflection reflection;
    reflection.input("target", PortKind::Image);
    reflection.input("lights", PortKind::Buffer);
    pass_ports::inputs<GBufferResources>(reflection);
    pass_ports::inputs<ShadowResources>(reflection);
    reflection.output("color", PortKind::Image);
    return reflection;
}

void DeferredLightingPass::compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports)
{
    const auto target = ports.image("target");
    const auto lights = ports.buffer("lights");
    const auto input = pass_ports::read<GBufferResources>(ports);
    const auto shadow = pass_ports::read<ShadowResources>(ports);

    gBuffer = input;
    useRasterLighting(graph, lights, shadow);
    for (const auto image :
         {gBuffer.diffuseOcclusion,
          gBuffer.normalRoughness,
          gBuffer.emissiveSpecularF90,
          gBuffer.specularF0DiffuseFresnel,
          gBuffer.sheenColorRoughness,
          gBuffer.depth})
        graph.use(
            image,
            {
                .stages = vk::PipelineStageFlagBits2::eFragmentShader,
                .access = vk::AccessFlagBits2::eShaderSampledRead,
                .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
            }
        );

    const bool hdr = setup.config.feature.hdr;
    if (hdr)
    {
        constexpr auto kRequiredFeatures = vk::FormatFeatureFlagBits::eColorAttachment |
            vk::FormatFeatureFlagBits::eColorAttachmentBlend | vk::FormatFeatureFlagBits::eSampledImage |
            vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
        const auto features =
            setup.vulkan.physicalDeviceHandle().getFormatProperties(kHdrColorFormat).optimalTilingFeatures;
        CHECK(
            (features & kRequiredFeatures) == kRequiredFeatures,
            "GPU does not support an RGBA16F HDR color attachment with blending and sampling"
        );
    }
    const auto extent = graph.imageExtent(gBuffer.depth);
    const auto color = graph.write(
        hdr ? graph.createImage({.format = kHdrColorFormat, .extent = {extent.width, extent.height, 1}}) : target
    );
    attachments.colors[0] = {color, vk::AttachmentLoadOp::eDontCare};
    attachments.declare(graph);

    constexpr std::array kBindings{
        RenderInterface::kGBufferDiffuseBinding,
        RenderInterface::kGBufferNormalBinding,
        RenderInterface::kGBufferEmissiveBinding,
        RenderInterface::kGBufferSpecularBinding,
        RenderInterface::kGBufferSheenBinding,
        RenderInterface::kGBufferDepthBinding,
    };
    std::array<vk::DescriptorSetLayoutBinding, kBindings.size()> gBufferBindings;
    for (size_t index = 0; index < kBindings.size(); ++index)
    {
        gBufferBindings[index] = {
            .binding         = kBindings[index],
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eFragment,
        };
    }
    auto& device             = *renderContext().device;
    descriptorSetLayout      = device.createDescriptorSetLayout(gBufferBindings);
    const std::array sets{
        &device.sceneDescriptorSetLayout,
        &device.lightDescriptorSetLayout,
        &descriptorSetLayout,
        &device.shadowDescriptorSetLayout
    };
    const std::array ranges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(DeferredLightingPushConstants)
    }};
    pipelineLayout = device.getOrCreatePipelineLayout(sets, ranges);

    auto&               ctx = renderContext();
    const ShaderDefines variant{
        {"ENABLE_IBL", setup.config.feature.ibl ? 1 : 0},
        {"ENABLE_CLEARCOAT", 0},
        {"ENABLE_SHEEN", 1}
    };
    pipeline      = device.getOrCreatePipeline({
             .shader = ctx.shaderManager->getOrLoad(kDeferredLightingShader, variant),
             .layout = pipelineLayout,
             .state  = PipelineState{.vertexLayout = VertexLayoutPreset::None, .depthTest = false, .depthWrite = false},
             .colorFormats = {graph.imageInfo(color).format},
    });
    descriptorSet = device.allocateDescriptorSet(descriptorSetLayout);
    ports.setOutput("color", color);
}

void DeferredLightingPass::bindResources(RenderGraph& graph, const PassSetup& setup)
{
    const std::array images{
        gBuffer.diffuseOcclusion,
        gBuffer.normalRoughness,
        gBuffer.emissiveSpecularF90,
        gBuffer.specularF0DiffuseFresnel,
        gBuffer.sheenColorRoughness,
        gBuffer.depth
    };
    constexpr std::array kBindings{
        RenderInterface::kGBufferDiffuseBinding,
        RenderInterface::kGBufferNormalBinding,
        RenderInterface::kGBufferEmissiveBinding,
        RenderInterface::kGBufferSpecularBinding,
        RenderInterface::kGBufferSheenBinding,
        RenderInterface::kGBufferDepthBinding
    };
    for (size_t index = 0; index < images.size(); ++index)
    {
        const auto                    image  = images[index];
        const auto                    format = graph.imageInfo(image).format;
        const vk::DescriptorImageInfo imageInfo{
            .imageView   = graph.getImage(image).getOrCreateImageView({
                  .viewType = vk::ImageViewType::e2D,
                  .format   = format,
                  .subresourceRange =
                    {.aspectMask = format == vk::Format::eD32Sfloat ? vk::ImageAspectFlagBits::eDepth
                                                                      : vk::ImageAspectFlagBits::eColor,
                       .levelCount = 1,
                       .layerCount = 1},
            }),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
        const vk::WriteDescriptorSet write{
            .dstSet          = *descriptorSet,
            .dstBinding      = kBindings[index],
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .pImageInfo      = &imageInfo,
        };
        setup.vulkan.deviceHandle().updateDescriptorSets(write, {});
    }
}

void DeferredLightingPass::execute(
    RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution
)
{
    auto& commands = commandBuffer;
    attachments.begin(graph, commands);
    commands.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sharedSets{execution.data.sceneSetHandle(), execution.data.lightSetHandle()};
    commands.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        RenderInterface::kSceneSet,
        sharedSets,
        {}
    );
    const std::array passSets{*descriptorSet, execution.data.shadowSetHandle()};
    commands
        .bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, RenderInterface::kDeferredPassSet, passSets, {});
    const auto&                         viewport = execution.view.viewport;
    const DeferredLightingPushConstants parameters{
        .viewport = {viewport.x, viewport.y, viewport.width, viewport.height}
    };
    commands.pushConstants<DeferredLightingPushConstants>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        parameters
    );
    commands.draw(3, 1, 0, 0);
    recordDraw(context().profiler, 3);
    commands.endRendering();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<DeferredLightingPass>();
}
