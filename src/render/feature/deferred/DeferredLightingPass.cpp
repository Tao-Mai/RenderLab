#include "render/feature/deferred/DeferredLightingPass.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/present/Swapchain.h"
#include "render/resource/ShaderData.h"

#include <array>

namespace
{
const ShaderAsset::ID kDeferredLightingShader = "deferred_lighting";
}

DeferredLightingPass::Output DeferredLightingPass::setup(
    RenderGraph& graph, const PassSetup& setup, GBufferPass::Output input, ShadowPass::Output shadow
)
{
    gBuffer                   = input;
    const auto resolvedShadow = bindings.setup(graph, setup, shadow);
    for (const auto image :
         {gBuffer.albedoOcclusion,
          gBuffer.normalRoughness,
          gBuffer.emissiveMetallic,
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

    const bool hdr = context().config->rendererConfig().feature.hdr;
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
    const auto extent = setup.swapchain.extent();
    color             = hdr ? graph.createImage({.format = kHdrColorFormat, .extent = {extent.width, extent.height, 1}})
                            : graph.importSwapchain();
    color             = graph.write(color);
    graph.use(
        color,
        {
            .stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .access = vk::AccessFlagBits2::eColorAttachmentWrite,
            .layout = vk::ImageLayout::eColorAttachmentOptimal,
        }
    );

    const std::array gBufferBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kGBufferAlbedoBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kGBufferNormalBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kGBufferEmissiveBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kGBufferDepthBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kGBufferSheenBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
    };
    auto& device     = *renderContext().device;
    descriptorSetLayout      = device.createDescriptorSetLayout(gBufferBindings);
    emptyDescriptorSetLayout = device.createDescriptorSetLayout(std::span<const vk::DescriptorSetLayoutBinding>{});
    const std::array sets{
        &device.sceneDescriptorSetLayout,
        &device.lightingDescriptorSetLayout,
        &emptyDescriptorSetLayout,
        &descriptorSetLayout
    };
    const std::array ranges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(DeferredLightingPushConstants)
    }};
    pipelineLayout = device.createPipelineLayout(sets, ranges);

    auto&               ctx = renderContext();
    const ShaderDefines variant{
        {"ENABLE_IBL", context().config->rendererConfig().feature.ibl ? 1 : 0},
        {"ENABLE_CLEARCOAT", 0},
        {"ENABLE_SHEEN", 1}
    };
    pipeline = device.getOrCreatePipeline({
        .shader = ctx.shaderManager->getOrLoad(kDeferredLightingShader, variant),
        .layout = &pipelineLayout,
        .state  = PipelineState{.vertexLayout = VertexLayoutPreset::None, .depthTest = false, .depthWrite = false},
        .colorFormats = {graph.imageInfo(color).format},
    });
    descriptorSets.clear();
    for (uint32_t index = 0; index < maxFramesInFlight; ++index)
        descriptorSets.push_back(device.allocateDescriptorSet(descriptorSetLayout));
    return {color, gBuffer.depth, resolvedShadow};
}

void DeferredLightingPass::prepare(RenderGraph& graph, const PassSetup& setup) const
{
    bindings.prepare(graph, setup);
    const std::array images{
        gBuffer.albedoOcclusion,
        gBuffer.normalRoughness,
        gBuffer.emissiveMetallic,
        gBuffer.depth,
        gBuffer.sheenColorRoughness
    };
    constexpr std::array kBindings{
        RenderInterface::kGBufferAlbedoBinding,
        RenderInterface::kGBufferNormalBinding,
        RenderInterface::kGBufferEmissiveBinding,
        RenderInterface::kGBufferDepthBinding,
        RenderInterface::kGBufferSheenBinding
    };
    for (uint32_t frame = 0; frame < maxFramesInFlight; ++frame)
    {
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
                .dstSet          = *descriptorSets[frame],
                .dstBinding      = kBindings[index],
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &imageInfo,
            };
            setup.vulkan.deviceHandle().updateDescriptorSets(write, {});
        }
    }
}

void DeferredLightingPass::execute(
    RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution
) const
{
    const auto                        extent = execution.setup.swapchain.extent();
    const vk::RenderingAttachmentInfo attachment{
        .imageView   = graph.getImage(color).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = graph.imageInfo(color).format,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
        }),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eDontCare,
        .storeOp     = vk::AttachmentStoreOp::eStore,
    };
    auto& commands = commandBuffer;
    commands.beginRendering(
        {.renderArea = {{0, 0}, extent}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &attachment}
    );
    commands.setViewport(
        0,
        vk::Viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f}
    );
    commands.setScissor(0, vk::Rect2D{{0, 0}, extent});
    commands.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sharedSets{execution.data.sceneSetHandle(), execution.data.lightingSetHandle()};
    commands.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *pipelineLayout,
        RenderInterface::kSceneSet,
        sharedSets,
        {}
    );
    const std::array passSets{*descriptorSets[execution.frameIndex]};
    commands
        .bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, RenderInterface::kPassSet, passSets, {});
    const auto&                         viewport = execution.view.viewport;
    const DeferredLightingPushConstants parameters{
        .viewport = {viewport.x, viewport.y, viewport.width, viewport.height}
    };
    commands.pushConstants<DeferredLightingPushConstants>(
        *pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        parameters
    );
    commands.draw(3, 1, 0, 0);
    recordDraw(context().profiler, 3);
    commands.endRendering();
}
