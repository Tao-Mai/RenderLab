#include "render/feature/postprocess/Bloom.h"

#include "asset/BuiltinAssets.h"
#include "core/Context.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/resource/RenderResourceManager.h"

#include <array>
#include <glm/vec4.hpp>

namespace
{
constexpr uint32_t kSourceImageBinding = 0;
constexpr uint32_t kSamplerBinding     = 1;
constexpr uint32_t kBaseImageBinding   = 2;
constexpr auto     kShaderStages       = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

struct alignas(16) BloomParameters
{
    glm::vec4 viewport;  // Normalized min/max, identical across the image pyramid.
    glm::vec2 texelSize;
    float     filterRadius;
    float     intensity;
    float     scatter;
};
static_assert(sizeof(BloomParameters) == 48);

vk::ImageView imageView(RenderGraph& graph, RenderGraph::ImageHandle image)
{
    return graph.getImage(image).getOrCreateImageView({
        .viewType         = vk::ImageViewType::e2D,
        .format           = graph.imageInfo(image).format,
        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
    });
}
}  // namespace

Bloom::Output Bloom::setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle hdrColor)
{
    DCHECK(graph.imageInfo(hdrColor).format == kHdrColorFormat);
    const auto features =
        setup.vulkan.physicalDeviceHandle().getFormatProperties(kHdrColorFormat).optimalTilingFeatures;
    CHECK(features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);
    filters.clear();

    using Stage    = FilterPass::Stage;
    const auto add = [&](Stage stage, RenderGraph::ImageHandle source, RenderGraph::ImageHandle base = {})
    {
        auto& pass = *filters.emplace_back(std::make_unique<FilterPass>(stage));
        return graph.addPass(pass, setup, source, base).color;
    };

    std::vector<RenderGraph::ImageHandle> pyramid;
    auto                                  source = hdrColor;
    const auto                            levels = setup.config.feature.bloom.levels;
    for (uint32_t level = 0; level < levels; ++level)
    {
        const auto horizontal = add(Stage::DownsampleHorizontal, source);
        source                = add(Stage::DownsampleVertical, horizontal);
        pyramid.push_back(source);
        const auto extent = graph.imageInfo(source).extent;
        if (extent.width == 1 && extent.height == 1)
            break;
    }

    auto blurred = pyramid.back();
    for (size_t level = pyramid.size() - 1; level > 0; --level)
        blurred = add(Stage::Upsample, blurred, pyramid[level - 1]);
    return {add(Stage::Composite, hdrColor, blurred)};
}

Bloom::Output Bloom::FilterPass::setup(
    RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle input, RenderGraph::ImageHandle baseInput
)
{
    source             = input;
    base               = baseInput.id == RenderGraph::kInvalidHandle ? input : baseInput;
    const auto sampled = RenderGraph::ImageAccess{
        .stages = vk::PipelineStageFlagBits2::eFragmentShader,
        .access = vk::AccessFlagBits2::eShaderSampledRead,
        .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    graph.use(source, sampled);
    if (base.id != source.id)
        graph.use(base, sampled);

    auto extent = graph.imageInfo(stage == Stage::Upsample ? base : source).extent;
    if (stage == Stage::DownsampleHorizontal)
    {
        extent.width  = std::max(1u, extent.width / 2);
        extent.height = std::max(1u, extent.height / 2);
    }
    color = graph.write(graph.createImage({.format = kHdrColorFormat, .extent = extent}));
    graph.use(
        color,
        {
            .stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .access = vk::AccessFlagBits2::eColorAttachmentWrite,
            .layout = vk::ImageLayout::eColorAttachmentOptimal,
        }
    );

    const std::array bindings{
        vk::DescriptorSetLayoutBinding{
            .binding         = kSourceImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = kShaderStages
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = kSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = kShaderStages
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = kBaseImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = kShaderStages
        },
    };
    auto& ctx           = renderContext();
    auto& device        = *ctx.device;
    descriptorSetLayout = device.createDescriptorSetLayout(bindings);
    const std::array layouts{&descriptorSetLayout};
    const std::array ranges{vk::PushConstantRange{.stageFlags = kShaderStages, .size = sizeof(BloomParameters)}};
    pipelineLayout    = device.getOrCreatePipelineLayout(layouts, ranges);
    const auto shader = ctx.shaderManager->getOrLoad("bloom", {{"BLOOM_STAGE", static_cast<int>(stage)}});
    pipeline          = device.getOrCreatePipeline({
                 .shader       = shader,
                 .layout       = pipelineLayout,
                 .state        = {.vertexLayout = VertexLayoutPreset::None, .depthTest = false, .depthWrite = false},
                 .colorFormats = {kHdrColorFormat},
    });
    sampler           = ctx.resources->sampler(BuiltinAssets::Sampler::linearClamp);
    descriptorSet     = device.allocateDescriptorSet(descriptorSetLayout);
    return {color};
}

void Bloom::FilterPass::writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const
{
    const std::array images{
        vk::DescriptorImageInfo{
            .imageView   = imageView(graph, source),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        },
        vk::DescriptorImageInfo{.sampler = sampler},
        vk::DescriptorImageInfo{
            .imageView   = imageView(graph, base),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        },
    };
    const std::array writes{
        vk::WriteDescriptorSet{
            .dstSet          = *descriptorSet,
            .dstBinding      = kSourceImageBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .pImageInfo      = &images[0]
        },
        vk::WriteDescriptorSet{
            .dstSet          = *descriptorSet,
            .dstBinding      = kSamplerBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eSampler,
            .pImageInfo      = &images[1]
        },
        vk::WriteDescriptorSet{
            .dstSet          = *descriptorSet,
            .dstBinding      = kBaseImageBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .pImageInfo      = &images[2]
        },
    };
    setup.vulkan.deviceHandle().updateDescriptorSets(writes, {});
}

void Bloom::FilterPass::execute(
    RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution
) const
{
    const auto                        extent = graph.imageInfo(color).extent;
    const vk::RenderingAttachmentInfo attachment{
        .imageView   = imageView(graph, color),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eDontCare,
        .storeOp     = vk::AttachmentStoreOp::eStore,
    };
    auto& commands = commandBuffer;
    commands.beginRendering({
        .renderArea           = {{0, 0}, {extent.width, extent.height}},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &attachment,
    });
    commands.setViewport(
        0,
        vk::Viewport(0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1)
    );
    commands.setScissor(0, vk::Rect2D({0, 0}, {extent.width, extent.height}));
    commands.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sets{*descriptorSet};
    commands.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, sets, {});

    const auto            sourceExtent = graph.imageInfo(source).extent;
    const auto            frameExtent  = execution.setup.renderExtent;
    const auto&           viewport     = execution.view.viewport;
    const auto&           settings     = execution.setup.config.feature.bloom;
    const BloomParameters parameters{
        .viewport =
            {viewport.x / frameExtent.width,
             viewport.y / frameExtent.height,
             (viewport.x + viewport.width) / frameExtent.width,
             (viewport.y + viewport.height) / frameExtent.height},
        .texelSize    = {1.0f / sourceExtent.width, 1.0f / sourceExtent.height},
        .filterRadius = settings.filterRadius,
        .intensity    = settings.intensity,
        .scatter      = settings.scatter,
    };
    commands.pushConstants<BloomParameters>(pipelineLayout, kShaderStages, 0, parameters);
    commands.draw(3, 1, 0, 0);
    recordDraw(context().profiler, 3);
    commands.endRendering();
}
