#include "passes/postprocess/ToneMappingPass/ToneMappingPass.h"
#include "editor/PassEditor.h"

#include "asset/BuiltinAssets.h"
#include "core/Context.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderData.h"

#include <array>

RenderPassReflection ToneMappingPass::reflect(const PassSetup&) const
{
    RenderPassReflection reflection;
    reflection.input("hdrColor", PortKind::Image);
    reflection.input("target", PortKind::Image);
    reflection.input("exposure", PortKind::Buffer);
    reflection.output("color", PortKind::Image);
    return reflection;
}

void ToneMappingPass::compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports)
{
    const auto target = ports.image("target");
    hdrColor = ports.image("hdrColor");
    exposure = ports.buffer("exposure");
    const auto color = graph.write(target);
    attachments.colors.front() = {color, vk::AttachmentLoadOp::eDontCare};

    graph.use(
        exposure,
        {
            .stages = vk::PipelineStageFlagBits2::eFragmentShader,
            .access = vk::AccessFlagBits2::eShaderStorageRead,
        }
    );
    graph.use(
        hdrColor,
        {
            .stages = vk::PipelineStageFlagBits2::eFragmentShader,
            .access = vk::AccessFlagBits2::eShaderSampledRead,
            .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }
    );
    attachments.declare(graph);

    const auto targetFormat   = graph.imageInfo(target).format;
    const bool srgbAttachment = targetFormat == vk::Format::eB8G8R8A8Srgb || targetFormat == vk::Format::eR8G8B8A8Srgb;
    CHECK(
        srgbAttachment || targetFormat == vk::Format::eB8G8R8A8Unorm || targetFormat == vk::Format::eR8G8B8A8Unorm,
        "unsupported tone mapping output format: {}",
        vk::to_string(targetFormat)
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
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kToneMappingExposureBinding,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eFragment,
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
                 .colorFormats = {targetFormat},
    });
    sampler           = ctx.resources->sampler(BuiltinAssets::Sampler::linearClamp);

    descriptorSet = device.allocateDescriptorSet(descriptorSetLayout);

    ports.setOutput("color", color);
}

void ToneMappingPass::bindResources(RenderGraph& graph, const PassSetup& setup)
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
    const vk::DescriptorImageInfo  samplerInfo{.sampler = sampler};
    const vk::DescriptorBufferInfo exposureInfo{
        .buffer = graph.getBuffer(exposure).handle(),
        .range  = graph.getBuffer(exposure).size(),
    };
    const std::array writes = {
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
        vk::WriteDescriptorSet{
            .dstSet          = *descriptorSet,
            .dstBinding      = RenderInterface::kToneMappingExposureBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &exposureInfo,
        },
    };
    device.updateDescriptorSets(writes, {});
}

void ToneMappingPass::execute(
    RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame&
)
{
    attachments.begin(graph, commands);
    commands.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sets = {*descriptorSet};
    commands.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, sets, {});

    const auto&                    settings = parameters;
    const ToneMappingPushConstants parameters{
        .mode       = static_cast<uint32_t>(settings.mode),
        .encodeSrgb = encodeSrgb ? 1u : 0u,
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

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<ToneMappingPass>("ToneMappingPass", makePassEditor<ToneMappingPass>("Tone mapping"));
}
