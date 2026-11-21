#include "render/device/ResourceBarriers.h"
#include "passes/postprocess/Bloom/Bloom.h"

#include "core/Logger.h"
#include "core/profiling/Profiler.h"

#include "asset/BuiltinAssets.h"
#include "core/Context.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/resources/RenderResourceManager.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <format>
#include <glm/vec4.hpp>

namespace
{
constexpr std::string_view kHorizontal = "horizontal";
constexpr std::string_view kVertical = "vertical";
constexpr std::string_view kUpsample = "upsample";
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

uint32_t pyramidLevels(Falcor::uint2 extent, uint32_t limit)
{
    const auto levels = std::bit_width(std::max({2u, extent.x, extent.y})) - 1;
    return std::min(static_cast<uint32_t>(levels), limit);
}

std::string pyramidImageName(std::string_view stage, uint32_t level)
{
    return std::format("{}{}", stage, level);
}
}  // namespace

void BloomSettings::validate() const
{
    CHECK(std::isfinite(intensity) && intensity >= 0.0f && intensity <= 1.0f);
    CHECK(std::isfinite(scatter) && scatter >= 0.05f && scatter <= 0.95f);
    CHECK(std::isfinite(filterRadius) && filterRadius > 0.0f);
    CHECK(levels > 0 && levels <= kMaxBloomLevels);
}

Bloom::Bloom(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)), parameters(Falcor::deserializeFromProperties<BloomSettings>(properties))
{
    parameters.validate();
}

Falcor::Properties Bloom::getProperties() const
{
    return Falcor::serializeToProperties(parameters);
}

void Bloom::setProperties(const Falcor::Properties& properties)
{
    BloomSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    const bool recompile = next.levels != parameters.levels;
    parameters = std::move(next);
    if (recompile)
        requestRecompile();
}

Falcor::RenderPassReflection Bloom::reflect(const CompileData& compileData)
{
    auto extent = compileData.defaultTexDims;
    if (const auto* input = compileData.connectedResources.getField("hdrColor"))
    {
        extent.x = input->getWidth() ? input->getWidth() : extent.x;
        extent.y = input->getHeight() ? input->getHeight() : extent.y;
    }

    Falcor::RenderPassReflection reflection;
    reflection.addInput("hdrColor", "HDR scene color").bindFlags(Falcor::ResourceBindFlags::ShaderResource).format(Falcor::ResourceFormat::RGBA16Float);
    reflection.addOutput("color", "Scene color with bloom")
        .format(Falcor::ResourceFormat::RGBA16Float)
        .bindFlags(Falcor::ResourceBindFlags::RenderTarget)
        .texture2D(extent.x, extent.y);

    const auto levels = pyramidLevels(extent, parameters.levels);
    auto size = extent;
    const auto addImage = [&](std::string_view stage, uint32_t level)
    {
        reflection.addInternal(pyramidImageName(stage, level), "Bloom pyramid image")
            .texture2D(size.x, size.y)
            .format(Falcor::ResourceFormat::RGBA16Float)
            .bindFlags(Falcor::ResourceBindFlags::ShaderResource | Falcor::ResourceBindFlags::RenderTarget);
    };
    for (uint32_t level = 0; level < levels; ++level)
    {
        size = {std::max(1u, size.x / 2), std::max(1u, size.y / 2)};
        addImage(kHorizontal, level);
        addImage(kVertical, level);
        if (level + 1 < levels)
            addImage(kUpsample, level);
    }
    return reflection;
}

void Bloom::compile(Falcor::RenderContext* passContext, const CompileData&)
{
    const auto features =
        passContext->vulkan->physicalDeviceHandle().getFormatProperties(kHdrColorFormat).optimalTilingFeatures;
    CHECK(features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);
    filters.clear();
}

