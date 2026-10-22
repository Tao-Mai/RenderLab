#include "render/feature/postprocess/ToneMappingPass.h"

#include "asset/BuiltinAssets.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/present/Swapchain.h"
#include "render/resource/RenderResourceManager.h"
#include "render/resource/ShaderData.h"

#include <array>

ToneMappingPass::Output ToneMappingPass::setup(
    RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle hdrColorHandle, RenderGraph::ImageHandle target
)
{
    hdrColor = hdrColorHandle;
    color    = graph.write(target);
    graph.use(
        hdrColor,
        {
            .stages = vk::PipelineStageFlagBits2::eFragmentShader,
            .access = vk::AccessFlagBits2::eShaderSampledRead,
            .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }
    );
    graph.use(
        color,
        {
            .stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .access = vk::AccessFlagBits2::eColorAttachmentWrite,
            .layout = vk::ImageLayout::eColorAttachmentOptimal,
        }
    );

    const auto surfaceFormat = setup.swapchain.surfaceFormat();
    CHECK(surfaceFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear);
    const bool srgbAttachment =
        surfaceFormat.format == vk::Format::eB8G8R8A8Srgb || surfaceFormat.format == vk::Format::eR8G8B8A8Srgb;
    CHECK(
        srgbAttachment || surfaceFormat.format == vk::Format::eB8G8R8A8Unorm ||
            surfaceFormat.format == vk::Format::eR8G8B8A8Unorm,
        "unsupported tone mapping output format: {}",
        vk::to_string(surfaceFormat.format)
    );
    encodeSrgb = !srgbAttachment;

    const std::array toneMappingBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kToneMappingImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kToneMappingSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
    };
    auto& device        = *renderContext().device;
    descriptorSetLayout = device.createDescriptorSetLayout(toneMappingBindings);
    const std::array sets{&descriptorSetLayout};
    const std::array ranges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(ToneMappingPushConstants)
    }};
    pipelineLayout = device.getOrCreatePipelineLayout(sets, ranges);

    auto&      ctx    = renderContext();
    const auto shader = ctx.shaderManager->getOrLoad("tonemapping");
    pipeline          = device.getOrCreatePipeline({
                 .shader = shader,
                 .layout = pipelineLayout,
                 .state  = PipelineState{.vertexLayout = VertexLayoutPreset::None, .depthTest = false, .depthWrite = false},
                 .colorFormats = {surfaceFormat.format},
    });
    sampler           = ctx.resources->sampler(BuiltinAssets::Sampler::linearClamp);

    descriptorSet = device.allocateDescriptorSet(descriptorSetLayout);

    return {color};
}

void ToneMappingPass::writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const
{
    const auto&                   device = setup.vulkan.deviceHandle();
    const vk::DescriptorImageInfo imageInfo{
        .imageView   = graph.getImage(hdrColor).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = graph.imageInfo(hdrColor).format,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
        }),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo samplerInfo{.sampler = sampler};
    const std::array              writes = {
        vk::WriteDescriptorSet{
                         .dstSet          = *descriptorSet,
                         .dstBinding      = RenderInterface::kToneMappingImageBinding,
                         .descriptorCount = 1,
                         .descriptorType  = vk::DescriptorType::eSampledImage,
                         .pImageInfo      = &imageInfo,
        },
        vk::WriteDescriptorSet{
                         .dstSet          = *descriptorSet,
                         .dstBinding      = RenderInterface::kToneMappingSamplerBinding,
                         .descriptorCount = 1,
                         .descriptorType  = vk::DescriptorType::eSampler,
                         .pImageInfo      = &samplerInfo,
        },
    };
    device.updateDescriptorSets(writes, {});
}

void ToneMappingPass::execute(
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
    commands.beginRendering({
        .renderArea           = {{0, 0}, extent},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &attachment,
    });
    commands.setViewport(
        0,
        vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f)
    );
    commands.setScissor(0, vk::Rect2D({0, 0}, extent));
    commands.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sets = {*descriptorSet};
    commands.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, sets, {});

    const auto&                    settings = context().config->rendererConfig().feature.toneMapping;
    const ToneMappingPushConstants parameters{
        .exposureScale = settings.exposureScale(),
        .mode          = static_cast<uint32_t>(settings.mode),
        .encodeSrgb    = encodeSrgb ? 1u : 0u,
    };
    commands.pushConstants<ToneMappingPushConstants>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        parameters
    );
    commands.draw(3, 1, 0, 0);
    recordDraw(context().profiler, 3);
    commands.endRendering();
}
