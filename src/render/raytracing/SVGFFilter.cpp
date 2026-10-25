#include "render/raytracing/SVGFFilter.h"

#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/resource/GpuShader.h"

namespace
{
constexpr uint32_t kWorkgroupSize        = 8;
constexpr uint32_t kSignalInputCount     = 4;
constexpr uint32_t kFilterBindingCount   = 6;
constexpr uint32_t kFinalBindingCount    = 7;
constexpr uint32_t kFeedbackBindingCount = 8;
constexpr auto     kStorageWrite         = RenderGraph::ImageAccess{
                .stages = vk::PipelineStageFlagBits2::eComputeShader,
                .access = vk::AccessFlagBits2::eShaderStorageWrite,
                .layout = vk::ImageLayout::eGeneral,
};
constexpr auto kSampledRead = RenderGraph::ImageAccess{
    .stages = vk::PipelineStageFlagBits2::eComputeShader,
    .access = vk::AccessFlagBits2::eShaderSampledRead,
    .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
};

struct FilterParameters
{
    glm::vec4 viewport;
    uint32_t  stepSize;
    uint32_t  historyLayer;
};
static_assert(sizeof(FilterParameters) == 24);
}  // namespace

SVGFFilter::Output SVGFFilter::setup(
    RenderGraph&             graph,
    const PassSetup&         setup,
    RayTracingPass::Output   traced,
    SVGFReprojection::Output lighting,
    const SVGFReprojection&  reprojection
)
{
    if (filters.empty())
        for (uint32_t iteration = 0; iteration < kSvgfFilterIterations; ++iteration)
            filters.push_back(std::make_unique<FilterPass>(iteration, reprojection));
    struct SignalPair
    {
        RenderGraph::ImageHandle direct;
        RenderGraph::ImageHandle indirect;
    };
    const auto                extent = graph.imageInfo(lighting.direct).extent;
    std::array<SignalPair, 2> signals{
        SignalPair{lighting.direct, lighting.indirect},
        SignalPair{
            graph.createImage({.format = vk::Format::eR32G32B32A32Sfloat, .extent = extent}),
            graph.createImage({.format = vk::Format::eR32G32B32A32Sfloat, .extent = extent}),
        },
    };
    for (uint32_t iteration = 0; iteration < kSvgfFilterIterations; ++iteration)
    {
        auto&      pass       = *filters[iteration];
        const bool first      = iteration == 0;
        const bool last       = iteration + 1 == kSvgfFilterIterations;
        const auto readIndex  = iteration % signals.size();
        const auto writeIndex = (iteration + 1) % signals.size();
        const auto filtered   = graph.addPass(
            pass,
            setup,
            FilterInput{
                  .direct          = signals[readIndex].direct,
                  .indirect        = signals[readIndex].indirect,
                  .targetDirect    = last ? RenderGraph::ImageHandle{} : signals[writeIndex].direct,
                  .targetIndirect  = last ? RenderGraph::ImageHandle{} : signals[writeIndex].indirect,
                  .depth           = traced.depth,
                  .normal          = traced.worldNormal,
                  .emission        = last ? traced.emission : RenderGraph::ImageHandle{},
                  .albedo          = last ? traced.albedo : RenderGraph::ImageHandle{},
                  .directHistory   = first ? lighting.directHistory : RenderGraph::ImageHandle{},
                  .indirectHistory = first ? lighting.indirectHistory : RenderGraph::ImageHandle{},
            }
        );
        if (last)
            return {filtered.color};
        signals[writeIndex] = {filtered.direct, filtered.indirect};
    }
    std::unreachable();
}

SVGFFilter::FilterOutput SVGFFilter::FilterPass::setup(RenderGraph& graph, const PassSetup& setup, FilterInput source)
{
    input  = source;
    output = {};
    extent = graph.imageInfo(input.direct).extent;
    for (const auto image : {input.direct, input.indirect, input.depth, input.normal})
        graph.use(image, kSampledRead);
    if (last())
    {
        graph.use(input.emission, kSampledRead);
        graph.use(input.albedo, kSampledRead);
        output.color = graph.write(graph.createImage({.format = kHdrColorFormat, .extent = extent}));
        graph.use(output.color, kStorageWrite);
    }
    else
    {
        output.direct   = graph.write(input.targetDirect);
        output.indirect = graph.write(input.targetIndirect);
        graph.use(output.direct, kStorageWrite);
        graph.use(output.indirect, kStorageWrite);
    }
    if (first())
    {
        input.directHistory   = graph.write(input.directHistory);
        input.indirectHistory = graph.write(input.indirectHistory);
        graph.use(input.directHistory, kStorageWrite);
        graph.use(input.indirectHistory, kStorageWrite);
    }
    if (!*pipeline)
        createPipeline(setup.vulkan);
    return output;
}