void Bloom::prepareFilters(const Falcor::RenderContext& context, const Falcor::RenderData& renderData)
{
    auto& cache = *context.vulkanResources;
    const auto hdrColor = cache.registerImage(renderData.getTexture("hdrColor"));
    const auto color = cache.registerImage(renderData.getTexture("color"));
    const auto extent = cache.imageInfo(hdrColor).extent;
    const auto levels = pyramidLevels({extent.width, extent.height}, parameters.levels);
    using Stage = Filter::Stage;
    const auto image = [&](std::string_view stage, uint32_t level)
    {
        return cache.registerImage(renderData.getTexture(pyramidImageName(stage, level)));
    };

    std::array<VulkanResources::ImageHandle, kMaxBloomLevels> pyramid;
    auto source = hdrColor;
    for (uint32_t level = 0; level < levels; ++level)
    {
        const auto horizontal = image(kHorizontal, level);
        const auto vertical = image(kVertical, level);
        filters.emplace_back(Stage::DownsampleHorizontal, parameters).compile(context, source, source, horizontal);
        filters.emplace_back(Stage::DownsampleVertical, parameters).compile(context, horizontal, horizontal, vertical);
        pyramid[level] = vertical;
        source = vertical;
    }

    auto blurred = pyramid[levels - 1];
    for (uint32_t level = levels - 1; level > 0; --level)
    {
        const auto base = pyramid[level - 1];
        const auto target = image(kUpsample, level - 1);
        filters.emplace_back(Stage::Upsample, parameters).compile(context, blurred, base, target);
        blurred = target;
    }
    filters.emplace_back(Stage::Composite, parameters).compile(context, hdrColor, blurred, color);
}

void Bloom::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto&       cache    = *renderCommands->vulkanResources;
    auto&       commands = *renderCommands->commands;
    const auto& frame    = *renderCommands;
    const auto  extent   = renderData.getDefaultTextureDims();
    if (filters.empty())
        prepareFilters(frame, renderData);

    for (const auto& filter : filters)
        filter.execute(cache, commands, {extent.x, extent.y}, frame.view->viewport);
}

void Bloom::Filter::compile(
    const Falcor::RenderContext& context,
    VulkanResources::ImageHandle input,
    VulkanResources::ImageHandle baseInput,
    VulkanResources::ImageHandle target
)
{
    auto& cache = *context.vulkanResources;
    source = input;

    constexpr auto kSampled = vulkan_access::sampled(vk::PipelineStageFlagBits2::eFragmentShader);
    accesses.add(source, kSampled);
    accesses.add(baseInput, kSampled);
    accesses.add(target, vulkan_access::kColorWrite);
    accesses.declare(cache);

    attachments.colors.front() = {target, vk::AttachmentLoadOp::eDontCare};

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
    auto& device        = *context.getDevice();
    descriptorSetLayout = device.createDescriptorSetLayout(bindings);
    const std::array layouts{&descriptorSetLayout};
    const std::array ranges{vk::PushConstantRange{.stageFlags = kShaderStages, .size = sizeof(BloomParameters)}};
    pipelineLayout    = device.getOrCreatePipelineLayout(layouts, ranges);
    const auto shader = context.shaderManager->getOrLoad("bloom", {{"BLOOM_STAGE", std::to_string(static_cast<int>(stage))}});
    pipeline          = device.getOrCreatePipeline({
                 .shader       = shader,
                 .layout       = pipelineLayout,
                 .state        = {.vertexLayout = VertexLayoutPreset::None, .depthTest = false, .depthWrite = false},
                 .colorFormats = {kHdrColorFormat},
    });
    const auto sampler = context.resources->sampler(BuiltinAssets::Sampler::linearClamp);
    descriptorSet     = device.allocateDescriptorSet(descriptorSetLayout);

    cache.writeDescriptor(*descriptorSet, kSourceImageBinding, source);
    cache.writeDescriptor(*descriptorSet, kSamplerBinding, sampler);
    cache.writeDescriptor(*descriptorSet, kBaseImageBinding, baseInput);
}

void Bloom::Filter::execute(VulkanResources& cache, vk::raii::CommandBuffer& commands, vk::Extent2D frameExtent, const ViewportRect& viewport) const
{
    accesses.transition(cache, commands);
    attachments.begin(cache, commands);
    commands.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sets{*descriptorSet};
    commands.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, sets, {});

    const auto            sourceExtent = cache.imageInfo(source).extent;
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
