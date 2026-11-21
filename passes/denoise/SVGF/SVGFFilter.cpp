#include "passes/denoise/SVGF/SVGFFilter.h"

#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/device/VulkanContext.h"

namespace
{
constexpr uint32_t kSignalInputCount     = 4;
constexpr uint32_t kFilterBindingCount   = 6;
constexpr uint32_t kFirstOrLastBindingCount = 7;
constexpr auto     kStorageWrite         = VulkanResources::ImageAccess{
                .stages = vk::PipelineStageFlagBits2::eComputeShader,
                .access = vk::AccessFlagBits2::eShaderStorageWrite,
                .layout = vk::ImageLayout::eGeneral,
};
constexpr auto kSampledRead = VulkanResources::ImageAccess{
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

enum class FilterVariant
{
    First = 0,
    Filter = 1,
    Final = 2,
};
}  // namespace

void SVGFFilter::compile(
    const Falcor::RenderContext& context,
    const DenoiserInputs& traced,
    const SVGFReprojection::Output& lighting,
    const std::array<VulkanResources::ImageHandle, kLightingLayerCount>& scratch,
    VulkanResources::ImageHandle color,
    const SVGFReprojection& reprojection
)
{
    if (filters.empty())
        for (uint32_t iteration = 0; iteration < kSvgfFilterIterations; ++iteration)
            filters.emplace_back(iteration, reprojection);
    const std::array signals{std::array{lighting.direct, lighting.indirect}, scratch};
    for (uint32_t iteration = 0; iteration < filters.size(); ++iteration)
    {
        const auto& [direct, indirect] = signals[iteration % signals.size()];
        const auto& [targetDirect, targetIndirect] = signals[(iteration + 1) % signals.size()];
        filters[iteration].compile(
            context,
            {
                .direct          = direct,
                .indirect        = indirect,
                .targetDirect    = targetDirect,
                .targetIndirect  = targetIndirect,
                .color           = color,
                .depth           = traced.depth,
                .normal          = traced.worldNormal,
                .emission        = traced.emission,
                .albedo          = traced.albedo,
                .lightingHistory = lighting.lightingHistory,
            }
        );
    }
}

void SVGFFilter::execute(VulkanResources& cache, vk::raii::CommandBuffer& commands, const Falcor::RenderContext& frame) const
{
    for (const auto& filter : filters)
        filter.execute(cache, commands, frame);
}

void SVGFFilter::FilterPass::compile(const Falcor::RenderContext& context, const FilterResources& resources)
{
    auto& cache = *context.vulkanResources;
    extent = cache.imageInfo(resources.direct).extent;
    accesses.clear();
    for (const auto image : {resources.direct, resources.indirect, resources.depth, resources.normal})
        accesses.add(image, kSampledRead);
    if (last())
    {
        accesses.add(resources.emission, kSampledRead);
        accesses.add(resources.albedo, kSampledRead);
        accesses.add(resources.color, kStorageWrite);
    }
    else
    {
        accesses.add(resources.targetDirect, kStorageWrite);
        accesses.add(resources.targetIndirect, kStorageWrite);
    }
    if (first())
        accesses.add(resources.lightingHistory, kStorageWrite);
    accesses.declare(cache);
    if (!*pipeline)
        createPipeline(context);

    const std::array handles{
        resources.direct,
        resources.indirect,
        resources.depth,
        resources.normal,
        last() ? resources.color : resources.targetDirect,
        last() ? resources.emission : resources.targetIndirect,
        last() ? resources.albedo : resources.lightingHistory,
    };
    const uint32_t bindingCount = first() || last() ? kFirstOrLastBindingCount : kFilterBindingCount;
    for (uint32_t binding = 0; binding < bindingCount; ++binding)
    {
        const auto image   = handles[binding];
        const bool sampled = binding < kSignalInputCount || (last() && binding >= 5);
        const auto type    = sampled ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eStorageImage;
        const auto view =
            cache.imageInfo(image).arrayLayers == 1 ? vk::ImageViewType::e2D : vk::ImageViewType::e2DArray;
        cache.writeDescriptor(*descriptorSet, binding, image, type, view);
    }
}

void SVGFFilter::FilterPass::createPipeline(const Falcor::RenderContext& context)
{
    const uint32_t bindingCount = first() || last() ? kFirstOrLastBindingCount : kFilterBindingCount;
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
    auto& device        = *context.getDevice();
    descriptorSetLayout = device.createDescriptorSetLayout(bindings);
    const vk::PushConstantRange range{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .size       = sizeof(FilterParameters)
    };
    pipelineLayout = device.getOrCreatePipelineLayout(std::array{&descriptorSetLayout}, std::array{range});
    const auto variant = first() ? FilterVariant::First : last() ? FilterVariant::Final : FilterVariant::Filter;
    const auto shader = context.shaderManager->getOrLoad("svgf_filter", {{"SVGF_STAGE", std::to_string(static_cast<int>(variant))}});
    pipeline = device.createComputePipeline(shader, pipelineLayout);
    descriptorSet = device.allocateDescriptorSet(descriptorSetLayout);
}

void SVGFFilter::FilterPass::execute(
    VulkanResources& cache, vk::raii::CommandBuffer& commands, const Falcor::RenderContext& frame
) const
{
    accesses.transition(cache, commands);
    commands.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commands.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, *descriptorSet, {});
    const auto& viewport = frame.view->viewport;
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
    pipeline.dispatch(commands, {extent.width, extent.height, 1});
}