void SVGFFilter::FilterPass::createPipeline(const VulkanContext& vulkan)
{
    const uint32_t bindingCount = first() ? kFeedbackBindingCount : last() ? kFinalBindingCount : kFilterBindingCount;
    std::vector<vk::DescriptorSetLayoutBinding> bindings(bindingCount);
    for (uint32_t binding = 0; binding < bindingCount; ++binding)
    {
        const bool sampled = binding < kSignalInputCount || (last() && binding >= 5);
        bindings[binding]  = {
             .binding         = binding,
             .descriptorType  = sampled ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eStorageImage,
             .descriptorCount = 1,
             .stageFlags      = vk::ShaderStageFlagBits::eCompute,
        };
    }
    auto& device        = *renderContext().device;
    descriptorSetLayout = device.createDescriptorSetLayout(bindings);
    const vk::PushConstantRange range{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .size       = sizeof(FilterParameters)
    };
    pipelineLayout = device.getOrCreatePipelineLayout(std::array{&descriptorSetLayout}, std::array{range});
    const char*                   shaderPath = first() ? "shaders/svgf_filter.first.spv"
                          : last()                     ? "shaders/svgf_filter.final.spv"
                                                       : "shaders/svgf_filter.spv";
    GpuShader                     shader{vulkan.deviceHandle(), shaderPath};
    const vk::raii::PipelineCache cache{nullptr};
    pipeline      = vkCheck(vulkan.deviceHandle().createComputePipeline(
        cache,
        {
                 .stage  = {.stage = vk::ShaderStageFlagBits::eCompute, .module = shader.handle(), .pName = "computeMain"},
                 .layout = pipelineLayout,
        }
    ));
    descriptorSet = device.allocateDescriptorSet(descriptorSetLayout);
}

void SVGFFilter::FilterPass::writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const
{
    const std::array handles{
        input.direct,
        input.indirect,
        input.depth,
        input.normal,
        last() ? output.color : output.direct,
        last() ? input.emission : output.indirect,
        last() ? input.albedo : input.directHistory,
        input.indirectHistory
    };
    const uint32_t bindingCount = first() ? kFeedbackBindingCount : last() ? kFinalBindingCount : kFilterBindingCount;
    std::array<vk::DescriptorImageInfo, kFeedbackBindingCount> images;
    std::array<vk::WriteDescriptorSet, kFeedbackBindingCount>  writes;
    for (uint32_t binding = 0; binding < bindingCount; ++binding)
    {
        const auto  image   = handles[binding];
        const auto& info    = graph.imageInfo(image);
        const bool  sampled = binding < kSignalInputCount || (last() && binding >= 5);
        images[binding]     = {
                .imageView   = graph.getImage(image).getOrCreateImageView({
                      .viewType = info.arrayLayers == 1 ? vk::ImageViewType::e2D : vk::ImageViewType::e2DArray,
                      .format   = info.format,
                      .subresourceRange =
                    {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = info.arrayLayers},
            }),
                .imageLayout = sampled ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eGeneral,
        };
        writes[binding] = {
            .dstSet          = *descriptorSet,
            .dstBinding      = binding,
            .descriptorCount = 1,
            .descriptorType  = sampled ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eStorageImage,
            .pImageInfo      = &images[binding],
        };
    }
    setup.vulkan.deviceHandle().updateDescriptorSets(std::span{writes.data(), bindingCount}, {});
}

void SVGFFilter::FilterPass::execute(RenderGraph&, vk::raii::CommandBuffer& commands, const PassFrame& frame) const
{
    commands.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commands.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, *descriptorSet, {});
    const auto& viewport = frame.view.viewport;
    commands.pushConstants<FilterParameters>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eCompute,
        0,
        FilterParameters{
            .viewport     = {viewport.x, viewport.y, viewport.width, viewport.height},
            .stepSize     = 1u << iteration,
            .historyLayer = reprojection.colorHistoryLayer(),
        }
    );
    commands.dispatch(
        (extent.width + kWorkgroupSize - 1) / kWorkgroupSize,
        (extent.height + kWorkgroupSize - 1) / kWorkgroupSize,
        1
    );
}
