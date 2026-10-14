#include "render/feature/postprocess/BloomPass.h"

#include "asset/BuiltinAssets.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "render/DescriptorManager.h"
#include "render/PipelineManager.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/device/VulkanContext.h"
#include "render/present/Swapchain.h"
#include "render/resource/RenderResourceManager.h"

#include <array>
#include <glm/vec4.hpp>

namespace
{
constexpr uint32_t kSourceImageBinding = 0;
constexpr uint32_t kSamplerBinding     = 1;
constexpr uint32_t kBaseImageBinding   = 2;
constexpr auto     kShaderStages       = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

struct BloomParameters
{
    glm::vec4 viewport;  // Normalized min/max, identical across the image pyramid.
    glm::vec2 texelSize;
    float     filterRadius;
    float     intensity;
};
static_assert(sizeof(BloomParameters) == 32);

vk::ImageView imageView(RenderGraph& graph, RenderGraph::ImageHandle image, uint32_t frameIndex)
{
    return graph.getImage(image, frameIndex)
        .getOrCreateImageView({
            .viewType         = vk::ImageViewType::e2D,
            .format           = graph.imageInfo(image).format,
            .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
        });
}
}  // namespace

RenderGraph::ImageHandle BloomPass::setupChain(
    RenderGraph& graph, RenderGraph::ImageHandle hdrColor, std::vector<std::unique_ptr<BloomPass>>& storage
)
{
    DCHECK(graph.imageInfo(hdrColor).format == kHdrColorFormat);
    const auto features =
        graph.vulkan().physicalDeviceHandle().getFormatProperties(kHdrColorFormat).optimalTilingFeatures;
    CHECK(features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);
    storage.clear();

    const auto add = [&](Stage stage, RenderGraph::ImageHandle source, RenderGraph::ImageHandle base = {})
    {
        auto& pass = *storage.emplace_back(std::make_unique<BloomPass>(stage));
        return graph.addPass(pass, source, base).color;
    };
    std::vector<RenderGraph::ImageHandle> pyramid;
    auto                                  source = hdrColor;
    const auto                            levels = context().config->rendererConfig().feature.bloom.levels;
    for (uint32_t level = 0; level < levels; ++level)
    {
        source = add(Stage::Downsample, source);
        pyramid.push_back(source);
        const auto extent = graph.imageInfo(source).extent;
        if (extent.width == 1 && extent.height == 1)
            break;
    }

    auto blurred = pyramid.back();
    for (size_t level = pyramid.size() - 1; level > 0; --level)
        blurred = add(Stage::Upsample, blurred, pyramid[level - 1]);
    return add(Stage::Composite, hdrColor, blurred);
}

BloomPass::Output
BloomPass::setup(RenderGraph& graph, RenderGraph::ImageHandle input, RenderGraph::ImageHandle baseInput)
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
    if (stage == Stage::Downsample)
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
    auto&            ctx    = renderContext();
    const auto       layout = ctx.descriptors->getOrCreateLayout(bindings);
    const std::array layouts{layout};
    const std::array ranges{vk::PushConstantRange{.stageFlags = kShaderStages, .size = sizeof(BloomParameters)}};
    pipelineLayout    = ctx.pipelineManager->getOrCreateLayout(layouts, ranges);
    const auto shader = ctx.shaderManager->getOrLoad("bloom", {{"BLOOM_STAGE", static_cast<int>(stage)}});
    pipeline          = ctx.pipelineManager->getOrCreate({
                 .shader       = shader,
                 .layout       = pipelineLayout,
                 .state        = {.vertexLayout = VertexLayoutPreset::None, .depthTest = false, .depthWrite = false},
                 .colorFormats = {kHdrColorFormat},
    });
    sampler           = ctx.resources->sampler(BuiltinAssets::Sampler::linearClamp);
    descriptorSets.clear();
    for (uint32_t index = 0; index < maxFramesInFlight; ++index)
        descriptorSets.push_back(ctx.descriptors->allocate(layout));
    return {color};
}

void BloomPass::prepare(RenderGraph& graph) const
{
    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex)
    {
        const std::array images{
            vk::DescriptorImageInfo{
                .imageView   = imageView(graph, source, frameIndex),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
            },
            vk::DescriptorImageInfo{.sampler = sampler},
            vk::DescriptorImageInfo{
                .imageView   = imageView(graph, base, frameIndex),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
            },
        };
        const std::array writes{
            vk::WriteDescriptorSet{
                .dstSet          = *descriptorSets[frameIndex],
                .dstBinding      = kSourceImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &images[0]
            },
            vk::WriteDescriptorSet{
                .dstSet          = *descriptorSets[frameIndex],
                .dstBinding      = kSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &images[1]
            },
            vk::WriteDescriptorSet{
                .dstSet          = *descriptorSets[frameIndex],
                .dstBinding      = kBaseImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &images[2]
            },
        };
        graph.vulkan().deviceHandle().updateDescriptorSets(writes, {});
    }
}

void BloomPass::execute(RenderGraph& graph) const
{
    const auto                        extent = graph.imageInfo(color).extent;
    const vk::RenderingAttachmentInfo attachment{
        .imageView   = imageView(graph, color, graph.frameIndex()),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eDontCare,
        .storeOp     = vk::AttachmentStoreOp::eStore,
    };
    auto& commands = graph.commands();
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
    const std::array sets{*descriptorSets[graph.frameIndex()]};
    commands.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, sets, {});

    const auto            sourceExtent = graph.imageInfo(source).extent;
    const auto            frameExtent  = graph.swapchain().extent();
    const auto&           viewport     = graph.view().viewport;
    const auto&           settings     = context().config->rendererConfig().feature.bloom;
    const BloomParameters parameters{
        .viewport =
            {viewport.x / frameExtent.width,
             viewport.y / frameExtent.height,
             (viewport.x + viewport.width) / frameExtent.width,
             (viewport.y + viewport.height) / frameExtent.height},
        .texelSize    = {1.0f / sourceExtent.width, 1.0f / sourceExtent.height},
        .filterRadius = settings.filterRadius,
        .intensity    = settings.intensity,
    };
    commands.pushConstants<BloomParameters>(pipelineLayout, kShaderStages, 0, parameters);
    commands.draw(3, 1, 0, 0);
    graph.recordDraw(3);
    commands.endRendering();
}
